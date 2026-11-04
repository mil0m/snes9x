// End-to-end check of the corruption engine inside the real emulator, no window
// needed:
//
//   corrupt-smoke <rom> <recipe.json> [frames=600] [warmup=600] [--guard | --instant [seeds] | --ahead [seeds] | --keep [seeds] | --eyes [seeds] | --derail-fp [learn] | --learn [held=3] [from=0] | --replay | --copies | --map | --access [dir] | --codemap [file] | --jumps [file] | --calls | --bodies | --kill [seeds] [count] | --change [seeds] [count] | --risk [count] [trial] [number|branch|math|skip] | --foresee [count] [trial] [kind] | --try [count] [trial] [kind] | --dump <dir>]
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
// same state, and prints the speed with and without tracing (best of two; and with the
// instant revert's watch on, which on a clean ROM only counts instructions), the coverage,
// the ROM banks with the most code, and the first instructions it found. With a file it
// also writes the map there, in the library's format.
//
// With --jumps it records the code map and the indirect jumps for `frames` frames after the
// warmup (the recipe is only read, not run), alternating with plain runs and runs recording
// the map alone, and prints what noting jumps costs, the jump tables found behind them
// (direct and through RAM, seen and likely entries) and the busiest ones. With a file it
// also writes the jump log there, in the library's format.
//
// With --swaps [seeds=10] [count=3] it records the code map and jumps for `frames` frames,
// finds the jump tables, then for each seed picks `count` entries and boots the game three
// ways, unguarded, for `frames` frames each: those entries pointer-swapped, the same entries
// given random bytes (the control), and clean. It prints how often each is still running at
// the end (the CPU not stopped, no reboot, and the frame interrupt on or the picture changing
// in the last 5 s), how many of those still
// move (the picture changed in the last 5 s; a still title screen doesn't), and how many
// read a changed entry.
//
// With --derail-fp [learn=frames] it learns the code map from a boot for `learn` frames,
// then plays `frames` more with the instant revert's watch held open on the clean game, so
// every landing it notes is a false alarm, and counts them by kind (roadmap #51).
//
// With --instant [seeds=5] it learns the code map from a clean boot (or CORRUPT_CODEMAP),
// then plays the recipe guarded for `frames` frames per seed twice, the guard alone and
// with the instant revert, and compares rollbacks, frames lost, changes kept and liveness.
// CORRUPT_INSTANT_VERBOSE=1 prints every catch. --ahead [seeds=5] is the same with the
// instant revert in both arms and the second testing each live ROM firing first (#52).
// --eyes [seeds=5] has both of those in both arms, the first blind after a landing as
// before #57. (--first-use and --prove measured #55 and #56, both removed.)
//
// With --learn [held] it plays the recipe guarded for five seeds while learning a crash
// map, then plays `held` other seeds three ways: plain, avoiding the learned hot blocks,
// and avoiding as many random blocks the rules wrote (the control). `from` skips that many
// held-out seeds, to split a long comparison over several processes.
#include "headless_core.h"

#include "corrupt/code_map.h"
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/calls.h"
#include "corrupt/function_guard.h"
#include "corrupt/function_change.h"
#include "corrupt/change_risk.h"
#include "corrupt/try_change.h"
#include "corrupt/jump_tables.h"
#include "corrupt/pointer_swap.h"
#include "corrupt/crash_guard.h"
#include "corrupt/derail.h"
#include "corrupt/instant_revert.h"
#include "corrupt/test_ahead.h"
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
#include "corrupt/moment.h"

#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "ppu.h"
#include "dma.h"
#include "gfx.h"
#include "getset.h"
#include "apu/bapu/snes/snes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <random>
#include <sstream>

using namespace corrupt;

