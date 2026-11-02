#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "headless_core.h"
#include "test_rom.h"

#include "corrupt/code_map.h"
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/hook.h"
#include "corrupt/instant_revert.h"
#include "corrupt/test_ahead.h"
#include "corrupt/jump_tables.h"
#include "corrupt/pointer_swap.h"
#include "corrupt/rom_heat.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/rom_blame.h"
#include "corrupt/survivors.h"
#include "corrupt/run_control.h"
#include "corrupt/memory_watch.h"
#include "corrupt/function_guard.h"
#include "corrupt/function_change.h"
#include "corrupt/paint_history.h"
#include "corrupt/paint_store.h"
#include "corrupt/paint_watch.h"

#include "snes9x.h"
#include "memmap.h"
void S9xSoftReset(void); // cpuexec.h pulls in the whole CPU core

#include <atomic>
#include <chrono>
#include <thread>
#include <unistd.h>

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
    RomKeeper rom{ memory };
    SnesCoreAccess access;
    CrashGuard guard;
    RunControl control;
    std::vector<GuardOutcome> outcomes;
    std::vector<RunControl::Started> starts;

    RomBlame *blame = nullptr; // collects after every frame, before the guard, when set
    // The instant revert, wired like the app when set (needs the blame and a tracer).
    RomTracer *tracer = nullptr;
    InstantRevert *instant = nullptr;
    std::vector<InstantRevertResult> reverts;
    // Test before applying, wired like the app when set (needs the tracer and its map).
    TestAhead *ahead = nullptr;
    CodeMap *map = nullptr;
    std::vector<AheadResult> tests;

    Harness(testrom::Crash crash, Recipe recipe, PolicyKind policy)
        : Harness(testrom::build(crash), std::move(recipe), policy)
    {
    }

    Harness(const std::vector<uint8_t> &rom_image, Recipe recipe, PolicyKind policy)
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(rom_image));
        SnesCoreAccess::watch_resets();
        rom.set_clock([this] { return engine.current_frame(); });
        rom.game_loaded(SnesCoreAccess::locked_rom());
        engine.set_locked(rom.locked_mask());
        engine.set_recipe(std::move(recipe));
        engine.set_active(true);
        guard.set_policy(policy);
        current = this;
        S9xCorruptFrameHook = [] {
            current->engine.on_frame(current->rom);
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
            if (tracer && instant && blame)
                if (auto r = instant->after_frame(tracer->take_derail(), engine.current_frame(), access, *blame))
                    reverts.push_back(*r);
            if (auto s = control.between_frames(engine, access, &rom); s != RunControl::Started::Nothing)
                starts.push_back(s);
            if (blame)
                blame->collect(engine.current_frame());
            auto out = guard.between_frames(engine, access);
            rom.after_guard(out);
            if (out.kind != GuardOutcome::Nothing)
                outcomes.push_back(out);
            if (tracer && instant)
                instant->keep(access, engine.current_frame(), engine.active() && tracer->tracing());
            if (ahead && tracer && map)
            {
                AheadHooks hooks;
                hooks.take_derail = [this] { return tracer->take_derail(); };
                auto t = ahead->before_frame(engine, rom, access, { map->data(), map->size() }, hooks);
                if (t.kind != AheadResult::Nothing)
                    tests.push_back(t);
            }
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

TEST_CASE("a game flipping overscan around VBlank can't freeze the emulator")
{
    // The old core never started VBlank here, so S9xMainLoop never returned. Fail, don't hang.
    std::atomic<bool> done{ false };
    std::thread watchdog([&] {
        for (int i = 0; i < 200 && !done; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!done)
        {
            fprintf(stderr, "FATAL: the overscan-flipping ROM froze the emulator (no VBlank)\n");
            _exit(1);
        }
    });
    REQUIRE(headless::init());
    REQUIRE(headless::load_rom_memory(testrom::build_overscan_flip()));
    uint8_t before = Memory.RAM[testrom::kFrameCounterAddr];
    headless::run_frames(30);
    done = true;
    watchdog.join();
    CHECK(uint8_t(Memory.RAM[testrom::kFrameCounterAddr] - before) >= 29); // one NMI a frame
}

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

TEST_SUITE("ROM corruption in the real core")
{
    // The test ROM is 32 KB of LoROM at $00:8000, so ROM offset = address - 0x8000.
    constexpr uint32_t kNmiInc = 0x0030; // INC $11 (E6 11): the frame counter
    constexpr uint32_t kMainWai = 0x0011; // WAI (CB), once a frame in the main loop

    Rule poke_rom(uint32_t offset, uint8_t value, Trigger when)
    {
        Rule r;
        r.name = "poke rom";
        r.range = { Target::ROM, offset, offset };
        r.op = { .type = OpType::Random, .min = value, .max = value };
        r.when = when;
        return r;
    }

    Recipe one(Rule r)
    {
        Recipe recipe;
        recipe.name = "rom";
        recipe.seed = 1;
        recipe.rules = { r };
        return recipe;
    }

    TEST_CASE("the header and vectors are locked, wherever the mapping puts them")
    {
        Harness h(testrom::Crash::None, one(poke_rom(0x7FFC, 0x00, { .type = TriggerType::OnceAtFrame, .frame = 5 })),
                  PolicyKind::Skip);
        auto locked = SnesCoreAccess::locked_rom();
        REQUIRE(locked.size() == 1);
        CHECK(locked[0].first == 0x7FB0); // LoROM: $00:FFB0 is ROM 0x7FB0
        CHECK(locked[0].last == 0x7FFF);
        h.run(20);
        CHECK(h.memory.read(Target::ROM, 0x7FFC) == 0x00); // the reset vector's low byte: $8000
        CHECK(h.memory.read(Target::ROM, 0x7FFD) == 0x80);
        CHECK(h.rom.changed() == 0);
        CHECK(alive(h));
    }

    TEST_CASE("classic: an at_load patch boots with the game (INC becomes DEC, the counter runs backwards)")
    {
        Harness h(testrom::Crash::None, one(poke_rom(kNmiInc, 0xC6, { .type = TriggerType::AtLoad })), PolicyKind::Skip);
        h.engine.hold_starts(true);
        h.engine.set_active(false);
        h.run(30); // the clean game counts up
        uint8_t a = h.wram(testrom::kFrameCounterAddr);
        h.run(10);
        CHECK(uint8_t(h.wram(testrom::kFrameCounterAddr) - a) == 10);

        h.engine.set_active(true);
        h.run(1); // the start: patch, power cycle, anchor
        REQUIRE(h.starts.size() == 1);
        CHECK(h.memory.read(Target::ROM, kNmiInc) == 0xC6);
        CHECK(h.wram(testrom::kFrameCounterAddr) == 0x55); // freshly booted: RAM is 0x55
        h.run(20);
        uint8_t b = h.wram(testrom::kFrameCounterAddr);
        h.run(10);
        CHECK(uint8_t(b - h.wram(testrom::kFrameCounterAddr)) == 10); // down now

        h.engine.set_active(false);
        h.engine.set_recipe({}); // another recipe starts from the clean ROM
        h.engine.set_active(true);
        h.run(1);
        CHECK(h.memory.read(Target::ROM, kNmiInc) == 0xE6);
    }

    TEST_CASE("Export ROM: the corrupted bytes, with a checksum that matches them")
    {
        Harness h(testrom::Crash::None, one(poke_rom(kNmiInc, 0xC6, { .type = TriggerType::AtLoad })), PolicyKind::Skip);
        h.engine.hold_starts(true);
        h.engine.set_active(false);
        h.engine.set_active(true);
        h.run(1);
        auto image = SnesCoreAccess::rom_image();
        REQUIRE(image.size() == 0x8000);
        CHECK(image[kNmiInc] == 0xC6);
        uint16_t sum = 0;
        for (auto b : image)
            sum = uint16_t(sum + b);
        uint16_t complement = uint16_t(image[0x7FDC] | image[0x7FDD] << 8);
        uint16_t checksum = uint16_t(image[0x7FDE] | image[0x7FDF] << 8);
        CHECK(checksum == sum);
        CHECK(uint16_t(complement ^ checksum) == 0xFFFF);
        CHECK(h.memory.read(Target::ROM, 0x7FDE) != image[0x7FDE]); // the live ROM keeps its header
        auto raw = SnesCoreAccess::rom_image(false);
        CHECK(raw[0x7FDE] == h.memory.read(Target::ROM, 0x7FDE));
    }

    TEST_CASE("blame by execution: only the changed byte the game ran into is reverted")
    {
        namespace rare = testrom::rare;
        constexpr uint32_t kPadding = 0x5000; // never run, never read
        Recipe recipe;
        recipe.name = "classic";
        recipe.seed = 1;
        recipe.rules = { poke_rom(rare::kRoutine, 0xDB, { .type = TriggerType::AtLoad }), // INC -> STP
                         poke_rom(kNmiInc, 0xC6, { .type = TriggerType::AtLoad }),        // INC -> DEC, harmless
                         poke_rom(kPadding, 0x12, { .type = TriggerType::AtLoad }) };
        Harness h(testrom::build_rare(), recipe, PolicyKind::Skip);
        CodeMap map(Memory.CalculatedSize);
        RomTracer tracer;
        REQUIRE(tracer.start(map));
        RomBlame blame(h.rom);
        blame.attach(map.data(), map.size());
        h.guard.set_byte_blame(&blame);
        h.blame = &blame;
        h.engine.hold_starts(true);
        h.engine.set_active(false);
        h.engine.set_active(true);
        h.run(200); // boots from the corrupted ROM; the routine hasn't run yet
        CHECK(h.outcomes.empty());
        CHECK(blame.watched() == 3);
        CHECK(blame.used() == 1); // the DEC
        CHECK(alive(h));

        uint8_t runs = h.wram(rare::kRunsAddr);
        h.memory.write(Target::WRAM, testrom::kTriggerAddr, testrom::kTriggerValue); // the player goes somewhere new
        h.run(60);
        REQUIRE(h.outcomes.size() == 1); // STP: caught
        auto &out = h.outcomes[0];
        CHECK(out.kind == GuardOutcome::RolledBack);
        CHECK(out.suspects.empty()); // no rule wrote anything near the crash
        CHECK(out.rom_blamed == std::vector<uint32_t>{ rare::kRoutine });
        CHECK(h.memory.read(Target::ROM, rare::kRoutine) == 0xE6); // the STP is gone for good
        CHECK(h.memory.read(Target::ROM, kNmiInc) == 0xC6);       // the rest of the corruption stays
        CHECK(h.memory.read(Target::ROM, kPadding) == 0x12);
        CHECK(h.rom.changed() == 2);
        CHECK(alive(h));

        h.memory.write(Target::WRAM, testrom::kTriggerAddr, testrom::kTriggerValue); // and there again
        h.run(10);
        CHECK(h.outcomes.size() == 1);
        CHECK(h.wram(rare::kRunsAddr) == uint8_t(runs + 1)); // the routine ran, fixed
        CHECK(alive(h));
        tracer.stop();
    }

    // Learns a code map from the clean game, then runs `rule` as a classic run aimed with it.
    struct Aimed
    {
        CodeMap map{ Memory.CalculatedSize };
        RomTracer tracer;
        Harness h;
        Aimed(Rule rule) : h(testrom::Crash::None, one(rule), PolicyKind::Skip)
        {
            map = CodeMap(Memory.CalculatedSize);
            REQUIRE(tracer.start(map));
            h.engine.hold_starts(true);
            h.engine.set_active(false);
            h.run(30);
            h.engine.set_code_flags([this] { return std::span<const uint8_t>(map.flags()); });
            h.engine.set_active(true);
            h.run(1);
        }
        ~Aimed() { tracer.stop(); }
    };

    Rule aimed_rule(OpType type, Aim aim)
    {
        Rule r = poke_rom(0, 0, { .type = TriggerType::AtLoad });
        r.range = { Target::ROM, 0, 0x7FFF };
        r.op = { .type = type, .min = 0, .max = 255 };
        r.aim = aim;
        return r;
    }

    TEST_CASE("code ops in the real core: a NOP-out of the only arith instruction stops the frame counter")
    {
        Aimed a(aimed_rule(OpType::NopOut, { .classes = { OpClass::Arith } }));
        CHECK(a.h.engine.aimed_count(0) == 1); // INC $11 in the NMI handler
        CHECK(a.h.memory.read(Target::ROM, kNmiInc) == 0xEA);
        CHECK(a.h.memory.read(Target::ROM, kNmiInc + 1) == 0xEA);
        CHECK(a.h.memory.read(Target::ROM, kNmiInc + 2) == 0x40); // the RTI after it stays
        a.h.run(5); // booted: the reset code cleared the counter
        uint8_t before = a.h.wram(testrom::kFrameCounterAddr);
        a.h.run(20);
        CHECK(a.h.wram(testrom::kFrameCounterAddr) == before);
        CHECK(!(CPU.Flags & HALTED_FLAG));
        CHECK(a.h.outcomes.empty());
    }

    TEST_CASE("code ops in the real core: a branch flip, and operands that keep the code aligned")
    {
        {
            Aimed flip(aimed_rule(OpType::BranchFlip, {}));
            CHECK(flip.h.engine.aimed_count(0) == 1); // the main loop's BNE
            CHECK(flip.h.memory.read(Target::ROM, 0x0016) == 0xF0);
            CHECK(alive(flip.h)); // BEQ falls through to BRA main: still a loop
        }

        Rule operands = aimed_rule(OpType::Random, { .role = Role::Operand });
        operands.count = 3;
        Aimed op(operands);
        REQUIRE(op.h.engine.event_log().size() >= 1);
        for (auto &e : op.h.engine.event_log())
        {
            CAPTURE(e.write.offset);
            CHECK((op.map.at(e.write.offset) & codeflag::Operand));
            CHECK(!(op.map.at(e.write.offset) & codeflag::Opcode));
        }
    }

    TEST_CASE("survivors and pins: a change that proved itself is kept, through crashes and new runs")
    {
        namespace rare = testrom::rare;
        Recipe recipe = one(poke_rom(kNmiInc, 0xC6, { .type = TriggerType::AtLoad })); // INC -> DEC: harmless
        Harness h(testrom::build_rare(), recipe, PolicyKind::Skip);
        CodeMap map(Memory.CalculatedSize);
        RomTracer tracer;
        REQUIRE(tracer.start(map));
        RomBlame blame(h.rom);
        blame.attach(map.data(), map.size());
        h.guard.set_byte_blame(&blame);
        h.blame = &blame;
        h.engine.hold_starts(true);
        h.engine.set_active(false);
        h.engine.set_active(true);
        h.run(1000); // 15 s and more of use, no crash

        std::span<const uint8_t> now(h.memory.data(Target::ROM), h.memory.size(Target::ROM));
        auto list = find_survivors(blame, h.rom.clean(), now, map.flags(), SnesCoreAccess::rom_addresses());
        REQUIRE(list.size() == 1);
        CHECK(list[0].offset == kNmiInc);
        CHECK(list[0].before == "INC $11");
        CHECK(list[0].after == "DEC $11");
        CHECK(list[0].address == 0x808030);

        // Pin it, then drop the rule that made it: the pin alone keeps it, from the next run on.
        h.engine.add_pins({ { kNmiInc, 0xC6, 0xE6, "count down" } });
        h.engine.remove_rule(0);
        h.engine.set_active(false);
        h.engine.set_active(true);
        h.run(1);
        blame.set_pinned({ kNmiInc });
        CHECK(h.control.last_pins().written == 1);
        CHECK(h.memory.read(Target::ROM, kNmiInc) == 0xC6);
        CHECK(h.wram(testrom::kFrameCounterAddr) == 0x55); // a pinned ROM boots fresh, like any classic run

        // A crash with the pinned byte in use: something else is blamed, the pin stays.
        h.engine.add_rule(poke_rom(rare::kRoutine, 0xDB, { .type = TriggerType::OnceAtFrame, .frame = 300 }));
        h.run(400);
        h.memory.write(Target::WRAM, testrom::kTriggerAddr, testrom::kTriggerValue);
        h.run(60);
        REQUIRE(!h.outcomes.empty());
        CHECK(h.memory.read(Target::ROM, kNmiInc) == 0xC6);
        CHECK(h.memory.read(Target::ROM, rare::kRoutine) == 0xE6);
        CHECK(alive(h));
        tracer.stop();
    }

    TEST_CASE("live: a ROM edit that crashes the game is rolled back and undone")
    {
        Harness h(testrom::Crash::None, one(poke_rom(kMainWai, 0xDB, { .type = TriggerType::OnceAtFrame, .frame = 150 })),
                  PolicyKind::Skip);
        h.engine.hold_starts(true);
        h.engine.set_active(false);
        h.engine.set_active(true);
        h.run(400);
        REQUIRE_FALSE(h.outcomes.empty()); // STP: caught
        CHECK(h.outcomes[0].kind == GuardOutcome::RolledBack);
        CHECK(h.outcomes.size() == 1); // without the undo the STP would stay and crash again
        CHECK(h.memory.read(Target::ROM, kMainWai) == 0xCB); // the WAI is back
        CHECK(alive(h));
    }

    // Paint (#66): drafted, applied as a batch with a restore point, watched for ten seconds.
    struct Painting
    {
        RecipePaintStore store;
        PaintHistory history{ store };
        PaintWatch watch;
        explicit Painting(Harness &h) : store(h.rom, h.engine) {}

        PaintWatch::Outcome play(Harness &h, int frames)
        {
            PaintWatch::Outcome out;
            for (int i = 0; i < frames && out.result == PaintWatch::Result::Nothing; i++)
            {
                h.run(1);
                out = watch.observe(h.access.sample());
            }
            return out;
        }
    };

    TEST_CASE("paint: a stroke that stops the game is taken back after Apply, with corruption off")
    {
        Harness h(testrom::Crash::None, Recipe{}, PolicyKind::Skip);
        h.engine.set_active(false); // not corrupting: the crash guard isn't watching
        Painting p(h);
        h.run(30);
        p.history.stroke(Target::ROM, { { kMainWai, 0xDB } }); // STP over the main loop's WAI
        CHECK(h.memory.read(Target::ROM, kMainWai) == 0xCB);   // drafted: not in the game
        CHECK(alive(h));

        auto restore = h.access.save_state();
        auto applied = p.history.apply();
        REQUIRE(applied);
        p.watch.arm(restore, applied->entry);
        CHECK(h.memory.read(Target::ROM, kMainWai) == 0xDB);
        CHECK(patch_byte(h.engine.recipe().patches, Target::ROM, kMainWai) == 0xDB);

        auto out = p.play(h, 120);
        REQUIRE(out.result == PaintWatch::Result::Crashed);
        CHECK(out.verdict->signal == CrashSignal::CpuStopped);
        CHECK(h.outcomes.empty()); // the guard never saw it
        REQUIRE(h.access.load_state(out.state));
        CHECK(p.history.take_back(out.entry));
        CHECK(h.memory.read(Target::ROM, kMainWai) == 0xCB);
        CHECK_FALSE(patch_byte(h.engine.recipe().patches, Target::ROM, kMainWai));
        CHECK(p.history.draft().at(kMainWai) == 0xDB); // back in the draft, to change
        CHECK(alive(h));
    }

    TEST_CASE("paint: a stroke the game survives for ten seconds stays, in the game and the recipe")
    {
        Harness h(testrom::Crash::None, Recipe{}, PolicyKind::Skip);
        h.engine.set_active(false);
        Painting p(h);
        h.run(30);
        constexpr uint32_t kData = 0x3000; // padding the game never runs
        uint8_t clean = h.memory.read(Target::ROM, kData);
        p.history.stroke(Target::ROM, { { kData, 0x55 }, { kData + 1, 0x66 } });
        auto restore = h.access.save_state();
        auto applied = p.history.apply();
        REQUIRE(applied);
        p.watch.arm(restore, applied->entry);
        auto out = p.play(h, int(PaintWatch::kFrames) + 10);
        CHECK(out.result == PaintWatch::Result::Kept);
        CHECK(h.memory.read(Target::ROM, kData) == 0x55);
        CHECK(patch_byte(h.engine.recipe().patches, Target::ROM, kData + 1) == 0x66);
        CHECK(alive(h));
        // Undo still takes it out, by hand.
        CHECK(p.history.undo());
        CHECK(h.memory.read(Target::ROM, kData) == clean);
        CHECK_FALSE(patch_byte(h.engine.recipe().patches, Target::ROM, kData));
    }
}

extern uint8 S9xOpLengthsM0X0[256], S9xOpLengthsM0X1[256], S9xOpLengthsM1X0[256], S9xOpLengthsM1X1[256];

TEST_SUITE("the code map tracer in the real core")
{
    namespace cf = codeflag;
    namespace tr = testrom::traced;

    TEST_CASE("instruction lengths agree with the core's own tables")
    {
        for (int op = 0; op < 256; op++)
        {
            CAPTURE(op);
            CHECK(op_length(uint8_t(op), false, false) == S9xOpLengthsM0X0[op]);
            CHECK(op_length(uint8_t(op), false, true) == S9xOpLengthsM0X1[op]);
            CHECK(op_length(uint8_t(op), true, false) == S9xOpLengthsM1X0[op]);
            CHECK(op_length(uint8_t(op), true, true) == S9xOpLengthsM1X1[op]);
        }
    }

    TEST_CASE("it marks opcodes, operands, widths, data reads and DMA, byte for byte")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_traced()));
        SnesCoreAccess::watch_resets();
        CodeMap map(Memory.CalculatedSize);
        RomTracer tracer;
        REQUIRE(tracer.start(map));
        headless::run_frames(10);
        tracer.stop();
        CHECK(!(CPU.Flags & HALTED_FLAG));

        auto at = [&](uint32_t o) { return uint8_t(map.at(o) & cf::All); }; // without the touched bit
        uint32_t n = tr::kNmi;
        CHECK(at(n + 0) == cf::Opcode); // REP: no width of its own
        CHECK(at(n + 1) == cf::Operand);
        CHECK(at(n + 2) == (cf::Opcode | cf::Op16)); // LDA #$1234
        CHECK(at(n + 3) == (cf::Operand | cf::Op16));
        CHECK(at(n + 4) == (cf::Operand | cf::Op16));
        CHECK(at(n + 5) == (cf::Opcode | cf::Op16)); // LDA $9000: its operand isn't data
        CHECK(at(n + 6) == (cf::Operand | cf::Op16));
        CHECK(at(n + 7) == (cf::Operand | cf::Op16));
        CHECK(at(n + 8) == cf::Opcode); // SEP
        CHECK(at(n + 10) == (cf::Opcode | cf::Op8)); // LDA #$05
        CHECK(at(n + 11) == (cf::Operand | cf::Op8));
        CHECK(at(n + 12) == (cf::Opcode | cf::Op8)); // LDA long
        CHECK(at(n + 15) == (cf::Operand | cf::Op8));
        CHECK(at(n + 0x34) == cf::Opcode); // RTI
        CHECK(at(n + 0x35) == 0);          // never runs

        CHECK(at(tr::kTable + 0) == cf::Data); // the 16-bit read
        CHECK(at(tr::kTable + 1) == cf::Data);
        CHECK(at(tr::kTable + 2) == 0);
        CHECK(at(tr::kTable + 0x10) == cf::Data); // the 8-bit read
        CHECK(at(tr::kTable + 0x11) == 0);

        CHECK(at(tr::kDmaSource) == cf::Dma);
        CHECK(at(tr::kDmaSource + tr::kDmaBytes - 1) == cf::Dma);
        CHECK(at(tr::kDmaSource + tr::kDmaBytes) == 0);

        CHECK(at(0x0000) == cf::Opcode);            // SEI, in emulation mode
        CHECK(at(0x0003) == (cf::Opcode | cf::Op8)); // STZ $10
        CHECK(at(0x0011) == cf::Opcode);            // WAI in the main loop
        CHECK(at(0x0030) == cf::Opcode);            // the JMP to the handler
        CHECK(at(0x0031) == cf::Operand);
        CHECK((at(0x7FEA) & cf::Data)); // the NMI vector, read by the CPU
        CHECK(at(0x5000) == 0);         // padding nobody touches

        // Every byte used carries the touched bit too, and nothing else does.
        for (uint32_t o : { n, n + 3, n + 0x34, tr::kTable, tr::kTable + 0x10, tr::kDmaSource, 0x0011u, 0x7FEAu })
        {
            CAPTURE(o);
            CHECK((map.at(o) & cf::Touched));
        }
        for (uint32_t o : { n + 0x35, tr::kTable + 2, tr::kDmaSource + tr::kDmaBytes, 0x5000u })
        {
            CAPTURE(o);
            CHECK(map.at(o) == 0);
        }

        auto c = map.coverage();
        CHECK(c.data == 5); // the table bytes and the NMI vector
        CHECK(c.dma == size_t(tr::kDmaBytes));
        CHECK(c.both_widths == 0);
    }

    TEST_CASE("live heat: what ran and was read this frame glows, padding doesn't")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_traced()));
        CodeMap map(Memory.CalculatedSize);
        RomTracer tracer;
        REQUIRE(tracer.start(map));
        RomHeat heat;
        heat.reset(map.size());
        headless::run_frames(3);
        heat.observe(map.data(), map.size()); // starts watching: only clears
        CHECK(heat.level(tr::kNmi) == 0);
        headless::run_frames(1);
        heat.observe(map.data(), map.size());
        tracer.stop();
        CHECK(heat.level(tr::kNmi) == 255);    // the handler ran
        CHECK(heat.level(tr::kTable) == 255);  // its table was read
        CHECK(heat.level(tr::kDmaSource) == 255); // and DMA copied from here
        CHECK(heat.level(0x5000) == 0);        // padding nobody touches
        CHECK(heat.level(0x0000) == 0);        // the reset code ran long ago, not this frame
        CHECK(!(map.at(tr::kNmi) & codeflag::Touched));
    }

    TEST_CASE("ROM offsets get the CPU addresses a LoROM game runs them from")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_traced()));
        auto where = SnesCoreAccess::rom_addresses();
        REQUIRE(where.chunks.size() == 8); // 32 KB
        CHECK(where.cpu_address(0x0000) == 0x808000);
        CHECK(where.cpu_address(tr::kNmi) == 0x808040);
        CHECK(where.cpu_address(0x7FFF) == 0x80FFFF);
        CHECK(where.cpu_address(0x8000) == std::nullopt);
    }

    TEST_CASE("it records nothing once stopped, and puts every table entry back")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_traced()));
        SnesCoreAccess::watch_resets();
        auto before = RomTracer::opcode_tables();
        CodeMap map(Memory.CalculatedSize);
        {
            RomTracer tracer;
            REQUIRE(tracer.start(map));
            CHECK(RomTracer::opcode_tables() != before);
            CHECK(!RomTracer().start(map)); // one at a time
        } // stops when it goes
        CHECK(RomTracer::opcode_tables() == before);
        headless::run_frames(5);
        CHECK(map.coverage().seen == 0);
        CodeMap wrong(Memory.CalculatedSize + 1);
        CHECK(!RomTracer().start(wrong));
    }

    TEST_CASE("it chains with the reset watch, which keeps counting reboots")
    {
        Harness h(testrom::Crash::Reboot, trigger_at(100000), PolicyKind::Skip);
        auto watched = RomTracer::opcode_tables();
        CodeMap map(Memory.CalculatedSize);
        RomTracer tracer;
        REQUIRE(tracer.start(map));
        auto before = h.access.sample().reboots;
        h.run(100);
        h.memory.write(Target::WRAM, testrom::kTriggerAddr, testrom::kTriggerValue);
        h.run(60);
        CHECK(h.access.sample().reboots == before + 1);
        // Resetting the console moves the reset watch; the tracer stays on top of it.
        h.access.reset_console();
        h.run(30);
        CHECK((map.at(0x0018) & cf::All) == cf::Opcode); // the JML back to reset
        CHECK((map.at(0x0019) & cf::All) == cf::Operand);
        tracer.stop();
        CHECK(RomTracer::opcode_tables() == watched);
        CHECK(alive(h));
    }
}

