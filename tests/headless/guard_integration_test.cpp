#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "headless_core.h"
#include "test_rom.h"

#include "corrupt/code_map.h"
#include "corrupt/core_access.h"
#include "corrupt/core_memory.h"
#include "corrupt/crash_guard.h"
#include "corrupt/hook.h"
#include "corrupt/jump_tables.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/rom_blame.h"
#include "corrupt/survivors.h"
#include "corrupt/run_control.h"
#include "corrupt/memory_watch.h"

#include "snes9x.h"
#include "memmap.h"
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
    RomKeeper rom{ memory };
    SnesCoreAccess access;
    CrashGuard guard;
    RunControl control;
    std::vector<GuardOutcome> outcomes;
    std::vector<RunControl::Started> starts;

    RomBlame *blame = nullptr; // collects after every frame, before the guard, when set

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
            if (auto s = control.between_frames(engine, access, &rom); s != RunControl::Started::Nothing)
                starts.push_back(s);
            if (blame)
                blame->collect(engine.current_frame());
            auto out = guard.between_frames(engine, access);
            rom.after_guard(out);
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