namespace
{

Engine engine;
CoreMemory memory;
MomentRecorder recorder; // idle until --keep feeds it frames
RecordedMemory recorded{ memory, recorder };
RomKeeper rom{ recorded }; // what the engine writes through, as in the app
SnesCoreAccess access;
MomentPlayer player; // --keep's checks play moments back through the frame hook
size_t verified = 0;
size_t mismatched = 0;

void frame_hook()
{
    if (player.playing())
    {
        player.in_hook(memory);
        access.note_frame_start();
        return;
    }
    uint64_t frame_before = engine.status().frame;
    recorder.hook_begin();
    engine.on_frame(rom);
    recorder.hook_end();
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
    std::vector<uint32_t> changed_by_you; // the recipe's changes: a crash they cause goes back to before them
    for (auto &c : recipe.changes)
        for (size_t i = 0; i < c.value.size(); i++)
            changed_by_you.push_back(uint32_t(c.at + i));
    if (!getenv("CORRUPT_OWN_OFF"))
        blame.set_own(changed_by_you);
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
    // CORRUPT_PRESS=<hotkey>@<frame>: press a hotkey then, as a player turning a kept change on.
    int press_key = 0, press_at = -1;
    if (const char *p = getenv("CORRUPT_PRESS"))
        sscanf(p, "%d@%d", &press_key, &press_at);
    for (int i = 0; i < frames; i++)
    {
        if (i == press_at)
            engine.press_hotkey(press_key);
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
    DerailWatch derails;
    auto timed = [&](bool traced, bool watched = false) {
        headless::load_state(start);
        if (traced && !tracer.start(map))
            return -1.0;
        if (watched)
            tracer.watch_derails(&derails);
        auto t0 = std::chrono::steady_clock::now();
        headless::run_frames(frames);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        tracer.stop();
        return frames / s;
    };
    double plain = 0, traced = 0, watched = 0;
    for (int round = 0; round < 2; round++)
    {
        plain = std::max(plain, timed(false));
        traced = std::max(traced, timed(true));
        watched = std::max(watched, timed(true, true));
    }
    if (traced < 0)
    {
        fprintf(stderr, "the tracer didn't start\n");
        return 1;
    }
    printf("speed    %.0f fps plain, %.0f fps traced: tracing costs %.1f%%\n", plain, traced,
           100.0 * (plain / traced - 1.0));
    printf("         %.0f fps traced with the instant revert's watch on: %.1f%% more\n", watched,
           100.0 * (traced / watched - 1.0));

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

// --calls: what call noting costs (the code map and jumps, then calls too), then a run of
// `frames` with the call graph (#26): functions found after 10 s, 1 min and 5 min, how they
// are called, how many run in nearly every frame, and the busiest.
int run_calls(int frames)
{
    auto start = headless::save_state();
    CodeMap map(Memory.CalculatedSize);
    JumpLog jumps;
    CallKeeper calls;
    RomTracer tracer;
    auto timed = [&](bool with_calls) {
        headless::load_state(start);
        map = CodeMap(Memory.CalculatedSize);
        jumps.clear();
        calls.reset();
        if (!tracer.start(map, &jumps, with_calls ? &calls.mine() : nullptr))
            return -1.0;
        int n = std::min(frames, 1800);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; i++)
        {
            headless::run_frames(1);
            calls.end_frame();
        }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        tracer.stop();
        return n / s;
    };
    double fps[2] = { 0, 0 };
    for (int round = 0; round < 2; round++)
        for (int how = 0; how < 2; how++)
            fps[how] = std::max(fps[how], timed(how == 1));
    if (fps[1] < 0)
    {
        fprintf(stderr, "the tracer didn't start\n");
        return 1;
    }
    printf("speed    %.0f fps with the code map and jumps, %.0f with calls too (%.1f%%)\n", fps[0], fps[1],
           100.0 * (fps[0] / fps[1] - 1.0));

    headless::load_state(start);
    map = CodeMap(Memory.CalculatedSize);
    calls.reset();
    if (!tracer.start(map, nullptr, &calls.mine()))
        return 1;
    auto report = [&](int f) {
        auto fs = calls.functions();
        size_t types[4] = {}, every = 0;
        for (auto &fn : fs)
        {
            types[int(fn.type)]++;
            every += fn.frames * 10 >= uint64_t(f) * 9;
        }
        printf("%5.0f s  %4zu functions (%zu near, %zu long, %zu mixed), %zu called in 90%%+ of frames, %zu call sites\n",
               f / 60.0, fs.size(), types[1], types[2], types[3], every, calls.mine().size());
    };
    for (int f = 1; f <= frames; f++)
    {
        headless::run_frames(1);
        calls.end_frame();
        if (f == 600 || f == 3600 || f == 18000 || f == frames)
            report(f);
    }
    tracer.stop();
    auto where = SnesCoreAccess::rom_addresses();
    auto fs = busiest(calls.functions(), 0);
    std::sort(fs.begin(), fs.end(), [](const Function &a, const Function &b) { return a.frames > b.frames; });
    printf("busiest  (frames called in, calls, callers)\n");
    for (size_t i = 0; i < fs.size() && i < 12; i++)
        printf("         $%06X %-5s %6llu %8u %3u\n", where.cpu_address(fs[i].entry).value_or(0), fs[i].type == CallType::Near ? "near" : "long",
               (unsigned long long)fs[i].frames, fs[i].calls, fs[i].callers);
    return 0;
}

// --bodies: learn for `frames` (the code map and the call graph), then each function's body
// (#58): how many have one, their sizes, how much of the code they cover, how long finding them
// takes, and what guards would cost (the 10 busiest, shallow and deep).
int run_bodies(int frames)
{
    CodeMap map(Memory.CalculatedSize);
    CallKeeper calls;
    RomTracer tracer;
    if (!tracer.start(map, nullptr, &calls.mine()))
        return 1;
    for (int f = 0; f < frames; f++)
    {
        headless::run_frames(1);
        calls.end_frame();
    }
    tracer.stop();
    auto where = SnesCoreAccess::rom_addresses();
    auto fs = calls.functions();
    std::vector<uint8_t> rom(Memory.ROM, Memory.ROM + Memory.CalculatedSize);
    auto t0 = std::chrono::steady_clock::now();
    auto bodies = function_bodies(rom, map.flags(), fs, where);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    std::vector<size_t> sizes;
    size_t empty = 0, truncated = 0, open = 0, pieces = 0;
    std::vector<bool> covered(rom.size(), false);
    for (auto &[entry, b] : bodies)
    {
        if (b.code.empty())
        {
            empty++;
            continue;
        }
        sizes.push_back(b.bytes);
        truncated += b.truncated;
        open += b.open_ended;
        auto runs = b.runs();
        pieces += runs.size();
        for (auto &[first, last] : runs)
            for (uint32_t o = first; o <= last; o++)
                covered[o] = true;
    }
    if (getenv("CORRUPT_BODIES_VERBOSE")) // a few busy ones, instruction by instruction
        for (size_t i = 0; i < fs.size() && i < 8; i++)
        {
            auto &b = bodies.at(fs[i].entry);
            printf("  $%06X %zu B:", where.cpu_address(b.entry).value_or(0), b.bytes);
            for (auto &in : b.code)
            {
                auto d = instruction_at(rom, map.flags(), in.at, where);
                printf(" [%06X %s]", in.at, d ? d->text.c_str() : "?");
            }
            uint32_t end = b.code.empty() ? b.entry : b.code.back().at + b.code.back().length;
            printf("  next %06X flags %02X\n", end, map.at(end));
        }
    std::sort(sizes.begin(), sizes.end());
    size_t code = map.coverage().code, in_bodies = 0;
    for (size_t o = 0; o < rom.size(); o++)
        in_bodies += covered[o] && (map.at(o) & codeflag::Code);
    auto pct = [&](size_t n) { return sizes.empty() ? 0 : sizes[std::min(sizes.size() - 1, sizes.size() * n / 100)]; };
    printf("bodies   %zu functions, %zu with a body (%zu not known as code), found in %.1f ms\n", fs.size(), sizes.size(),
           empty, ms);
    printf("sizes    median %zu B, 90%% %zu B, largest %zu B; %.1f pieces each; %zu truncated at 4 KB, %zu leave "
           "through a table\n",
           pct(50), pct(90), sizes.empty() ? 0 : sizes.back(), sizes.empty() ? 0.0 : double(pieces) / sizes.size(),
           truncated, open);
    printf("cover    %zu of %zu code bytes the map knows are in some function's body (%.0f%%)\n", in_bodies, code,
           code ? 100.0 * in_bodies / code : 0.0);
    auto log = calls.combined();
    auto busy = busiest(fs, 10);
    std::vector<InPlay> shallow, deep;
    for (auto &f : busy)
    {
        shallow.push_back({ f.entry, false });
        deep.push_back({ f.entry, true });
    }
    t0 = std::chrono::steady_clock::now();
    auto deep_mask = guard_mask(deep, bodies, &log);
    double guard_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("guards   the 10 busiest: %zu bytes shallow, %zu with what they call (%.1f ms)\n",
           guard_mask(shallow, bodies, &log).count(Target::ROM), deep_mask.count(Target::ROM), guard_ms);
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
        swappable += t.swappable();
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

struct ArmResult
{
    int running = 0;   // the CPU not stopped, no reboot, some sign of life in the last 5 s
    int alive = 0;     // ...and the picture still changing at the end
    int used = 0;      // runs where the game read a changed entry
    int differed = 0;  // runs whose last picture differs from the clean run's
};

// One unguarded run from a power cycle: is the game alive at the end, did it read `entries`.
bool run_alive(int frames, const std::vector<uint32_t> &entries, bool &used, uint64_t &last_picture, bool &running)
{
    access.reset_console();
    CodeMap seen(Memory.CalculatedSize);
    RomTracer tracer;
    tracer.start(seen);
    uint64_t picture = 0;
    int moving_late = 0, nmi_late = 0;
    uint32_t reboots = access.sample().reboots;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        auto signals = access.sample();
        if (signals.picture_valid && signals.picture_hash != picture && i >= frames - 300)
            moving_late++;
        if (signals.nmi_enabled && i >= frames - 300)
            nmi_late++;
        if (signals.picture_valid)
            picture = signals.picture_hash;
    }
    tracer.stop();
    auto end = access.sample();
    used = false;
    for (auto o : entries)
        used |= (seen.at(o) & (codeflag::Data | codeflag::Code)) != 0;
    last_picture = picture;
    // Some games (Pocky & Rocky) keep the frame interrupt off and time frames otherwise, so
    // either sign of life in the last 5 s will do.
    running = !end.cpu_halted && !end.emulation_stopped && end.reboots == reboots && (nmi_late || moving_late);
    return running && moving_late >= 3;
}

int run_swaps(int frames, int seeds, int count)
{
    auto start = headless::save_state();
    CodeMap map(Memory.CalculatedSize);
    JumpLog log;
    RomTracer tracer;
    if (!tracer.start(map, &log))
        return 1;
    access.reset_console(); // learn from a boot, as the swapped runs will start
    headless::run_frames(frames);
    tracer.stop();
    std::span<const uint8_t> clean(rom.clean().data(), rom.clean().size());
    auto found = find_jump_tables(log, clean, map.flags(), SnesCoreAccess::rom_offsets());
    auto slots = swap_slots(found.tables, 0, uint32_t(clean.size() - 1), false);
    printf("tables   %zu found, %zu entries to swap, %d seeds of %d entries, %d frames each\n", found.tables.size(),
           slots.size(), seeds, count, frames);
    if (slots.empty())
        return 1;

    ArmResult swap, random, plain;
    for (int seed = 1; seed <= seeds; seed++)
    {
        Rng pick_rng(uint64_t(seed) * 0x9E3779B97F4A7C15ULL);
        std::vector<uint32_t> picks;
        for (int i = 0; i < count && picks.size() < slots.size(); i++)
        {
            auto p = uint32_t(pick_rng.range(0, int64_t(slots.size()) - 1));
            if (std::find(picks.begin(), picks.end(), p) == picks.end())
                picks.push_back(p);
        }
        std::vector<uint32_t> entries;
        for (auto p : picks)
            for (int i = 0; i < slots[p].stride; i++)
                entries.push_back(slots[p].offset + uint32_t(i));

        uint64_t clean_picture = 0;
        for (int arm = 2; arm >= 0; arm--) // clean first, for the picture to compare with
        {
            headless::load_state(start);
            rom.restore_clean();
            Rng rng{ uint64_t(seed) };
            if (arm == 0)
                apply_pointer_swap(slots, picks, false, rom, rng);
            else if (arm == 1)
                for (auto o : entries)
                    rom.write(Target::ROM, o, uint8_t(rng.range(0, 255)));
            bool used = false, running = false;
            uint64_t picture = 0;
            bool alive = run_alive(frames, entries, used, picture, running);
            if (arm == 2)
                clean_picture = picture;
            auto &r = arm == 0 ? swap : arm == 1 ? random : plain;
            r.alive += alive;
            r.running += running;
            r.used += used;
            r.differed += picture != clean_picture;
            if (getenv("CORRUPT_SWAPS_VERBOSE"))
                printf("  seed %2d %-6s %s%s\n", seed, arm == 0 ? "swap" : arm == 1 ? "random" : "clean",
                       alive ? "moving " : running ? "running" : "dead   ", used ? ", entries read" : "");
        }
    }
    rom.restore_clean();
    auto line = [&](const char *name, const ArmResult &r) {
        printf("%-8s %2d of %d running, %2d of them moving; %2d read a changed entry, %2d ended on another picture than clean\n",
               name, r.running, seeds, r.alive, r.used, r.differed);
    };
    line("swap", swap);
    line("random", random);
    line("clean", plain);
    return 0;
}

// --kill [seeds=10] [count=6]: learns the call graph (and the code map) from `frames` of the
// clean game, then per seed plays `frames` from the warm-up's end three ways, unguarded:
// turning one of the 64 busiest functions off every frames/(count+1) frames (#34), writing a
// random byte over one opcode the map knows at the same moments (the control), and clean. Is
// the game still running and moving at the end, did it run the changed byte, and does its
// last picture differ from the clean run's.
int run_kill(int frames, int seeds, int count)
{
    auto start = headless::save_state();
    CodeMap map(Memory.CalculatedSize);
    CallKeeper calls;
    RomTracer tracer;
    if (!tracer.start(map, nullptr, &calls.mine()))
        return 1;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        calls.end_frame();
    }
    tracer.stop();
    auto functions = busiest(calls.functions(), 64);
    std::vector<uint32_t> opcodes;
    for (uint32_t o = 0; o < map.size(); o++)
        if ((map.at(o) & codeflag::Code) == codeflag::Opcode && !rom.locked_mask().is_hot(Target::ROM, o))
            opcodes.push_back(o);
    printf("learned  %zu functions (picking from the %zu busiest), %zu opcodes; %d seeds, %d changes each, %d frames\n",
           calls.functions().size(), functions.size(), opcodes.size(), seeds, count, frames);
    if (functions.empty() || opcodes.empty())
        return 1;
    int every = frames / (count + 1);

    ArmResult kill, random, plain;
    for (int seed = 1; seed <= seeds; seed++)
    {
        Rng pick_rng(uint64_t(seed) * 0x9E3779B97F4A7C15ULL);
        std::vector<uint32_t> killed, bytes;
        for (int i = 0; i < count; i++)
        {
            uint32_t f;
            do
                f = uint32_t(pick_rng.range(0, int64_t(functions.size()) - 1));
            while (functions.size() > size_t(count) &&
                   std::find(killed.begin(), killed.end(), functions[f].entry) != killed.end());
            killed.push_back(functions[f].entry);
            bytes.push_back(opcodes[size_t(pick_rng.range(0, int64_t(opcodes.size()) - 1))]);
        }
        uint64_t clean_picture = 0;
        for (int arm = 2; arm >= 0; arm--) // clean first, for the picture to compare with
        {
            headless::load_state(start);
            rom.restore_clean();
            Rng rng{ uint64_t(seed) };
            CodeMap seen(Memory.CalculatedSize);
            RomTracer watch;
            watch.start(seen);
            uint64_t picture = 0;
            int moving_late = 0, nmi_late = 0;
            uint32_t reboots = access.sample().reboots;
            std::vector<uint32_t> changed;
            for (int i = 0; i < frames; i++)
            {
                if (i && i % every == 0 && changed.size() < size_t(count))
                {
                    size_t k = changed.size();
                    if (arm == 0)
                    {
                        auto it = std::find_if(functions.begin(), functions.end(),
                                               [&](const Function &f) { return f.entry == killed[k]; });
                        rom.write(Target::ROM, killed[k], return_byte(it->type));
                        changed.push_back(killed[k]);
                    }
                    else if (arm == 1)
                    {
                        rom.write(Target::ROM, bytes[k], uint8_t(rng.range(0, 255)));
                        changed.push_back(bytes[k]);
                    }
                    else
                        changed.push_back(kNoEntry);
                    if (changed.back() != kNoEntry) // "ran" means since its change
                        seen.data()[changed.back()] &= uint8_t(~codeflag::Touched);
                }
                headless::run_frames(1);
                auto signals = access.sample();
                if (signals.picture_valid && signals.picture_hash != picture && i >= frames - 300)
                    moving_late++;
                if (signals.nmi_enabled && i >= frames - 300)
                    nmi_late++;
                if (signals.picture_valid)
                    picture = signals.picture_hash;
            }
            watch.stop();
            auto end = access.sample();
            bool used = false;
            for (uint32_t o : changed)
                used |= o != kNoEntry && (seen.at(o) & codeflag::Touched);
            bool running = !end.cpu_halted && !end.emulation_stopped && end.reboots == reboots && (nmi_late || moving_late);
            bool alive = running && moving_late >= 3;
            if (arm == 2)
                clean_picture = picture;
            auto &r = arm == 0 ? kill : arm == 1 ? random : plain;
            r.alive += alive;
            r.running += running;
            r.used += used;
            r.differed += picture != clean_picture;
            if (getenv("CORRUPT_KILL_VERBOSE"))
                printf("  seed %2d %-6s %s%s\n", seed, arm == 0 ? "kill" : arm == 1 ? "random" : "clean",
                       alive ? "moving " : running ? "running" : "dead   ", used ? ", ran a changed byte" : "");
        }
    }
    rom.restore_clean();
    auto line = [&](const char *name, const ArmResult &r) {
        printf("%-8s %2d of %d running, %2d of them moving; %2d ran a changed byte, %2d ended on another picture than clean\n",
               name, r.running, seeds, r.alive, r.used, r.differed);
    };
    line("kill", kill);
    line("random", random);
    line("clean", plain);
    return 0;
}

// --change [seeds] [count]: like --kill, but changing how functions work (#60): every
// frames/(count+1) frames one change (a new number, the opposite branch, or the other half of
// a math pair) in the body of one of the 64 busiest functions, against the same kinds of change
// anywhere in the code the map knows, and a clean run. Unguarded.
int run_change(int frames, int seeds, int count)
{
    auto start = headless::save_state();
    CodeMap map(Memory.CalculatedSize);
    CallKeeper calls;
    RomTracer tracer;
    if (!tracer.start(map, nullptr, &calls.mine()))
        return 1;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        calls.end_frame();
    }
    tracer.stop();
    auto where = SnesCoreAccess::rom_addresses();
    const auto &clean = rom.clean();
    // CORRUPT_CHANGE_KIND=number|branch|math: only that kind, to see which keeps a game going.
    const char *only_kind = getenv("CORRUPT_CHANGE_KIND");
    auto wanted_kind = only_kind ? parse_change_kind(only_kind) : std::nullopt;
    auto changeable = [&](std::vector<ChangeOption> os) {
        std::erase_if(os, [&](const ChangeOption &o) {
            if (o.kind != ChangeKind::Number && o.kind != ChangeKind::Branch && o.kind != ChangeKind::Math)
                return true;
            if (wanted_kind && o.kind != *wanted_kind)
                return true;
            for (uint32_t i = 0; i < o.length; i++)
                if (rom.locked_mask().is_hot(Target::ROM, o.at + i))
                    return true;
            return false;
        });
        return os;
    };
    // Inside: the busiest functions' bodies. Anywhere: every instruction the map knows.
    // Low: the inside ones whose value goes somewhere low risk (#77).
    std::vector<ChangeOption> inside, anywhere, low;
    for (auto &f : busiest(calls.functions(), 64))
    {
        auto body = function_body(clean, map.flags(), f.entry, where);
        auto os = changeable(change_options(body, clean, map.flags(), where));
        inside.insert(inside.end(), os.begin(), os.end());
        for (auto &o : os)
            if (risk_of(feed_of(o, body, clean, map.flags(), where).feed) == Risk::Low)
                low.push_back(o);
    }
    std::sort(low.begin(), low.end(), [](auto &a, auto &b) { return a.at < b.at; });
    low.erase(std::unique(low.begin(), low.end(), [](auto &a, auto &b) { return a.at == b.at; }), low.end());
    std::sort(inside.begin(), inside.end(), [](auto &a, auto &b) { return a.at < b.at; });
    inside.erase(std::unique(inside.begin(), inside.end(), [](auto &a, auto &b) { return a.at == b.at; }), inside.end());
    FunctionBody all;
    for (uint32_t o = 0; o < map.size(); o++)
        if (map.at(o) & codeflag::Opcode)
            if (auto in = instruction_at(clean, map.flags(), o, where); in && in->start == o)
                all.code.push_back({ o, uint8_t(in->length), in->both_widths });
    anywhere = changeable(change_options(all, clean, map.flags(), where));
    auto kinds = [](const std::vector<ChangeOption> &os) {
        size_t n[3] = {};
        for (auto &o : os)
            n[o.kind == ChangeKind::Number ? 0 : o.kind == ChangeKind::Branch ? 1 : 2]++;
        return std::to_string(n[0]) + " numbers, " + std::to_string(n[1]) + " branches, " + std::to_string(n[2]) + " math";
    };
    printf("learned  %zu functions; inside the 64 busiest: %s; of them low risk: %s; anywhere: %s; %d seeds, %d "
           "changes each, %d frames\n",
           calls.functions().size(), kinds(inside).c_str(), kinds(low).c_str(), kinds(anywhere).c_str(), seeds, count,
           frames);
    if (inside.empty() || anywhere.empty() || low.empty())
        return 1;
    int every = frames / (count + 1);
    auto bytes_for = [](const ChangeOption &o, Rng &rng) {
        switch (o.kind)
        {
        case ChangeKind::Number: {
            uint32_t v;
            do
                v = uint32_t(rng.range(0, o.width == 2 ? 0xFFFF : 0xFF));
            while (v == o.value);
            return number_bytes(o, v);
        }
        case ChangeKind::Branch: return branch_bytes(o, BranchTo::Opposite);
        default: return math_bytes(o);
        }
    };