TEST_SUITE("jump tables in the real core")
{
    namespace dp = testrom::dispatch;

    TEST_CASE("the tracer notes both kinds of dispatch, and the tables are found behind them")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_dispatch()));
        SnesCoreAccess::watch_resets();
        CodeMap map(Memory.CalculatedSize);
        JumpLog log;
        RomTracer tracer;
        REQUIRE(tracer.start(map, &log));
        headless::run_frames(20);
        tracer.stop();
        SnesCoreAccess::unwatch_resets();
        CHECK(!(CPU.Flags & HALTED_FLAG));

        for (int i = 0; i < dp::kEntries; i++)
        {
            CAPTURE(i);
            CHECK(Memory.RAM[dp::kCounts + i] > 0); // every routine ran
            CHECK(Memory.RAM[dp::kRamCounts + i] > 0);
            auto routine = 0x008000 + dp::kRoutines + 0x10 * uint32_t(i);
            auto direct = log.all().find(JumpKey{ dp::kSite, dp::kTable + 2 * uint32_t(i), routine });
            REQUIRE(direct != log.all().end());
            CHECK(direct->second.opcode == jumpop::JsrIndexed);
            auto through = log.all().find(JumpKey{ dp::kRamSite, kNoEntry, 0x008000 + dp::kRamRoutines + 0x10 * uint32_t(i) });
            REQUIRE(through != log.all().end());
            CHECK(through->second.opcode == jumpop::JmpPointer);
        }
        CHECK(log.size() == size_t(2 * dp::kEntries));

        auto offsets = SnesCoreAccess::rom_offsets();
        CHECK(offsets.offset(0x008100) == dp::kRoutines);
        CHECK(offsets.offset(0x808100) == dp::kRoutines); // a mirror
        CHECK(offsets.offset(0x7E0000) == std::nullopt);  // WRAM
        CHECK(offsets.offset(0x002100) == std::nullopt);  // I/O

        std::span<const uint8_t> rom(Memory.ROM, Memory.CalculatedSize);
        auto found = find_jump_tables(log, rom, map.flags(), offsets);
        REQUIRE(found.tables.size() == 2);
        auto &direct = found.tables[0];
        CHECK(direct.site == dp::kSite);
        CHECK(direct.kind == JumpKind::Direct);
        CHECK(direct.first() == dp::kTable);
        CHECK(direct.seen() == size_t(dp::kEntries));
        CHECK(direct.likely() == 1); // the fifth entry, which points at routine 0
        auto &through = found.tables[1];
        CHECK(through.site == dp::kRamSite);
        CHECK(through.kind == JumpKind::ThroughRam);
        CHECK(through.first() == dp::kRamTable);
        CHECK(through.seen() == size_t(dp::kEntries));
        CHECK(through.likely() == 0);
        CHECK(found.pointer_jumps == 0);
        CHECK(found.ambiguous == 0);
    }

    TEST_CASE("a pointer swap makes the dispatcher run another real routine, both kinds")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_dispatch()));
        CodeMap map(Memory.CalculatedSize);
        JumpLog log;
        RomTracer tracer;
        REQUIRE(tracer.start(map, &log));
        headless::run_frames(20);
        tracer.stop();
        std::span<const uint8_t> rom(Memory.ROM, Memory.CalculatedSize);
        auto tables = find_jump_tables(log, rom, map.flags(), SnesCoreAccess::rom_offsets()).tables;

        CoreMemory memory;
        for (auto [table, counts, routines] : { std::tuple{ dp::kTable, dp::kCounts, dp::kRoutines },
                                                std::tuple{ dp::kRamTable, dp::kRamCounts, dp::kRamRoutines } })
        {
            CAPTURE(table);
            auto slots = swap_slots(tables, table, table + 1, false); // entry 0 only
            REQUIRE(slots.size() == 1);
            CHECK(slots[0].donors.size() == size_t(dp::kEntries - 1));
            Rng rng(5);
            CHECK(apply_pointer_swap(slots, { 0 }, false, memory, rng).size() >= 1);
            uint16_t now = uint16_t(Memory.ROM[table] | (Memory.ROM[table + 1] << 8));
            int k = (now - 0x8000 - int(routines)) / 0x10;
            REQUIRE(k >= 1);
            REQUIRE(k < dp::kEntries);
            for (int i = 0; i < dp::kEntries; i++)
                Memory.RAM[counts + i] = 0;
            headless::run_frames(8); // each entry comes up twice
            CHECK(Memory.RAM[counts] == 0);     // routine 0 no longer runs...
            CHECK(Memory.RAM[counts + k] == 4); // ...routine k runs in its place, and for its own entry
            CHECK(!(CPU.Flags & HALTED_FLAG));
        }
    }

    TEST_CASE("without a jump log the tracer notes nothing and still records the map")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_dispatch()));
        CodeMap map(Memory.CalculatedSize);
        RomTracer tracer;
        REQUIRE(tracer.start(map));
        headless::run_frames(5);
        tracer.stop();
        CHECK((map.at(dp::kSite) & codeflag::Opcode));
    }
}

