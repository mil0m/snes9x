#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "headless_core.h"
#include "test_rom.h"

#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/hook.h"
#include "corrupt/run_control.h"
#include "corrupt/memory_watch.h"

#include "snes9x.h"
void S9xSoftReset(void); // cpuexec.h pulls in the whole CPU core

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
        SnesCoreAccess::watch_resets();
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

TEST_SUITE("reboots in the real core")
{
    TEST_CASE("booting and resetting the console are not reboots")
    {
        Harness h(testrom::Crash::None, trigger_at(100000), PolicyKind::Skip);
        auto before = h.access.sample().reboots; // a process-wide count
        h.run(120);
        CHECK(h.access.sample().reboots == before);
        S9xSoftReset();
        h.run(120);
        CHECK(h.access.sample().reboots == before);
        CHECK(alive(h));
    }

    TEST_CASE("a game that reboots itself is caught, rolled back, and plays on")
    {
        Harness h(testrom::Crash::Reboot, trigger_at(120), PolicyKind::Skip);
        auto before = h.access.sample().reboots;
        h.run(130);
        CHECK(h.access.sample().reboots == before + 1);
        REQUIRE(h.outcomes.size() == 1);
        CHECK(h.outcomes[0].kind == GuardOutcome::RolledBack);
        CHECK(h.outcomes[0].verdict.signal == CrashSignal::Reboot);
        CHECK(h.outcomes[0].restored_frame < 120);
        h.run(600);
        CHECK(h.outcomes.size() == 1);
        CHECK(alive(h));
        // Rolled back past the reboot: the frame counter never went back to zero.
        CHECK(h.wram(testrom::kFrameCounterAddr) > 100);
    }

    TEST_CASE("a reboot with no corruption before it is left alone")
    {
        Harness h(testrom::Crash::Reboot, trigger_at(100000), PolicyKind::Skip);
        auto before = h.access.sample().reboots;
        h.run(100);
        h.memory.write(Target::WRAM, testrom::kTriggerAddr, testrom::kTriggerValue); // the game's own doing
        h.run(60);
        CHECK(h.access.sample().reboots == before + 1);
        CHECK(h.outcomes.empty());
        CHECK(alive(h));
    }
}

TEST_CASE("the memory watch sees the game's frame counter and the recipe's writes")
{
    Rule noise;
    noise.name = "noise";
    noise.range = { Target::WRAM, 0x4000, 0x40FF };
    noise.op = { .type = OpType::Random, .min = 0, .max = 255 };
    noise.when = { .type = TriggerType::EveryFrames, .every = 2 };
    noise.count = 4;
    Recipe r;
    r.name = "noise";
    r.seed = 5;
    r.rules = { noise };
    Harness h(testrom::Crash::None, r, PolicyKind::Skip);

    MemoryWatch watch;
    watch.watch(Target::WRAM, h.memory.size(Target::WRAM));
    for (int i = 0; i < 300; i++)
    {
        h.run(1);
        watch.observe(h.memory, h.engine.event_log(), h.engine.current_frame());
    }
    auto &s = watch.snapshot();
    CHECK(s.heat[testrom::kFrameCounterAddr] > 40.0f); // NMI counts every frame
    CHECK(s.heat[0x3000] == 0.0f);                       // nobody touches it
    uint64_t hits = 0;
    for (uint32_t a = 0x4000; a <= 0x40FF; a++)
        hits += s.hits[a];
    CHECK(hits >= 590); // 150 firings x 4 bytes, minus the odd unchanged value
    CHECK(s.hits[0x3000] == 0u);
    CHECK(s.bytes[testrom::kFrameCounterAddr] == h.wram(testrom::kFrameCounterAddr));
}


void S9xSetCPU(uint8, uint16); // the $42xx/$43xx registers; cpuexec.h pulls in the whole CPU core
void S9xSetPPU(uint8, uint16); // the $21xx registers
extern uint32 *S9xWramReads, *S9xWramWrites;