    ArmResult in_arm, any_arm, low_arm, plain;
    for (int seed = 1; seed <= seeds; seed++)
    {
        Rng pick_rng(uint64_t(seed) * 0x9E3779B97F4A7C15ULL);
        std::vector<ChangeOption> in_picks, any_picks, low_picks;
        for (int i = 0; i < count; i++)
        {
            in_picks.push_back(inside[size_t(pick_rng.range(0, int64_t(inside.size()) - 1))]);
            any_picks.push_back(anywhere[size_t(pick_rng.range(0, int64_t(anywhere.size()) - 1))]);
        }
        Rng low_rng(uint64_t(seed) * 0xD1B54A32D192ED03ULL); // its own draws: the other arms pick as before
        for (int i = 0; i < count; i++)
            low_picks.push_back(low[size_t(low_rng.range(0, int64_t(low.size()) - 1))]);
        uint64_t clean_picture = 0;
        for (int arm = 3; arm >= 0; arm--) // clean first, for the picture to compare with
        {
            headless::load_state(start);
            rom.restore_clean();
            Rng rng{ uint64_t(seed) };
            CodeMap seen(Memory.CalculatedSize);
            RomTracer watch;
            watch.start(seen);
            uint64_t picture = 0;
            int moving_late = 0, nmi_late = 0;
            uint32_t reboots = access.sample().reboots;
            std::vector<uint32_t> changed;
            for (int i = 0; i < frames; i++)
            {
                if (i && i % every == 0 && changed.size() < size_t(count))
                {
                    size_t k = changed.size();
                    if (arm < 3)
                    {
                        auto &o = arm == 0 ? in_picks[k] : arm == 1 ? any_picks[k] : low_picks[k];
                        auto b = bytes_for(o, rng);
                        for (size_t j = 0; j < b.size(); j++)
                            rom.write(Target::ROM, uint32_t(o.at + j), b[j]);
                        changed.push_back(o.at);
                        seen.data()[o.at] &= uint8_t(~codeflag::Touched); // "ran" means since its change
                    }
                    else
                        changed.push_back(kNoEntry);
                }
                headless::run_frames(1);
                auto signals = access.sample();
                if (signals.picture_valid && signals.picture_hash != picture && i >= frames - 300)
                    moving_late++;
                if (signals.nmi_enabled && i >= frames - 300)
                    nmi_late++;
                if (signals.picture_valid)
                    picture = signals.picture_hash;
            }
            watch.stop();
            auto end = access.sample();
            bool used = false;
            for (uint32_t o : changed)
                used |= o != kNoEntry && (seen.at(o) & codeflag::Touched);
            bool running = !end.cpu_halted && !end.emulation_stopped && end.reboots == reboots && (nmi_late || moving_late);
            bool alive = running && moving_late >= 3;
            if (arm == 3)
                clean_picture = picture;
            auto &r = arm == 0 ? in_arm : arm == 1 ? any_arm : arm == 2 ? low_arm : plain;
            r.alive += alive;
            r.running += running;
            r.used += used;
            r.differed += picture != clean_picture;
            if (getenv("CORRUPT_KILL_VERBOSE"))
                printf("  seed %2d %-8s %s%s\n", seed, arm == 0 ? "inside" : arm == 1 ? "anywhere" : arm == 2 ? "low risk" : "clean",
                       alive ? "moving " : running ? "running" : "dead   ", used ? ", ran a changed byte" : "");
        }
    }
    rom.restore_clean();
    auto line = [&](const char *name, const ArmResult &r) {
        printf("%-8s %2d of %d running, %2d of them moving; %2d ran a changed byte, %2d ended on another picture than clean\n",
               name, r.running, seeds, r.alive, r.used, r.differed);
    };
    line("inside", in_arm);
    line("anywhere", any_arm);
    line("low risk", low_arm);
    line("clean", plain);
    return 0;
}

// --risk [count] [trial] [kind]: does what a change feeds into (corrupt/change_risk.h) say whether
// the game survives it? Learns `frames` from the warmed-up start, then from that moment tries
// `count` single changes from the 64 busiest functions' bodies (numbers, branches, math,
// skipped calls, a quarter each), one per run of `trial` frames, unguarded, and counts per
// class: still moving, frozen (running but the picture still) or dead. Numbers are tried twice:
// any new value, and nudged (±1, ×2, ÷2). CORRUPT_RISK_TSV=<file> writes one line per run.
// What a run ahead would have seen of one run, frame by frame (--foresee, #78).
struct Trace
{
    std::vector<uint64_t> picture;
    std::vector<uint8_t> nmi, stopped, reboot, runaway;
    int first_use = -1; // the frame the changed bytes first ran (1-based), -1: never
};