TEST_SUITE("instant revert in the real core")
{
    namespace dr = testrom::derail;

    // The derail ROM with its code map learned, the blame and the instant revert wired up,
    // and corruption on with nothing in the recipe: changes are made by hand below.
    struct Derailing
    {
        Harness h;
        CodeMap map{ 0 };
        RomTracer tracer;
        DerailWatch watch;
        RomBlame blame{ h.rom };
        InstantRevert instant;

        explicit Derailing(const std::vector<uint8_t> &image, bool on = true, Recipe recipe = {})
            : h(image, std::move(recipe), PolicyKind::Skip)
        {
            map = CodeMap(Memory.CalculatedSize);
            REQUIRE(tracer.start(map));
            tracer.watch_derails(&watch);
            blame.attach(map.data(), map.size());
            h.guard.set_byte_blame(&blame);
            h.blame = &blame;
            h.tracer = &tracer;
            instant.set_enabled(on);
            h.instant = &instant;
            REQUIRE(h.engine.active());
            h.run(30); // the clean game: the map learns its code
        }
        uint8_t rom(uint32_t o) { return h.memory.read(Target::ROM, o); }
    };

    TEST_CASE("a branch into the middle of an instruction is put back within the frame")
    {
        Derailing d(testrom::build_derail());
        CHECK((d.map.at(dr::kStp) & codeflag::Code) == codeflag::Operand);
        d.h.rom.write(Target::ROM, dr::kInnocent, 0x15); // INC $15: harmless
        d.h.rom.write(Target::ROM, dr::kBranch, 0x02);   // BRA +2: onto the STP
        uint8_t frames = d.h.wram(testrom::kFrameCounterAddr);
        uint8_t runs = d.h.wram(dr::kRunsAddr);
        d.h.run(1);
        REQUIRE(d.h.reverts.size() == 1);
        auto &r = d.h.reverts[0];
        CHECK(r.kind == Landing::MidInstruction);
        CHECK(r.pc == 0x008000 + dr::kStp);
        CHECK(r.offsets == std::vector<uint32_t>{ dr::kBranch });
        CHECK(d.rom(dr::kBranch) == 0x00);
        CHECK(d.rom(dr::kInnocent) == 0x15); // ran 160 instructions before: out of the window
        // The frame never happened: the counters are where they were, the CPU isn't stopped.
        CHECK(d.h.wram(testrom::kFrameCounterAddr) == frames);
        CHECK(d.h.wram(dr::kRunsAddr) == runs);
        CHECK(!(CPU.Flags & HALTED_FLAG));
        // The STP was never marked as an opcode: the map stopped learning at the landing.
        CHECK(!(d.map.at(dr::kStp) & codeflag::Opcode));

        d.h.run(20);
        CHECK(d.h.outcomes.empty()); // the guard never saw a crash
        CHECK(d.h.reverts.size() == 1);
        CHECK(uint8_t(d.h.wram(testrom::kFrameCounterAddr) - frames) == 20);
        CHECK(uint8_t(d.h.wram(dr::kRunsAddr) - runs) == 20);
        CHECK(d.h.rom.changed() == 1);
        CHECK(d.instant.total() == 1);
        CHECK(alive(d.h));
    }

    // #57: after a landing the map learns nothing more that frame, but the blame still sees
    // the changed bytes the game runs. (The slow opcode table's half isn't reached: the core
    // only runs it for an instruction crossing the end of a bank.)
    TEST_CASE("after a landing, changed bytes run later in the frame still count as used")
    {
        namespace la = testrom::landing;
        Derailing d(testrom::build_landing(), false); // no instant revert: the frame stands
        REQUIRE((d.map.at(la::kLanding) & codeflag::Code) == codeflag::Operand);
        d.h.rom.write(Target::ROM, la::kInnocent, 0x17);
        uint8_t runs = d.h.wram(la::kRunsAddr);
        d.h.run(1);
        // No landing yet: ordinary uses.
        REQUIRE(d.blame.use(la::kInnocent));
        CHECK(d.blame.use(la::kInnocent)->frames_used == 1);
        CHECK(d.tracer.seen_after_landing() == 0);

        d.h.rom.write(Target::ROM, la::kBranch, 0x01); // BRA +1: onto the NOP in LDA #$EA
        d.h.run(1);
        CHECK(uint8_t(d.h.wram(la::kRunsAddr) - runs) == 2); // the game went on
        CHECK(d.blame.use(la::kInnocent)->frames_used == 2);
        CHECK(d.tracer.seen_after_landing() == 1);
        // Nothing taught: the landing is still only an operand.
        CHECK((d.map.at(la::kLanding) & codeflag::Code) == codeflag::Operand);
        d.h.run(5);
        CHECK(d.blame.use(la::kInnocent)->frames_used == 7);
        CHECK(d.tracer.seen_after_landing() == 6);
        CHECK((d.map.at(la::kLanding) & codeflag::Code) == codeflag::Operand);
        CHECK(alive(d.h));
    }

    TEST_CASE("blind after a landing (as before #57, for measuring), they go unseen")
    {
        namespace la = testrom::landing;
        Derailing d(testrom::build_landing(), false);
        d.tracer.set_blind_after_landing(true);
        d.h.rom.write(Target::ROM, la::kInnocent, 0x17);
        d.h.rom.write(Target::ROM, la::kBranch, 0x01);
        d.h.run(3);
        REQUIRE(d.blame.use(la::kInnocent));
        CHECK(d.blame.use(la::kInnocent)->frames_used == 0);
        CHECK(d.blame.use(la::kBranch)->frames_used == 3); // ran before the landing
        CHECK(d.tracer.seen_after_landing() == 0);
    }

    TEST_CASE("switched off, the guard catches the same crash as before")
    {
        Derailing d(testrom::build_derail(), false);
        d.h.rom.write(Target::ROM, dr::kBranch, 0x02);
        d.h.run(5);
        CHECK(d.h.reverts.empty());
        REQUIRE(d.h.outcomes.size() == 1);
        CHECK(d.h.outcomes[0].verdict.signal == CrashSignal::CpuStopped);
        CHECK(d.h.outcomes[0].rom_blamed == std::vector<uint32_t>{ dr::kBranch });
        CHECK(d.rom(dr::kBranch) == 0x00);
        CHECK(alive(d.h));
    }

    TEST_CASE("a changed byte that never sends the CPU astray stays")
    {
        Derailing d(testrom::build_derail());
        d.h.rom.write(Target::ROM, dr::kInnocent, 0x15);
        d.h.run(60);
        CHECK(d.h.reverts.empty());
        CHECK(d.h.outcomes.empty());
        CHECK(d.rom(dr::kInnocent) == 0x15);
        CHECK(d.h.wram(0x15) > 0);
        CHECK(alive(d.h));
    }

    TEST_CASE("a jump table entry pointing into the middle of a routine is put back; a swapped one stays")
    {
        namespace dp = testrom::dispatch;
        Derailing d(testrom::build_dispatch());
        // Entry 1 one byte on: into routine 1's operand. Entry 2 pointing at routine 3: a swap.
        uint32_t bad = dp::kTable + 2, swapped = dp::kTable + 4;
        d.h.rom.write(Target::ROM, bad, uint8_t(d.rom(bad) + 1));
        d.h.rom.write(Target::ROM, swapped, uint8_t(dp::kRoutines + 0x30));
        d.h.run(8);
        REQUIRE(d.h.reverts.size() == 1);
        CHECK(d.h.reverts[0].kind == Landing::MidInstruction);
        CHECK(d.h.reverts[0].offsets == std::vector<uint32_t>{ bad });
        CHECK(d.rom(bad) == uint8_t(dp::kRoutines + 0x10));
        CHECK(d.rom(swapped) == uint8_t(dp::kRoutines + 0x30));
        CHECK(d.h.outcomes.empty());
        CHECK(alive(d.h));
    }

    Recipe live_branch_and_innocent()
    {
        Recipe r;
        r.name = "live";
        r.seed = 5;
        Rule bad;
        bad.name = "bad branch";
        bad.range = { Target::ROM, dr::kBranch, dr::kBranch };
        bad.op = { .type = OpType::Random, .min = 2, .max = 2 }; // onto the STP, every pick
        bad.when = { .type = TriggerType::EveryFrames, .every = 60 };
        r.rules.push_back(bad);
        Rule fine = bad;
        fine.name = "innocent";
        fine.range = { Target::ROM, dr::kInnocent, dr::kInnocent };
        fine.op = { .type = OpType::Random, .min = 0x15, .max = 0x15 };
        r.rules.push_back(fine);
        return r;
    }

    TEST_CASE("test before applying: a live change that would branch onto the STP never reaches the game")
    {
        Derailing d(testrom::build_derail(), true, live_branch_and_innocent());
        TestAhead ahead;
        ahead.set_config({ .frames = 60, .tries = 3 });
        d.h.ahead = &ahead;
        d.h.map = &d.map;
        uint32_t reboots = d.h.access.sample().reboots;
        uint8_t frames = d.h.wram(testrom::kFrameCounterAddr);
        d.h.run(130); // two firings (frames 60 and 120) after the warm-up's 30
        REQUIRE(!d.h.tests.empty());
        for (auto &t : d.h.tests)
        {
            CAPTURE(t.summary);
            // The bad rule has nothing else to pick, so after its tries it's skipped; the
            // landing named its byte, so the innocent rule's write goes ahead.
            CHECK(t.kind == AheadResult::Skipped);
            CHECK(t.skipped == std::vector<uint32_t>{ dr::kBranch });
            CHECK(t.runs == 3); // a landing is proof: no control runs
        }
        CHECK(d.rom(dr::kBranch) == 0x00);
        CHECK(d.rom(dr::kInnocent) == 0x15);
        CHECK(d.h.reverts.empty());  // the instant revert never had anything to catch
        CHECK(d.h.outcomes.empty()); // nor the guard
        CHECK(uint8_t(d.h.wram(testrom::kFrameCounterAddr) - frames) == 130); // no frame repeated or lost
        CHECK(d.h.access.sample().reboots == reboots);
        CHECK(IPPU.RenderThisFrame);
        CHECK(alive(d.h));
    }

    // The fuse ROM with a light (20 frames) and an innocent change made by hand, and a rule
    // that sends the game somewhere new (the rare routine) at `frame`.
    Recipe somewhere_new_at(uint64_t frame)
    {
        Recipe r = trigger_at(frame);
        r.name = "somewhere new";
        return r;
    }

    TEST_CASE("a lit fuse stops the game and the guard rolls back")
    {
        namespace fz = testrom::fuse;
        Derailing d(testrom::build_fuse(), true, somewhere_new_at(60));
        d.h.rom.write(Target::ROM, fz::kLight, 20);
        d.h.run(80);
        REQUIRE(d.h.outcomes.size() == 1);
        CHECK(d.h.outcomes[0].verdict.signal == CrashSignal::CpuStopped);
        CHECK(d.h.reverts.empty()); // no landing: nothing for the instant revert
        CHECK(d.rom(fz::kLight) == 0x00);
    }

    constexpr uint32_t kNmiCounterInc = 0x0030; // INC $11 in the NMI handler (the frame counter)

    // A fuse lit long before the crash (200 frames): the guard's usual window never sees it,
    // so guessing costs more than one rollback. A known limit: #56 tried replaying the crash
    // to find the fuse and was removed (it didn't pay off on real games; see the safety plan).
    // The instant revert is off: the STP comes right after the NMI handler runs the DEC, so
    // it would put that innocent byte back (blaming what ran last) before the guard sees it.
    struct LongFuse
    {
        Derailing d{ testrom::build_fuse(), false, somewhere_new_at(60) };
        LongFuse()
        {
            namespace fz = testrom::fuse;
            // Made as a run's start makes them (a rollback doesn't undo those).
            d.h.rom.write_at_start(Target::ROM, fz::kLight, 200);
            d.h.rom.write_at_start(Target::ROM, kNmiCounterInc, 0xC6); // INC -> DEC in the NMI handler: harmless, runs every frame
        }
    };

    TEST_CASE("a fuse lit long before the crash costs guessing more than one rollback")
    {
        LongFuse lf;
        auto &d = lf.d;
        d.h.run(900);
        CHECK(d.h.outcomes.size() >= 2); // it blames the DEC first: it's what ran near the crash
        CHECK(alive(d.h));
    }

    TEST_CASE("without testing first, the same recipe needs the instant revert")
    {
        Derailing d(testrom::build_derail(), true, live_branch_and_innocent());
        d.h.run(130);
        CHECK(!d.h.reverts.empty());
        CHECK(d.rom(dr::kInnocent) == 0x15);
    }
}


