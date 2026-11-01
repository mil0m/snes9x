#include "test_ahead.h"
#include "code_map.h"
#include "rng.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

namespace corrupt
{

namespace
{

// Only what can be told in a few seconds with nobody at the controls.
DetectorConfig definite_only()
{
    DetectorConfig c;
    c.nmi_off_frames = 0;
    c.frozen_frames = 0;
    c.black_frames = 0;
    return c;
}

std::string seconds_in(uint32_t frames)
{
    char buf[32];
    snprintf(buf, sizeof buf, " %.1f s in", frames / 60.0);
    return buf;
}

} // namespace

TestAhead::Verdict TestAhead::run(Engine &engine, RomKeeper &rom, CoreAccess &core, std::span<uint8_t> flags,
                                  const AheadHooks &hooks, const std::vector<uint32_t> &written)
{
    Verdict v;
    uint64_t start = engine.current_frame();
    if (!core.save_state_into(state))
    {
        v.kind = Verdict::Inconclusive;
        return v;
    }
    auto snap = engine.snapshot();
    uint16_t hotkeys = engine.pressed_hotkeys();
    flags_before.assign(flags.begin(), flags.end());
    for (uint32_t o : written) // "used" means used in this run
        if (o < flags.size())
            flags[o] &= uint8_t(~codeflag::Touched);

    engine.quiet_after(start + 1, Target::ROM); // this firing only, not the ones after it
    detector.set_config(definite_only());
    if (hooks.begin)
        hooks.begin();
    core.begin_unseen();
    if (hooks.take_derail)
        hooks.take_derail(); // nothing from before counts
    std::vector<bool> seen(written.size(), false);
    size_t seen_count = 0;
    uint32_t last_first_use = 0;
    for (uint32_t f = 1; f <= cfg.frames; f++)
    {
        core.run_unseen_frame();
        for (size_t i = 0; i < written.size(); i++)
            if (!seen[i] && written[i] < flags.size() && (flags[written[i]] & codeflag::Touched))
            {
                seen[i] = true;
                seen_count++;
                last_first_use = f;
            }
        if (hooks.take_derail)
            if (auto note = hooks.take_derail())
            {
                for (uint32_t o : note->culprits)
                    if (std::find(written.begin(), written.end(), o) != written.end())
                        v.culprits.push_back(o);
                bool ours = !v.culprits.empty();
                v.kind = ours ? Verdict::Crashed : Verdict::Inconclusive;
                v.guilty = v.used = ours;
                v.text = std::string(landing_text(note->kind)) + " at " + describe_pc(note->pc) + seconds_in(f);
                break;
            }
        if (auto crash = detector.observe(start + f, core.sample()))
        {
            v.kind = Verdict::Crashed;
            v.text = crash->text + seconds_in(f);
            break;
        }
        // Every written byte has been used and held up a while: that's the answer.
        if (!written.empty() && seen_count == written.size() && f >= last_first_use + cfg.settle_frames)
            break;
    }
    for (uint32_t o : written)
    {
        bool u = o < flags.size() && (flags[o] & codeflag::Touched);
        v.used = v.used || u;
        if (!u && std::find(v.culprits.begin(), v.culprits.end(), o) == v.culprits.end())
            v.unused.push_back(o);
    }
    core.end_unseen();
    if (hooks.end)
        hooks.end();

    // As it was: the core, the engine (and its pressed hotkeys), the ROM, the code map.
    engine.quiet_after(std::nullopt);
    core.load_state(state);
    engine.restore(snap);
    engine.set_pressed_hotkeys(hotkeys);
    rom.revert_after(start);
    std::copy(flags_before.begin(), flags_before.end(), flags.begin());
    return v;
}

AheadResult TestAhead::before_frame(Engine &engine, RomKeeper &rom, CoreAccess &core, std::span<uint8_t> flags,
                                    const AheadHooks &hooks)
{
    AheadResult out;
    if (!cfg.frames || !engine.active() || engine.waiting() || !rom.loaded())
        return out;
    uint64_t start = engine.current_frame();
    uint64_t next = start + 1;
    engine.drop_vetoes_through(start); // those frames have run for real
    auto planned = engine.preview_next(rom, Target::ROM);
    if (planned.empty())
        return out;

    auto t0 = std::chrono::steady_clock::now();
    auto offsets_of = [](const std::vector<LoggedEvent> &events) {
        std::vector<uint32_t> o;
        for (auto &e : events)
            o.push_back(e.write.offset);
        return o;
    };

    firings_tested++;
    std::optional<bool> baseline_crashes; // the same window without the writes (asked once)
    std::string bad_names;                // the rules whose picks crashed
    std::set<uint32_t> bad;               // ...by index
    // A re-roll that survives only because the game never touched it proves nothing; the
    // first such is kept in reserve (its rules' salts) while a pick the game ran is sought.
    std::vector<std::pair<uint32_t, uint64_t>> reserve;
    auto salts_of = [&](const std::set<uint32_t> &rules) {
        std::vector<std::pair<uint32_t, uint64_t>> s;
        for (uint32_t i : rules)
            if (i < engine.recipe().rules.size())
                s.push_back({ i, engine.recipe().rules[i].salt });
        return s;
    };
    for (unsigned attempt = 0;; attempt++)
    {
        auto written = offsets_of(planned);
        auto v = run(engine, rom, core, flags, hooks, written);
        out.runs++;
        bool guilty = v.kind == Verdict::Crashed && v.guilty;
        if (v.kind == Verdict::Crashed && !v.guilty && v.used)
        {
            if (!baseline_crashes)
            {
                engine.veto(next, Target::ROM, written);
                auto control = run(engine, rom, core, flags, hooks, written);
                engine.drop_vetoes_through(next);
                out.runs++;
                baseline_crashes = control.kind == Verdict::Crashed;
            }
            guilty = !*baseline_crashes; // only the changed run fell over
        }
        bool last = attempt + 1 >= cfg.tries;
        if (!guilty)
        {
            bool dormant = attempt > 0 && v.kind == Verdict::Survived && !v.used;
            if (!dormant || last)
            {
                out.untried = v.unused;
                out.written = written;
                out.kind = attempt ? AheadResult::Rerolled : AheadResult::Kept;
                break;
            }
            if (reserve.empty())
                reserve = salts_of(bad);
        }
        else
        {
            out.failure = v.text;
            // Whose picks: the rules that wrote the bytes a landing blamed, or every rule that wrote.
            std::set<uint32_t> now_bad;
            for (auto &e : planned)
                if (v.culprits.empty() ||
                    std::find(v.culprits.begin(), v.culprits.end(), e.write.offset) != v.culprits.end())
                    now_bad.insert(e.rule_index);
            for (uint32_t i : now_bad)
                if (bad.insert(i).second && i < engine.recipe().rules.size())
                    bad_names += (bad_names.empty() ? "" : ", ") + engine.recipe().rules[i].name;
            if (last)
            {
                if (!reserve.empty())
                {
                    // A pick that didn't crash, if untried, beats none.
                    for (auto [i, salt] : reserve)
                        engine.reroll_rule(i, salt);
                    auto kept = engine.preview_next(rom, Target::ROM);
                    out.written = out.untried = offsets_of(kept);
                    out.kind = AheadResult::Rerolled;
                    break;
                }
                // Skip those rules' writes this time; the others go ahead.
                for (auto &e : planned)
                    (now_bad.count(e.rule_index) ? out.skipped : out.written).push_back(e.write.offset);
                engine.veto(next, Target::ROM, out.skipped);
                out.kind = AheadResult::Skipped;
                break;
            }
        }
        // Another pick: the guilty rules roll again, as the guard's re-roll does.
        for (uint32_t i : bad)
        {
            Rng rng = Rng::for_firing(engine.recipe().seed ^ 0x7E57A11EADULL, i, next * 8 + attempt);
            engine.reroll_rule(i, rng.next_u64() | 1);
        }
        out.rerolls++;
        planned = engine.preview_next(rom, Target::ROM);
        if (planned.empty())
        {
            out.kind = AheadResult::Rerolled; // the new picks don't fire this frame
            break;
        }
    }
    out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    switch (out.kind)
    {
    case AheadResult::Rerolled:
        firings_rerolled++;
        out.summary = "Tested ahead: " + bad_names + " crashed the game (" + out.failure + "), so it rolled again" +
                      (out.written.empty()                     ? ""
                       : out.untried.size() < out.written.size() ? " and the new pick holds up"
                                                                 : " (the new pick isn't used yet, so it goes in untried)");
        break;
    case AheadResult::Skipped:
        firings_skipped++;
        out.summary = "Tested ahead: every pick of " + bad_names + " crashed the game (the last " + out.failure +
                      "), so it's skipped this time";
        break;
    default:
        break;
    }
    return out;
}

} // namespace corrupt
