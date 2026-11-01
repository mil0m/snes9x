#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/proven_blame.h"

#include <map>

using namespace corrupt;
namespace cf = codeflag;

namespace
{

constexpr uint8_t kFuse = 0xDB; // run once, it stops the CPU `fuse_frames` later

// A game whose state is a frame counter, a fuse and a stopped CPU (the ROM, as in the real
// core, isn't in a saved state). Each frame the engine runs first, then the game runs the
// ROM bytes that are due: `every` from their frame on, `once` only in that frame.
struct Game : CoreAccess
{
    Engine &engine;
    RomKeeper &rom;
    std::vector<uint8_t> &flags;
    std::map<uint32_t, uint32_t> every, once;
    std::map<uint32_t, uint32_t> hidden; // run once at that frame without the tracer seeing it
    std::map<uint32_t, uint32_t> later;  // run once at that frame, unless a fuse is already lit
    uint32_t fuse_frames = 200;
    uint32_t frame = 0, fuse = 0;
    bool halted = false;
    std::optional<uint32_t> crash_at;      // older damage: stops then whatever the ROM holds
    std::optional<uint32_t> real_crash_at; // ...but only when played for real (needs input)
    bool unseen = false;
    int unseen_frames = 0;

    Game(Engine &e, RomKeeper &r, std::vector<uint8_t> &f) : engine(e), rom(r), flags(f) {}

    void run(uint32_t o)
    {
        flags[o] |= cf::Opcode | cf::Touched;
        if (!fuse && rom.read(Target::ROM, o) == kFuse)
            fuse = fuse_frames;
    }
    void frame_once()
    {
        engine.on_frame(rom);
        if (halted)
            return;
        frame++;
        if (fuse && --fuse == 0)
            halted = true;
        for (auto [o, from] : every)
            if (frame >= from)
                run(o);
        for (auto [o, at] : once)
            if (frame == at)
                run(o);
        for (auto [o, at] : later)
            if (frame == at && !fuse)
                run(o);
        for (auto [o, at] : hidden)
            if (frame == at && !fuse && rom.read(Target::ROM, o) == kFuse)
                fuse = fuse_frames;
        if ((crash_at && frame >= *crash_at) || (!unseen && real_crash_at && frame >= *real_crash_at))
            halted = true;
    }
    std::vector<uint8_t> save_state() override
    {
        return { uint8_t(frame), uint8_t(frame >> 8), uint8_t(fuse), uint8_t(fuse >> 8), uint8_t(halted) };
    }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        frame = s[0] | (s[1] << 8);
        fuse = s[2] | (s[3] << 8);
        halted = s[4];
        return true;
    }
    FrameSignals sample() override
    {
        FrameSignals s;
        s.cpu_halted = halted;
        s.opcode = 0xDB;
        return s;
    }
    void begin_unseen() override { unseen = true; }
    void run_unseen_frame() override
    {
        unseen_frames++;
        frame_once();
    }
    void end_unseen() override { unseen = false; }
};

struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    RomBlame blame{ rom };
    Engine engine;
    std::vector<uint8_t> flags;
    Game game{ engine, rom, flags };
    CrashGuard guard;
    ProvenBlame prover{ blame, rom };
    std::vector<GuardOutcome> outcomes;

    explicit Fixture(bool prove = true)
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
        guard.set_policy(PolicyKind::Skip);
        guard.set_byte_blame(&blame);
        prover.set_map(flags);
        prover.set_detector(guard.detector_config());
        if (prove)
        {
            guard.set_prover(&prover);
            guard.set_prove_first_crash(true);
        }
    }
    void change(uint32_t o, uint8_t v) { rom.write(Target::ROM, o, v); }
    // Real frames, as the app runs them.
    void run(int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            game.frame_once();
            blame.collect(engine.current_frame());
            auto out = guard.between_frames(engine, game);
            rom.after_guard(out);
            if (out.kind != GuardOutcome::Nothing)
                outcomes.push_back(out);
        }
    }
    uint8_t at(uint32_t o) { return mem.of(Target::ROM)[o]; }
};

// The culprit runs once at frame 100 and stops the CPU 200 frames later; two harmless
// changes run every frame near the crash, where the guard's guess looks.
void plant(Fixture &f)
{
    f.change(0x20, kFuse);
    f.game.once[0x20] = 100;
    f.change(0x30, 0x42);
    f.change(0x31, 0x43);
    f.game.every[0x30] = 250;
    f.game.every[0x31] = 250;
}

} // namespace

