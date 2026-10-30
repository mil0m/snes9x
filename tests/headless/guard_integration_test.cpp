#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "headless_core.h"
#include "test_rom.h"

#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/hook.h"
#include "corrupt/run_control.h"

#include "snes9x.h"

using namespace corrupt;

namespace
{

// Wired like the Qt front end: the engine runs inside the frame (hook), the
// guard runs between frames.
struct Harness
{
    static inline Harness *current = nullptr;

    Engine engine;
    CoreMemory memory;
    SnesCoreAccess access;
    CrashGuard guard;
    RunControl control;
    std::vector<GuardOutcome> outcomes;
    std::vector<RunControl::Started> starts;

    Harness(testrom::Crash crash, Recipe recipe, PolicyKind policy)
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build(crash)));
        engine.set_recipe(std::move(recipe));
        engine.set_active(true);
        guard.set_policy(policy);
        current = this;
        S9xCorruptFrameHook = [] {
            current->engine.on_frame(current->memory);
            current->access.note_frame_start();
        };
    }

    ~Harness()
    {
        S9xCorruptFrameHook = nullptr;
        current = nullptr;
    }

    void run(int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            headless::run_frames(1);
            if (auto s = control.between_frames(engine, access); s != RunControl::Started::Nothing)
                starts.push_back(s);
            auto out = guard.between_frames(engine, access);
            if (out.kind != GuardOutcome::Nothing)
                outcomes.push_back(out);
        }
    }

    uint8_t wram(uint32_t addr) { return memory.read(Target::WRAM, addr); }
};

Recipe trigger_at(uint64_t frame)
{
    Rule poke;
    poke.name = "poke";
    poke.range = { Target::WRAM, testrom::kTriggerAddr, testrom::kTriggerAddr };
    poke.op = { .type = OpType::Random, .min = testrom::kTriggerValue, .max = testrom::kTriggerValue };
    poke.when = { .type = TriggerType::OnceAtFrame, .frame = frame };
    Recipe r;
    r.name = "trigger";
    r.seed = 1;
    r.rules.push_back(poke);
    return r;
}

// The game is alive if its NMI handler keeps counting.
bool alive(Harness &h)
{
    uint8_t before = h.wram(testrom::kFrameCounterAddr);
    h.run(10);
    return h.wram(testrom::kFrameCounterAddr) != before && !(CPU.Flags & HALTED_FLAG);
}

} // namespace

TEST_CASE("the test ROM runs and its NMI counts frames")
{
    Harness h(testrom::Crash::None, trigger_at(100000), PolicyKind::Skip);
    h.run(120);
    CHECK(alive(h));
    CHECK(h.outcomes.empty());
}

TEST_CASE("the healthy ROM ignores the trigger value")
{
    Harness h(testrom::Crash::None, trigger_at(60), PolicyKind::Skip);
    h.run(300);
    CHECK(h.outcomes.empty());
    CHECK(alive(h));
}

TEST_CASE("STP: caught on the spot, rolled back, game keeps running")
{
    Harness h(testrom::Crash::Stop, trigger_at(120), PolicyKind::Skip);
    h.run(130);
    REQUIRE(h.outcomes.size() == 1);
    CHECK(h.outcomes[0].verdict.signal == CrashSignal::CpuStopped);
    CHECK(h.outcomes[0].verdict.text.find("STP") != std::string::npos);
    CHECK(h.outcomes[0].restored_frame < 120);
    h.run(600);
    CHECK(h.outcomes.size() == 1);
    CHECK(h.wram(testrom::kTriggerAddr) != testrom::kTriggerValue);
    CHECK(alive(h));
}

TEST_CASE("NMI switched off: caught after its threshold, rolled back")
{
    Harness h(testrom::Crash::NmiOff, trigger_at(120), PolicyKind::Skip);
    h.run(120 + 330);
    REQUIRE(h.outcomes.size() == 1);
    CHECK(h.outcomes[0].verdict.signal == CrashSignal::NmiOff);
    h.run(600);
    CHECK(h.outcomes.size() == 1);
    CHECK(alive(h));
}

TEST_CASE("a crash that keeps happening: gives up, corruption off, game alive")
{
    // Re-rolling can't help when the only value crashes.
    Harness h(testrom::Crash::Stop, trigger_at(120), PolicyKind::Reroll);
    h.run(2000);
    REQUIRE_FALSE(h.outcomes.empty());
    CHECK(h.outcomes.back().kind == GuardOutcome::GaveUp);
    CHECK_FALSE(h.engine.active());
    CHECK(alive(h));
}

TEST_SUITE("saved moments in the real core")
{
    Recipe busy_noise()
    {
        Rule wram;
        wram.name = "wram";
        wram.range = { Target::WRAM, 0x4000, 0x4FFF };
        wram.op = { .type = OpType::Random, .min = 0, .max = 255 };
        wram.when = { .type = TriggerType::ChancePerFrame, .chance = 0.5 };
        wram.count = 8;
        Rule palette = wram;
        palette.name = "palette";
        palette.range = { Target::CGRAM, 0, 0x1FF };
        palette.op = { .type = OpType::Add, .min = -3, .max = 3 };
        palette.when = { .type = TriggerType::EveryFrames, .every = 4 };
        Recipe r;
        r.name = "busy";
        r.seed = 2024;
        r.rules = { wram, palette };
        return r;
    }

    std::vector<uint8_t> all_wram(Harness & h)
    {
        std::vector<uint8_t> out;
        for (uint32_t a = 0; a < 0x20000; a++)
            out.push_back(h.wram(a));
        return out;
    }

    TEST_CASE("starting from the saved moment replays the same run in the emulator")
    {
        Harness h(testrom::Crash::None, busy_noise(), PolicyKind::Skip);
        h.engine.set_active(false);
        h.engine.hold_starts(true);
        h.run(45); // the game has been running for a while

        h.engine.set_active(true);
        h.run(1);
        REQUIRE(h.starts == std::vector{ RunControl::Started::Fresh });
        auto anchor = h.control.anchor();
        h.run(240);
        auto wram = all_wram(h);
        auto picture = headless::frame_hash();
        auto counter = h.wram(testrom::kFrameCounterAddr);

        h.run(133); // play on
        CHECK(h.wram(testrom::kFrameCounterAddr) != counter);
        h.control.request_start_from(anchor);
        h.engine.reroll(busy_noise().seed);
        h.run(1);
        CHECK(h.starts.back() == RunControl::Started::FromAnchor);
        h.run(240);
        CHECK(h.wram(testrom::kFrameCounterAddr) == counter);
        CHECK(all_wram(h) == wram);
        CHECK(headless::frame_hash() == picture);
        CHECK(h.outcomes.empty());
    }
}

