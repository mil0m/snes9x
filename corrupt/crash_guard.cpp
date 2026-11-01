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

bool replayable(CrashSignal s)
{
    switch (s)
    {
    case CrashSignal::CpuStopped:
    case CrashSignal::Deadlock:
    case CrashSignal::RunawayPc:
    case CrashSignal::NmiOff:
    case CrashSignal::Reboot: return true;
    case CrashSignal::FrozenPicture:
    case CrashSignal::BlackScreen:
    case CrashSignal::Manual: return false;
    }
    return false;
}

void CrashGuard::reset()
{
    ring.clear();
    detector.reset();
    crashes_per_rule.clear();
    last_frame.reset();
    recovering_since.reset();
    consecutive = 0;
    fruitless = 0;
    total = 0;
    manual_pending = false;
    last_verdict.reset();
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
        fruitless = 0;
        last_verdict.reset();
        JournalEntry e;
        e.kind = JournalEntry::Recovered;
        e.frame = frame;
        journal(e, engine);
    }
    return {};
}

std::vector<unsigned> CrashGuard::rule_crashes(const Engine &engine) const
{
    std::vector<unsigned> out;
    for (size_t i = 0; i < engine.recipe().rules.size(); i++)
    {
        auto it = crashes_per_rule.find(engine.rule_id(i));
        out.push_back(it == crashes_per_rule.end() ? 0 : it->second);
    }
    return out;
}

