// End-to-end check of the corruption engine inside the real emulator, no window
// needed:
//
//   corrupt-smoke <rom> <recipe.json> [frames=600] [warmup=600] [--guard | --learn [held=3] [from=0] | --replay | --copies | --map | --access [dir] | --codemap [file] | --jumps [file] | --dump <dir>]
//
// Boots the ROM, runs `warmup` frames, saves a state, then runs `frames` twice
// from that state: once clean, once with the recipe. Reports what the recipe
// changed, and checks after every frame that each byte the engine logged really
// reads back from emulator memory (i.e. CoreMemory writes land).
//
// With --guard it instead runs the recipe once under the crash guard, wired like
// the Qt app, and reports every crash caught and rollback made. Changed ROM bytes are
// blamed by use (the code map tracer runs); `--guard noblame` blames rules only, as before.
//
// With --replay it starts the recipe at a frame boundary the way the Qt app does
// (RunControl), runs it, plays on, then starts again from the saved moment and
// checks that the second run ends with exactly the same memory and picture.
//
// With --copies it lists where WRAM holds copies of the sprite table and the palette
// after the warmup (the recipe is only read, not run), as the memory viewer finds them.
// With --map it prints the memory viewer's map of VRAM and ARAM after the warmup, with
// how many bytes in each area are nonzero (graphics and sound data actually there).
//
// With --access it counts the game's WRAM reads and writes for `frames` frames after the
// warmup (the recipe is only read, not run), as the memory viewer's Reads and Writes
// overlays do, and prints the busiest blocks and how much of WRAM is read but never written.
// With a dir it also writes wram.bin and the averages a frame as wram.reads.f32 and
// wram.writes.f32 (raw floats), which CORRUPT_DUMP_DIR turns into viewer pictures.
//
// With --codemap it records the code map for `frames` frames after the warmup (the recipe
// is only read, not run), twice, alternating with plain runs of the same frames from the
// same state, and prints the speed with and without tracing (best of two), the coverage,
// the ROM banks with the most code, and the first instructions it found. With a file it
// also writes the map there, in the library's format.
//
// With --jumps it records the code map and the indirect jumps for `frames` frames after the
// warmup (the recipe is only read, not run), alternating with plain runs and runs recording
// the map alone, and prints what noting jumps costs, the jump tables found behind them
// (direct and through RAM, seen and likely entries) and the busiest ones. With a file it
// also writes the jump log there, in the library's format.
//
// With --learn [held] it plays the recipe guarded for five seeds while learning a crash
// map, then plays `held` other seeds three ways: plain, avoiding the learned hot blocks,
// and avoiding as many random blocks the rules wrote (the control). `from` skips that many
// held-out seeds, to split a long comparison over several processes.
#include "headless_core.h"

#include "corrupt/code_map.h"
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/jump_tables.h"
#include "corrupt/crash_guard.h"
#include "corrupt/rom_blame.h"
#include "corrupt/survivors.h"
#include "corrupt/crash_map_keeper.h"
#include "corrupt/engine.h"
#include "corrupt/hook.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/run_control.h"
#include "corrupt/memory_watch.h"
#include "corrupt/memory_map.h"

#include "snes9x.h"
#include "memmap.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

using namespace corrupt;

