#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/crash_guard.h"

#include <algorithm>
#include <cstring>

using namespace corrupt;

namespace
{

constexpr uint32_t kCrashByte = 0x10; // WRAM: 0xEE here means "the CPU stopped"
constexpr uint8_t kCrashValue = 0xEE;
constexpr uint32_t kLatentByte = 0x20; // 0xEE here crashes only later (see FakeCore::latent_from)

// A pretend emulator: its state is FakeMemory, its picture always animates,
// and it "crashes" when the crash byte holds 0xEE.
struct FakeCore : CoreAccess
{
    FakeMemory &mem;
    uint64_t picture = 0;
    bool frozen = false;
    bool pressing = false; // the player mashing buttons
    // Latent damage: crash only once the game reaches `latent_from` and reads the byte.
    const Engine *engine = nullptr;
    uint64_t latent_from = 0;
    uint64_t input_clock = 0;
    int loads = 0;

    explicit FakeCore(FakeMemory &mem) : mem(mem) {}

    std::vector<uint8_t> save_state() override
    {
        std::vector<uint8_t> out;
        for (auto &b : mem.bytes)
            out.insert(out.end(), b.begin(), b.end());
        return out;
    }

    bool load_state(const std::vector<uint8_t> &state) override
    {
        size_t at = 0;
        for (auto &b : mem.bytes)
        {
            std::memcpy(b.data(), state.data() + at, b.size());
            at += b.size();
        }
        loads++;
        return true;
    }

    FrameSignals sample() override
    {
        FrameSignals s;
        s.cpu_halted = mem.of(Target::WRAM)[kCrashByte] == kCrashValue;
        if (latent_from && engine->current_frame() >= latent_from && mem.of(Target::WRAM)[kLatentByte] == kCrashValue)
            s.cpu_halted = true;
        s.picture_hash = frozen ? 1 : ++picture;
        s.input = pressing && (++input_clock / 20) % 2 ? 0x0080 : 0x0000;
        return s;
    }
};

Rule poke_crash_byte(uint64_t at_frame, int min, int max)
{
    Rule r;
    r.name = "poke";
    r.range = { Target::WRAM, kCrashByte, kCrashByte };
    r.op = { .type = OpType::Random, .min = min, .max = max };
    r.when = { .type = TriggerType::OnceAtFrame, .frame = at_frame };
    return r;
}

Rule harmless_noise()
{
    Rule r;
    r.name = "noise";
    r.range = { Target::WRAM, 0x1000, 0x1FFF };
    r.op = { .type = OpType::Random, .min = 0, .max = 255 };
    r.when = { .type = TriggerType::EveryFrames, .every = 10 };
    return r;
}

struct Rig
{
    FakeMemory mem;
    FakeCore core{ mem };
    Engine engine;
    CrashGuard guard;
    std::vector<JournalEntry> journal;
    std::vector<GuardOutcome> outcomes;

    explicit Rig(Recipe recipe, PolicyKind policy = PolicyKind::Reroll)
    {
        engine.set_recipe(std::move(recipe));
        engine.set_active(true);
        guard.set_policy(policy);
        guard.on_journal = [this](const JournalEntry &e) { journal.push_back(e); };
    }

    // One emulated frame: the hook runs the engine, then the front end runs the guard.
    void frame()
    {
        engine.on_frame(mem);
        auto out = guard.between_frames(engine, core);
        if (out.kind != GuardOutcome::Nothing)
            outcomes.push_back(out);
    }

    void frames(int n)
    {
        for (int i = 0; i < n; i++)
            frame();
    }

    uint8_t crash_byte() { return mem.of(Target::WRAM)[kCrashByte]; }
};

Recipe recipe_of(std::vector<Rule> rules)
{
    Recipe r;
    r.name = "test";
    r.seed = 4242;
    r.rules = std::move(rules);
    return r;
}

} // namespace

TEST_CASE("checkpoints are taken at the start and every interval, up to the capacity")
{
    Rig rig(recipe_of({ harmless_noise() }));
    rig.frames(1);
    CHECK(rig.guard.checkpoint_count() == 1); // the start of the run
    rig.frames(59);
    CHECK(rig.guard.checkpoint_count() == 2); // frame 60
    rig.frames(60 * 40);
    CHECK(rig.guard.checkpoint_count() == CrashGuard::kDefaultCheckpoints);
    CHECK(rig.outcomes.empty());
}

