#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/try_change.h"

#include <algorithm>

using namespace corrupt;
namespace cf = codeflag;

namespace
{

constexpr uint8_t kHalt = 0xDB;   // run, it stops the CPU
constexpr uint8_t kFreeze = 0xF0; // run, the game keeps going but the picture stops (a wait loop)

// A game whose picture is its frame counter plus the byte at 0x10 (so another value shows),
// running the ROM bytes in `runs` each frame after the engine (as from the frame hook).
struct Game : CoreAccess
{
    Engine &engine;
    RomKeeper &rom;
    std::vector<uint8_t> &flags;
    std::vector<uint32_t> runs = { 0x10 };
    uint32_t frame = 0;
    bool halted = false, frozen = false;
    bool still = false; // the clean game shows one picture (a menu nobody touches)
    std::optional<uint32_t> crash_at;
    int unseen_frames = 0, drawn = 0;
    bool drawing = false;

    Game(Engine &e, RomKeeper &r, std::vector<uint8_t> &f) : engine(e), rom(r), flags(f) {}

    void frame_once()
    {
        engine.on_frame(rom);
        if (halted)
            return;
        for (auto o : runs)
        {
            flags[o] |= cf::Opcode | cf::Touched;
            uint8_t b = rom.read(Target::ROM, o);
            halted |= b == kHalt;
            frozen |= b == kFreeze;
        }
        if (!frozen)
            frame++;
        if (crash_at && frame >= *crash_at)
            halted = true;
    }
    std::vector<uint8_t> save_state() override
    {
        return { uint8_t(frame), uint8_t(frame >> 8), uint8_t(halted), uint8_t(frozen) };
    }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        frame = s[0] | (s[1] << 8);
        halted = s[2];
        frozen = s[3];
        return true;
    }
    FrameSignals sample() override
    {
        FrameSignals s;
        s.cpu_halted = halted;
        s.picture_valid = drawing;
        s.picture_hash = still ? 7 : frame * 1000 + rom.read(Target::ROM, 0x10);
        return s;
    }
    void run_unseen_frame(bool draw) override
    {
        unseen_frames++;
        drawing = draw;
        drawn += draw;
        frame_once();
    }
    void end_unseen() override { drawing = false; }
};

struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    Engine engine;
    std::vector<uint8_t> flags;
    Game game{ engine, rom, flags };
    RunAhead runner;

    Fixture()
    {
        rom.set_clock([this] { return engine.current_frame(); });
        rom.game_loaded({});
        flags.assign(mem.size(Target::ROM), 0);
        mem.of(Target::ROM)[0x10] = 0x01;
        Recipe r;
        r.name = "test";
        engine.set_recipe(r);
        engine.set_active(true);
        for (int i = 0; i < 5; i++)
            game.frame_once();
    }
    Change change(uint8_t value, uint32_t at = 0x10)
    {
        Change c;
        c.at = at;
        c.value = { value };
        c.was = { mem.of(Target::ROM)[at] };
        return c;
    }
    TryResult go(const Change &c, bool use_known = true)
    {
        return try_change(runner, engine, rom, game, flags, {}, c, 120, use_known);
    }
};

std::vector<uint64_t> moving(size_t n, uint64_t from = 0)
{
    std::vector<uint64_t> v;
    for (size_t i = 0; i < n; i++)
        v.push_back(from + i);
    return v;
}

} // namespace