TEST_SUITE("call graph")
{
    namespace cl = testrom::calls;

    const Function *function_at(const std::vector<Function> &fs, uint32_t entry)
    {
        auto it = std::find_if(fs.begin(), fs.end(), [&](const Function &f) { return f.entry == entry; });
        return it == fs.end() ? nullptr : &*it;
    }

    TEST_CASE("the tracer notes every call, near, long and mixed, and in how many frames each ran")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_calls()));
        SnesCoreAccess::watch_resets();
        CodeMap map(Memory.CalculatedSize);
        CallKeeper calls;
        RomTracer tracer;
        REQUIRE(tracer.start(map, nullptr, &calls.mine()));
        for (int i = 0; i < 30; i++)
        {
            headless::run_frames(1);
            calls.end_frame();
        }
        tracer.stop();
        SnesCoreAccess::unwatch_resets();
        CHECK(!(CPU.Flags & HALTED_FLAG));
        CHECK(Memory.RAM[cl::kCounts + 3] != 0x55); // the inline one ran and came back past its data

        auto &all = calls.mine().all();
        REQUIRE(all.count({ cl::kNearSite, cl::kNear }));
        CHECK(all.at({ cl::kNearSite, cl::kNear }).opcode == callop::Jsr);
        CHECK(all.at({ cl::kLongSite, cl::kLong }).opcode == callop::Jsl);
        CHECK(all.at({ cl::kMixedNearSite, cl::kMixed }).opcode == callop::Jsr);
        CHECK(all.at({ cl::kMixedLongSite, cl::kMixed }).opcode == callop::Jsl);
        CHECK(all.at({ cl::kInlineSite, cl::kInline }).opcode == callop::Jsr);
        CHECK(calls.mine().size() == 5);

        auto fs = calls.functions();
        REQUIRE(fs.size() == 4);
        auto *near = function_at(fs, cl::kNear);
        REQUIRE(near);
        CHECK(near->type == CallType::Near);
        CHECK(near->callers == 1);
        CHECK(near->frames >= 28); // once a frame, every frame
        CHECK(near->now == near->frames);
        CHECK(near->calls == near->frames);
        CHECK(function_at(fs, cl::kLong)->type == CallType::Long);
        auto *mixed = function_at(fs, cl::kMixed);
        CHECK(mixed->type == CallType::Mixed);
        CHECK(mixed->callers == 2);
        CHECK(mixed->calls == 2 * mixed->frames);
        CHECK(function_at(fs, cl::kInline)->type == CallType::Near);
    }

    TEST_CASE("a JSR through a table is a near call; a JMP through RAM isn't a call")
    {
        namespace dp = testrom::dispatch;
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_dispatch()));
        SnesCoreAccess::watch_resets();
        CodeMap map(Memory.CalculatedSize);
        CallKeeper calls;
        RomTracer tracer;
        REQUIRE(tracer.start(map, nullptr, &calls.mine()));
        for (int i = 0; i < 20; i++)
        {
            headless::run_frames(1);
            calls.end_frame();
        }
        tracer.stop();
        SnesCoreAccess::unwatch_resets();
        auto fs = calls.functions();
        REQUIRE(fs.size() == size_t(dp::kEntries)); // the four routines behind JSR ($9100,X)
        for (int i = 0; i < dp::kEntries; i++)
        {
            auto *f = function_at(fs, dp::kRoutines + 0x10 * uint32_t(i));
            REQUIRE(f);
            CHECK(f->type == CallType::Near);
            CHECK(f->first_site == dp::kSite);
        }
    }

    TEST_CASE("frames that aren't real (a run ahead) teach it nothing")
    {
        REQUIRE(headless::init());
        REQUIRE(headless::load_rom_memory(testrom::build_calls()));
        SnesCoreAccess::watch_resets();
        CodeMap map(Memory.CalculatedSize);
        CallKeeper calls;
        RomTracer tracer;
        REQUIRE(tracer.start(map, nullptr, &calls.mine()));
        tracer.note_jumps(false);
        headless::run_frames(10);
        CHECK(calls.mine().empty());
        tracer.note_jumps(true);
        headless::run_frames(1);
        tracer.stop();
        SnesCoreAccess::unwatch_resets();
        CHECK(calls.mine().size() == 5);
    }
}