// Would a test of `window` frames have called this run a failure? Checks, by name:
// "definite": what #52's run ahead sees (CPU stopped, deadlock, a reboot, 30 frames running
// from I/O or open bus); "still": the picture is one picture for the window's last `hold`
// frames while the clean run's picture changed at least 3 times in them; "still-any": the
// same in any `hold` frames of the window; "nmi": NMI off for the last `hold` frames while
// the clean run's was on in some of them.
bool foresees(const std::string &check, const Trace &t, const Trace &clean, int window, int hold = 60)
{
    int n = std::min<int>(window, int(t.picture.size()));
    if (check == "definite")
    {
        int run = 0;
        for (int i = 0; i < n; i++)
        {
            if (t.stopped[i] || t.reboot[i])
                return true;
            run = t.runaway[i] ? run + 1 : 0;
            if (run >= 30)
                return true;
        }
        return false;
    }
    if (int(t.picture.size()) < window)
        return foresees("definite", t, clean, window); // it stopped early: the CPU did
    auto still_at = [&](int end) { // frames (end-hold, end]
        if (end < hold || end > int(clean.picture.size()))
            return false;
        int clean_changes = 0;
        for (int i = end - hold + 1; i < end; i++)
        {
            if (t.picture[i] != t.picture[end - hold])
                return false;
            clean_changes += clean.picture[i] != clean.picture[i - 1];
        }
        return clean_changes >= 3;
    };
    if (check == "still")
        return still_at(window);
    if (check == "still-any")
    {
        for (int end = hold; end <= window; end++)
            if (still_at(end))
                return true;
        return false;
    }
    if (check == "nmi")
    {
        bool clean_on = false;
        for (int i = window - hold; i < window; i++)
        {
            if (t.nmi[i])
                return false;
            clean_on |= i < int(clean.nmi.size()) && clean.nmi[i];
        }
        return clean_on;
    }
    return false;
}

// mode: 0 --risk, 1 --foresee (#78: what a test of 3/5/10 s would have seen), 2 --try (#78:
// try_change itself, 5 s from the same moment, against the 30 s outcome).
int run_risk(int frames, int count, int trial, int only = -1, int mode = 0)
{
    bool foresee = mode == 1;
    CodeMap map(Memory.CalculatedSize);
    CallKeeper calls;
    RomTracer tracer;
    if (!tracer.start(map, nullptr, &calls.mine()))
        return 1;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        calls.end_frame();
    }
    tracer.stop();
    auto mid = headless::save_state();
    auto where = SnesCoreAccess::rom_addresses();
    const auto &clean = rom.clean();

    struct Pick
    {
        ChangeOption option;
        FeedReading feed;
        Function in; // the function whose body it's in
    };
    std::vector<Pick> by_kind[4]; // number, branch, math, skip
    std::set<std::pair<uint32_t, int>> seen_options;
    for (auto &f : busiest(calls.functions(), 64))
    {
        auto body = function_body(clean, map.flags(), f.entry, where);
        for (auto &o : change_options(body, clean, map.flags(), where))
        {
            int k = o.kind == ChangeKind::Number ? 0 : o.kind == ChangeKind::Branch ? 1 : o.kind == ChangeKind::Math ? 2
                    : o.kind == ChangeKind::Skip                                     ? 3
                                                                                     : -1;
            if (k < 0 || !seen_options.insert({ o.at, k }).second)
                continue;
            bool locked = false;
            for (uint32_t i = 0; i < o.length; i++)
                locked |= rom.locked_mask().is_hot(Target::ROM, o.at + i);
            if (!locked)
                by_kind[k].push_back({ o, feed_of(o, body, clean, map.flags(), where), f });
        }
    }
    printf("learned  %zu functions; options in the 64 busiest: %zu numbers, %zu branches, %zu math, %zu calls; "
           "%d changes, %d frames each\n",
           calls.functions().size(), by_kind[0].size(), by_kind[1].size(), by_kind[2].size(), by_kind[3].size(), count,
           trial);

    // One run from the moment: the end picture, and whether the game is alive, frozen or dead.
    enum Outcome
    {
        Moving,
        Frozen,
        Dead
    };
    auto run_one = [&](const std::vector<std::pair<uint32_t, uint8_t>> &writes, bool &ran, uint64_t &picture,
                       Trace *trace) {
        headless::load_state(mid);
        rom.restore_clean();
        CodeMap seen(Memory.CalculatedSize);
        RomTracer watch;
        watch.start(seen);
        for (auto [at, b] : writes)
        {
            rom.write(Target::ROM, at, b);
            seen.data()[at] &= uint8_t(~codeflag::Touched);
        }
        picture = 0;
        int moving_late = 0, nmi_late = 0;
        uint32_t reboots = access.sample().reboots;
        for (int i = 0; i < trial; i++)
        {
            headless::run_frames(1);
            auto signals = access.sample();
            if (trace)
            {
                trace->picture.push_back(signals.picture_valid ? signals.picture_hash
                                         : trace->picture.empty() ? 0
                                                                  : trace->picture.back());
                trace->nmi.push_back(signals.nmi_enabled);
                trace->stopped.push_back(signals.cpu_halted || signals.emulation_stopped);
                trace->reboot.push_back(signals.reboots != reboots);
                trace->runaway.push_back(signals.pc_class != PcClass::Ok);
                if (trace->first_use < 0)
                    for (auto [at, b] : writes)
                        if (seen.at(at) & codeflag::Touched)
                            trace->first_use = i + 1;
            }
            if (signals.picture_valid && signals.picture_hash != picture && i >= trial - 300)
                moving_late++;
            if (signals.nmi_enabled && i >= trial - 300)
                nmi_late++;
            if (signals.picture_valid)
                picture = signals.picture_hash;
            if (signals.cpu_halted || signals.emulation_stopped)
                break;
        }
        watch.stop();
        auto end = access.sample();
        ran = false;
        for (auto [at, b] : writes)
            ran |= (seen.at(at) & codeflag::Touched) != 0;
        bool running = !end.cpu_halted && !end.emulation_stopped && end.reboots == reboots && (nmi_late || moving_late);
        return !running ? Dead : moving_late >= 3 ? Moving : Frozen;
    };
    bool clean_ran = false;
    uint64_t clean_picture = 0;
    Trace clean_trace;
    auto clean_outcome = run_one({}, clean_ran, clean_picture, foresee ? &clean_trace : nullptr);
    struct Seen
    {
        Trace trace;
        bool failed;
        Risk risk;
    };
    std::vector<Seen> foreseen;
    // --try: the engine runs in the frame hook, as in the app, so try_change's flips land.
    RunAhead runner;
    std::map<std::string, std::pair<int, int>> tried; // verdict -> (failed in 30 s, kept moving)
    double try_ms = 0;
    int tries = 0;
    if (mode == 2)
    {
        Recipe none;
        none.name = "try";
        engine.set_recipe(none);
        engine.set_active(true);
        S9xCorruptFrameHook = &frame_hook;
    }
    printf("clean    %s\n", clean_outcome == Moving ? "moving" : clean_outcome == Frozen ? "frozen" : "dead");

    FILE *tsv = getenv("CORRUPT_RISK_TSV") ? fopen(getenv("CORRUPT_RISK_TSV"), "w") : nullptr;
    if (tsv)
        fprintf(tsv, "at\tkind\tarm\ttext\tfeed\twhy\tran\toutcome\tdiffers\tfunction\tcalls\tcallers\tframes\n");
    struct Tally
    {
        int n = 0, moving = 0, frozen = 0, dead = 0, differs = 0, unran = 0;
    };
    std::map<std::string, Tally> by_feed, by_kind_feed;
    Rng rng{ 0x5EED };
    const char *kind_names[4] = { "number", "branch", "math", "skip" };
    for (int k = 0; k < 4; k++)
    {
        auto &list = by_kind[k];
        std::vector<size_t> order(list.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = i;
        for (size_t i = order.size(); i > 1; i--) // a seeded shuffle
            std::swap(order[i - 1], order[size_t(rng.range(0, int64_t(i) - 1))]);
        if (only >= 0 && k != only)
            continue;
        for (size_t n = 0; n < order.size() && n < size_t(only >= 0 ? count : count / 4); n++)
        {
            auto &p = list[order[n]];
            auto &o = p.option;
            std::vector<std::pair<std::string, std::vector<uint8_t>>> arms;
            if (k == 0)
            {
                uint32_t top = o.width == 2 ? 0xFFFF : 0xFF, v;
                do
                    v = uint32_t(rng.range(0, top));
                while (v == o.value);
                arms.push_back({ "any", number_bytes(o, v) });
                uint32_t nudges[4] = { (o.value + 1) & top, (o.value - 1) & top, (o.value * 2) & top, o.value / 2 };
                uint32_t nv = nudges[rng.range(0, 3)];
                if (nv == o.value)
                    nv = (o.value + 1) & top;
                arms.push_back({ "nudged", number_bytes(o, nv) });
            }
            else if (k == 1)
                arms.push_back({ "opposite", branch_bytes(o, BranchTo::Opposite) });
            else if (k == 2)
                arms.push_back({ "swap", math_bytes(o) });
            else
                arms.push_back({ "skip", skip_bytes(o) });
            for (auto &[arm, bytes] : arms)
            {
                std::vector<std::pair<uint32_t, uint8_t>> writes;
                for (size_t j = 0; j < bytes.size(); j++)
                    writes.push_back({ uint32_t(o.at + j), bytes[j] });
                bool ran = false;
                uint64_t picture = 0;
                Trace trace;
                auto outcome = run_one(writes, ran, picture, foresee ? &trace : nullptr);
                if (foresee && ran)
                    foreseen.push_back({ std::move(trace), outcome != Moving, risk_of(p.feed.feed) });
                if (mode == 2 && ran)
                {
                    headless::load_state(mid);
                    rom.restore_clean();
                    CodeMap seen(Memory.CalculatedSize);
                    RomTracer watch;
                    watch.start(seen);
                    Change c;
                    c.at = o.at;
                    c.value = bytes;
                    for (size_t j = 0; j < bytes.size(); j++)
                        c.was.push_back(clean[o.at + j]);
                    auto t0 = std::chrono::steady_clock::now();
                    auto r = try_change(runner, engine, rom, access, { seen.data(), seen.size() }, {}, c, 300, true);
                    try_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    tries++;
                    watch.stop();
                    const char *names[] = { "running", "froze", "crashed", "unused", "unclear", "failed" };
                    auto &t = tried[names[r.kind]];
                    (outcome != Moving ? t.first : t.second)++;
                    if (getenv("CORRUPT_TRY_VERBOSE"))
                        printf("  %06X %-8s %-7s %s\n", o.at, outcome != Moving ? "failed" : "moving", names[r.kind],
                               r.text.c_str());
                }
                bool differs = picture != clean_picture;
                const char *out_name = outcome == Moving ? "moving" : outcome == Frozen ? "frozen" : "dead";
                if (tsv)
                    fprintf(tsv, "%06X\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%d\t%06X\t%u\t%u\t%llu\n", o.at, kind_names[k],
                            arm.c_str(), o.text.c_str(), feed_name(p.feed.feed), p.feed.why.c_str(), ran, out_name, differs,
                            p.in.entry, p.in.calls, p.in.callers, (unsigned long long)p.in.frames);
                std::string feed = feed_name(p.feed.feed);
                for (auto *t : { &by_feed[feed + (k == 0 ? " (" + arm + ")" : "")],
                                 &by_kind_feed[std::string(kind_names[k]) + " / " + arm] })
                {
                    if (!ran)
                    {
                        t->unran++;
                        continue;
                    }
                    t->n++;
                    t->moving += outcome == Moving;
                    t->frozen += outcome == Frozen;
                    t->dead += outcome == Dead;
                    t->differs += differs;
                }
            }
        }
    }
    if (tsv)
        fclose(tsv);
    rom.restore_clean();
    auto print = [](const char *title, const std::map<std::string, Tally> &m) {
        printf("\n%-28s %4s %7s %7s %5s %8s %6s\n", title, "ran", "moving", "frozen", "dead", "differs", "unran");
        for (auto &[name, t] : m)
            printf("%-28s %4d %7d %7d %5d %8d %6d\n", name.c_str(), t.n, t.moving, t.frozen, t.dead, t.differs, t.unran);
    };
    print("by what it feeds", by_feed);
    print("by kind", by_kind_feed);
    if (mode == 2)
    {
        S9xCorruptFrameHook = nullptr;
        engine.set_active(false);
        printf("\ntry 5 s   %d tries, %.0f ms each (both runs)\n%-10s %8s %8s\n", tries, tries ? try_ms / tries : 0.0,
               "verdict", "failed", "moving");
        for (auto &[name, t] : tried)
            printf("%-10s %8d %8d\n", name.c_str(), t.first, t.second);
    }
    if (foresee)
    {
        // Would a test of 3, 5 or 10 s have told the failures from the survivors? (#78)
        int failed = 0, survived = 0;
        for (auto &f : foreseen)
            (f.failed ? failed : survived)++;
        printf("\nforesee  %d ran in %d frames: %d failed, %d kept moving\n", failed + survived, trial, failed, survived);
        printf("%-10s %6s %8s %8s %8s %8s\n", "check", "window", "unran", "caught", "missed", "false");
        for (int window : { 180, 300, 600 })
            for (const char *check : { "definite", "still", "still-any", "nmi", "any" })
            {
                int unran = 0, caught = 0, missed = 0, false_alarms = 0;
                for (auto &f : foreseen)
                {
                    bool said = std::string(check) == "any"
                                    ? foresees("definite", f.trace, clean_trace, window) ||
                                          foresees("still-any", f.trace, clean_trace, window) ||
                                          foresees("nmi", f.trace, clean_trace, window)
                                    : foresees(check, f.trace, clean_trace, window);
                    if (f.trace.first_use < 0 || f.trace.first_use > window)
                    {
                        unran++;
                        continue;
                    }
                    if (f.failed)
                        (said ? caught : missed)++;
                    else
                        false_alarms += said;
                }
                printf("%-10s %5.0fs %8d %8d %8d %8d\n", check, window / 60.0, unran, caught, missed, false_alarms);
            }
        int risk_caught = 0, risk_false = 0;
        for (auto &f : foreseen)
            if (f.risk == Risk::Medium || f.risk == Risk::High)
                (f.failed ? risk_caught : risk_false)++;
        printf("%-10s %6s %8s %8d %8d %8d   (Medium or High in Change It)\n", "risk", "-", "-", risk_caught,
               failed - risk_caught, risk_false);
        // When the failures showed: frames from first use to the first definite or still sign.
        printf("\nfailures by when a sign showed (from first use):");
        int buckets[5] = {}; // <1 s, 1-3 s, 3-5 s, 5-10 s, later or never
        for (auto &f : foreseen)
            if (f.failed && f.trace.first_use > 0)
            {
                int at = -1;
                for (int w = f.trace.first_use; w <= int(f.trace.picture.size()) && at < 0; w += 10)
                    if (foresees("definite", f.trace, clean_trace, w) || foresees("still-any", f.trace, clean_trace, w) ||
                        foresees("nmi", f.trace, clean_trace, w))
                        at = w - f.trace.first_use;
                buckets[at < 0 ? 4 : at < 60 ? 0 : at < 180 ? 1 : at < 300 ? 2 : at < 600 ? 3 : 4]++;
            }
        printf(" <1 s %d, 1-3 s %d, 3-5 s %d, 5-10 s %d, later or never %d\n", buckets[0], buckets[1], buckets[2],
               buckets[3], buckets[4]);
    }
    return 0;
}

