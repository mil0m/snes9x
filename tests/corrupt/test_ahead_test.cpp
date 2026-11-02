#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/rom_blame.h"
#include "corrupt/test_ahead.h"

#include <algorithm>

using namespace corrupt;
namespace cf = codeflag;

namespace
{

constexpr uint8_t kPoison = 0xDB;

// A game whose whole state is a frame counter and a stopped CPU. Each frame the engine runs
// first (as from the frame hook), then the game runs the ROM bytes in `runs`: one holding
// kPoison stops the CPU. `crash_at`: the game stops at that frame anyway (older damage).
struct Game : CoreAccess
{
    Engine &engine;
    RomKeeper &rom;
    std::vector<uint8_t> &flags;
    std::vector<uint32_t> runs = { 0x10 };
    std::optional<std::vector<uint32_t>> fragile; // where poison stops it; unset: everything it runs
    uint32_t frame = 0;
    bool halted = false;
    std::optional<uint32_t> crash_at;
    int saves = 0, loads = 0, unseen_begins = 0, unseen_ends = 0, unseen_frames = 0;

    Game(Engine &e, RomKeeper &r, std::vector<uint8_t> &f) : engine(e), rom(r), flags(f) {}

    void frame_once()
    {
        engine.on_frame(rom);
        if (halted)
            return;
        frame++;
        for (auto o : runs)
        {
            flags[o] |= cf::Opcode | cf::Touched;
            bool weak = !fragile || std::find(fragile->begin(), fragile->end(), o) != fragile->end();
            if (weak && rom.read(Target::ROM, o) == kPoison)
                halted = true;
        }
        if (crash_at && frame >= *crash_at)
            halted = true;
    }
    std::vector<uint8_t> save_state() override
    {
        saves++;
        return { uint8_t(frame), uint8_t(frame >> 8), uint8_t(halted) };
    }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        loads++;
        frame = s[0] | (s[1] << 8);
        halted = s[2];
        return true;
    }
    FrameSignals sample() override
    {
        FrameSignals s;
        s.cpu_halted = halted;
        s.opcode = kPoison;
        return s;
    }
    void begin_unseen() override { unseen_begins++; }
    void run_unseen_frame() override
    {
        unseen_frames++;
        frame_once();
    }
    void end_unseen() override { unseen_ends++; }
};

Rule live_rom_rule(uint32_t first, uint32_t last, uint8_t min, uint8_t max)
{
    Rule r;
    r.name = "drip";
    r.range = { Target::ROM, first, last };
    r.op = { .type = OpType::Random, .min = min, .max = max };
    r.when = { .type = TriggerType::EveryFrames, .every = 2 };
    return r;
}

struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    Engine engine;
    std::vector<uint8_t> flags;
    Game game{ engine, rom, flags };
    TestAhead ahead;

    explicit Fixture(Rule rule, uint64_t seed = 1)
    {
        rom.set_clock([this] { return engine.current_frame(); });
        rom.game_loaded({});
        flags.assign(mem.size(Target::ROM), 0);
        Recipe r;
        r.name = "test";
        r.seed = seed;
        r.rules.push_back(rule);
        engine.set_recipe(r);
        engine.set_active(true);
        ahead.set_config({ .frames = 30, .tries = 3 });
    }
    // One real frame, with the test first.
    AheadResult step(const AheadHooks &hooks = {})
    {
        auto out = ahead.before_frame(engine, rom, game, flags, hooks);
        game.frame_once();
        return out;
    }
};

} // namespace