TEST_SUITE("proven blame")
{
    TEST_CASE("guessing: the culprit ran before the window, so the guard rolls back more than once")
    {
        Fixture f(false);
        plant(f);
        f.run(1500);
        CHECK(f.outcomes.size() >= 2);
        CHECK(!f.outcomes[0].proven);
    }

    TEST_CASE("proven: one rollback, to a start before the culprit ran; only it is fixed")
    {
        Fixture f;
        plant(f);
        f.run(1500);
        REQUIRE(f.outcomes.size() == 1);
        auto &out = f.outcomes[0];
        CAPTURE(out.summary);
        CHECK(out.kind == GuardOutcome::RolledBack);
        CHECK(out.proven);
        CHECK(out.rom_blamed == std::vector<uint32_t>{ 0x20 });
        CHECK(out.restored_frame <= 100);
        CHECK(out.suspects.empty()); // no rule blamed
        CHECK(out.summary.find("found by replaying it") != std::string::npos);
        // From 240: reproduce, not those two, not every changed byte either (the fuse is
        // lit by then); from 0: all clean, 0x20 alone.
        CHECK(out.proof_runs == 5);
        CHECK(f.at(0x20) == 0);
        CHECK(f.at(0x30) == 0x42);
        CHECK(f.at(0x31) == 0x43);
        CHECK(!f.game.halted);
        CHECK(f.prover.proved() == 1);
    }

    TEST_CASE("by default only a crash that comes back is replayed: the first is a guess")
    {
        Fixture f;
        f.guard.set_prove_first_crash(false);
        plant(f);
        f.run(1500);
        REQUIRE(f.outcomes.size() == 2);
        CHECK(!f.outcomes[0].proven); // guessed: the two that ran near the crash
        CHECK(f.outcomes[1].proven);  // the same crash again: replayed, the fuse found
        CHECK(f.outcomes[1].rom_blamed == std::vector<uint32_t>{ 0x20 });
        CHECK(!f.game.halted);
    }

    TEST_CASE("a crash that needs the player (not in a replay) falls back to guessing")
    {
        Fixture f;
        f.change(0x30, 0x42);
        f.game.every[0x30] = 50;
        f.game.real_crash_at = 300;
        f.run(310);
        REQUIRE(!f.outcomes.empty());
        CHECK(!f.outcomes[0].proven);
        CHECK(f.prover.tried() == 1);
        CHECK(f.prover.proved() == 0);
    }

    TEST_CASE("a crash the changed bytes don't explain falls back to guessing")
    {
        Fixture f;
        f.change(0x30, 0x42);
        f.game.every[0x30] = 50;
        f.game.crash_at = 300; // happens with the ROM clean too
        f.run(310);
        REQUIRE(!f.outcomes.empty());
        CHECK(!f.outcomes[0].proven);
        CHECK(f.prover.proved() == 0);
    }

    TEST_CASE("narrowed to the one bad byte of several; the rest stay")
    {
        Fixture f;
        for (uint32_t o = 0x20; o < 0x28; o++)
        {
            f.change(o, o == 0x25 ? kFuse : 0x42);
            f.game.once[o] = 100;
        }
        f.run(1500);
        REQUIRE(f.outcomes.size() == 1);
        CHECK(f.outcomes[0].proven);
        CHECK(f.outcomes[0].rom_blamed == std::vector<uint32_t>{ 0x25 });
        CHECK(f.rom.changed() == 7);
        CHECK(!f.game.halted);
    }

    // A byte that would crash the game only if it got further (it never runs for real: the
    // real culprit's fuse is lit first) sorts before the real one. Narrowed by "this half
    // crashes on its own", the proof fixed it and the crash came back (#57's finding).
    TEST_CASE("narrowed by what prevents the crash, not by what could cause one")
    {
        Fixture f;
        f.change(0x30, kFuse);
        f.game.once[0x30] = 100; // the real culprit
        f.change(0x20, kFuse);
        f.game.later[0x20] = 120; // only reached when 0x30 is clean
        f.change(0x40, 0x42);
        f.game.every[0x40] = 50; // harmless, used all along
        f.run(1500);
        REQUIRE(f.outcomes.size() == 1);
        CAPTURE(f.outcomes[0].summary);
        CHECK(f.outcomes[0].proven);
        // Cleaning 0x30 alone would let 0x20 crash the game later: both go, the harmless one stays.
        CHECK(f.outcomes[0].rom_blamed == std::vector<uint32_t>{ 0x20, 0x30 });
        CHECK(f.at(0x40) == 0x42);
        CHECK(!f.game.halted);
    }

    TEST_CASE("out of budget while narrowing: all the proven bytes are fixed, still in one rollback")
    {
        Fixture f;
        f.prover.set_config({ .max_runs = 3 });
        for (uint32_t o = 0x20; o < 0x28; o++)
        {
            f.change(o, o == 0x25 ? kFuse : 0x42);
            f.game.once[o] = 100;
        }
        f.run(1500);
        REQUIRE(f.outcomes.size() == 1);
        CHECK(f.outcomes[0].proven);
        CHECK(f.outcomes[0].rom_blamed.size() > 1);
        CHECK(f.outcomes[0].proof_runs <= 3);
        CHECK(!f.game.halted);
    }

    TEST_CASE("a culprit the tracer never saw used is found among all the changed bytes")
    {
        Fixture f;
        f.change(0x20, kFuse);
        f.game.hidden[0x20] = 100;
        for (uint32_t o = 0x30; o < 0x34; o++)
        {
            f.change(o, 0x42);
            f.game.every[o] = 50;
        }
        f.change(0x40, 0x42); // changed, never used
        f.run(1500);
        REQUIRE(f.outcomes.size() == 1);
        CHECK(f.outcomes[0].proven);
        CHECK(f.outcomes[0].rom_blamed == std::vector<uint32_t>{ 0x20 });
        CHECK(f.prover.unseen_culprits == 1);
        CHECK(f.rom.changed() == 5);
        CHECK(!f.game.halted);
    }

    TEST_CASE("everything the replays touch comes back as it was")
    {
        Fixture f;
        plant(f);
        f.run(299); // just before the crash
        auto flags = f.flags;
        uint64_t frame = f.engine.current_frame();
        std::vector<ProofStart> starts; // a start the replays can't prove anything from
        auto state = f.game.save_state();
        auto snap = f.engine.snapshot();
        starts.push_back({ frame, &state, &snap });
        CrashVerdict v{ CrashSignal::CpuStopped, frame, frame, false, "test" };
        CHECK(!f.prover.prove(f.engine, f.game, v, starts));
        CHECK(f.flags == flags);
        CHECK(f.engine.current_frame() == frame);
        CHECK(f.game.frame == 299);
        CHECK(f.at(0x20) == kFuse);
    }
}