// A code map saved earlier (CORRUPT_CODEMAP, --codemap's file) merged into `map`.
bool load_saved_map(CodeMap &map)
{
    const char *file = getenv("CORRUPT_CODEMAP");
    if (!file)
        return false;
    std::ifstream in(file, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto loaded = decode_code_map(bytes, error);
    if (loaded && map.merge(*loaded))
    {
        printf("codemap  %zu code bytes known from %s\n", map.coverage().code, file);
        return true;
    }
    printf("codemap  %s not used: %s\n", file, error.empty() ? "another ROM size" : error.c_str());
    return false;
}

std::string flag_text(uint8_t f)
{
    std::string out;
    if (f & codeflag::Opcode)
        out += "opcode ";
    if (f & codeflag::Operand)
        out += "operand ";
    if (f & codeflag::Data)
        out += "data ";
    if (f & codeflag::Dma)
        out += "dma ";
    return out.empty() ? "never seen" : out.substr(0, out.size() - 1);
}

// --derail-fp: on the clean game, every instruction counts as just after a changed byte,
// so every landing the watch notes is a false alarm the game's own code would raise. The
// map is learned from a boot for `learn` frames first, and keeps learning, as in play.
// --nmi: the clean game from power-on for `frames` frames, and every stretch of 30 frames
// or more with the frame interrupt (NMI) off: what the crash guard's "NMI off" threshold
// has to stay above.
int run_nmi(int frames)
{
    rom.restore_clean();
    access.reset_console();
    int off_since = -1, longest = 0, longest_at = 0;
    for (int f = 1; f <= frames + 1; f++)
    {
        bool on = true;
        if (f <= frames)
        {
            headless::run_frames(1);
            on = access.sample().nmi_enabled;
        }
        if (!on && off_since < 0)
            off_since = f;
        if (on && off_since >= 0)
        {
            int len = f - off_since;
            if (len >= 30)
                printf("nmi off  frames %5d-%5d  %.1f s\n", off_since, f - 1, len / 60.0);
            if (len > longest)
            {
                longest = len;
                longest_at = off_since;
            }
            off_since = -1;
        }
    }
    printf("longest  %.1f s from frame %d (the guard's threshold: 5.0 s)\n", longest / 60.0, longest_at);
    return 0;
}

int run_derail_fp(int frames, int learn)
{
    CodeMap map(Memory.CalculatedSize);
    load_saved_map(map);
    RomTracer tracer;
    if (!tracer.start(map))
        return 1;
    access.reset_console();
    headless::run_frames(learn);
    DerailWatch watch;
    watch.hold_open(true);
    tracer.watch_derails(&watch);
    auto offsets = SnesCoreAccess::rom_offsets();
    std::map<Landing, int> kinds;
    int shown = 0;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        auto note = tracer.take_derail();
        if (!note)
            continue;
        kinds[note->kind]++;
        if (shown++ < 12)
        {
            auto o = offsets.offset(note->pc);
            uint8_t f = o ? map.at(*o) : 0;
            const uint8_t *code = o && *o < Memory.CalculatedSize ? Memory.ROM + *o : nullptr;
            printf("  frame %6d  %s  %-40s %s, byte %s\n", learn + i + 1, describe_pc(note->pc).c_str(),
                   landing_text(note->kind), o ? flag_text(f).c_str() : "not ROM",
                   code ? hex(*code).c_str() : "?");
        }
    }
    tracer.stop();
    int total = 0;
    for (auto [k, n] : kinds)
        total += n;
    printf("false    %d landings in %d frames after learning %d (%.1f an hour), map %zu code bytes\n", total, frames,
           learn, total * 216000.0 / frames, map.coverage().code);
    for (auto [k, n] : kinds)
        printf("  %-40s %5d (%.1f an hour)\n", landing_text(k), n, n * 216000.0 / frames);
    return 0;
}

struct InstantArm
{
    int caught = 0, gave_up = 0, reverts = 0, alive = 0, running = 0;
    uint64_t lost = 0;          // frames thrown away: rollbacks' distance, one per instant revert
    size_t changed = 0, used = 0, fixed = 0;
    double keep_ms = 0;
    int keeps = 0;
    std::map<Landing, int> kinds;
    // Test before applying (#52).
    int tested = 0, kept = 0, rerolled = 0, skipped = 0, runs = 0;
    size_t written = 0, untried = 0; // bytes that went ahead, and of them those not used in the window
    double ahead_ms = 0;
    // Eyes open after a landing (#57): changed bytes seen used only thanks to it.
    uint64_t seen_after_landing = 0;
    void add(const InstantArm &o)
    {
        seen_after_landing += o.seen_after_landing;
        caught += o.caught;
        gave_up += o.gave_up;
        reverts += o.reverts;
        alive += o.alive;
        running += o.running;
        lost += o.lost;
        changed += o.changed;
        used += o.used;
        fixed += o.fixed;
        keep_ms += o.keep_ms;
        keeps += o.keeps;
        for (auto [k, n] : o.kinds)
            kinds[k] += n;
        tested += o.tested;
        kept += o.kept;
        rerolled += o.rerolled;
        skipped += o.skipped;
        runs += o.runs;
        ahead_ms += o.ahead_ms;
        written += o.written;
        untried += o.untried;
    }
};