namespace
{

Engine engine;
CoreMemory memory;
RomKeeper rom{ memory }; // what the engine writes through, as in the app
SnesCoreAccess access;
size_t verified = 0;
size_t mismatched = 0;

void frame_hook()
{
    uint64_t frame_before = engine.status().frame;
    engine.on_frame(rom);
    access.note_frame_start();
    // Only this frame's events; the log is bounded, so count from the end.
    for (auto it = engine.event_log().rbegin(); it != engine.event_log().rend() && it->frame > frame_before; ++it)
    {
        if (memory.read(it->write.target, it->write.offset) == it->write.new_value)
            verified++;
        else
            mismatched++;
    }
}

// Starts the engine's run the way the app does: from the clean ROM, with the at_load
// rules applied (and a power cycle, for classic ROM recipes).
void start_run()
{
    RunControl control;
    engine.hold_starts(true);
    engine.set_active(false);
    engine.set_active(true);
    control.between_frames(engine, access, &rom);
    engine.hold_starts(false);
}

bool classic(const Recipe &recipe)
{
    for (auto &r : recipe.rules)
        if (r.enabled && r.when.type == TriggerType::AtLoad && r.range.target == Target::ROM)
            return true;
    return false;
}

// With `blame` (the default), changed ROM bytes are blamed by use, as in the app: the
// code map tracer marks what the game runs and reads, and the guard reverts those.
int run_guarded(const Recipe &recipe, int frames, bool blame_by_use)
{
    CodeMap map(Memory.CalculatedSize);
    // A code map saved earlier (--codemap's file) gives aimed rules something to aim at.
    if (const char *file = getenv("CORRUPT_CODEMAP"))
    {
        std::ifstream in(file, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string error;
        auto loaded = decode_code_map(bytes, error);
        if (loaded && map.merge(*loaded))
            printf("codemap  %zu code bytes known from %s\n", map.coverage().code, file);
        else
            printf("codemap  %s not used: %s\n", file, error.empty() ? "another ROM size" : error.c_str());
    }
    std::vector<uint8_t> aim_flags;
    engine.set_code_flags([&] {
        aim_flags = map.flags();
        for (auto &f : aim_flags)
            f &= codeflag::All;
        return std::span<const uint8_t>(aim_flags);
    });
    RomTracer tracer;
    RomBlame blame(rom); // before the run starts, so it sees the at_load changes
    if (blame_by_use && tracer.start(map))
    {
        blame.attach(map.data(), map.size());
        blame.set_policy(recipe.rom_policy.value_or(RomPolicy::Revert), recipe.seed);
    }
    CrashGuard guard;
    if (blame.active())
        guard.set_byte_blame(&blame);
    guard.set_policy(recipe.crash_policy.value_or(PolicyKind::Reroll));
    guard.set_detector_config(recipe.crash_detection.value_or(DetectorConfig{}));
    int journal_lines = 0;
    guard.on_journal = [&](const JournalEntry &) { journal_lines++; };

    engine.set_recipe(recipe);
    start_run();
    printf("rom      %zu bytes changed at the start%s\n", rom.changed(), classic(recipe) ? ", then a power cycle" : "");
    printf("blame    %s\n", blame.active() ? "by use (the code map tracer is on)" : "rules only");
    guard.between_frames(engine, access); // the run's first checkpoint, at its start, as in the app
    S9xCorruptFrameHook = &frame_hook;
    int caught = 0, gave_up = 0, played = 0;
    size_t acted = 0, survivors = 0;
    int moving = 0, nmi_on = 0; // is the game alive: a changing picture, the frame interrupt on
    uint64_t last_picture = 0;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        played++;
        auto signals = access.sample();
        if (signals.picture_valid && signals.picture_hash != last_picture)
            moving++;
        if (signals.picture_valid)
            last_picture = signals.picture_hash;
        nmi_on += signals.nmi_enabled;
        blame.collect(engine.current_frame());
        auto out = guard.between_frames(engine, access);
        rom.after_guard(out);
        survivors += blame.take_survivals().size();
        if (out.kind == GuardOutcome::Nothing)
            continue;
        caught++;
        acted += out.rom_blamed.size();
        gave_up += out.kind == GuardOutcome::GaveUp;
        printf("  call %5d  %s\n", i + 1, out.summary.c_str());
        if (out.kind == GuardOutcome::GaveUp)
            break;
    }
    S9xCorruptFrameHook = nullptr;
    printf("guard    %d crashes caught, %d gave up, engine %s, %d journal lines\n", caught, gave_up,
           engine.active() ? "still active" : "off", journal_lines);
    printf("rom      %zu bytes changed at the end, %zu of them used by the game; %zu blamed and fixed, %zu survived 15 s of use\n",
           rom.changed(), blame.used(), acted, survivors);
    printf("played   %d of %d frames before %s; the picture changed in %d, the frame interrupt was on in %d\n", played,
           frames, gave_up ? "giving up" : "the end", moving, nmi_on);
    if (tracer.tracing())
        printf("codemap  %zu code bytes seen\n", map.coverage().code);
    if (const uint8_t *now = memory.data(Target::ROM))
    {
        auto list = find_survivors(blame, rom.clean(), { now, memory.size(Target::ROM) }, map.flags(),
                                   SnesCoreAccess::rom_addresses());
        printf("survive  %zu changed bytes used in %u+ frames with no crash after\n", list.size(), kSurvivorFrames);
        for (size_t i = 0; i < list.size() && i < 8; i++)
        {
            auto &v = list[i];
            printf("  %06X  %02X -> %02X  %6u frames  %s%s%s\n", v.address.value_or(v.offset), v.was, v.now,
                   v.frames_used, v.before.c_str(), v.before.empty() ? "data" : " -> ", v.after.c_str());
        }
    }
    tracer.stop();
    engine.set_code_flags({});
    printf("reboots  %u seen by the reset watch (rolled-back ones included)\n", access.sample().reboots);
    printf("writes   %zu verified in emulator memory, %zu mismatched\n", verified, mismatched);
    return mismatched == 0 ? 0 : 1;
}

std::vector<std::vector<uint8_t>> snapshot_targets();

// One guarded run that also learns a crash map. Returns crashes caught. With a fixed mask,
// the rules avoid that instead of what the keeper learns.
// With probes, it also counts the logged writes that land in each probe mask.
struct Probe
{
    const HotMask *mask = nullptr;
    size_t hits = 0;
};

int guarded_learning(const Recipe &recipe, int frames, CrashMapKeeper &keeper, bool &gave_up,
                     const HotMask *fixed = nullptr, std::vector<Probe> *probes = nullptr, size_t *writes = nullptr)
{
    CrashGuard guard;
    guard.set_policy(recipe.crash_policy.value_or(PolicyKind::Reroll));
    guard.set_detector_config(recipe.crash_detection.value_or(DetectorConfig{}));
    engine.set_recipe(recipe);
    engine.set_hot_mask(fixed ? *fixed : keeper.hot_mask());
    engine.set_active(true);
    S9xCorruptFrameHook = &frame_hook;
    int caught = 0;
    gave_up = false;
    for (int i = 0; i < frames && !gave_up; i++)
    {
        headless::run_frames(1);
        keeper.observe(engine.event_log(), engine.current_frame());
        if (writes)
            for (auto it = engine.event_log().rbegin();
                 it != engine.event_log().rend() && it->frame == engine.current_frame(); ++it)
            {
                ++*writes;
                for (auto &p : *probes)
                    if (p.mask->is_hot(it->write.target, it->write.offset))
                    {
                        p.hits++;
                        if (getenv("CORRUPT_LEARN_VERBOSE") && fixed && fixed->empty() && &p == &probes->front())
                            printf("    frame %llu: plain writes %s %s = %02X, which learned skips\n",
                                   (unsigned long long)it->frame, target_name(it->write.target),
                                   hex(it->write.offset).c_str(), it->write.new_value);
                    }
            }
        auto out = guard.between_frames(engine, access);
        if (out.kind == GuardOutcome::Nothing)
            continue;
        caught++;
        gave_up = out.kind == GuardOutcome::GaveUp;
        if (getenv("CORRUPT_LEARN_VERBOSE"))
            printf("    frame %llu: %s, back to %llu\n", (unsigned long long)engine.current_frame(),
                   out.summary.c_str(), (unsigned long long)out.restored_frame);
        if (keeper.crashed(out) && !fixed)
            engine.set_hot_mask(keeper.hot_mask());
    }
    S9xCorruptFrameHook = nullptr;
    engine.set_active(false);
    return caught;
}

// --learn: play the recipe guarded for five sessions (seeds) while learning a crash map,
// then, as a fair test, three other seeds from the same start with and without every
// rule avoiding what was learned.
int run_learning(const Recipe &recipe, int frames, int held, int from)
{
    auto start = headless::save_state();
    CrashMapKeeper first;
    bool gave_up = false;
    // Several sessions' worth, the way the map builds up over time.
    for (uint64_t seed : { recipe.seed, 101ull, 202ull, 303ull, 404ull })
    {
        headless::load_state(start);
        Recipe learning = recipe;
        learning.seed = seed;
        CrashMaps so_far;
        so_far.mine = first.mine();
        first.reset(so_far);
        int crashes = guarded_learning(learning, frames, first, gave_up);
        printf("learn    seed %-4llu %d crashes%s; %zu hot blocks so far\n", (unsigned long long)seed, crashes,
               gave_up ? " (gave up)" : "", first.hot_blocks());
    }
    for (auto t : kAllTargets)
    {
        std::vector<std::pair<uint32_t, BlockCounts>> blocks(first.combined().blocks(t).begin(),
                                                             first.combined().blocks(t).end());
        if (blocks.empty())
            continue;
        size_t crashed = 0, hot = 0, crashes = 0, survivals = 0;
        for (auto &[b, c] : blocks)
        {
            crashed += c.crashes > 0;
            hot += first.combined().hot_in(t, c);
            crashes += c.crashes;
            survivals += c.survivals;
        }
        printf("  %-5s %zu blocks seen, %zu in a crash window, %zu hot; overall crash rate %.2f\n", target_name(t),
               blocks.size(), crashed, hot, double(crashes) / double(std::max<size_t>(1, crashes + survivals)));
        std::sort(blocks.begin(), blocks.end(), [](auto &a, auto &b) {
            return CrashMap::danger_of(a.second) > CrashMap::danger_of(b.second);
        });
        for (size_t i = 0; i < blocks.size() && i < 8 && blocks[i].second.crashes; i++)
            printf("    %s  %u crashes, %u survivals, danger %.2f%s\n", hex(blocks[i].first).c_str(),
                   blocks[i].second.crashes, blocks[i].second.survivals, CrashMap::danger_of(blocks[i].second),
                   first.combined().hot_in(t, blocks[i].second) ? "  HOT" : "");
    }

    // Fair test: other seeds (so other writes than the ones learned from), each played from
    // the same start three ways: plain, avoiding the learned hot blocks, and, as a control,
    // avoiding as many blocks picked at random from the ones the rules wrote. Both masks stay
    // fixed during the test, so it measures what was learned. If random does as well as
    // learned, the map learned nothing.
    HotMask learned = first.hot_mask();
    std::vector<std::pair<Target, uint32_t>> cool;
    for (auto t : kAllTargets)
        for (auto &[b, c] : first.combined().blocks(t))
            if (!first.combined().hot_in(t, c))
                cool.push_back({ t, b });
    std::sort(cool.begin(), cool.end());
    size_t hot_count = first.hot_blocks();

    int totals[3] = {}, learned_better = 0, learned_worse = 0;
    for (int i = from; i < from + held; i++)
    {
        uint64_t seed = 11ull * (i + 1);
        std::mt19937_64 pick(seed);
        auto shuffled = cool;
        std::shuffle(shuffled.begin(), shuffled.end(), pick);
        HotMask random;
        for (size_t k = 0; k < hot_count && k < shuffled.size(); k++)
            random.add_block(shuffled[k].first, shuffled[k].second);

        Recipe plain = recipe;
        plain.seed = seed;
        Recipe avoiding = plain;
        for (auto &r : avoiding.rules)
            r.avoid_hot = true;
        int crashes[3];
        const HotMask *masks[3] = { nullptr, &learned, &random };
        std::vector<Probe> probes = { { &learned }, { &random } }; // counted in every arm; printed for plain
        size_t writes[3] = {};
        for (int arm = 0; arm < 3; arm++)
        {
            headless::load_state(start);
            CrashMapKeeper keeper;
            HotMask none;
            crashes[arm] = guarded_learning(arm ? avoiding : plain, frames, keeper, gave_up, arm ? masks[arm] : &none,
                                            &probes, &writes[arm]);
            totals[arm] += crashes[arm];
        }
        printf("seed %-3llu %2d crashes plain, %2d avoiding learned, %2d avoiding random; writes %zu / %zu / %zu\n",
               (unsigned long long)seed, crashes[0], crashes[1], crashes[2], writes[0], writes[1], writes[2]);
        learned_better += crashes[1] < crashes[0];
        learned_worse += crashes[1] > crashes[0];
    }
    printf("total    %d crashes plain, %d avoiding the %zu learned blocks, %d avoiding %zu random ones\n",
           totals[0], totals[1], hot_count, totals[2], hot_count);
    printf("         learned beat plain on %d of %d seeds, lost on %d\n", learned_better, held, learned_worse);
    return 0;
}

int run_replay(const Recipe &recipe, int frames)
{
    RunControl control;
    engine.hold_starts(true);
    engine.set_recipe(recipe);
    engine.set_active(true);
    S9xCorruptFrameHook = &frame_hook;
    auto step = [&](int n) {
        for (int i = 0; i < n; i++)
        {
            headless::run_frames(1);
            control.between_frames(engine, access);
        }
    };

    step(1);
    auto anchor = control.anchor();
    step(frames);
    auto first = snapshot_targets();
    auto first_picture = headless::frame_hash();
    auto first_firings = engine.status().firings;

    step(frames / 2 + 17); // play on, somewhere else
    control.request_start_from(anchor);
    engine.reroll(recipe.seed);
    step(1);
    bool loaded = control.anchor_is_from_saved();
    step(frames);
    auto second = snapshot_targets();
    S9xCorruptFrameHook = nullptr;

    printf("replay   moment of %zu bytes, %s\n", anchor.size(), loaded ? "loaded" : "NOT loaded");
    printf("engine   %llu firings first run, %llu second\n", (unsigned long long)first_firings,
           (unsigned long long)engine.status().firings);
    bool same = loaded && first_firings == engine.status().firings;
    for (size_t t = 0; t < kTargetCount; t++)
    {
        size_t diff = 0;
        for (size_t i = 0; i < first[t].size(); i++)
            diff += first[t][i] != second[t][i];
        same = same && diff == 0;
        printf("  %-6s %6zu bytes differ between the two runs\n", target_name(kAllTargets[t]), diff);
    }
    bool same_picture = headless::frame_hash() == first_picture;
    printf("picture  %s\n", same_picture ? "identical" : "DIFFERENT");
    printf("result   %s\n", same && same_picture ? "replayed exactly" : "did NOT replay exactly");
    return same && same_picture ? 0 : 1;
}

std::vector<std::vector<uint8_t>> snapshot_targets()
{
    std::vector<std::vector<uint8_t>> out;
    for (auto t : kAllTargets)
    {
        std::vector<uint8_t> bytes(memory.size(t));
        for (uint32_t i = 0; i < bytes.size(); i++)
            bytes[i] = memory.read(t, i);
        out.push_back(std::move(bytes));
    }
    return out;
}

int run_access(int frames, const char *dir)
{
    std::vector<uint64_t> reads(0x20000), writes(0x20000);
    {
        WramAccessCounter counter;
        MemoryWatch unused; // take() hands the counts to a watch; sum them here instead
        counter.start();
        for (int f = 0; f < frames; f++)
        {
            headless::run_frames(1);
            for (uint32_t i = 0; i < 0x20000; i++)
            {
                reads[i] += S9xWramReads[i];
                writes[i] += S9xWramWrites[i];
            }
            counter.take(unused); // starts the counts over
        }
    }
    auto all = snapshot_targets();
    auto &wram = all[size_t(Target::WRAM)];
    std::vector<std::pair<Target, CopyRun>> copies;
    for (auto t : { Target::OAM, Target::CGRAM })
        for (auto &run : find_copies(wram, all[size_t(t)], 32))
            copies.push_back({ t, run });
    auto copy_of = [&](uint32_t block) -> std::string {
        for (auto &[t, run] : copies)
            if (block + 15 >= run.haystack_offset && block < run.haystack_offset + run.length)
                return std::string("  (copy of ") + target_name(t) + ")";
        return "";
    };

    uint64_t total_reads = 0, total_writes = 0;
    size_t read_only = 0, write_only = 0, both = 0, untouched = 0;
    for (uint32_t i = 0; i < 0x20000; i++)
    {
        total_reads += reads[i];
        total_writes += writes[i];
        read_only += reads[i] && !writes[i];
        write_only += writes[i] && !reads[i];
        both += reads[i] && writes[i];
        untouched += !reads[i] && !writes[i];
    }
    printf("access   %d frames: %.0f reads and %.0f writes of WRAM a frame\n", frames, double(total_reads) / frames,
           double(total_writes) / frames);
    printf("bytes    %zu read and written, %zu only read, %zu only written, %zu untouched\n", both, read_only,
           write_only, untouched);
    for (auto [name, counts] : { std::pair{ "reads", &reads }, std::pair{ "writes", &writes } })
    {
        std::vector<std::pair<uint32_t, uint64_t>> blocks;
        for (uint32_t b = 0; b < 0x20000; b += 16)
        {
            uint64_t n = 0;
            for (uint32_t i = b; i < b + 16; i++)
                n += (*counts)[i];
            if (n)
                blocks.push_back({ b, n });
        }
        std::sort(blocks.begin(), blocks.end(), [](auto &a, auto &b) { return a.second > b.second; });
        printf("busiest  %s, per 16-byte block:\n", name);
        for (size_t i = 0; i < blocks.size() && i < 12; i++)
            printf("  %s  %9.1f a frame%s\n", hex(blocks[i].first).c_str(), double(blocks[i].second) / frames,
                   copy_of(blocks[i].first).c_str());
    }
    if (dir)
    {
        auto write = [&](const char *name, const void *data, size_t bytes) {
            std::string path = std::string(dir) + "/" + name;
            std::ofstream out(path, std::ios::binary);
            out.write(static_cast<const char *>(data), std::streamsize(bytes));
            printf("dump     %s\n", path.c_str());
        };
        std::vector<float> r(0x20000), w(0x20000);
        for (size_t i = 0; i < r.size(); i++)
        {
            r[i] = float(double(reads[i]) / frames);
            w[i] = float(double(writes[i]) / frames);
        }
        write("wram.bin", wram.data(), wram.size());
        write("wram.reads.f32", r.data(), r.size() * sizeof(float));
        write("wram.writes.f32", w.data(), w.size() * sizeof(float));
    }
    for (auto &[t, run] : copies)
    {
        uint64_t n = 0;
        for (uint32_t i = run.haystack_offset; i < run.haystack_offset + run.length; i++)
            n += reads[i];
        printf("copy     wram %s..%s of %s: read %.2f times a byte a frame\n", hex(run.haystack_offset).c_str(),
               hex(run.haystack_offset + run.length - 1).c_str(), target_name(t), double(n) / run.length / frames);
    }
    return 0;
}

int run_codemap(int frames, const char *file)
{
    auto start = headless::save_state();
    CodeMap map(Memory.CalculatedSize);
    RomTracer tracer;
    auto timed = [&](bool traced) {
        headless::load_state(start);
        if (traced && !tracer.start(map))
            return -1.0;
        auto t0 = std::chrono::steady_clock::now();
        headless::run_frames(frames);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        tracer.stop();
        return frames / s;
    };
    double plain = 0, traced = 0;
    for (int round = 0; round < 2; round++)
    {
        plain = std::max(plain, timed(false));
        traced = std::max(traced, timed(true));
    }
    if (traced < 0)
    {
        fprintf(stderr, "the tracer didn't start\n");
        return 1;
    }
    printf("speed    %.0f fps plain, %.0f fps traced: tracing costs %.1f%%\n", plain, traced,
           100.0 * (plain / traced - 1.0));

    auto c = map.coverage();
    auto pct = [&](size_t n) { return 100.0 * double(n) / double(c.size); };
    printf("coverage %zu bytes of ROM: code %zu (%.2f%%: %zu opcodes, %zu operands), data %zu (%.2f%%), dma %zu (%.2f%%)\n",
           c.size, c.code, pct(c.code), c.opcode, c.operand, c.data, pct(c.data), c.dma, pct(c.dma));
    printf("widths   %zu code bytes ran both 8- and 16-bit\n", c.both_widths);

    auto where = SnesCoreAccess::rom_addresses();
    std::vector<std::pair<uint32_t, size_t>> banks; // ROM offset of each 64 KB, code bytes in it
    for (uint32_t b = 0; b < map.size(); b += 0x10000)
    {
        size_t n = 0;
        for (uint32_t i = b; i < std::min<size_t>(map.size(), b + 0x10000); i++)
            n += (map.at(i) & codeflag::Code) != 0;
        if (n)
            banks.push_back({ b, n });
    }
    std::sort(banks.begin(), banks.end(), [](auto &a, auto &b) { return a.second > b.second; });
    printf("banks    with the most code (ROM offset, CPU address):\n");
    for (size_t i = 0; i < banks.size() && i < 8; i++)
    {
        auto at = where.cpu_address(banks[i].first);
        printf("  %s  %s  %zu code bytes\n", hex(banks[i].first).c_str(),
               at ? hex(*at).c_str() : "unmapped", banks[i].second);
    }
    std::span<const uint8_t> bytes(Memory.ROM, Memory.CalculatedSize);
    printf("sample   the first instructions found:\n");
    int shown = 0;
    for (uint32_t i = 0; i < map.size() && shown < 8; i++)
        if (map.at(i) & codeflag::Opcode)
            if (auto in = instruction_at(bytes, map, i, where))
            {
                printf("  %s  %-14s %s%s\n", in->address ? hex(*in->address).c_str() : hex(i).c_str(),
                       in->text.c_str(), class_name(in->cls), in->both_widths ? "  (both widths)" : "");
                shown++;
            }
    if (file)
    {
        auto encoded = encode_code_map(map);
        std::ofstream out(file, std::ios::binary);
        out.write(encoded.data(), std::streamsize(encoded.size()));
        printf("wrote    %s (%zu bytes)\n", file, encoded.size());
    }
    return 0;
}

int run_jumps(int frames, const char *file)
{
    auto start = headless::save_state();
    CodeMap map(Memory.CalculatedSize);
    JumpLog log;
    RomTracer tracer;
    auto timed = [&](int how) { // 0 plain, 1 the map, 2 the map and jumps
        headless::load_state(start);
        if (how)
        {
            map = CodeMap(Memory.CalculatedSize);
            log.clear();
            if (!tracer.start(map, how == 2 ? &log : nullptr))
                return -1.0;
        }
        auto t0 = std::chrono::steady_clock::now();
        headless::run_frames(frames);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        tracer.stop();
        return frames / s;
    };
    double fps[3] = { 0, 0, 0 };
    for (int round = 0; round < 2; round++)
        for (int how = 0; how < 3; how++)
            fps[how] = std::max(fps[how], timed(how));
    if (fps[2] < 0)
    {
        fprintf(stderr, "the tracer didn't start\n");
        return 1;
    }
    printf("speed    %.0f fps plain, %.0f with the code map (%.1f%%), %.0f with jumps too (%.1f%%)\n", fps[0], fps[1],
           100.0 * (fps[0] / fps[1] - 1.0), fps[2], 100.0 * (fps[0] / fps[2] - 1.0));

    size_t direct = 0, through = 0;
    std::vector<uint32_t> sites;
    for (auto &[key, seen] : log.all())
    {
        (key.entry != kNoEntry ? direct : through)++;
        sites.push_back(key.site);
    }
    std::sort(sites.begin(), sites.end());
    sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
    printf("jumps    %zu distinct at %zu sites: %zu through a ROM pointer, %zu through RAM\n", log.size(), sites.size(),
           direct, through);

    std::span<const uint8_t> rom(Memory.ROM, Memory.CalculatedSize);
    auto offsets = SnesCoreAccess::rom_offsets();
    auto found = find_jump_tables(log, rom, map.flags(), offsets);
    size_t n[2] = {}, seen[2] = {}, likely[2] = {};
    for (auto &t : found.tables)
    {
        int k = t.kind == JumpKind::Direct ? 0 : 1;
        n[k]++;
        seen[k] += t.seen();
        likely[k] += t.likely();
    }
    printf("tables   %zu direct (%zu entries seen, %zu likely), %zu through RAM (%zu seen, %zu likely)\n", n[0], seen[0],
           likely[0], n[1], seen[1], likely[1]);
    printf("         %zu jumps through RAM no table explains, %zu ambiguous\n", found.pointer_jumps, found.ambiguous);
    size_t swappable = 0;
    for (auto &t : found.tables)
        swappable += t.seen() >= 2 || (t.seen() && t.likely());
    printf("swap     %zu tables have something to swap with (2+ seen, or seen and likely)\n", swappable);

    auto where = SnesCoreAccess::rom_addresses();
    auto order = found.tables;
    auto hits = [](const JumpTable &t) {
        uint64_t h = 0;
        for (auto &e : t.entries)
            h += e.hits;
        return h;
    };
    std::sort(order.begin(), order.end(), [&](auto &a, auto &b) { return hits(a) > hits(b); });
    printf("busiest  tables (site, instruction, kind, table, entries seen + likely, targets):\n");
    for (size_t i = 0; i < order.size() && i < 15; i++)
    {
        auto &t = order[i];
        auto in = instruction_at(rom, map, t.site, where);
        auto at = where.cpu_address(t.site);
        auto table_at = where.cpu_address(t.first());
        std::string targets;
        int shown = 0;
        for (auto &e : t.entries)
            if (e.seen && shown++ < 4)
                targets += " " + hex(e.target);
        printf("  %s  %-16s %-11s %s x%d  %zu + %zu  %llu hits ->%s%s\n", at ? hex(*at).c_str() : hex(t.site).c_str(),
               in ? in->text.c_str() : "?", t.kind == JumpKind::Direct ? "direct" : "through RAM",
               table_at ? hex(*table_at).c_str() : hex(t.first()).c_str(), t.stride, t.seen(), t.likely(),
               (unsigned long long)hits(t), targets.c_str(), t.seen() > 4 ? " ..." : "");
    }
    if (file)
    {
        auto encoded = encode_jump_log(log, Memory.CalculatedSize);
        std::ofstream out(file, std::ios::binary);
        out.write(encoded.data(), std::streamsize(encoded.size()));
        printf("wrote    %s (%zu bytes)\n", file, encoded.size());
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <rom> <recipe.json> [frames=600] [warmup=600]\n", argv[0]);
        return 2;
    }
    int frames = argc > 3 ? atoi(argv[3]) : 600;
    int warmup = argc > 4 ? atoi(argv[4]) : 600;

    std::ifstream in(argv[2]);
    std::stringstream text;
    text << in.rdbuf();
    auto parsed = parse_recipe(text.str(), {});
    if (!parsed.recipe)
    {
        for (auto &e : parsed.errors)
            fprintf(stderr, "recipe: %s\n", e.c_str());
        return 1;
    }

    if (!headless::init() || !headless::load_rom_file(argv[1]))
    {
        fprintf(stderr, "couldn't load %s\n", argv[1]);
        return 1;
    }
    printf("ROM      %s\n", Memory.ROMName);
    SnesCoreAccess::watch_resets();
    rom.set_clock([] { return engine.current_frame(); });
    rom.game_loaded(SnesCoreAccess::locked_rom());
    engine.set_locked(rom.locked_mask());
    printf("recipe   %s (seed %llu, %zu rules)\n", parsed.recipe->name.c_str(),
           (unsigned long long)parsed.recipe->seed, parsed.recipe->rules.size());

    for (size_t i = 0; i < parsed.recipe->rules.size(); i++)
        if (auto err = validate_rule(parsed.recipe->rules[i], memory))
        {
            fprintf(stderr, "rule %zu doesn't fit this game: %s\n", i, err->c_str());
            return 1;
        }

    headless::run_frames(warmup);
    if (argc > 5 && std::string(argv[5]) == "--guard")
        return run_guarded(*parsed.recipe, frames, !(argc > 6 && std::string(argv[6]) == "noblame"));
    if (argc > 5 && std::string(argv[5]) == "--map")
    {
        auto all = snapshot_targets();
        auto video = SnesCoreAccess::video_layout();
        printf("mode     %d\n", video.mode);
        for (auto [t, map] : { std::pair{ Target::VRAM, video_map(video) },
                               std::pair{ Target::ARAM, merge_maps(fixed_map(Target::ARAM, 0x10000),
                                                                   sound_map(SnesCoreAccess::sound_layout())) } })
            for (auto &a : map)
            {
                size_t nonzero = 0;
                for (uint32_t i = a.range.first; i <= a.range.last; i++)
                    nonzero += all[size_t(t)][i] != 0;
                printf("  %-5s %s..%s  %-30s %3zu%% nonzero  %s\n", target_name(t), hex(a.range.first).c_str(),
                       hex(a.range.last).c_str(), a.name.c_str(), nonzero * 100 / a.range.length(), a.about.c_str());
            }
        return 0;
    }
    if (argc > 5 && std::string(argv[5]) == "--copies")
    {
        auto all = snapshot_targets();
        auto &wram = all[size_t(Target::WRAM)];
        for (auto t : { Target::OAM, Target::CGRAM })
        {
            auto runs = find_copies(wram, all[size_t(t)], 32);
            printf("%-6s %zu copies in WRAM\n", target_name(t), runs.size());
            for (size_t i = 0; i < runs.size() && i < 5; i++)
                printf("  wram %s..%s  = %s %s..%s (%u bytes)\n", hex(runs[i].haystack_offset).c_str(),
                       hex(runs[i].haystack_offset + runs[i].length - 1).c_str(), target_name(t),
                       hex(runs[i].needle_offset).c_str(), hex(runs[i].needle_offset + runs[i].length - 1).c_str(),
                       runs[i].length);
        }
        return 0;
    }
    if (argc > 5 && std::string(argv[5]) == "--codemap")
        return run_codemap(frames, argc > 6 ? argv[6] : nullptr);
    if (argc > 5 && std::string(argv[5]) == "--jumps")
        return run_jumps(frames, argc > 6 ? argv[6] : nullptr);
    if (argc > 5 && std::string(argv[5]) == "--access")
        return run_access(frames, argc > 6 ? argv[6] : nullptr);
    if (argc > 6 && std::string(argv[5]) == "--dump")
    {
        // Raw contents of every target, for looking at real memory in the viewer's tests.
        auto all = snapshot_targets();
        for (auto t : kAllTargets)
        {
            std::string path = std::string(argv[6]) + "/" + target_name(t) + ".bin";
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char *>(all[size_t(t)].data()), std::streamsize(all[size_t(t)].size()));
            printf("%-6s %6zu bytes -> %s\n", target_name(t), all[size_t(t)].size(), path.c_str());
        }
        return 0;
    }
    if (argc > 5 && std::string(argv[5]) == "--learn")
        return run_learning(*parsed.recipe, frames, argc > 6 ? atoi(argv[6]) : 3, argc > 7 ? atoi(argv[7]) : 0);
    if (argc > 5 && std::string(argv[5]) == "--replay")
        return run_replay(*parsed.recipe, frames);

