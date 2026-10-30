#include "crash_guard.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace corrupt
{

namespace
{

std::string seconds(uint64_t frames)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f s", frames / 60.0);
    return buf;
}

std::string join(const std::vector<std::string> &names)
{
    std::string out;
    for (auto &n : names)
        out += (out.empty() ? "" : ", ") + n;
    return out;
}

} // namespace

void CrashGuard::reset()
{
    ring.clear();
    detector.reset();
    crashes_per_rule.clear();
    last_frame.reset();
    recovering_since.reset();
    consecutive = 0;
    total = 0;
    manual_pending = false;
}

GuardOutcome CrashGuard::between_frames(Engine &engine, CoreAccess &core)
{
    if (!engine.active())
    {
        last_frame.reset();
        return {};
    }

    uint64_t frame = engine.current_frame();
    // Going backwards means the run restarted (re-activated, re-rolled, a new recipe).
    // Standing still does not: after STP or a deadlock the frame hook stops running,
    // and that is exactly when the guard must keep looking.
    if (last_frame && frame < *last_frame)
        reset();
    last_frame = frame;
    if (crashes_per_rule.size() != engine.recipe().rules.size())
        crashes_per_rule.assign(engine.recipe().rules.size(), 0);

    std::optional<CrashVerdict> verdict;
    if (manual_pending)
    {
        manual_pending = false;
        uint64_t since = frame > kManualLookback ? frame - kManualLookback : 0;
        verdict = CrashVerdict{ CrashSignal::Manual, frame, since, false, "marked as crashed" };
    }
    else
        verdict = detector.observe(frame, core.sample());

    if (verdict)
    {
        auto out = handle(*verdict, engine, core);
        if (out.kind != GuardOutcome::Nothing)
            return out;
    }

    if (ring.empty() || frame % kCheckpointInterval == 0)
    {
        ring.push_back({ frame, core.save_state(), engine.snapshot() });
        while (ring.size() > kDefaultCheckpoints)
            ring.pop_front();
    }

    if (recovering_since && frame >= *recovering_since + kCleanFrames)
    {
        recovering_since.reset();
        consecutive = 0;
        JournalEntry e;
        e.kind = JournalEntry::Recovered;
        e.frame = frame;
        journal(e, engine);
    }
    return {};
}

std::vector<uint32_t> CrashGuard::suspects_between(const Engine &engine, uint64_t after, uint64_t through) const
{
    std::vector<uint32_t> suspects;
    std::set<uint32_t> seen;
    // Newest first, so the rule that wrote last leads the list.
    for (auto it = engine.event_log().rbegin(); it != engine.event_log().rend(); ++it)
    {
        if (it->frame <= after)
            break;
        if (it->frame <= through && seen.insert(it->rule_index).second)
            suspects.push_back(it->rule_index);
    }
    return suspects;
}

void CrashGuard::restore(const Checkpoint &cp, Engine &engine, CoreAccess &core)
{
    uint64_t frame = cp.frame;
    core.load_state(cp.state);
    engine.restore(cp.engine);
    while (!ring.empty() && ring.back().frame > frame)
        ring.pop_back();
    detector.reset();
    last_frame = frame;
}

GuardOutcome CrashGuard::handle(const CrashVerdict &verdict, Engine &engine, CoreAccess &core)
{
    uint64_t back = kRollbackMargin + uint64_t(consecutive) * kStepBackFrames;
    uint64_t target = verdict.since_frame > back ? verdict.since_frame - back : 0;
    uint64_t through = verdict.signal == CrashSignal::Manual ? verdict.frame : verdict.since_frame;

    // The newest checkpoint before the trouble that has corruption to blame after it.
    const Checkpoint *cp = nullptr;
    std::vector<uint32_t> suspects;
    for (auto it = ring.rbegin(); it != ring.rend(); ++it)
    {
        if (it->frame > target)
            continue;
        cp = &*it;
        suspects = suspects_between(engine, cp->frame, through);
        if (!suspects.empty())
            break;
    }
    if (!cp && !ring.empty())
    {
        cp = &ring.front();
        suspects = suspects_between(engine, cp->frame, through);
    }
    if (!cp)
        return {};

    if (suspects.empty())
    {
        if (verdict.heuristic)
            return {}; // a calm screen with nothing corrupted is just a calm screen
        for (uint32_t i = 0; i < engine.recipe().rules.size(); i++)
            if (engine.recipe().rules[i].enabled)
                suspects.push_back(i);
    }

    consecutive++;
    total++;
    for (auto s : suspects)
        crashes_per_rule[s]++;

    GuardOutcome out;
    out.verdict = verdict;
    out.suspects = suspects;
    for (auto s : suspects)
        out.suspect_names.push_back(engine.recipe().rules[s].name);
    out.restored_frame = cp->frame;

    JournalEntry e;
    e.signal = crash_signal_name(verdict.signal);
    e.text = verdict.text;
    e.frame = verdict.frame;
    e.since_frame = verdict.since_frame;
    e.restored_frame = cp->frame;
    e.frames_lost = verdict.frame - cp->frame;
    e.suspects = out.suspect_names;
    e.attempt = consecutive;

    if (consecutive > kMaxAttempts)
    {
        restore(*cp, engine, core);
        engine.set_active(false);
        recovering_since.reset();
        last_frame.reset();
        out.kind = GuardOutcome::GaveUp;
        out.summary = "Kept crashing (" + verdict.text + "): corruption turned off";
        e.kind = JournalEntry::GaveUp;
        journal(e, engine);
        return out;
    }

    if (!suspects.empty())
        out.action = decide(policy, crashes_per_rule[suspects.front()]);
    uint64_t restored = cp->frame;
    restore(*cp, engine, core);

    for (auto s : suspects)
    {
        switch (decide(policy, crashes_per_rule[s]))
        {
        case ActionKind::Reroll:
        {
            Rng rng(engine.recipe().seed ^ (uint64_t(total) * 0x9e3779b97f4a7c15ULL) ^ (uint64_t(s) + 1));
            engine.reroll_rule(s, rng.next_u64() | 1);
            break;
        }
        case ActionKind::Skip:
            engine.mute_rule_until(s, verdict.frame);
            break;
        case ActionKind::Dampen:
            engine.set_rule_intensity(s, std::max(0.05, engine.recipe().rules[s].intensity * 0.5));
            break;
        case ActionKind::Disable:
            engine.set_rule_enabled(s, false);
            break;
        }
    }

    recovering_since = restored;
    out.kind = GuardOutcome::RolledBack;
    out.summary = "Crash caught (" + verdict.text + "): back " + seconds(verdict.frame - restored) + ", " +
                  (suspects.empty() ? std::string("no rule to blame")
                                    : action_name(out.action) + std::string(" ") + join(out.suspect_names));
    e.kind = JournalEntry::Crash;
    e.action = suspects.empty() ? "none" : action_name(out.action);
    journal(e, engine);
    return out;
}

void CrashGuard::journal(JournalEntry e, const Engine &engine)
{
    if (!on_journal)
        return;
    e.recipe = engine.recipe().name;
    e.seed = engine.recipe().seed;
    e.policy = policy_name(policy);
    on_journal(e);
}

} // namespace corrupt