TEST_CASE("a crash rolls back to before the guilty write and play continues")
{
    Rig rig(recipe_of({ poke_crash_byte(130, kCrashValue, kCrashValue) }), PolicyKind::Skip);
    rig.frames(130);
    REQUIRE(rig.outcomes.size() == 1);
    auto &out = rig.outcomes[0];
    CHECK(out.kind == GuardOutcome::RolledBack);
    CHECK(out.verdict.signal == CrashSignal::CpuStopped);
    CHECK(out.restored_frame <= 100);
    CHECK(rig.engine.status().frame == out.restored_frame);
    CHECK(rig.crash_byte() == 0); // memory is back to before the poke
    REQUIRE(out.suspects.size() == 1);
    CHECK(out.suspects[0] == 0);

    rig.frames(600); // skip mutes the rule past its frame: no more crashes
    CHECK(rig.outcomes.size() == 1);
    CHECK(rig.crash_byte() == 0);
}

TEST_CASE("re-roll gives the suspect rule a new salt and usually gets past the crash")
{
    // Values 0xEC..0xEF: a 1 in 4 chance per roll of the crashing 0xEE.
    Recipe recipe = recipe_of({ poke_crash_byte(130, 0xEC, 0xEF) });
    // Pick a seed whose first roll crashes, so the test exercises a rollback.
    for (uint64_t seed = 1;; seed++)
    {
        FakeMemory probe;
        Engine e;
        recipe.seed = seed;
        e.set_recipe(recipe);
        e.set_active(true);
        for (int i = 0; i < 130; i++)
            e.on_frame(probe);
        if (probe.of(Target::WRAM)[kCrashByte] == kCrashValue)
            break;
    }

    Rig rig(recipe, PolicyKind::Reroll);
    rig.frames(130 + 6 * 130);
    REQUIRE_FALSE(rig.outcomes.empty());
    CHECK(rig.outcomes.front().kind == GuardOutcome::RolledBack);
    CHECK(rig.outcomes.back().kind == GuardOutcome::RolledBack); // never gave up
    CHECK(rig.engine.recipe().rules[0].salt != 0u);
    CHECK(rig.crash_byte() != kCrashValue);
    CHECK(rig.engine.active());
}

TEST_CASE("it gives up after five crashes without a clean stretch, and turns corruption off")
{
    // Re-rolling can't help: the only possible value crashes.
    Rig rig(recipe_of({ poke_crash_byte(130, kCrashValue, kCrashValue) }), PolicyKind::Reroll);
    rig.frames(130 * 8);
    REQUIRE(rig.outcomes.size() == CrashGuard::kMaxAttempts + 1);
    CHECK(rig.outcomes.back().kind == GuardOutcome::GaveUp);
    CHECK_FALSE(rig.engine.active());
    CHECK(rig.crash_byte() != kCrashValue);
    CHECK(rig.journal.back().kind == JournalEntry::GaveUp);
}

TEST_CASE("disable turns the suspect off; dampen halves its intensity")
{
    Rig off(recipe_of({ poke_crash_byte(130, kCrashValue, kCrashValue) }), PolicyKind::Disable);
    off.frames(400);
    CHECK_FALSE(off.engine.recipe().rules[0].enabled);
    CHECK(off.outcomes.size() == 1);

    Rig damp(recipe_of({ poke_crash_byte(130, kCrashValue, kCrashValue) }), PolicyKind::Dampen);
    damp.frames(131);
    CHECK(damp.engine.recipe().rules[0].intensity == 0.5);
}

TEST_CASE("escalate re-rolls first, then dampens, then disables")
{
    CHECK(escalation_action(1) == ActionKind::Reroll);
    CHECK(escalation_action(2) == ActionKind::Reroll);
    CHECK(escalation_action(3) == ActionKind::Dampen);
    CHECK(escalation_action(4) == ActionKind::Dampen);
    CHECK(escalation_action(5) == ActionKind::Disable);
}

TEST_CASE("a heuristic signal with no corruption to blame is ignored")
{
    Recipe quiet = recipe_of({ poke_crash_byte(1, 0, 0) });
    quiet.rules[0].when = { .type = TriggerType::Hotkey, .hotkey = 1 }; // never pressed
    Rig rig(quiet);
    rig.core.frozen = true;
    rig.core.pressing = true;
    rig.frames(1200);
    CHECK(rig.outcomes.empty());
    CHECK(rig.core.loads == 0);
}

TEST_CASE("a frozen picture after corruption is rolled back")
{
    Rig rig(recipe_of({ harmless_noise() }));
    rig.frames(200);
    rig.core.frozen = true;
    rig.core.pressing = true;
    rig.frames(400);
    REQUIRE_FALSE(rig.outcomes.empty());
    CHECK(rig.outcomes[0].verdict.signal == CrashSignal::FrozenPicture);
    CHECK(rig.outcomes[0].restored_frame < 200);
}