TEST_SUITE("kill switches")
{
    namespace cl = testrom::calls;

    // The calls ROM, guarded like the app: the tracer, the derail watch, the blame and the
    // instant revert, after 30 clean frames.
    struct Switching
    {
        Harness h{ testrom::build_calls(), Recipe{}, PolicyKind::Skip };
        CodeMap map{ 0 };
        RomTracer tracer;
        DerailWatch watch;
        RomBlame blame{ h.rom };
        InstantRevert instant;

        Switching()
        {
            map = CodeMap(Memory.CalculatedSize);
            REQUIRE(tracer.start(map));
            tracer.watch_derails(&watch);
            blame.attach(map.data(), map.size());
            h.guard.set_byte_blame(&blame);
            h.blame = &blame;
            h.tracer = &tracer;
            h.instant = &instant;
            h.run(30);
        }
        uint8_t rom(uint32_t o) { return h.memory.read(Target::ROM, o); }
        uint8_t runs(int i) { return h.wram(cl::kCounts + uint32_t(i)); }
        void flip(uint32_t entry, uint8_t ret) { h.engine.flip_function(entry, ret, h.rom.clean()[entry]); }
    };

    TEST_CASE("a near function switched off stops running and the game goes on; switched on, it runs again")
    {
        Switching s;
        s.flip(cl::kNear, kRts);
        s.h.run(1);
        CHECK(s.rom(cl::kNear) == kRts);
        uint8_t near = s.runs(0), others = s.runs(1);
        s.h.run(20);
        CHECK(s.runs(0) == near);
        CHECK(s.runs(1) == uint8_t(others + 20));
        CHECK(alive(s.h));
        s.flip(cl::kNear, kRts);
        s.h.run(5);
        CHECK(s.rom(cl::kNear) == 0xE6);
        CHECK(s.runs(0) != near);
        CHECK(s.h.reverts.empty());
        CHECK(s.h.outcomes.empty());
        CHECK(s.h.engine.event_log().empty()); // not a rule's write
    }

    TEST_CASE("a long function switched off with RTL, from a recipe switch on its hotkey")
    {
        Switching s;
        Switch sw;
        sw.entry = cl::kLong;
        sw.ret = kRtl;
        sw.was = 0xE6;
        sw.hotkey = 2;
        s.h.engine.add_switch(sw);
        s.h.engine.press_hotkey(2);
        s.h.run(1);
        CHECK(s.rom(cl::kLong) == kRtl);
        uint8_t long_runs = s.runs(1), near = s.runs(0);
        s.h.run(20);
        CHECK(s.runs(1) == long_runs);
        CHECK(s.runs(0) == uint8_t(near + 20));
        CHECK(alive(s.h));
        s.h.engine.press_hotkey(2);
        s.h.run(2);
        CHECK(s.rom(cl::kLong) == 0xE6);
        CHECK(s.runs(1) != long_runs);
    }

    TEST_CASE("a switch on a timer (#61): off 3 frames of every 10, the counter misses exactly those")
    {
        Switching s;
        REQUIRE(s.h.engine.current_frame() == 30);
        Switch sw;
        sw.entry = cl::kNear;
        sw.ret = kRts;
        sw.was = 0xE6;
        sw.every = 10;
        sw.for_frames = 3;
        s.h.engine.add_switch(sw);
        uint8_t near = s.runs(0), others = s.runs(1);
        s.h.run(30); // frames 31-60: off in 31-33, 41-43 and 51-53
        CHECK(s.runs(0) == uint8_t(near + 21));
        CHECK(s.runs(1) == uint8_t(others + 30));
        CHECK(s.rom(cl::kNear) == 0xE6);
        CHECK(alive(s.h));
    }

    TEST_CASE("a rollback to before the flip turns it back on")
    {
        Switching s;
        uint64_t before = s.h.engine.current_frame();
        s.flip(cl::kNear, kRts);
        s.h.run(10);
        CHECK(s.rom(cl::kNear) == kRts);
        s.h.rom.revert_after(before);
        CHECK(s.rom(cl::kNear) == 0xE6);
    }

    TEST_CASE("a function that reads data after its call runs into it when switched off: the instant revert turns it back on")
    {
        Switching s;
        uint8_t inline_runs = s.runs(3);
        s.flip(cl::kInline, kRts);
        s.h.run(3);
        REQUIRE(s.h.reverts.size() == 1);
        CHECK(s.h.reverts[0].kind == Landing::Brk);
        CHECK(s.h.reverts[0].offsets == std::vector<uint32_t>{ cl::kInline });
        CHECK(s.rom(cl::kInline) == 0xC2);
        CHECK(s.runs(3) != inline_runs); // the frame played again with it back on
        CHECK(alive(s.h));
    }
}

