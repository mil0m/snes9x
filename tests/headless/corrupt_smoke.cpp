// End-to-end check of the corruption engine inside the real emulator, no window
// needed:
//
//   corrupt-smoke <rom> <recipe.json> [frames=600] [warmup=600] [--guard | --learn [held=3] [from=0] | --replay | --copies | --map | --dump <dir>]
//
// Boots the ROM, runs `warmup` frames, saves a state, then runs `frames` twice
// from that state: once clean, once with the recipe. Reports what the recipe
// changed, and checks after every frame that each byte the engine logged really
// reads back from emulator memory (i.e. CoreMemory writes land).
//
// With --guard it instead runs the recipe once under the crash guard, wired like
// the Qt app, and reports every crash caught and rollback made.
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
// With --learn [held] it plays the recipe guarded for five seeds while learning a crash
// map, then plays `held` other seeds three ways: plain, avoiding the learned hot blocks,
// and avoiding as many random blocks the rules wrote (the control). `from` skips that many
// held-out seeds, to split a long comparison over several processes.
#include "headless_core.h"

#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/crash_map_keeper.h"
#include "corrupt/engine.h"
#include "corrupt/hook.h"
#include "corrupt/recipe_json.h"
#include "corrupt/run_control.h"
#include "corrupt/memory_watch.h"
#include "corrupt/memory_map.h"

#include "snes9x.h"
#include "memmap.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

using namespace corrupt;

namespace
{

Engine engine;
CoreMemory memory;
SnesCoreAccess access;
size_t verified = 0;
size_t mismatched = 0;

void frame_hook()
{
    uint64_t frame_before = engine.status().frame;
    engine.on_frame(memory);
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

int run_guarded(const Recipe &recipe, int frames)
{
    CrashGuard guard;
    guard.set_policy(recipe.crash_policy.value_or(PolicyKind::Reroll));
    guard.set_detector_config(recipe.crash_detection.value_or(DetectorConfig{}));
    int journal_lines = 0;
    guard.on_journal = [&](const JournalEntry &) { journal_lines++; };

    engine.set_recipe(recipe);
    engine.set_active(true);
    S9xCorruptFrameHook = &frame_hook;
    int caught = 0, gave_up = 0;
    for (int i = 0; i < frames; i++)
    {
        headless::run_frames(1);
        auto out = guard.between_frames(engine, access);
        if (out.kind == GuardOutcome::Nothing)
            continue;
        caught++;
        gave_up += out.kind == GuardOutcome::GaveUp;
        printf("  call %5d  %s\n", i + 1, out.summary.c_str());
        if (out.kind == GuardOutcome::GaveUp)
            break;
    }
    S9xCorruptFrameHook = nullptr;
    printf("guard    %d crashes caught, %d gave up, engine %s, %d journal lines\n", caught, gave_up,
           engine.active() ? "still active" : "off", journal_lines);
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
                    p.hits += p.mask->is_hot(it->write.target, it->write.offset);
            }
        auto out = guard.between_frames(engine, access);
        if (out.kind == GuardOutcome::Nothing)
            continue;
        caught++;
        gave_up = out.kind == GuardOutcome::GaveUp;
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
        size_t crashed = 0, hot = 0;
        for (auto &[b, c] : blocks)
        {
            crashed += c.crashes > 0;
            hot += CrashMap::hot_of(c);
        }
        printf("  %-5s %zu blocks seen, %zu in a crash window, %zu hot\n", target_name(t), blocks.size(), crashed, hot);
        std::sort(blocks.begin(), blocks.end(), [](auto &a, auto &b) {
            return CrashMap::danger_of(a.second) > CrashMap::danger_of(b.second);
        });
        for (size_t i = 0; i < blocks.size() && i < 8 && blocks[i].second.crashes; i++)
            printf("    %s  %u crashes, %u survivals, danger %.2f%s\n", hex(blocks[i].first).c_str(),
                   blocks[i].second.crashes, blocks[i].second.survivals, CrashMap::danger_of(blocks[i].second),
                   CrashMap::hot_of(blocks[i].second) ? "  HOT" : "");
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
            if (!CrashMap::hot_of(c))
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
        return run_guarded(*parsed.recipe, frames);
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

    // Clean run.
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
    engine.set_active(true);
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
