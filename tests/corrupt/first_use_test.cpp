#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/first_use.h"

#include <map>

using namespace corrupt;
namespace cf = codeflag;

namespace
{

constexpr uint8_t kFuse = 0xDB;  // lights a fuse: the CPU stops a few frames after it runs
constexpr uint8_t kLands = 0x55; // the tracer would note a landing right after it runs

// A game whose whole state is a frame counter, a fuse and a stopped CPU (the ROM, as in the
// real core, isn't part of a saved state). Each frame the engine runs first (as from the
// frame hook), then the game runs the ROM bytes in `runs` that have come up by then (from
// their frame on). A byte holding kFuse lights the fuse (or, with `any_change_lights`,
// any changed value at a byte in `fragile` does).
struct Game : CoreAccess
{
    Engine &engine;
    RomKeeper &rom;
    std::vector<uint8_t> &flags;
    std::map<uint32_t, uint32_t> runs; // offset -> the frame the game starts running it
    std::vector<uint32_t> fragile;
    bool any_change_lights = false;
    std::optional<std::pair<uint32_t, uint32_t>> together; // lights the fuse only both changed
    uint32_t fuse_frames = 5;
    uint32_t frame = 0, fuse = 0;
    bool halted = false;
    std::optional<uint32_t> crash_at; // older damage: stops at that frame whatever the ROM holds
    int unseen_frames = 0, loads = 0;

    Game(Engine &e, RomKeeper &r, std::vector<uint8_t> &f) : engine(e), rom(r), flags(f) {}

    bool lights(uint32_t o) const
    {
        uint8_t v = rom.read(Target::ROM, o);
        if (any_change_lights && std::find(fragile.begin(), fragile.end(), o) != fragile.end())
            return v != rom.clean()[o];
        return v == kFuse;
    }
    void frame_once()
    {
        engine.on_frame(rom);
        if (halted)
            return;
        frame++;
        if (fuse && --fuse == 0)
            halted = true;
        for (auto [o, from] : runs)
            if (frame >= from)
            {
                flags[o] |= cf::Opcode | cf::Touched;
                if (!fuse && !halted && lights(o))
                    fuse = fuse_frames;
            }
        if (together && !fuse && !halted && frame >= runs[together->first] &&
            rom.read(Target::ROM, together->first) != rom.clean()[together->first] &&
            rom.read(Target::ROM, together->second) != rom.clean()[together->second])
            fuse = fuse_frames;
        if (crash_at && frame >= *crash_at)
            halted = true;
    }
    std::vector<uint8_t> save_state() override
    {
        return { uint8_t(frame), uint8_t(frame >> 8), uint8_t(fuse), uint8_t(halted) };
    }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        loads++;
        frame = s[0] | (s[1] << 8);
        fuse = s[2];
        halted = s[3];
        return true;
    }
    FrameSignals sample() override
    {
        FrameSignals s;
        s.cpu_halted = halted;
        s.opcode = 0xDB;
        return s;
    }
    void run_unseen_frame() override
    {
        unseen_frames++;
        frame_once();
    }
};

struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    RomBlame blame{ rom };
    Engine engine;
    std::vector<uint8_t> flags;
    Game game{ engine, rom, flags };
    FirstUseTest first;
    std::vector<uint8_t> checkpoint;
    EngineSnapshot engine_checkpoint;
    AheadHooks hooks;

    Fixture()
    {
        rom.set_clock([this] { return engine.current_frame(); });
        rom.game_loaded({});
        flags.assign(mem.size(Target::ROM), 0);
        blame.attach(flags.data(), flags.size());
        Recipe r;
        r.name = "test";
        r.seed = 1;
        engine.set_recipe(r);
        engine.set_active(true);
        first.set_config({ .frames = 30, .settle_frames = 5, .gap = 1, .batch = 16, .max_runs = 8, .tries = 3 });
        game.runs = { { 0x10, 0 } };
    }
    void change(uint32_t o, uint8_t v, uint32_t from)
    {
        rom.write(Target::ROM, o, v);
        game.runs[o] = from;
    }
    // One real frame, as the app does it: the checkpoint, the frame, the blame, the test.
    FirstUseResult step()
    {
        checkpoint = game.save_state();
        engine_checkpoint = engine.snapshot();
        game.frame_once();
        blame.collect(engine.current_frame());
        first.note_uses(blame, rom, engine.current_frame());
        return first.test(engine, rom, blame, game, flags, hooks, &checkpoint, &engine_checkpoint);
    }
    // Real frames until the game has run frame `n`, with what the last test said.
    FirstUseResult until(uint32_t n)
    {
        FirstUseResult last;
        while (game.frame < n && !game.halted)
            last = step();
        return last;
    }
    uint8_t at(uint32_t o) { return mem.of(Target::ROM)[o]; }
};

} // namespace