// One guarded run of `recipe` from `start`, as the app does it, with or without the instant revert.
void instant_arm(const Recipe &recipe, int frames, const CodeMap &learned, bool on, const std::vector<uint8_t> &start,
                 InstantArm &a, bool test_first = false, bool blind = false)
{
    headless::load_state(start);
    rom.restore_clean();
    CodeMap map = learned;
    std::vector<uint8_t> aim_flags;
    engine.set_code_flags([&] {
        aim_flags = map.flags();
        for (auto &f : aim_flags)
            f &= codeflag::All;
        return std::span<const uint8_t>(aim_flags);
    });
    RomTracer tracer;
    tracer.start(map);
    tracer.set_blind_after_landing(blind);
    DerailWatch watch;
    if (on)
        tracer.watch_derails(&watch);
    RomBlame blame(rom);
    blame.attach(map.data(), map.size());
    blame.set_policy(recipe.rom_policy.value_or(RomPolicy::Revert), recipe.seed);
    CrashGuard guard;
    guard.set_byte_blame(&blame);
    guard.set_policy(recipe.crash_policy.value_or(PolicyKind::Reroll));
    guard.set_detector_config(recipe.crash_detection.value_or(DetectorConfig{}));
    InstantRevert instant;
    instant.set_enabled(on);
    TestAhead ahead;
    ahead.set_config({ .frames = test_first ? uint32_t(std::lround(recipe.test_first.value_or(3.0) * 60)) : 0u });

    engine.set_recipe(recipe);
    start_run();
    guard.between_frames(engine, access);
    instant.keep(access, engine.current_frame(), on);
    S9xCorruptFrameHook = &frame_hook;
    bool verbose = getenv("CORRUPT_INSTANT_VERBOSE");
    uint32_t reboots = access.sample().reboots;
    uint64_t picture = 0;
    int moving_late = 0, nmi_late = 0;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        if (on)
            if (auto r = instant.after_frame(tracer.take_derail(), engine.current_frame(), access, blame))
            {
                a.reverts++;
                a.lost++;
                a.fixed += r->offsets.size();
                a.kinds[r->kind]++;
                if (verbose)
                    printf("    frame %5d  %s\n", i + 1, r->summary.c_str());
            }
        auto signals = access.sample();
        bool late = i >= frames - 300;
        if (late && signals.picture_valid && signals.picture_hash != picture)
            moving_late++;
        if (late && signals.nmi_enabled)
            nmi_late++;
        if (signals.picture_valid)
            picture = signals.picture_hash;
        if (late && i == frames - 300)
            reboots = signals.reboots;
        // CORRUPT_WATCH_BYTE=<hex ROM offset>: that byte's map flags and value each frame (the first 400).
        if (static const char *w = getenv("CORRUPT_WATCH_BYTE"); w && i < 400)
        {
            uint32_t o = uint32_t(strtoul(w, nullptr, 16));
            printf("    frame %d (engine %llu): %06X flags %02X, rom %02X (clean %02X), PC %06X\n", i + 1,
                   (unsigned long long)engine.current_frame(), o, map.at(o), rom.read(Target::ROM, o), rom.clean()[o],
                   access.sample().pc);
        }
        blame.collect(engine.current_frame());
        auto out = guard.between_frames(engine, access);
        rom.after_guard(out);
        if (out.kind != GuardOutcome::Nothing)
        {
            a.caught++;
            a.lost += out.verdict.frame - out.restored_frame;
            a.fixed += out.rom_blamed.size();
            if (verbose)
                printf("    frame %5d  %s\n", i + 1, out.summary.c_str());
            if (out.kind == GuardOutcome::GaveUp)
            {
                a.gave_up++;
                break;
            }
        }
        if (on)
        {
            auto t0 = std::chrono::steady_clock::now();
            instant.keep(access, engine.current_frame(), engine.active() && tracer.tracing());
            a.keep_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            a.keeps++;
        }
        if (test_first)
        {
            AheadHooks hooks;
            hooks.take_derail = [&] { return tracer.take_derail(); };
            auto t = ahead.before_frame(engine, rom, access, { map.data(), map.size() }, hooks);
            if (t.kind != AheadResult::Nothing)
            {
                a.tested++;
                a.kept += t.kind == AheadResult::Kept;
                a.rerolled += t.kind == AheadResult::Rerolled;
                a.skipped += t.kind == AheadResult::Skipped;
                a.runs += int(t.runs);
                a.ahead_ms += t.ms;
                a.written += t.written.size();
                a.untried += t.untried.size();
                if (verbose && t.kind != AheadResult::Kept)
                    printf("    frame %5d  %s (%.0f ms)\n", i + 1, t.summary.c_str(), t.ms);
            }
        }
    }
    S9xCorruptFrameHook = nullptr;
    auto end = access.sample();
    bool running = !end.cpu_halted && !end.emulation_stopped && end.reboots == reboots && (nmi_late || moving_late);
    a.running += running;
    a.alive += running && moving_late >= 3;
    a.changed += rom.changed();
    a.used += blame.used();
    a.seen_after_landing += tracer.seen_after_landing();
    tracer.stop();
    engine.set_active(false);
    engine.set_code_flags({});
}

// Two arms per seed over the same learned map: `arms` says, per arm, whether the instant
// revert and the test before applying are on.
struct ArmSetup
{
    bool instant = false, ahead = false, blind = false;
};

int compare_arms(const Recipe &recipe, int frames, int seeds, const std::array<ArmSetup, 2> &arms,
                 const std::array<const char *, 2> &names)
{
    auto start = headless::save_state();
    CodeMap learned(Memory.CalculatedSize);
    std::vector<JumpTable> tables;
    if (!load_saved_map(learned))
    {
        RomTracer tracer;
        JumpLog log;
        if (!tracer.start(learned, &log))
            return 1;
        access.reset_console();
        headless::run_frames(frames);
        tracer.stop();
        std::span<const uint8_t> clean(rom.clean().data(), rom.clean().size());
        tables = find_jump_tables(log, clean, learned.flags(), SnesCoreAccess::rom_offsets()).tables;
        printf("codemap  %zu code bytes and %zu jump tables learned from a clean boot of %d frames\n",
               learned.coverage().code, tables.size(), frames);
    }
    engine.set_jump_tables([&] { return tables; }); // for pointer swaps
    InstantArm totals[2];
    for (int seed = 1; seed <= seeds; seed++)
    {
        Recipe r = recipe;
        r.seed = recipe.seed + uint64_t(seed) - 1;
        for (int arm = 0; arm < 2; arm++)
        {
            InstantArm one;
            if (getenv("CORRUPT_INSTANT_VERBOSE"))
                printf("  seed %llu, %s\n", (unsigned long long)r.seed, names[arm]);
            instant_arm(r, frames, learned, arms[arm].instant, start, one, arms[arm].ahead, arms[arm].blind);
            totals[arm].add(one);
            if (getenv("CORRUPT_INSTANT_VERBOSE"))
                printf("    -> %d caught, %d instant, %d tested (%d re-rolled, %d skipped), %llu "
                       "frames lost, %zu changed at the end, %s\n",
                       one.caught, one.reverts, one.tested, one.rerolled, one.skipped,
                       (unsigned long long)one.lost,
                       one.changed, one.alive ? "moving" : one.running ? "running" : "dead");
        }
    }
    rom.restore_clean();
    engine.set_jump_tables({});
    printf("seeds    %d of %d frames each\n", seeds, frames);
    for (int arm = 0; arm < 2; arm++)
    {
        auto &a = totals[arm];
        printf("%-8s %3d guard rollbacks (%d gave up), %3d instant reverts, %6llu frames lost; at the end %4zu bytes "
               "changed (%zu used), %zu fixed; %d running, %d moving\n",
               names[arm], a.caught, a.gave_up, a.reverts, (unsigned long long)a.lost, a.changed, a.used, a.fixed,
               a.running, a.alive);
        if (a.tested)
            printf("         tested %d firings first: %d kept, %d re-rolled, %d skipped; %d runs ahead, %.0f ms each "
                   "firing on average, %.1f s in all; %zu of the %zu bytes that went ahead weren't used in the window\n",
                   a.tested, a.kept, a.rerolled, a.skipped, a.runs, a.ahead_ms / a.tested, a.ahead_ms / 1000, a.untried,
                   a.written);
        if (a.seen_after_landing)
            printf("         after a landing: %llu uses of changed bytes seen only thanks to #57\n",
                   (unsigned long long)a.seen_after_landing);
    }
    return 0;
}

// --ahead [seeds=5]: like --instant, but both arms have the instant revert, and the second
// tests each live ROM firing first (#52).
int run_ahead(const Recipe &recipe, int frames, int seeds)
{
    return compare_arms(recipe, frames, seeds, { ArmSetup{ true, false }, ArmSetup{ true, true } },
                        { "instant", "ahead" });
}

// --eyes [seeds=5]: both arms with the guard, #51 and #52; the first is blind after a
// landing, as before #57, the second sees the changed bytes run after one.
int run_eyes(const Recipe &recipe, int frames, int seeds)
{
    return compare_arms(recipe, frames, seeds, { ArmSetup{ true, true, true }, ArmSetup{ true, true, false } },
                        { "blind", "eyes" });
}

