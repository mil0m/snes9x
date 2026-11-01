#include "first_use.h"
#include "derail.h"

#include <algorithm>
#include <chrono>

namespace corrupt
{

void FirstUseTest::note_uses(const RomBlame &blame, const RomKeeper &rom, uint64_t frame)
{
    if (!cfg.frames)
        return;
    for (uint32_t o : blame.used_this_frame())
    {
        const ByteUse *use = blame.use(o);
        if (!use || use->frames_used != 1)
            continue; // not its first use
        uint8_t value = rom.read(Target::ROM, o);
        if (auto v = vouched.find(o); v != vouched.end())
        {
            bool fresh = frame <= v->second + cfg.frames + 1;
            vouched.erase(v);
            if (fresh)
            {
                tested_value[o] = value; // #52's window ran it with this value
                continue;
            }
        }
        if (auto t = tested_value.find(o); t != tested_value.end() && t->second == value)
            continue;
        if (std::none_of(queue.begin(), queue.end(), [&](const Queued &q) { return q.offset == o; }))
            queue.push_back({ o, value, frame });
    }
}

void FirstUseTest::vouch(const std::vector<uint32_t> &offsets, uint64_t frame)
{
    if (!cfg.frames)
        return;
    for (uint32_t o : offsets)
        vouched[o] = frame;
    // Vouches that never met their first use don't pile up.
    if (vouched.size() > 4096)
        std::erase_if(vouched, [&](const auto &v) { return v.second + cfg.frames + 1 < frame; });
}

void FirstUseTest::forget()
{
    queue.clear();
    tested_value.clear();
    vouched.clear();
    failures.clear();
    test_frame.clear();
    last_test.reset();
}

FirstUseResult FirstUseTest::test(Engine &engine, RomKeeper &rom, RomBlame &blame, CoreAccess &core,
                                  std::span<uint8_t> flags, const AheadHooks &hooks,
                                  const std::vector<uint8_t> *checkpoint, const EngineSnapshot *engine_checkpoint)
{
    FirstUseResult out;
    uint64_t now = engine.current_frame();
    if (!cfg.frames || queue.empty() || !checkpoint || !rom.loaded())
        return out;
    if (last_test && now < *last_test + cfg.gap)
        return out;

    // Still what was queued: changed (watched by the blame), the same value, and not yet
    // run for a whole window for real (then it has had its test, live).
    const auto &clean = rom.clean();
    std::erase_if(queue, [&](const Queued &q) {
        return !blame.use(q.offset) || q.offset >= clean.size() || rom.read(Target::ROM, q.offset) != q.value ||
               now > q.frame + cfg.frames;
    });
    if (queue.empty())
        return out;
    size_t n = std::min(queue.size(), std::max<size_t>(cfg.batch, 1));
    std::vector<uint32_t> batch;
    for (size_t i = 0; i < n; i++)
        batch.push_back(queue[i].offset);
    queue.erase(queue.begin(), queue.begin() + std::ptrdiff_t(n));

    auto t0 = std::chrono::steady_clock::now();
    last_test = now;
    test_count++;
    byte_count += unsigned(batch.size());
    out.tested = batch;
    for (uint32_t o : batch)
        test_frame[o] = now;

    // A run with `changed` as they are and the rest of the batch clean.
    uint32_t window = cfg.frames;
    auto play = [&](const std::vector<uint32_t> &changed) {
        RunSpec spec;
        spec.from = checkpoint;
        spec.engine_from = engine_checkpoint;
        spec.watched = changed;
        for (uint32_t o : batch)
            if (std::find(changed.begin(), changed.end(), o) == changed.end())
                spec.borrowed.push_back({ o, clean[o] });
        // No ROM writes in the runs, the replayed frame's included: they've been made for real
        // (the ROM isn't in a saved state), and only the bytes under test may differ.
        spec.quiet_after = engine_checkpoint ? std::min(now, engine_checkpoint->frame) : now;
        spec.frames = window;
        spec.settle_frames = cfg.settle_frames;
        spec.nmi_off_frames = cfg.nmi_off_frames;
        out.runs++;
        return runner.play(engine, rom, core, flags, hooks, spec);
    };
    auto crashed = [](const RunVerdict &v) { return v.kind == RunVerdict::Crashed && v.used; };
    auto without = [](std::vector<uint32_t> from, const std::vector<uint32_t> &gone) {
        std::erase_if(from, [&](uint32_t o) { return std::find(gone.begin(), gone.end(), o) != gone.end(); });
        return from;
    };

    std::vector<uint32_t> remaining = batch, guilty;
    std::optional<bool> baseline_crashes;
    while (!remaining.empty() && out.runs < cfg.max_runs)
    {
        auto v = play(remaining);
        if (!crashed(v))
            break; // the rest hold up (or a landing blamed older changes: the instant revert's)
        if (out.failure.empty())
            out.failure = v.text;
        if (v.guilty)
        {
            guilty.insert(guilty.end(), v.culprits.begin(), v.culprits.end());
            remaining = without(remaining, v.culprits);
            continue; // and the rest once more without them
        }
        // A plain crash: does the game have it without the batch too?
        if (!baseline_crashes)
        {
            uint32_t full = window;
            window = std::min(cfg.frames, v.frames + cfg.settle_frames); // past where it crashed is enough
            baseline_crashes = play({}).kind == RunVerdict::Crashed;
            window = full;
        }
        if (*baseline_crashes)
        {
            out.older_damage = true;
            break;
        }
        // Narrow down to one byte that crashes alone: halves, keeping a half that crashes.
        std::vector<uint32_t> suspects = remaining;
        bool proven = remaining.size() == 1;
        while (suspects.size() > 1 && out.runs < cfg.max_runs)
        {
            std::vector<uint32_t> half(suspects.begin(), suspects.begin() + std::ptrdiff_t(suspects.size() / 2));
            auto r = play(half);
            if (crashed(r) && r.guilty)
            {
                suspects = r.culprits;
                proven = true;
                break;
            }
            proven = crashed(r);
            suspects = proven ? half : without(suspects, half);
        }
        if (suspects.size() == 1 && !proven && out.runs < cfg.max_runs)
            proven = crashed(play(suspects));
        if (!proven)
        {
            // Out of runs, or no byte crashes alone (they need each other): all of them.
            guilty.insert(guilty.end(), remaining.begin(), remaining.end());
            remaining.clear();
            break;
        }
        guilty.insert(guilty.end(), suspects.begin(), suspects.end());
        remaining = without(remaining, suspects);
    }

    for (uint32_t o : without(batch, guilty))
        tested_value[o] = rom.read(Target::ROM, o);
    if (guilty.empty())
    {
        out.kind = FirstUseResult::Kept;
        out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return out;
    }

    // Fixed the policy's way; a byte on its last try goes back to clean whatever the policy.
    std::vector<uint32_t> by_policy, last_try;
    for (uint32_t o : guilty)
        (++failures[o] >= cfg.tries ? last_try : by_policy).push_back(o);
    std::string action;
    for (auto [bytes, as] : { std::pair{ &by_policy, std::optional<RomPolicy>{} },
                              std::pair{ &last_try, std::optional<RomPolicy>{ RomPolicy::Revert } } })
    {
        if (bytes->empty())
            continue;
        auto fixed = blame.fix(*bytes, as);
        out.fixed.insert(out.fixed.end(), fixed.offsets.begin(), fixed.offsets.end());
        if (!fixed.text.empty())
            action += (action.empty() ? "" : ", ") + fixed.text;
    }
    for (uint32_t o : last_try)
        failures.erase(o);
    core.load_state(*checkpoint); // the frame plays again without them
    fix_count += unsigned(out.fixed.size());
    out.kind = FirstUseResult::Fixed;
    out.frame = now;
    out.rom_action = action;
    out.summary = "Tested at first use: " + std::to_string(out.fixed.size()) +
                  (out.fixed.size() == 1 ? " changed byte" : " changed bytes") + " crashed the game (" + out.failure +
                  ") where the clean ROM didn't, so " + action + " and played the frame again";
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

JournalEntry journal_entry(const FirstUseResult &r)
{
    JournalEntry e;
    e.kind = JournalEntry::FirstUse;
    e.signal = "first_use";
    e.text = r.failure;
    e.frame = r.frame;
    e.since_frame = r.frame;
    e.restored_frame = r.frame;
    e.frames_lost = 1;
    e.action = "none";
    e.attempt = 1;
    e.rom_blamed = r.fixed;
    e.rom_action = r.rom_action;
    return e;
}

} // namespace corrupt
