#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/instant_revert.h"
#include "corrupt/journal.h"

using namespace corrupt;
namespace cf = codeflag;

namespace
{

// A core whose whole state is one number: which frame the game is on.
struct TinyCore : CoreAccess
{
    uint8_t game_frame = 0;
    int saves = 0, loads = 0;
    bool fail_loads = false;
    std::vector<uint8_t> save_state() override
    {
        saves++;
        return { game_frame };
    }
    bool load_state(const std::vector<uint8_t> &s) override
    {
        if (fail_loads || s.size() != 1)
            return false;
        loads++;
        game_frame = s[0];
        return true;
    }
    FrameSignals sample() override { return {}; }
};

struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    RomBlame blame{ rom };
    std::vector<uint8_t> flags;
    TinyCore core;
    InstantRevert instant;
    uint64_t frame = 0;

    Fixture()
    {
        rom.set_clock([this] { return frame; });
        rom.game_loaded({});
        flags.assign(mem.size(Target::ROM), 0);
        blame.attach(flags.data(), flags.size());
    }
    void change(uint32_t o) { rom.write(Target::ROM, o, 0x5A); }
    // One frame: the game moves on, then between frames as the session does it.
    std::optional<InstantRevertResult> run(std::optional<DerailWatch::Note> note = std::nullopt, bool protect = true)
    {
        frame++;
        core.game_frame++;
        auto out = instant.after_frame(std::move(note), frame, core, blame);
        instant.keep(core, frame, protect);
        return out;
    }
    static DerailWatch::Note note(std::vector<uint32_t> culprits, Landing kind = Landing::MidInstruction)
    {
        return { kind, 0x80ABCD, std::move(culprits) };
    }
};

} // namespace

TEST_SUITE("instant revert")
{
    TEST_CASE("a landing replays the frame without the culprit")
    {
        Fixture f;
        f.change(0x100);
        f.change(0x200);
        f.run();
        f.run();
        CHECK(f.instant.has_checkpoint());
        CHECK(f.core.game_frame == 2);
        auto out = f.run(Fixture::note({ 0x100 }));
        REQUIRE(out);
        CHECK(f.core.loads == 1);
        CHECK(f.core.game_frame == 2); // frame 3 never happened; it runs again next
        CHECK(out->offsets == std::vector<uint32_t>{ 0x100 });
        CHECK(out->frame == 3);
        CHECK(f.mem.of(Target::ROM)[0x100] == 0);
        CHECK(f.mem.of(Target::ROM)[0x200] == 0x5A); // the innocent change stays
        CHECK(out->summary == "Caught at $80:ABCD (jumped into the middle of an instruction): reverted 1 ROM byte and "
                              "played the frame again");
        CHECK(f.instant.total() == 1);
        // The next checkpoint is the replayed start, so the engine's count goes on.
        f.run();
        CHECK(f.core.game_frame == 3);
        CHECK(f.instant.in_row() == 0);
    }

    TEST_CASE("nothing without a checkpoint from the frame just before")
    {
        Fixture f;
        f.change(0x100);
        CHECK(!f.run(Fixture::note({ 0x100 }))); // the very first frame: none yet
        f.run(std::nullopt, false);              // nothing to protect: none kept
        CHECK(!f.instant.has_checkpoint());
        CHECK(!f.run(Fixture::note({ 0x100 })));
        f.run();
        f.frame += 5; // the engine jumped (a rollback, a new run): the checkpoint is stale
        CHECK(!f.run(Fixture::note({ 0x100 })));
        CHECK(f.core.loads == 0);
        CHECK(f.mem.of(Target::ROM)[0x100] == 0x5A);
    }

    TEST_CASE("a frame whose count stood still (STP stops the frame hook) is still the one after the checkpoint")
    {
        Fixture f;
        f.change(0x100);
        f.run();
        f.core.game_frame++;
        CHECK(f.instant.after_frame(Fixture::note({ 0x100 }), f.frame, f.core, f.blame));
        CHECK(f.core.game_frame == 1);
    }

    TEST_CASE("culprits that aren't changed any more, or a state that won't load, change nothing")
    {
        Fixture f;
        f.change(0x100);
        f.run();
        CHECK(!f.run(Fixture::note({ 0x300 })));
        CHECK(f.core.loads == 0);
        f.core.fail_loads = true;
        CHECK(!f.run(Fixture::note({ 0x100 })));
        CHECK(f.mem.of(Target::ROM)[0x100] == 0x5A);
    }

    TEST_CASE("re-roll draws new values instead")
    {
        Fixture f;
        f.blame.set_policy(RomPolicy::Reroll, 3);
        f.change(0x100);
        f.run();
        auto out = f.run(Fixture::note({ 0x100 }));
        REQUIRE(out);
        CHECK(out->rom_action == "re-rolled 1 ROM byte");
        CHECK(f.mem.of(Target::ROM)[0x100] != 0x5A);
        CHECK(f.mem.of(Target::ROM)[0x100] != 0);
    }

    TEST_CASE("after kMaxInRow in a row it leaves the rest to the guard until a clean frame")
    {
        Fixture f;
        f.blame.set_policy(RomPolicy::Reroll, 3); // the culprit stays changed: it can land again
        f.change(0x100);
        f.run();
        for (unsigned i = 0; i < InstantRevert::kMaxInRow; i++)
            CHECK(f.run(Fixture::note({ 0x100 })));
        CHECK(!f.run(Fixture::note({ 0x100 })));
        f.run(); // clean
        CHECK(f.run(Fixture::note({ 0x100 })));
    }

    TEST_CASE("the journal line names the landing and the bytes")
    {
        Fixture f;
        f.change(0x100);
        f.run();
        auto out = f.run(Fixture::note({ 0x100 }, Landing::Brk));
        REQUIRE(out);
        auto e = journal_entry(*out);
        CHECK(e.kind == JournalEntry::InstantRevert);
        CHECK(e.signal == "brk");
        CHECK(e.text == "ran into empty ROM (BRK) at $80:ABCD");
        CHECK(e.rom_blamed == std::vector<uint32_t>{ 0x100 });
        CHECK(e.frames_lost == 1);
        auto line = journal_line(e, BackupClock::time_point{});
        CHECK(line.find("\"event\":\"instant_revert\"") != std::string::npos);
        CHECK(line.find("\"rom_bytes\":[\"0x100\"]") != std::string::npos);
    }

    TEST_CASE("switched off it never acts and keeps nothing")
    {
        Fixture f;
        f.instant.set_enabled(false);
        f.change(0x100);
        f.run();
        CHECK(!f.instant.has_checkpoint());
        CHECK(!f.run(Fixture::note({ 0x100 })));
        CHECK(f.core.saves == 0);
    }
}