// --instant [seeds=5]: the recipe played guarded for `frames` frames per seed, twice: the
// guard alone (as before #51), and the guard with the instant revert. The code map is
// learned first from a clean boot (or CORRUPT_CODEMAP), as if the game had been played.
int run_instant(const Recipe &recipe, int frames, int seeds)
{
    auto start = headless::save_state();
    CodeMap learned(Memory.CalculatedSize);
    if (!load_saved_map(learned))
    {
        RomTracer tracer;
        if (!tracer.start(learned))
            return 1;
        access.reset_console();
        headless::run_frames(frames);
        tracer.stop();
        printf("codemap  %zu code bytes learned from a clean boot of %d frames\n", learned.coverage().code, frames);
    }
    InstantArm arms[2];
    for (int seed = 1; seed <= seeds; seed++)
    {
        Recipe r = recipe;
        r.seed = recipe.seed + uint64_t(seed) - 1;
        for (int arm = 0; arm < 2; arm++)
        {
            InstantArm one;
            if (getenv("CORRUPT_INSTANT_VERBOSE"))
                printf("  seed %llu, %s\n", (unsigned long long)r.seed, arm ? "instant revert" : "guard only");
            instant_arm(r, frames, learned, arm == 1, start, one);
            auto &a = arms[arm];
            a.caught += one.caught;
            a.gave_up += one.gave_up;
            a.reverts += one.reverts;
            a.alive += one.alive;
            a.running += one.running;
            a.lost += one.lost;
            a.changed += one.changed;
            a.used += one.used;
            a.fixed += one.fixed;
            a.keep_ms += one.keep_ms;
            a.keeps += one.keeps;
            for (auto [k, n] : one.kinds)
                a.kinds[k] += n;
            if (getenv("CORRUPT_INSTANT_VERBOSE"))
                printf("    -> %d caught, %d instant, %llu frames lost, %zu changed at the end, %s\n", one.caught,
                       one.reverts, (unsigned long long)one.lost, one.changed,
                       one.alive ? "moving" : one.running ? "running" : "dead");
        }
    }
    rom.restore_clean();
    printf("seeds    %d of %d frames each\n", seeds, frames);
    for (int arm = 0; arm < 2; arm++)
    {
        auto &a = arms[arm];
        printf("%-8s %3d guard rollbacks (%d gave up), %3d instant reverts, %6llu frames lost; at the end %4zu bytes "
               "changed (%zu used), %zu fixed; %d running, %d moving\n",
               arm ? "instant" : "guard", a.caught, a.gave_up, a.reverts, (unsigned long long)a.lost, a.changed, a.used,
               a.fixed, a.running, a.alive);
    }
    for (auto [k, n] : arms[1].kinds)
        printf("  %-40s %4d\n", landing_text(k), n);
    if (arms[1].keeps)
        printf("cost     %.3f ms a frame to keep the frame-start checkpoint (%zu bytes)\n", arms[1].keep_ms / arms[1].keeps,
               access.save_state().size());
    return 0;
}

// --keep [seeds=3]: does a kept moment play back exactly (#47)? The recipe is played guarded
// for `frames` frames per seed as the app plays it (the guard, the instant revert and test
// ahead on), with buttons pressed as a player might, recording as the app does. Every 7 s a
// moment is kept and checked: played unseen from its keyframe and compared with the game,
// once drawing its frames and once not, and twice more without its buttons and without its
// writes (each should drift, or the check proves nothing). CORRUPT_KEEP_VERBOSE=1 prints every
// check. CORRUPT_KEEP_DIAG=1 finds where a moment that drifts starts to: the first frame
// whose start differs from the live game's, which blocks of the core's state differ after it,
// and which of the core's globals differed at the keyframe (how #47 found VBlankStarted).

// Every memory in one number, for finding where a playback drifts.
uint64_t state_hash()
{
    uint64_t h = 1469598103934665603ull;
    for (auto t : { Target::WRAM, Target::VRAM, Target::OAM, Target::CGRAM, Target::ARAM, Target::ROM })
    {
        const uint8_t *d = memory.data(t);
        for (size_t i = 0, n = memory.size(t); i < n; i++)
            h = (h ^ (d ? d[i] : memory.read(t, uint32_t(i)))) * 1099511628211ull;
    }
    return h;
}

// The core's globals as bytes, for finding state a save doesn't hold (diagnostics).
std::map<std::string, std::vector<uint8_t>> core_globals()
{
    std::map<std::string, std::vector<uint8_t>> g;
    auto add = [&](const char *name, const void *p, size_t n) {
        g[name].assign(static_cast<const uint8_t *>(p), static_cast<const uint8_t *>(p) + n);
    };
    add("CPU", &CPU, sizeof CPU);
    add("ICPU", &ICPU, sizeof ICPU);
    add("Registers", &Registers, sizeof Registers);
    add("Timings", &Timings, sizeof Timings);
    add("PPU", &PPU, sizeof PPU);
    add("IPPU", &IPPU, sizeof IPPU);
    add("DMA", DMA, sizeof DMA);
    add("Settings", &Settings, sizeof Settings);
    add("FillRAM", Memory.FillRAM, 0x8000);
    add("smp", &SNES::smp, sizeof SNES::smp);
    add("dsp", &SNES::dsp, sizeof SNES::dsp);
    add("Memory", &Memory, sizeof Memory);
    add("GFX", &GFX, sizeof GFX);
    add("BG", &BG, sizeof BG);
    add("OpenBus", &OpenBus, 1);
    return g;
}

void print_globals_diff(const std::map<std::string, std::vector<uint8_t>> &a,
                        const std::map<std::string, std::vector<uint8_t>> &b)
{
    for (auto &[name, va] : a)
    {
        auto &vb = b.at(name);
        std::string where;
        int n = 0;
        for (size_t i = 0; i < va.size(); i++)
            if (va[i] != vb[i])
            {
                if (n++ < 12)
                {
                    char buf[48];
                    snprintf(buf, sizeof buf, " +%zu:%02X/%02X", i, va[i], vb[i]);
                    where += buf;
                }
            }
        if (n)
            printf("        %s: %d bytes differ:%s\n", name.c_str(), n, where.c_str());
    }
}

// Which blocks of two saved states differ ("CPU", "SND", ...), and where in them.
void print_state_diff(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    // A freeze: "#!s9xsnp:0011\n", then blocks "NAM:123456:" followed by that many bytes.
    auto blocks = [](const std::vector<uint8_t> &s) {
        std::map<std::string, std::pair<size_t, size_t>> out; // name -> (start, length)
        size_t at = 14;
        while (at + 11 <= s.size())
        {
            std::string name(s.begin() + at, s.begin() + at + 3);
            size_t len = std::stoul(std::string(s.begin() + at + 4, s.begin() + at + 10));
            out[name] = { at + 11, len };
            at += 11 + len;
        }
        return out;
    };
    auto ba = blocks(a), bb = blocks(b);
    for (auto &[name, ra] : ba)
    {
        auto it = bb.find(name);
        if (it == bb.end() || it->second.second != ra.second)
        {
            printf("        block %s: missing or another size\n", name.c_str());
            continue;
        }
        size_t first = SIZE_MAX, n = 0;
        for (size_t i = 0; i < ra.second; i++)
            if (a[ra.first + i] != b[it->second.first + i])
            {
                if (first == SIZE_MAX)
                    first = i;
                n++;
            }
        if (n)
            printf("        block %s: %zu of %zu bytes differ, the first at %zu\n", name.c_str(), n, ra.second, first);
    }
}

struct KeepTotals
{
    int kept = 0, exact_drawn = 0, exact_plain = 0, short_ones = 0, none = 0;
    int pads_matter = 0, writes_matter = 0, had_writes = 0, had_pads = 0; // the controls drift
    std::map<std::string, int> starts, differ;
    double ms_drawn = 0, ms_plain = 0, seconds = 0;
    size_t raw = 0, state = 0;
    int caught = 0, reverts = 0, tested = 0;
};