TEST_CASE("the core counts the game's WRAM reads and writes while asked to")
{
    Harness h(testrom::Crash::None, Recipe{}, PolicyKind::Skip);
    h.run(5);
    std::vector<uint32_t> reads(0x20000), writes(0x20000);
    S9xWramReads = reads.data();
    S9xWramWrites = writes.data();
    h.run(60);

    SUBCASE("CPU reads and writes, once a frame each")
    {
        // main: WAI; LDA $10 (once a frame). NMI: INC $11 (a read and a write).
        CHECK(reads[testrom::kTriggerAddr] >= 58);
        CHECK(reads[testrom::kTriggerAddr] <= 62);
        CHECK(writes[testrom::kTriggerAddr] == 0u);
        CHECK(reads[testrom::kFrameCounterAddr] >= 58);
        CHECK(writes[testrom::kFrameCounterAddr] >= 58);
        CHECK(writes[testrom::kFrameCounterAddr] <= 62);
        uint64_t stack = 0; // the NMI pushes its return address and flags
        for (uint32_t a = 0x100; a < 0x200; a++)
            stack += writes[a];
        CHECK(stack >= 4 * 58);
        CHECK(reads[0x3000] + writes[0x3000] == 0u);
    }

    SUBCASE("a DMA from WRAM to OAM reads its source")
    {
        std::fill(reads.begin(), reads.end(), 0);
        S9xSetCPU(0x00, 0x4300); // CPU -> PPU, one register
        S9xSetCPU(0x04, 0x4301); // $2104, OAM data
        S9xSetCPU(0x00, 0x4302); // from $7E:0300
        S9xSetCPU(0x03, 0x4303);
        S9xSetCPU(0x7E, 0x4304);
        S9xSetCPU(0x20, 0x4305); // 32 bytes
        S9xSetCPU(0x00, 0x4306);
        S9xSetCPU(0x01, 0x420B); // go, channel 0
        for (uint32_t a = 0x300; a < 0x320; a++)
            CHECK(reads[a] == 1u);
        CHECK(reads[0x2FF] == 0u);
        CHECK(reads[0x320] == 0u);
    }

    SUBCASE("writes through the WMDATA port count at its address")
    {
        std::fill(writes.begin(), writes.end(), 0);
        S9xSetPPU(0x00, 0x2181); // WRAM address $1:5000
        S9xSetPPU(0x50, 0x2182);
        S9xSetPPU(0x01, 0x2183);
        for (int i = 0; i < 3; i++)
            S9xSetPPU(0xAB, 0x2180);
        CHECK(writes[0x15000] == 1u);
        CHECK(writes[0x15002] == 1u);
        CHECK(writes[0x15003] == 0u);
        CHECK(h.wram(0x15001) == 0xAB);
    }

    S9xWramReads = S9xWramWrites = nullptr;
    h.run(2); // and nothing counts once they're gone
}

TEST_CASE("the access counter feeds the memory watch per frame")
{
    Harness h(testrom::Crash::None, Recipe{}, PolicyKind::Skip);
    MemoryWatch watch;
    watch.watch(Target::WRAM, h.memory.size(Target::WRAM));
    {
        WramAccessCounter counter;
        counter.start();
        CHECK(S9xWramReads != nullptr);
        for (int i = 0; i < 300; i++)
        {
            h.run(1);
            counter.take(watch);
        }
        auto &s = watch.snapshot();
        REQUIRE(s.reads.size() == 0x20000);
        CHECK(s.reads[testrom::kTriggerAddr] == doctest::Approx(1.0).epsilon(0.05)); // LDA $10 a frame
        CHECK(s.writes[testrom::kFrameCounterAddr] == doctest::Approx(1.0).epsilon(0.05)); // INC $11
        CHECK(s.reads[0x3000] == 0.0f);
    }
    CHECK(S9xWramReads == nullptr); // stopped when it went away
    CHECK(S9xWramWrites == nullptr);
}