TEST_SUITE("try a change before it lands")
{
    TEST_CASE("a freeze is one picture for the last second while the run without it moves")
    {
        auto control = moving(300);
        auto run = moving(100);
        run.resize(300, 99); // stopped at frame 99
        auto at = froze_at(run, control);
        REQUIRE(at.has_value());
        CHECK(*at == 99);
        CHECK(!froze_at(moving(300), control));                   // still moving
        CHECK(!froze_at(run, std::vector<uint64_t>(300, 5)));     // without it, still too: no verdict
        CHECK(!control_moves(std::vector<uint64_t>(300, 5)));
        CHECK(control_moves(control));
        // Still for under a second at the end isn't a freeze yet.
        auto brief = moving(260);
        brief.resize(300, 259);
        CHECK(!froze_at(brief, control));
        CHECK(froze_at(brief, control, { .hold = 30, .min_moves = 3 }));
    }

    TEST_CASE("a harmless change keeps running, shows, and everything is put back")
    {
        Fixture f;
        uint32_t frame = f.game.frame;
        auto flags = f.flags;
        auto r = f.go(f.change(0x02));
        CHECK(r.kind == TryResult::Running);
        CHECK(r.differs);
        CHECK(!r.still_anyway);
        CHECK(r.text.find("Still running after 2.0 s") != std::string::npos);
        CHECK(r.text.find("differs") != std::string::npos);
        CHECK(f.game.unseen_frames == 240); // with and without
        CHECK(f.game.drawn == 240);         // drawn, for the pictures
        // As it was: the game, the ROM, the map, the engine's queue.
        CHECK(f.game.frame == frame);
        CHECK(f.mem.of(Target::ROM)[0x10] == 0x01);
        CHECK(f.rom.changed() == 0);
        CHECK(f.flags == flags);
        CHECK(f.engine.pending_flips().empty());
        CHECK(f.engine.current_frame() == 5);
    }

    TEST_CASE("a change that stops the picture is a freeze, with when")
    {
        Fixture f;
        auto r = f.go(f.change(kFreeze));
        CHECK(r.kind == TryResult::Froze);
        CHECK(r.at == 0); // the first frame: the byte runs at once
        CHECK(r.text.find("froze after 0.0 s") != std::string::npos);
        CHECK(r.text.find("without the change it kept moving") != std::string::npos);
        CHECK(!f.game.frozen);
    }

    TEST_CASE("a change that stops the CPU is a crash")
    {
        Fixture f;
        auto r = f.go(f.change(kHalt));
        CHECK(r.kind == TryResult::Crashed);
        CHECK(r.text.find("crashed") != std::string::npos);
        CHECK(r.text.find("CPU") != std::string::npos);
        CHECK(!f.game.halted);
    }

    TEST_CASE("code the game doesn't run says nothing yet")
    {
        Fixture f;
        f.mem.of(Target::ROM)[0x20] = 0x01;
        auto r = f.go(f.change(kHalt, 0x20));
        CHECK(r.kind == TryResult::Unused);
        CHECK(r.text.find("didn't run this code") != std::string::npos);
        // Without the tracer nobody knows what was used: it ran without trouble, that's all.
        CHECK(f.go(f.change(kHalt, 0x20), false).kind == TryResult::Running);
    }

    TEST_CASE("when the game goes wrong without the change too, it can't tell")
    {
        Fixture f;
        f.game.crash_at = f.game.frame + 30;
        auto r = f.go(f.change(0x02));
        CHECK(r.kind == TryResult::Unclear);
        CHECK(r.text.find("Without this change the game also goes wrong") != std::string::npos);
    }

    TEST_CASE("a still screen without the change: running, and it says a freeze can't be told")
    {
        Fixture f;
        f.game.still = true;
        auto r = f.go(f.change(kFreeze));
        CHECK(r.kind == TryResult::Running);
        CHECK(r.still_anyway);
        CHECK(r.text.find("a freeze can't be told") != std::string::npos);
    }

    TEST_CASE("only while corruption is on, and the queue is left as it was")
    {
        Fixture f;
        f.engine.flip_bytes({ 0x30, { 0x09 }, { 0x00 }, Engine::Flip::How::On });
        auto r = f.go(f.change(0x02));
        CHECK(r.kind == TryResult::Running);
        REQUIRE(f.engine.pending_flips().size() == 1);
        CHECK(f.engine.pending_flips()[0].at == 0x30);
        f.engine.set_active(false);
        auto off = f.go(f.change(0x02));
        CHECK(off.kind == TryResult::Failed);
        CHECK(off.text.find("corruption is on") != std::string::npos);
    }

    TEST_CASE("a change that's on now is tried against the clean bytes")
    {
        Fixture f;
        auto c = f.change(kFreeze);
        f.engine.flip_bytes({ c.at, c.value, c.was, Engine::Flip::How::On });
        f.game.frame_once(); // it's in now... and froze the real game; put the game back
        f.game.frozen = false;
        REQUIRE(f.mem.of(Target::ROM)[0x10] == kFreeze);
        auto r = f.go(c);
        CHECK(r.kind == TryResult::Froze); // without it (clean) it moves
        CHECK(f.mem.of(Target::ROM)[0x10] == kFreeze);
    }
}