void keep_seed(const Recipe &recipe, int frames, const CodeMap &learned, const std::vector<uint8_t> &start,
               uint64_t input_seed, KeepTotals &k)
{
    headless::load_state(start);
    rom.restore_clean();
    CodeMap map = learned;
    std::vector<uint8_t> aim_flags;
    engine.set_code_flags([&] {
        aim_flags = map.flags();
        for (auto &f : aim_flags)
            f &= codeflag::All;
        return std::span<const uint8_t>(aim_flags);
    });
    RomTracer tracer;
    tracer.start(map);
    DerailWatch watch;
    tracer.watch_derails(&watch);
    RomBlame blame(rom);
    blame.attach(map.data(), map.size());
    blame.set_policy(recipe.rom_policy.value_or(RomPolicy::Revert), recipe.seed);
    CrashGuard guard;
    guard.set_byte_blame(&blame);
    guard.set_policy(recipe.crash_policy.value_or(PolicyKind::Reroll));
    guard.set_detector_config(recipe.crash_detection.value_or(DetectorConfig{}));
    InstantRevert instant;
    instant.set_enabled(true);
    TestAhead ahead;
    ahead.set_config({ .frames = uint32_t(std::lround(recipe.test_first.value_or(3.0) * 60)) });
    std::span<const uint8_t> clean(rom.clean().data(), rom.clean().size());

    recorder.reset();
    engine.set_recipe(recipe);
    start_run();
    guard.between_frames(engine, access);
    instant.keep(access, engine.current_frame(), true);
    S9xCorruptFrameHook = &frame_hook;
    bool verbose = getenv("CORRUPT_KEEP_VERBOSE");
    bool diag = getenv("CORRUPT_KEEP_DIAG");
    std::map<uint64_t, uint64_t> live; // the game's memory at each frame's start (diagnostics)
    std::map<uint64_t, std::vector<uint8_t>> live_after; // the core's state after the first frames after a cut
    std::map<uint64_t, std::map<std::string, std::vector<uint8_t>>> live_globals; // at each keyframe after a cut
    std::map<uint64_t, bool> live_drawn;
    std::mt19937 input{ uint32_t(input_seed) };
    Pads pads{};
    int hold_for = 0;
    // A player: a few buttons held for a while, then others, sometimes none.
    static const uint16_t kButtons[] = { 0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
                                         0x0080, 0x0040, 0x0020, 0x0010 };
    for (int i = 0; i < frames; i++)
    {
        if (--hold_for <= 0)
        {
            pads[0] = 0;
            if (input() % 4)
                for (int b = 0, n = 1 + int(input() % 2); b < n; b++)
                    pads[0] |= kButtons[input() % 12];
            hold_for = 5 + int(input() % 40);
        }
        access.set_pads(pads);
        recorder.before_frame(access, memory, clean, access.timeline());
        if (diag)
            live[recorder.frames_begun() - 1] = state_hash();
        if (diag && recorder.frames_recorded() == 1)
            live_globals[recorder.frames_begun() - 1] = core_globals();
        if (diag)
            live_drawn[recorder.frames_begun() - 1] = IPPU.RenderThisFrame;
        headless::run_frames(1);
        if (diag && recorder.frames_recorded() <= 3)
            live_after[recorder.frames_begun() - 1] = access.save_state();
        if (auto r = instant.after_frame(tracer.take_derail(), engine.current_frame(), access, blame))
        {
            k.reverts++;
            recorder.redo_frame(access.timeline());
            if (verbose)
                printf("    frame %5d  %s\n", i + 1, r->summary.c_str());
        }
        blame.collect(engine.current_frame());
        auto out = guard.between_frames(engine, access);
        if (out.kind != GuardOutcome::Nothing)
            recorder.cut("the crash guard rolled back");
        rom.after_guard(out);
        if (out.kind != GuardOutcome::Nothing)
        {
            k.caught++;
            if (verbose)
                printf("    frame %5d  %s\n", i + 1, out.summary.c_str());
            if (out.kind == GuardOutcome::GaveUp)
                break;
        }
        instant.keep(access, engine.current_frame(), engine.active() && tracer.tracing());
        {
            AheadHooks hooks;
            hooks.take_derail = [&] { return tracer.take_derail(); };
            recorder.hold(true);
            auto t = ahead.before_frame(engine, rom, access, { map.data(), map.size() }, hooks);
            recorder.hold(false);
            recorder.accept(access.timeline());
            k.tested += t.kind != AheadResult::Nothing;
        }
        if (i >= 600 && (i - 600) % 420 == 419)
        {
            auto m = recorder.take();
            if (!m)
            {
                k.none++;
                continue;
            }
            m->game = { Memory.ROMName, "", "" };
            k.kept++;
            k.seconds += m->seconds();
            if (m->frames.size() < recorder.config().lead)
            {
                k.short_ones++;
                k.starts[m->starts.empty() ? "(the run's start)" : m->starts]++;
            }
            auto bytes = encode_moment(*m);
            k.raw += bytes.size();
            k.state += m->state.size();
            recorder.hold(true);
            CheckHooks hooks;
            hooks.flags = { map.data(), map.size() };
            hooks.draw = true;
            auto drawn = check_moment(*m, player, access, memory, clean, hooks);
            hooks.draw = false;
            auto plain = check_moment(*m, player, access, memory, clean, hooks);
            // The controls: the same moment without its buttons, and without its writes. Each
            // should drift, or the check proves nothing.
            Moment no_pads = *m, no_writes = *m;
            bool pressed = false, wrote = !m->after.empty();
            for (auto &f : no_pads.frames)
            {
                pressed = pressed || f.pads != Pads{};
                f.pads = {};
            }
            for (auto &f : no_writes.frames)
            {
                wrote = wrote || !f.before.empty() || !f.during.empty();
                f.before.clear();
                f.during.clear();
            }
            no_writes.after.clear();
            if (pressed)
            {
                k.had_pads++;
                k.pads_matter += !check_moment(no_pads, player, access, memory, clean, hooks).exact;
            }
            if (wrote)
            {
                k.had_writes++;
                k.writes_matter += !check_moment(no_writes, player, access, memory, clean, hooks).exact;
            }
            recorder.hold(false);
            recorder.accept(access.timeline());
            tracer.take_derail(); // the playback's, which happened already
            k.exact_drawn += drawn.exact;
            k.exact_plain += plain.exact;
            k.ms_drawn += drawn.ms;
            k.ms_plain += plain.ms;
            for (auto t : drawn.differ)
                k.differ[target_name(t)]++;
            if (diag && !drawn.exact)
            {
                // Where it starts to drift: play it again unseen, comparing each frame's start.
                auto state = access.save_state();
                auto rom_now = std::vector<uint8_t>(memory.data(Target::ROM), memory.data(Target::ROM) + memory.size(Target::ROM));
                recorder.hold(true);
                access.begin_unseen();
                player.start(*m, access, memory, clean);
                for (size_t f = 0;; f++)
                {
                    if (!player.before_frame(access, memory))
                        break;
                    uint64_t h = state_hash();
                    if (auto lg = live_globals.find(m->from + f); f == 0 && lg != live_globals.end())
                    {
                        printf("      globals at the keyframe, live/playback:\n");
                        print_globals_diff(lg->second, core_globals());
                    }
                    auto it = live.find(m->from + f);
                    if (it == live.end() || it->second != h)
                    {
                        printf("      drifts at frame %zu of %zu (live frame %llu): %s; %zu before, %zu during\n", f,
                               m->frames.size(), (unsigned long long)(m->from + f),
                               it == live.end() ? "no live hash" : "memory differs", m->frames[f].before.size(),
                               m->frames[f].during.size());
                        if (f > 0)
                            printf("      the frame before: %zu before, %zu during, pads %04X, drawn live %d\n",
                                   m->frames[f - 1].before.size(), m->frames[f - 1].during.size(), m->frames[f - 1].pads[0],
                                   int(live_drawn[m->from + f - 1]));
                        if (auto a = live_after.find(m->from + f - 1); f > 0 && a != live_after.end())
                            print_state_diff(a->second, access.save_state());
                        // Is a playback consistent with itself? The keyframe, one frame, twice.
                        uint64_t hs[2];
                        for (auto &one : hs)
                        {
                            player.start(*m, access, memory, clean);
                            player.before_frame(access, memory);
                            access.run_unseen_frame(true);
                            player.before_frame(access, memory);
                            one = state_hash();
                        }
                        printf("      one frame from the keyframe, twice: %s (live %s)\n", hs[0] == hs[1] ? "the same" : "DIFFERENT",
                               hs[0] == live[m->from + 1] ? "matches" : "differs");
                        break;
                    }
                    access.run_unseen_frame(true);
                }
                player.stop();
                access.end_unseen();
                access.load_state(state);
                for (size_t i = 0; i < rom_now.size(); i++)
                    if (memory.read(Target::ROM, uint32_t(i)) != rom_now[i])
                        memory.write(Target::ROM, uint32_t(i), rom_now[i]);
                recorder.hold(false);
                recorder.accept(access.timeline());
            }
            if (verbose || !drawn.exact)
                printf("    frame %5d  kept %.1f s%s%s: drawn %s (%.0f ms), plain %s (%.0f ms)%s%s\n", i + 1,
                       m->seconds(), m->starts.empty() ? "" : ", from where ", m->starts.c_str(),
                       drawn.exact ? "exact" : "DRIFTS", drawn.ms, plain.exact ? "exact" : "drifts", plain.ms,
                       drawn.exact ? "" : ": ", drawn.exact ? "" : drawn.text.c_str());
        }
    }
    S9xCorruptFrameHook = nullptr;
    tracer.stop();
    engine.set_active(false);
    engine.set_code_flags({});
    recorder.reset();
}

int run_keep(const Recipe &recipe, int frames, int seeds)
{
    auto start = headless::save_state();
    CodeMap learned(Memory.CalculatedSize);
    if (!load_saved_map(learned))
    {
        RomTracer tracer;
        if (!tracer.start(learned))
            return 1;
        access.reset_console();
        headless::run_frames(frames);
        tracer.stop();
        printf("codemap  %zu code bytes learned from a clean boot of %d frames\n", learned.coverage().code, frames);
    }
    KeepTotals k;
    for (int seed = 1; seed <= seeds; seed++)
    {
        Recipe r = recipe;
        r.seed = recipe.seed + uint64_t(seed) - 1;
        if (getenv("CORRUPT_KEEP_VERBOSE"))
            printf("  seed %llu\n", (unsigned long long)r.seed);
        keep_seed(r, frames, learned, start, 1000 + seed, k);
    }
    rom.restore_clean();
    printf("seeds    %d of %d frames each; %d guard rollbacks, %d instant reverts, %d firings tested ahead\n", seeds,
           frames, k.caught, k.reverts, k.tested);
    if (!k.kept)
    {
        printf("kept     none (%d times there was nothing to keep)\n", k.none);
        return 1;
    }
    printf("kept     %d moments, %.1f s long on average; %d shorter than 10 s; %d times nothing to keep\n", k.kept,
           k.seconds / k.kept, k.short_ones, k.none);
    for (auto &[why, n] : k.starts)
        printf("  short: %-40s %d\n", why.c_str(), n);
    printf("exact    %d of %d drawing the frames (%.0f ms a check), %d of %d not drawing (%.0f ms)\n", k.exact_drawn,
           k.kept, k.ms_drawn / k.kept, k.exact_plain, k.kept, k.ms_plain / k.kept);
    for (auto &[t, n] : k.differ)
        printf("  differs: %-8s %d\n", t.c_str(), n);
    printf("controls without the buttons %d of %d drift; without the writes %d of %d drift\n", k.pads_matter,
           k.had_pads, k.writes_matter, k.had_writes);
    printf("size     %.0f KB a moment's file, packed (its saved state alone is %.0f KB unpacked)\n",
           k.raw / 1024.0 / k.kept, k.state / 1024.0 / k.kept);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0); // progress shows up even through a pipe or a kill
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
    if (argc > 5 && std::string(argv[5]) == "--derail-fp")
        return run_derail_fp(frames, argc > 6 ? atoi(argv[6]) : frames);
    if (argc > 5 && std::string(argv[5]) == "--nmi")
        return run_nmi(frames);
    if (argc > 5 && std::string(argv[5]) == "--eyes")
        return run_eyes(*parsed.recipe, frames, argc > 6 ? atoi(argv[6]) : 5);
    if (argc > 5 && std::string(argv[5]) == "--keep")
        return run_keep(*parsed.recipe, frames, argc > 6 ? atoi(argv[6]) : 3);
    if (argc > 5 && std::string(argv[5]) == "--ahead")
        return run_ahead(*parsed.recipe, frames, argc > 6 ? atoi(argv[6]) : 5);
    if (argc > 5 && std::string(argv[5]) == "--instant")
        return run_instant(*parsed.recipe, frames, argc > 6 ? atoi(argv[6]) : 5);
    if (argc > 5 && std::string(argv[5]) == "--swaps")
        return run_swaps(frames, argc > 6 ? atoi(argv[6]) : 10, argc > 7 ? atoi(argv[7]) : 3);
    if (argc > 5 && std::string(argv[5]) == "--kill")
        return run_kill(frames, argc > 6 ? atoi(argv[6]) : 10, argc > 7 ? atoi(argv[7]) : 6);
    if (argc > 5 && std::string(argv[5]) == "--calls")
        return run_calls(frames);
    if (argc > 5 && (std::string(argv[5]) == "--risk" || std::string(argv[5]) == "--foresee" || std::string(argv[5]) == "--try"))
    {
        int only = -1;
        const char *kinds[4] = { "number", "branch", "math", "skip" };
        for (int k = 0; argc > 8 && k < 4; k++)
            if (std::string(argv[8]) == kinds[k])
                only = k;
        return run_risk(frames, argc > 6 ? atoi(argv[6]) : 200, argc > 7 ? atoi(argv[7]) : 1800, only,
                        std::string(argv[5]) == "--foresee" ? 1 : std::string(argv[5]) == "--try" ? 2 : 0);
    }
    if (argc > 5 && std::string(argv[5]) == "--change")
        return run_change(frames, argc > 6 ? atoi(argv[6]) : 10, argc > 7 ? atoi(argv[7]) : 6);
    if (argc > 5 && std::string(argv[5]) == "--bodies")
        return run_bodies(frames);
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