TEST_SUITE("test at first use")
{
    TEST_CASE("a changed byte that holds up is tested once, at its first use, and nothing else changes")
    {
        Fixture f;
        f.change(0x20, 0x42, 5);
        CHECK(f.until(4).kind == FirstUseResult::Nothing);
        CHECK(f.game.unseen_frames == 0);
        auto out = f.step(); // frame 5: the first use
        CHECK(out.kind == FirstUseResult::Kept);
        CHECK(out.tested == std::vector<uint32_t>{ 0x20 });
        CHECK(out.runs == 1);
        CHECK(out.summary.empty());
        CHECK(f.game.unseen_frames == 6); // used in its first frame, then held up for 5
        // As it was: the game after frame 5, the ROM, the blame, the map.
        CHECK(f.game.frame == 5);
        CHECK(f.at(0x20) == 0x42);
        CHECK(f.blame.watched() == 1);
        CHECK(f.blame.use(0x20)->frames_used == 1);
        CHECK((f.flags[0x20] & cf::Changed));
        CHECK(!(f.flags[0x20] & cf::Touched));
        for (int i = 0; i < 40; i++)
            CHECK(f.step().kind == FirstUseResult::Nothing); // never again
        CHECK(f.first.tests() == 1);
        CHECK(f.first.bytes_tested() == 1);
    }

    TEST_CASE("the runs replay the frame's own rules; the engine's log and count come back as they were")
    {
        Fixture f;
        Rule poke;
        poke.name = "poke";
        poke.range = { Target::WRAM, 0x100, 0x100 };
        poke.op = { .type = OpType::Add, .min = 1, .max = 1 };
        poke.when = { .type = TriggerType::EveryFrames, .every = 1 };
        Recipe r = f.engine.recipe();
        r.rules.push_back(poke);
        f.engine.set_recipe(r);
        f.engine.set_active(true);
        f.change(0x20, 0x42, 5);
        f.until(4);
        auto before_frame = f.engine.current_frame();
        f.step();
        auto log = f.engine.event_log();
        auto frame = f.engine.current_frame();
        CHECK(frame == before_frame + 1);
        REQUIRE(!log.empty());
        CHECK(log.back().frame == frame);
        // The test ran (the step above), and the log ends with the real frame's write, once.
        CHECK(f.first.tests() == 1);
        CHECK(std::count_if(log.begin(), log.end(), [&](const LoggedEvent &e) { return e.frame == frame; }) == 1);
    }

    TEST_CASE("a byte whose crash comes frames later is put back, and its frame plays again")
    {
        Fixture f;
        f.change(0x20, kFuse, 5);
        f.until(4);
        auto out = f.step();
        CHECK(out.kind == FirstUseResult::Fixed);
        CHECK(out.fixed == std::vector<uint32_t>{ 0x20 });
        CHECK(out.runs == 2); // with it, then the control run without it
        CHECK(out.frame == 5);
        CHECK(out.failure.find("CPU stopped") != std::string::npos);
        CHECK(out.failure.find("0.1 s in") != std::string::npos);
        CHECK(out.rom_action == "reverted 1 ROM byte");
        CHECK(out.summary.find("played the frame again") != std::string::npos);
        CHECK(f.at(0x20) == 0);         // the clean value, for good
        CHECK(f.blame.watched() == 0);
        CHECK(f.game.frame == 4);       // back at the start of frame 5
        CHECK(f.game.fuse == 0);
        f.until(40);
        CHECK(!f.game.halted);
        CHECK(f.first.fixes() == 1);

        auto e = journal_entry(out);
        CHECK(e.kind == JournalEntry::FirstUse);
        CHECK(e.rom_blamed == std::vector<uint32_t>{ 0x20 });
        CHECK(e.frames_lost == 1);
    }

    TEST_CASE("without the test the same byte stops the game")
    {
        Fixture f;
        f.first.set_config({ .frames = 0 });
        f.change(0x20, kFuse, 5);
        f.until(20);
        CHECK(f.game.halted);
    }

    TEST_CASE("a crash the run without the byte has too isn't the byte's fault")
    {
        Fixture f;
        f.change(0x20, kFuse, 5);
        f.until(4);
        f.game.crash_at = 7; // older damage
        auto out = f.step();
        CHECK(out.kind == FirstUseResult::Kept);
        CHECK(out.older_damage);
        CHECK(out.runs == 2);
        CHECK(f.at(0x20) == kFuse);
    }

    TEST_CASE("a landing that names the byte is proof enough: no control run")
    {
        Fixture f;
        f.change(0x20, kLands, 5);
        f.hooks.take_derail = [&]() -> std::optional<DerailWatch::Note> {
            if (f.at(0x20) == kLands && (f.flags[0x20] & cf::Touched))
                return DerailWatch::Note{ Landing::MidInstruction, 0x808000, { 0x20 } };
            return std::nullopt;
        };
        f.until(4);
        auto out = f.step();
        CHECK(out.kind == FirstUseResult::Fixed);
        CHECK(out.runs == 1);
        CHECK(out.fixed == std::vector<uint32_t>{ 0x20 });
        CHECK(out.failure.find("middle of an instruction") != std::string::npos);
    }

    TEST_CASE("a landing that names another byte is left to the instant revert")
    {
        Fixture f;
        f.change(0x20, kLands, 5);
        f.hooks.take_derail = [&]() -> std::optional<DerailWatch::Note> {
            if (f.at(0x20) == kLands && (f.flags[0x20] & cf::Touched))
                return DerailWatch::Note{ Landing::Brk, 0x808000, { 0x300 } };
            return std::nullopt;
        };
        f.until(4);
        auto out = f.step();
        CHECK(out.kind == FirstUseResult::Kept);
        CHECK(out.runs == 1);
        CHECK(f.at(0x20) == kLands);
    }

    TEST_CASE("a batch with one bad byte narrows down to it, and the rest stay")
    {
        Fixture f;
        for (uint32_t o = 0x20; o < 0x24; o++)
            f.change(o, o == 0x22 ? kFuse : 0x42, 5);
        f.until(4);
        auto out = f.step();
        CHECK(out.kind == FirstUseResult::Fixed);
        CHECK(out.tested.size() == 4);
        CHECK(out.fixed == std::vector<uint32_t>{ 0x22 });
        // All four, none (control), 0x20-21 alone (fine), 0x22 alone (crashes), the other three.
        CHECK(out.runs == 5);
        CHECK(f.at(0x22) == 0);
        CHECK(f.at(0x20) == 0x42);
        CHECK(f.at(0x21) == 0x42);
        CHECK(f.at(0x23) == 0x42);
        f.until(40);
        CHECK(!f.game.halted);
    }

    TEST_CASE("two bytes that only crash together are both fixed")
    {
        Fixture f;
        f.change(0x20, 0x42, 5);
        f.change(0x21, 0x43, 5);
        f.game.together = { 0x20, 0x21 };
        f.until(4);
        auto out = f.step();
        CHECK(out.kind == FirstUseResult::Fixed);
        CHECK(out.fixed.size() == 2);
        CHECK(f.at(0x20) == 0);
        CHECK(f.at(0x21) == 0);
        f.until(40);
        CHECK(!f.game.halted);
    }

    TEST_CASE("tests are spaced out; a byte that waited too long has had its test, live")
    {
        Fixture f;
        f.first.set_config({ .frames = 20, .settle_frames = 5, .gap = 30 });
        f.change(0x20, 0x42, 5);
        f.change(0x21, 0x43, 6);
        f.change(0x22, 0x44, 40);
        f.until(4);
        CHECK(f.step().kind == FirstUseResult::Kept); // frame 5
        CHECK(f.step().kind == FirstUseResult::Nothing); // frame 6: too soon
        CHECK(f.first.queued() == 1);
        f.until(34);
        CHECK(f.first.queued() == 1);
        CHECK(f.step().kind == FirstUseResult::Nothing); // frame 35: due, but 0x21 has run 29 frames
        CHECK(f.first.queued() == 0);
        f.until(39);
        auto out = f.step(); // frame 40
        CHECK(out.kind == FirstUseResult::Kept);
        CHECK(out.tested == std::vector<uint32_t>{ 0x22 });
        CHECK(f.first.tests() == 2);
    }

    TEST_CASE("bytes test before applying saw used are skipped")
    {
        Fixture f;
        f.change(0x20, kFuse, 5);
        f.until(3);
        f.first.vouch({ 0x20 }, f.engine.current_frame());
        f.until(4);
        CHECK(f.step().kind == FirstUseResult::Nothing);
        CHECK(f.first.queued() == 0);
    }

    TEST_CASE("nothing is tested without a checkpoint; the byte waits")
    {
        Fixture f;
        f.change(0x20, 0x42, 5);
        f.until(5);
        // until() tested it already; a fresh byte, and a frame with no checkpoint:
        f.change(0x21, 0x43, 6);
        f.game.frame_once();
        f.blame.collect(f.engine.current_frame());
        f.first.note_uses(f.blame, f.rom, f.engine.current_frame());
        CHECK(f.first.test(f.engine, f.rom, f.blame, f.game, f.flags, f.hooks, nullptr).kind ==
              FirstUseResult::Nothing);
        CHECK(f.first.queued() == 1);
        CHECK(f.step().kind == FirstUseResult::Kept);
    }

    TEST_CASE("re-roll policy: the new value is tested at its next use, and after 3 failures it's reverted")
    {
        Fixture f;
        f.blame.set_policy(RomPolicy::Reroll, 9);
        f.game.fragile = { 0x20 };
        f.game.any_change_lights = true; // every changed value crashes
        f.change(0x20, 0x42, 5);
        f.until(4);
        auto one = f.step();
        CHECK(one.kind == FirstUseResult::Fixed);
        CHECK(one.rom_action == "re-rolled 1 ROM byte");
        CHECK(f.at(0x20) != 0x42);
        CHECK(f.at(0x20) != 0);
        auto two = f.step(); // frame 5 again, with the new value: its first use
        CHECK(two.kind == FirstUseResult::Fixed);
        CHECK(two.rom_action == "re-rolled 1 ROM byte");
        auto three = f.step();
        CHECK(three.kind == FirstUseResult::Fixed);
        CHECK(three.rom_action == "reverted 1 ROM byte");
        CHECK(f.at(0x20) == 0);
        CHECK(f.blame.watched() == 0);
        f.until(40);
        CHECK(!f.game.halted);
    }

    TEST_CASE("forget empties everything")
    {
        Fixture f;
        f.first.set_config({ .frames = 20, .settle_frames = 5, .gap = 30 });
        f.change(0x20, 0x42, 5);
        f.change(0x21, 0x43, 6);
        f.until(6);
        CHECK(f.first.queued() == 1);
        f.first.forget();
        CHECK(f.first.queued() == 0);
    }
}