unsigned &CrashGuard::crashes_of(const Engine &engine, uint32_t rule_index)
{
    return crashes_per_rule[engine.rule_id(rule_index)];
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

    // A crash that came back after a rollback (the same kind, starting at about the same
    // moment) is one guessing didn't fix: replay it to find its bytes (#56).
    bool again = consecutive > 0 && last_verdict && last_verdict->signal == verdict.signal &&
                 std::max(last_verdict->since_frame, verdict.since_frame) -
                         std::min(last_verdict->since_frame, verdict.since_frame) <=
                     kSameCrashFrames;
    last_verdict = verdict;
    if (again || prove_first)
        if (auto out = proven(verdict, engine, core, target); out.kind != GuardOutcome::Nothing)
            return out;

    // The newest checkpoint before the trouble that has corruption to blame after it:
    // rules' writes, or single bytes the game used (the ROM blame).
    const Checkpoint *cp = nullptr;
    std::vector<uint32_t> suspects;
    size_t byte_suspects = 0;
    auto blame_after = [&](const Checkpoint &c) {
        suspects = suspects_between(engine, c.frame, through);
        byte_suspects = bytes ? bytes->suspects(c.frame, through, verdict.heuristic) : 0;
        return !suspects.empty() || byte_suspects;
    };
    for (auto it = ring.rbegin(); it != ring.rend(); ++it)
    {
        if (it->frame > target)
            continue;
        cp = &*it;
        if (blame_after(*cp))
            break;
    }
    if (!cp && !ring.empty())
    {
        cp = &ring.front();
        blame_after(*cp);
    }
    if (!cp)
        return {};

    if (suspects.empty() && !byte_suspects)
    {
        if (verdict.heuristic)
            return {}; // a calm screen with nothing corrupted is just a calm screen
        for (uint32_t i = 0; i < engine.recipe().rules.size(); i++)
            if (engine.recipe().rules[i].enabled)
                suspects.push_back(i);
    }

    consecutive++;
    // Putting changed bytes back for good is progress: there are only so many of them.
    if (!(byte_suspects && bytes->fixes_for_good()))
        fruitless++;
    total++;
    for (auto s : suspects)
        crashes_of(engine, s)++;

    GuardOutcome out;
    out.verdict = verdict;
    out.suspects = suspects;
    for (auto s : suspects)
        out.suspect_names.push_back(engine.recipe().rules[s].name);
    out.restored_frame = cp->frame;
    out.blame_after = cp->frame;
    out.blame_through = through;

    JournalEntry e;
    e.signal = crash_signal_name(verdict.signal);
    e.text = verdict.text;
    e.frame = verdict.frame;
    e.since_frame = verdict.since_frame;
    e.restored_frame = cp->frame;
    e.frames_lost = verdict.frame - cp->frame;
    e.suspects = out.suspect_names;
    e.attempt = consecutive;

    if (fruitless > kMaxAttempts || consecutive > kMaxCrashesInRow)
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
        out.action = decide(policy, crashes_of(engine, suspects.front()));
    uint64_t restored = cp->frame;
    restore(*cp, engine, core);
    if (byte_suspects)
    {
        auto acted = bytes->act(restored, through, consecutive, verdict.heuristic);
        out.rom_blamed = std::move(acted.offsets);
        out.rom_action = std::move(acted.text);
        e.rom_blamed = out.rom_blamed;
        e.rom_action = out.rom_action;
    }

    for (auto s : suspects)
    {
        switch (decide(policy, crashes_of(engine, s)))
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
    std::vector<std::string> did;
    if (!suspects.empty())
        did.push_back(action_name(out.action) + std::string(" ") + join(out.suspect_names));
    if (!out.rom_action.empty())
        did.push_back(out.rom_action);
    out.summary = "Crash caught (" + verdict.text + "): back " + seconds(verdict.frame - restored) + ", " +
                  (did.empty() ? std::string("no rule to blame") : join(did));
    e.kind = JournalEntry::Crash;
    e.action = suspects.empty() ? "none" : action_name(out.action);
    journal(e, engine);
    return out;
}

GuardOutcome CrashGuard::proven(const CrashVerdict &verdict, Engine &engine, CoreAccess &core, uint64_t target)
{
    if (!prover || !bytes || !replayable(verdict.signal))
        return {};
    // The usual rollback target, then further back: damage is often done long before.
    std::vector<ProofStart> starts;
    std::vector<const Checkpoint *> cps;
    uint64_t limit = target;
    for (unsigned k = 0; k < kProofStarts; k++)
    {
        const Checkpoint *found = nullptr;
        for (auto it = ring.rbegin(); it != ring.rend(); ++it)
            if (it->frame <= limit && (cps.empty() || it->frame < cps.back()->frame))
            {
                found = &*it;
                break;
            }
        // Nothing that old: the oldest there is, if it's older than the last.
        if (!found && !ring.empty() && (cps.empty() || ring.front().frame < cps.back()->frame))
            found = &ring.front();
        if (!found)
            break;
        cps.push_back(found);
        starts.push_back({ found->frame, &found->state, &found->engine });
        if (found == &ring.front())
            break;
        limit = found->frame > kProofStep ? found->frame - kProofStep : 0;
    }
    if (starts.empty())
        return {};
    auto proof = prover->prove(engine, core, verdict, starts);
    if (!proof || proof->start >= cps.size() || proof->fixed.offsets.empty())
        return {};

    const Checkpoint &cp = *cps[proof->start];
    consecutive++;
    total++;
    if (!bytes->fixes_for_good())
        fruitless++;
    GuardOutcome out;
    out.verdict = verdict;
    out.restored_frame = cp.frame;
    out.blame_after = cp.frame;
    out.blame_through = verdict.frame;
    out.rom_blamed = proof->fixed.offsets;
    out.rom_action = proof->fixed.text;
    out.proven = true;
    out.proof_runs = proof->runs;
    out.proof_ms = proof->ms;

    JournalEntry e;
    e.signal = crash_signal_name(verdict.signal);
    e.text = verdict.text;
    e.frame = verdict.frame;
    e.since_frame = verdict.since_frame;
    e.restored_frame = cp.frame;
    e.frames_lost = verdict.frame - cp.frame;
    e.attempt = consecutive;
    e.rom_blamed = out.rom_blamed;
    e.rom_action = out.rom_action + " (proven by replay)";
    e.action = "none";

    restore(cp, engine, core);
    if (fruitless > kMaxAttempts || consecutive > kMaxCrashesInRow)
    {
        engine.set_active(false);
        recovering_since.reset();
        last_frame.reset();
        out.kind = GuardOutcome::GaveUp;
        out.summary = "Kept crashing (" + verdict.text + "): corruption turned off";
        e.kind = JournalEntry::GaveUp;
        journal(e, engine);
        return out;
    }
    recovering_since = cp.frame;
    out.kind = GuardOutcome::RolledBack;
    out.summary = "Crash caught (" + verdict.text + "): back " + seconds(verdict.frame - cp.frame) +
                  ", found by replaying it: " + out.rom_action;
    e.kind = JournalEntry::Crash;
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