TEST_CASE("per-rule crash counts and journal entries")
{
    Rig rig(recipe_of({ harmless_noise(), poke_crash_byte(130, kCrashValue, kCrashValue) }), PolicyKind::Skip);
    rig.frames(400);
    auto counts = rig.guard.rule_crashes();
    REQUIRE(counts.size() == 2);
    CHECK(counts[1] == 1);
    REQUIRE_FALSE(rig.journal.empty());
    auto &e = rig.journal.front();
    CHECK(e.kind == JournalEntry::Crash);
    CHECK(e.signal == "cpu_stopped");
    CHECK(e.policy == "skip");
    CHECK(std::find(e.suspects.begin(), e.suspects.end(), "poke") != e.suspects.end());
    CHECK(e.frames_lost > 0);
}

TEST_CASE("ten clean seconds after a rollback count as recovered")
{
    Rig rig(recipe_of({ poke_crash_byte(130, kCrashValue, kCrashValue) }), PolicyKind::Skip);
    rig.frames(130 + 700);
    REQUIRE(rig.journal.size() == 2);
    CHECK(rig.journal[1].kind == JournalEntry::Recovered);
    CHECK(rig.guard.consecutive_crashes() == 0);
}

TEST_CASE("marking a crash by hand rolls back about two seconds")
{
    Rig rig(recipe_of({ harmless_noise() }));
    rig.frames(600);
    rig.guard.mark_crash();
    rig.frame();
    REQUIRE(rig.outcomes.size() == 1);
    CHECK(rig.outcomes[0].verdict.signal == CrashSignal::Manual);
    CHECK(rig.outcomes[0].restored_frame <= 601 - 120);
}

TEST_CASE("an inactive engine is not guarded")
{
    Rig rig(recipe_of({ poke_crash_byte(130, kCrashValue, kCrashValue) }));
    rig.engine.set_active(false);
    rig.mem.of(Target::WRAM)[kCrashByte] = kCrashValue;
    rig.frames(50);
    CHECK(rig.outcomes.empty());
}

TEST_CASE("policy names round-trip")
{
    for (auto p : { PolicyKind::Reroll, PolicyKind::Skip, PolicyKind::Dampen, PolicyKind::Disable, PolicyKind::Escalate })
        CHECK(parse_policy(policy_name(p)) == p);
    CHECK_FALSE(parse_policy("yolo"));
}

TEST_CASE("a definite crash with no rule to blame still rolls back")
{
    Rig rig(recipe_of({ harmless_noise() }));
    rig.engine.set_rule_enabled(0, false);
    rig.frames(200);
    rig.mem.of(Target::WRAM)[kCrashByte] = kCrashValue; // lingering damage from earlier
    rig.frame();
    REQUIRE(rig.outcomes.size() == 1);
    CHECK(rig.outcomes[0].kind == GuardOutcome::RolledBack);
    CHECK(rig.outcomes[0].suspects.empty());
    CHECK(rig.crash_byte() == 0);
}

TEST_CASE("a hard crash that stops frames from advancing is still caught")
{
    // After STP the real core stops reaching the frame hook, so the engine's frame
    // count stalls; the guard must keep checking rather than mistake it for a restart.
    Rig rig(recipe_of({ harmless_noise() }), PolicyKind::Skip);
    rig.frames(100);
    rig.mem.of(Target::WRAM)[kCrashByte] = kCrashValue;
    for (int i = 0; i < 5; i++) // between_frames only: the hook no longer runs
    {
        auto out = rig.guard.between_frames(rig.engine, rig.core);
        if (out.kind != GuardOutcome::Nothing)
            rig.outcomes.push_back(out);
    }
    REQUIRE(rig.outcomes.size() == 1);
    CHECK(rig.outcomes[0].verdict.signal == CrashSignal::CpuStopped);
    CHECK(rig.crash_byte() == 0);
}

TEST_CASE("repeat crashes step further back, past latent damage")
{
    // The poke at frame 130 does nothing until the game reads it at frame 400, so a
    // short rollback lands after the damage. Each repeat must go further back.
    Rule latent = poke_crash_byte(130, kCrashValue, kCrashValue);
    latent.name = "latent";
    latent.range = { Target::WRAM, kLatentByte, kLatentByte };
    latent.op = { .type = OpType::Add, .min = kCrashValue, .max = kCrashValue }; // dampen halves it
    // Dampen keeps the noise rule writing on replay, so blame never runs out on its own.
    Rig rig(recipe_of({ harmless_noise(), latent }), PolicyKind::Dampen);
    rig.core.engine = &rig.engine;
    rig.core.latent_from = 400;
    rig.frames(2000);

    REQUIRE(rig.outcomes.size() >= 2);
    CHECK(rig.outcomes[0].restored_frame > 130); // the first, short rollback can't reach it
    bool reached = false;
    for (auto &o : rig.outcomes)
        reached |= o.restored_frame < 130;
    CHECK(reached);
    CHECK(rig.outcomes.back().kind == GuardOutcome::RolledBack); // never had to give up
    CHECK(rig.engine.active());
    CHECK(rig.mem.of(Target::WRAM)[kLatentByte] != kCrashValue);
}