TEST_SUITE("function bodies and guards")
{
    namespace bd = testrom::bodies;

    // The bodies ROM after 30 frames of learning, with every function's body found.
    struct Learned
    {
        Harness h{ testrom::build_bodies(), Recipe{}, PolicyKind::Skip };
        CodeMap map{ 0 };
        CallKeeper calls;
        RomTracer tracer;
        FunctionBodies bodies;

        Learned()
        {
            map = CodeMap(Memory.CalculatedSize);
            REQUIRE(tracer.start(map, nullptr, &calls.mine()));
            for (int i = 0; i < 30; i++)
            {
                h.run(1);
                calls.end_frame();
            }
            bodies = function_bodies(h.rom.clean(), map.flags(), calls.functions(), SnesCoreAccess::rom_addresses());
        }
        uint8_t rom(uint32_t o) { return h.memory.read(Target::ROM, o); }
    };

    TEST_CASE("the map gives a function's body exactly: both sides of its branch, past its call, not its helper")
    {
        Learned l;
        REQUIRE(l.bodies.count(bd::kStep));
        auto &b = l.bodies.at(bd::kStep);
        std::vector<uint32_t> starts;
        for (auto &i : b.code)
            starts.push_back(i.at);
        CHECK(starts == std::vector<uint32_t>{ 0x60, 0x62, 0x63, 0x65, 0x67, 0x69, 0x6B, 0x6D, 0x70 });
        CHECK(b.bytes == bd::kStepBytes);
        CHECK_FALSE(b.contains(bd::kHelper));
        CHECK_FALSE(b.open_ended);
        CHECK(l.bodies.at(bd::kHelper).bytes == 3);
        CHECK(l.bodies.at(bd::kOther).bytes == 3);
    }

    TEST_CASE("guarded with what it calls, a rule over all its code leaves it alone and it keeps counting")
    {
        Learned l;
        auto calls = l.calls.combined();
        HotMask guard = guard_mask({ { bd::kStep, true } }, l.bodies, &calls);
        CHECK(guard.count(Target::ROM) == bd::kStepBytes + 3);
        l.h.engine.set_guarded(guard);
        Rule r;
        r.range = { Target::ROM, bd::kStep, 0xBF }; // the step, the padding after it and the helper
        r.op.type = OpType::Random;
        r.op.min = 0;
        r.op.max = 255;
        r.when.type = TriggerType::EveryFrames;
        r.when.every = 1;
        r.count = 32;
        l.h.engine.add_rule(r);
        uint8_t counter = l.h.wram(bd::kCounter), helper = l.h.wram(bd::kHelperRuns);
        l.h.run(30);
        size_t changed = 0;
        for (uint32_t o = bd::kStep; o <= 0xBF; o++)
        {
            bool guarded = guard.is_hot(Target::ROM, o);
            if (guarded)
                CHECK(l.rom(o) == l.h.rom.clean()[o]);
            else
                changed += l.rom(o) != l.h.rom.clean()[o];
        }
        CHECK(changed > 20);
        for (auto &e : l.h.engine.event_log())
            CHECK_FALSE(guard.is_hot(Target::ROM, e.write.offset));
        CHECK(l.h.wram(bd::kCounter) == uint8_t(counter + 30));
        CHECK(l.h.wram(bd::kHelperRuns) == uint8_t(helper + 30));
        CHECK(alive(l.h));
    }
}