TEST_SUITE("test before applying")
{
    TEST_CASE("frames without a live ROM write cost nothing")
    {
        Fixture f(live_rom_rule(0x10, 0x10, 1, 1));
        auto out = f.ahead.before_frame(f.engine, f.rom, f.game, f.flags); // frame 1: the rule rests (every 2)
        CHECK(out.kind == AheadResult::Nothing);
        CHECK(f.game.saves == 0);
        CHECK(f.game.unseen_frames == 0);
    }

    TEST_CASE("a change that holds up goes ahead, and everything else is as it was")
    {
        Fixture f(live_rom_rule(0x10, 0x10, 0x42, 0x42));
        f.step();
        uint32_t frame = f.game.frame;
        auto flags = f.flags;
        auto out = f.ahead.before_frame(f.engine, f.rom, f.game, f.flags);
        CHECK(out.kind == AheadResult::Kept);
        CHECK(out.written == std::vector<uint32_t>{ 0x10 });
        CHECK(out.runs == 1);
        CHECK(out.summary.empty());
        CHECK(f.game.unseen_frames == 30);
        CHECK(f.game.unseen_begins == 1);
        CHECK(f.game.unseen_ends == 1);
        // Nothing of the run ahead is left: the game, the engine, the ROM, the map.
        CHECK(f.game.frame == frame);
        CHECK(f.engine.current_frame() == 1);
        CHECK(f.mem.of(Target::ROM)[0x10] == 0);
        CHECK(f.rom.changed() == 0);
        CHECK(f.flags == flags);
        f.game.frame_once(); // the real frame makes the change
        CHECK(f.mem.of(Target::ROM)[0x10] == 0x42);
        CHECK(f.ahead.tested() == 1);
    }

    TEST_CASE("a pressed hotkey is still pressed after the test")
    {
        Rule key = live_rom_rule(0x10, 0x10, 0x42, 0x42);
        key.when = { .type = TriggerType::Hotkey, .hotkey = 3 };
        Fixture f(key);
        f.engine.press_hotkey(3);
        auto out = f.step();
        CHECK(out.kind == AheadResult::Kept);
        CHECK(f.mem.of(Target::ROM)[0x10] == 0x42);
    }

    TEST_CASE("a change that always crashes is tried, re-rolled, and finally skipped")
    {
        Fixture f(live_rom_rule(0x10, 0x10, kPoison, kPoison));
        f.step();
        auto out = f.step();
        CHECK(out.kind == AheadResult::Skipped);
        CHECK(out.rerolls == 2);
        CHECK(out.runs == 4); // three picks and one control run
        CHECK(out.skipped == std::vector<uint32_t>{ 0x10 });
        CHECK(out.failure.find("CPU stopped") != std::string::npos);
        CHECK(out.summary.find("skipped this time") != std::string::npos);
        CHECK(out.headline.find("would crash every time") != std::string::npos);
        CHECK(out.headline.find("CPU") == std::string::npos); // short: the why is in the summary
        // The real frame didn't make it, and the game runs on.
        CHECK(f.mem.of(Target::ROM)[0x10] == 0);
        CHECK(!f.game.halted);
        CHECK(f.engine.event_log().empty());
        CHECK(f.ahead.skipped() == 1);
        // The veto was for that frame only.
        f.step();
        auto again = f.step();
        CHECK(again.kind == AheadResult::Skipped);
        CHECK(f.engine.vetoes() == 1);
    }

    TEST_CASE("a pick that crashes is re-rolled into one that holds up")
    {
        // The game only runs 0x10; the rule picks one of four bytes, so some seed starts on it.
        bool found = false;
        for (uint64_t seed = 1; seed < 200 && !found; seed++)
        {
            Fixture f(live_rom_rule(0x10, 0x13, kPoison, kPoison), seed);
            f.step();
            auto planned = f.engine.preview_next(f.rom, Target::ROM);
            if (planned.size() != 1 || planned[0].write.offset != 0x10)
                continue;
            auto out = f.step();
            if (out.kind != AheadResult::Rerolled)
                continue;
            found = true;
            CHECK(out.rerolls >= 1);
            REQUIRE(out.written.size() == 1);
            CHECK(out.written[0] != 0x10);
            CHECK(f.mem.of(Target::ROM)[0x10] == 0);
            CHECK(f.mem.of(Target::ROM)[out.written[0]] == kPoison); // harmless there
            CHECK(!f.game.halted);
            CHECK(out.summary.find("rolled again") != std::string::npos);
            CHECK(out.headline.find("would crash, so it picked again") != std::string::npos);
            CHECK(f.ahead.rerolled() == 1);
        }
        CHECK(found);
    }

    TEST_CASE("a crash the game has anyway isn't the change's fault")
    {
        Fixture f(live_rom_rule(0x10, 0x10, kPoison, kPoison));
        f.step();
        f.game.crash_at = f.game.frame + 5; // older damage: the control run crashes too
        f.game.runs = { 0x10 };
        auto out = f.ahead.before_frame(f.engine, f.rom, f.game, f.flags);
        // Poison and the old damage both stop it; the control run (without the write) too.
        CHECK(out.kind == AheadResult::Kept);
        CHECK(out.runs == 2);
    }

    TEST_CASE("a crash before the game ever used the change isn't its fault either")
    {
        Fixture f(live_rom_rule(0x10, 0x10, 0x42, 0x42));
        f.game.runs = { 0x20 }; // never runs 0x10
        f.step();
        f.game.crash_at = f.game.frame + 5;
        auto out = f.ahead.before_frame(f.engine, f.rom, f.game, f.flags);
        CHECK(out.kind == AheadResult::Kept);
        CHECK(out.runs == 1); // no control run needed
    }

    TEST_CASE("a landing blamed on the change is proof enough: no control run")
    {
        Fixture f(live_rom_rule(0x10, 0x10, 0x55, 0x55));
        f.game.runs = {};
        AheadHooks hooks;
        hooks.take_derail = [&]() -> std::optional<DerailWatch::Note> {
            if (f.mem.of(Target::ROM)[0x10] == 0x55)
                return DerailWatch::Note{ Landing::MidInstruction, 0x808000, { 0x10 } };
            return std::nullopt;
        };
        f.step(hooks);
        auto out = f.step(hooks);
        CHECK(out.kind == AheadResult::Skipped);
        CHECK(out.runs == 3);
        CHECK(out.failure.find("middle of an instruction") != std::string::npos);
    }

    TEST_CASE("a landing blamed on older changes leaves the decision to the instant revert")
    {
        Fixture f(live_rom_rule(0x10, 0x10, 0x55, 0x55));
        AheadHooks hooks;
        int begins = 0, ends = 0;
        hooks.begin = [&] { begins++; };
        hooks.end = [&] { ends++; };
        hooks.take_derail = [&]() -> std::optional<DerailWatch::Note> {
            if (f.mem.of(Target::ROM)[0x10] == 0x55)
                return DerailWatch::Note{ Landing::Brk, 0x808000, { 0x300 } };
            return std::nullopt;
        };
        f.step(hooks);
        auto out = f.step(hooks);
        CHECK(out.kind == AheadResult::Kept);
        CHECK(begins == 1);
        CHECK(ends == 1);
    }

    TEST_CASE("off, or with the engine off, it never runs")
    {
        Fixture f(live_rom_rule(0x10, 0x10, kPoison, kPoison));
        f.ahead.set_config({ .frames = 0 });
        f.step();
        CHECK(f.step().kind == AheadResult::Nothing);
        CHECK(f.game.halted); // untested, the poison went in
        Fixture g(live_rom_rule(0x10, 0x10, kPoison, kPoison));
        g.engine.set_active(false);
        CHECK(g.ahead.before_frame(g.engine, g.rom, g.game, g.flags).kind == AheadResult::Nothing);
    }

    TEST_CASE("the ROM blame's bookkeeping comes back as it was")
    {
        Fixture f(live_rom_rule(0x10, 0x10, 0x42, 0x42));
        RomBlame blame(f.rom);
        blame.attach(f.flags.data(), f.flags.size());
        f.rom.write(Target::ROM, 0x30, 7); // an older change
        f.step();
        CHECK(blame.watched() == 1);
        f.ahead.before_frame(f.engine, f.rom, f.game, f.flags);
        CHECK(blame.watched() == 1);
        CHECK((f.flags[0x30] & cf::Changed));
        CHECK(!(f.flags[0x10] & cf::Changed));
        f.game.frame_once();
        CHECK(blame.watched() == 2);
    }

    TEST_CASE("a run stops early once every written byte was used and held up a while")
    {
        Rule nudge = live_rom_rule(0x10, 0x10, 0, 0);
        nudge.op = { .type = OpType::Add, .min = 1, .max = 1 }; // a change every time
        Fixture f(nudge);
        f.ahead.set_config({ .frames = 30, .tries = 3, .settle_frames = 5 });
        f.step();
        auto out = f.ahead.before_frame(f.engine, f.rom, f.game, f.flags);
        CHECK(out.kind == AheadResult::Kept);
        CHECK(out.untried.empty());
        CHECK(f.game.unseen_frames == 6); // used in frame 1, held up through frame 6
        f.game.runs = {}; // a byte the game doesn't run: the whole window, and untried
        f.game.frame_once();
        f.game.frame_once();
        f.game.unseen_frames = 0;
        out = f.ahead.before_frame(f.engine, f.rom, f.game, f.flags);
        CHECK(out.kind == AheadResult::Kept);
        CHECK(out.untried == std::vector<uint32_t>{ 0x10 });
        CHECK(f.game.unseen_frames == 30);
    }

    TEST_CASE("a re-roll the game never touched is only a fallback for one it ran")
    {
        // The game runs 0x10 (poisoned, it crashes) and 0x11 (fine); 0x12 and 0x13 sit unused.
        int ran = 0, fallback = 0;
        for (uint64_t seed = 1; seed < 400; seed++)
        {
            Fixture f(live_rom_rule(0x10, 0x13, kPoison, kPoison), seed);
            f.game.runs = { 0x10, 0x11 };
            f.game.fragile = std::vector<uint32_t>{ 0x10 };
            f.step();
            auto planned = f.engine.preview_next(f.rom, Target::ROM);
            if (planned.size() != 1 || planned[0].write.offset != 0x10)
                continue;
            auto out = f.step();
            CAPTURE(seed);
            CHECK(!f.game.halted);
            if (out.kind != AheadResult::Rerolled)
                continue;
            REQUIRE(out.written.size() == 1);
            if (out.untried.empty())
            {
                CHECK(out.written[0] == 0x11); // a pick the game ran and survived
                CHECK(out.summary.find("holds up") != std::string::npos);
                ran++;
            }
            else
            {
                CHECK(out.untried == out.written); // the reserve: no pick it ran survived
                CHECK(out.written[0] >= 0x12);
                CHECK(out.summary.find("goes in untried") != std::string::npos);
                fallback++;
            }
        }
        CHECK(ran > 0);
        CHECK(fallback > 0);
    }
}