    auto start = headless::save_state();
    if (start.empty())
    {
        fprintf(stderr, "couldn't save a state\n");
        return 1;
    }

    // Clean run. A classic ROM recipe power-cycles as it starts, so the clean run does too.
    if (classic(*parsed.recipe))
        S9xReset();
    std::vector<uint64_t> clean_frames;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        clean_frames.push_back(headless::frame_hash());
    }
    auto clean = snapshot_targets();

    // Corrupted run from the same state.
    headless::load_state(start);
    engine.set_recipe(*parsed.recipe);
    start_run();
    S9xCorruptFrameHook = &frame_hook;
    std::vector<uint64_t> corrupt_frames;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        corrupt_frames.push_back(headless::frame_hash());
    }
    S9xCorruptFrameHook = nullptr;
    auto dirty = snapshot_targets();

    int differing = 0, first_difference = -1;
    for (int i = 0; i < frames; i++)
        if (clean_frames[size_t(i)] != corrupt_frames[size_t(i)])
        {
            differing++;
            if (first_difference < 0)
                first_difference = i + 1;
        }

    auto status = engine.status();
    printf("frames   %d after %d warmup\n", frames, warmup);
    printf("engine   %llu firings, %llu frames\n", (unsigned long long)status.firings, (unsigned long long)status.frame);
    printf("writes   %zu verified in emulator memory, %zu mismatched\n", verified, mismatched);
    printf("picture  %d of %d frames look different from the clean run", differing, frames);
    if (first_difference > 0)
        printf(" (first at frame %d)", first_difference);
    printf("\n");
    for (size_t t = 0; t < kTargetCount; t++)
    {
        size_t diff = 0;
        for (size_t i = 0; i < clean[t].size(); i++)
            diff += clean[t][i] != dirty[t][i];
        printf("  %-6s %6zu of %6zu bytes differ from the clean run\n", target_name(kAllTargets[t]), diff, clean[t].size());
    }
    for (auto &m : headless::messages())
        if (m.type >= S9X_ERROR)
            printf("core     %s\n", m.text.c_str());

    return mismatched == 0 && status.firings > 0 ? 0 : 1;
}