TEST_SUITE("function changes")
{
    namespace bd = testrom::bodies;

    // The bodies ROM learned for 30 frames, and its step function's change options.
    struct Changing
    {
        Harness h{ testrom::build_bodies(), Recipe{}, PolicyKind::Skip };
        CodeMap map{ 0 };
        CallKeeper calls;
        RomTracer tracer;
        RomAddresses where;
        std::vector<ChangeOption> options;

        Changing()
        {
            map = CodeMap(Memory.CalculatedSize);
            REQUIRE(tracer.start(map, nullptr, &calls.mine()));
            for (int i = 0; i < 30; i++)
            {
                h.run(1);
                calls.end_frame();
            }
            where = SnesCoreAccess::rom_addresses();
            auto body = function_body(h.rom.clean(), map.flags(), bd::kStep, where);
            options = change_options(body, h.rom.clean(), map.flags(), where);
        }
        const ChangeOption &at(uint32_t offset)
        {
            for (auto &o : options)
                if (o.at == offset)
                    return o;
            FAIL("no option at " << offset);
            return options.front();
        }
        void apply(const ChangeOption &o, ChangeKind kind, std::vector<uint8_t> value)
        {
            auto c = make_change(bd::kStep, o, kind, std::move(value), h.rom.clean());
            h.engine.flip_bytes({ c.at, c.value, c.was, Engine::Flip::How::On });
            h.run(1); // the flip lands at the start of this frame
        }
        uint8_t ram(uint32_t a) { return h.wram(a); }
    };

    TEST_CASE("the step function's options: two numbers, CLC and INC to swap, its branch and its call")
    {
        Changing c;
        REQUIRE(c.options.size() == 6);
        CHECK(c.at(0x62).kind == ChangeKind::Math); // CLC -> SEC
        CHECK(c.at(0x67).kind == ChangeKind::Number); // AND #$01
        CHECK(c.at(0x6B).kind == ChangeKind::Math); // INC $22 -> DEC
        CHECK(c.at(bd::kAdc).kind == ChangeKind::Number);
        CHECK(c.at(bd::kAdc).value == 1);
        CHECK(c.at(bd::kAdc).other == 0xE9);
        CHECK(c.at(bd::kBeq).kind == ChangeKind::Branch);
        CHECK(c.at(bd::kCall).kind == ChangeKind::Skip);
        CHECK(c.at(bd::kCall).target == bd::kHelper);
    }

    TEST_CASE("its number changed, the counter goes up by 3 a frame; as SBC it goes down")
    {
        Changing c;
        auto &adc = c.at(bd::kAdc);
        c.apply(adc, ChangeKind::Number, number_bytes(adc, 3));
        uint8_t before = c.ram(bd::kCounter);
        c.h.run(10);
        CHECK(c.ram(bd::kCounter) == uint8_t(before + 30));
        auto sbc = number_bytes(adc, 1);
        sbc[0] = adc.other;
        c.h.engine.flip_bytes({ adc.at, sbc, { 0x69, 0x01 }, Engine::Flip::How::On });
        c.h.run(1);
        before = c.ram(bd::kCounter);
        c.h.run(10);
        // SBC with the carry clear takes one more: 2 a frame.
        CHECK(c.ram(bd::kCounter) == uint8_t(before - 20));
        CHECK(alive(c.h));
        CHECK(c.h.engine.event_log().empty()); // not a rule's write
    }

    TEST_CASE("its call skipped, the helper stops; sent to the other function, that one runs twice a frame")
    {
        Changing c;
        auto &call = c.at(bd::kCall);
        c.apply(call, ChangeKind::Skip, skip_bytes(call));
        uint8_t helper = c.ram(bd::kHelperRuns), counter = c.ram(bd::kCounter);
        c.h.run(10);
        CHECK(c.ram(bd::kHelperRuns) == helper);
        CHECK(c.ram(bd::kCounter) == uint8_t(counter + 10));

        auto to = redirect_bytes(call, bd::kOther, c.where);
        REQUIRE(to);
        c.h.engine.flip_bytes({ call.at, *to, { 0x20, 0xA0, 0x80 }, Engine::Flip::How::On });
        c.h.run(1);
        uint8_t other = c.ram(bd::kOtherRuns);
        helper = c.ram(bd::kHelperRuns);
        c.h.run(10);
        CHECK(c.ram(bd::kOtherRuns) == uint8_t(other + 20)); // from main and from step
        CHECK(c.ram(bd::kHelperRuns) == helper);
        CHECK(alive(c.h));
    }

    TEST_CASE("its branch made always, the every-other-frame count stops; never, it counts every frame")
    {
        Changing c;
        auto &beq = c.at(bd::kBeq);
        c.apply(beq, ChangeKind::Branch, branch_bytes(beq, BranchTo::Always));
        uint8_t n = c.ram(bd::kBranchRuns);
        c.h.run(10);
        CHECK(c.ram(bd::kBranchRuns) == n);
        c.h.engine.flip_bytes({ beq.at, branch_bytes(beq, BranchTo::Never), { 0xF0, 0x02 }, Engine::Flip::How::On });
        c.h.run(1);
        n = c.ram(bd::kBranchRuns);
        c.h.run(10);
        CHECK(c.ram(bd::kBranchRuns) == uint8_t(n + 10));
        CHECK(alive(c.h));
    }

    TEST_CASE("a kept change on a hotkey toggles; a rollback to before it puts the game's code back")
    {
        Changing c;
        auto &adc = c.at(bd::kAdc);
        auto change = make_change(bd::kStep, adc, ChangeKind::Number, number_bytes(adc, 5), c.h.rom.clean());
        change.hotkey = 6;
        c.h.engine.add_change(change);
        uint64_t before = c.h.engine.current_frame();
        c.h.engine.press_hotkey(6);
        c.h.run(2);
        CHECK(change_on(change, c.h.memory));
        c.h.rom.revert_after(before);
        CHECK_FALSE(change_on(change, c.h.memory));
        CHECK(c.h.memory.read(Target::ROM, adc.at + 1) == 0x01);
    }
}
