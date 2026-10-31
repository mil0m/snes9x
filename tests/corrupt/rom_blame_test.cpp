#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/rom_blame.h"

#include <algorithm>

using namespace corrupt;
namespace cf = codeflag;

namespace
{
struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    RomBlame blame{ rom };
    std::vector<uint8_t> flags;
    uint64_t frame = 0;

    Fixture()
    {
        auto &bytes = mem.of(Target::ROM);
        for (size_t i = 0; i < bytes.size(); i++)
            bytes[i] = uint8_t(i * 7);
        rom.set_clock([this] { return frame; });
        rom.game_loaded({});
        flags.assign(bytes.size(), 0);
        blame.attach(flags.data(), flags.size());
    }
    uint8_t clean(uint32_t offset) const { return uint8_t(offset * 7); }
    uint8_t now(uint32_t offset) { return mem.of(Target::ROM)[offset]; }
    void change(uint32_t offset) { rom.write(Target::ROM, offset, uint8_t(clean(offset) ^ 0xFF)); }
    void touch(uint32_t offset) { flags[offset] |= cf::Opcode | cf::Touched; }
    // Runs frames up to and including `last`, touching `offsets` in each, collecting after each.
    void run_to(uint64_t last, std::initializer_list<uint32_t> offsets = {})
    {
        while (frame < last)
        {
            frame++;
            for (auto o : offsets)
                touch(o);
            blame.collect(frame);
        }
    }
};
} // namespace

TEST_SUITE("rom blame")
{
    TEST_CASE("only changed bytes the game used are suspects")
    {
        Fixture f;
        f.change(0x100);
        f.change(0x200);
        f.touch(0x300); // used, but clean: never a suspect
        f.run_to(5, { 0x100, 0x300 });
        CHECK(f.blame.watched() == 2);
        CHECK(f.blame.suspects(0, 10) == 1);
        auto *u = f.blame.use(0x100);
        REQUIRE(u);
        CHECK(u->first_used == 1);
        CHECK(u->last_used == 5);
        CHECK(u->frames_used == 5);
        REQUIRE(f.blame.use(0x200));
        CHECK(f.blame.use(0x200)->frames_used == 0);
        CHECK(!f.blame.use(0x300));
    }

    TEST_CASE("a use from before the change doesn't count, and collecting clears only the touched bit")
    {
        Fixture f;
        f.touch(0x100);
        f.change(0x100);
        f.run_to(1);
        CHECK(f.blame.use(0x100)->frames_used == 0);
        f.run_to(2, { 0x100 });
        CHECK(f.blame.use(0x100)->frames_used == 1);
        CHECK(f.flags[0x100] == cf::Opcode);
    }

    TEST_CASE("suspects are bytes used inside the window")
    {
        Fixture f;
        f.change(0x100);
        f.run_to(9);
        f.run_to(20, { 0x100 }); // used in frames 10-20
        f.run_to(40);
        CHECK(f.blame.suspects(5, 10) == 1);
        CHECK(f.blame.suspects(15, 30) == 1);
        CHECK(f.blame.suspects(20, 30) == 0); // used before the window only
        CHECK(f.blame.suspects(0, 9) == 0);   // only used after it
    }

    TEST_CASE("bytes back to their clean value are no longer watched")
    {
        Fixture f;
        f.change(0x100);
        CHECK(f.blame.watched() == 1);
        f.rom.write(Target::ROM, 0x100, f.clean(0x100));
        CHECK(f.blame.watched() == 0);
        f.change(0x100);
        f.change(0x101);
        f.rom.restore_clean();
        CHECK(f.blame.watched() == 0);
    }

    TEST_CASE("revert: newly reached code first, newest half, then all of it, then everything in the window")
    {
        Fixture f;
        constexpr uint32_t A = 0x10, B = 0x20, C = 0x30, D = 0x40;
        for (auto o : { A, B, C, D })
            f.change(o);
        f.run_to(49, { A }); // A has run all along
        f.run_to(50, { A, B });
        f.run_to(55, { A, C });
        f.run_to(58, { A, D });
        f.run_to(60, { A });

        auto r = f.blame.act(40, 60, 1);
        CHECK(r.offsets == std::vector<uint32_t>{ D, C }); // the newest half of B, C, D
        CHECK(r.text == "reverted 2 ROM bytes");
        CHECK(f.now(D) == f.clean(D));
        CHECK(f.now(C) == f.clean(C));
        CHECK(f.now(B) != f.clean(B));
        CHECK(f.blame.watched() == 2);

        r = f.blame.act(40, 60, 2);
        CHECK(r.offsets == std::vector<uint32_t>{ B });
        CHECK(r.text == "reverted 1 ROM byte");

        r = f.blame.act(40, 60, 3);
        CHECK(r.offsets == std::vector<uint32_t>{ A });
        CHECK(f.blame.watched() == 0);
        CHECK(f.blame.act(40, 60, 4).offsets.empty());
    }

    TEST_CASE("only_new leaves bytes that were running before the window alone")
    {
        Fixture f;
        f.change(0x10);
        f.change(0x20);
        f.run_to(30, { 0x10 });
        f.run_to(40, { 0x10, 0x20 });
        CHECK(f.blame.suspects(30, 40, true) == 1);
        CHECK(f.blame.suspects(30, 40, false) == 2);
        CHECK(f.blame.act(30, 40, 3, true).offsets == std::vector<uint32_t>{ 0x20 });
        CHECK(f.blame.suspects(30, 40, true) == 0);
        CHECK(f.blame.act(30, 40, 3, true).offsets.empty());
    }

    TEST_CASE("with nothing newly reached, the least proven half goes first")
    {
        Fixture f;
        f.change(0x10);
        f.change(0x20);
        f.change(0x30);
        f.run_to(10, { 0x10 });
        f.run_to(15, { 0x10, 0x20 });
        f.run_to(16, { 0x10, 0x20, 0x30 });
        f.run_to(30, { 0x10, 0x20, 0x30 });
        auto r = f.blame.act(20, 30, 1);
        CHECK(r.offsets == std::vector<uint32_t>{ 0x30, 0x20 }); // 15 and 20 frames used; 0x10 30
        r = f.blame.act(20, 30, 2);
        CHECK(r.offsets == std::vector<uint32_t>{ 0x10 });
    }

    TEST_CASE("a settled byte stays fixed through a deeper rollback")
    {
        Fixture f;
        f.change(0x100);                          // frame 0, like a classic run
        f.frame = 30;
        f.rom.write(Target::ROM, 0x100, 0x99);    // a live edit on top
        f.run_to(40, { 0x100 });
        f.blame.act(35, 40, 1);
        CHECK(f.now(0x100) == f.clean(0x100));
        f.rom.revert_after(10); // without settling, this would put frame 0's value back
        CHECK(f.now(0x100) == f.clean(0x100));
        CHECK(f.rom.changed() == 0);
    }

    TEST_CASE("re-roll: a fresh value, never the clean or the bad one, and the byte starts unproven")
    {
        Fixture f;
        f.blame.set_policy(RomPolicy::Reroll, 7);
        f.change(0x100);
        uint8_t bad = f.now(0x100);
        f.run_to(10, { 0x100 });
        auto r = f.blame.act(0, 10, 1);
        CHECK(r.offsets == std::vector<uint32_t>{ 0x100 });
        CHECK(r.text == "re-rolled 1 ROM byte");
        CHECK(f.now(0x100) != bad);
        CHECK(f.now(0x100) != f.clean(0x100));
        REQUIRE(f.blame.use(0x100));
        CHECK(f.blame.use(0x100)->frames_used == 0);
        f.rom.revert_after(5);
        CHECK(f.now(0x100) != bad); // settled too
    }

    TEST_CASE("a changed byte used for 15 s without a crash survived, once")
    {
        Fixture f;
        f.change(0x100);
        f.change(0x200); // never used: never a survivor
        f.run_to(10, { 0x100 }); // first used in frame 1
        f.run_to(RomBlame::kSurviveFrames);
        CHECK(f.blame.take_survivals().empty());
        f.run_to(1 + RomBlame::kSurviveFrames);
        CHECK(f.blame.take_survivals() == std::vector<uint32_t>{ 0x100 });
        f.run_to(2000, { 0x100 });
        CHECK(f.blame.take_survivals().empty());
    }

    TEST_CASE("without a code map nothing is ever used, and nothing is blamed")
    {
        Fixture f;
        f.blame.attach(nullptr, 0);
        f.change(0x100);
        f.run_to(10, { 0x100 });
        CHECK(f.blame.watched() == 1);
        CHECK(!f.blame.active());
        CHECK(f.blame.suspects(0, 10) == 0);
        CHECK(f.blame.act(0, 10, 3).offsets.empty());
        f.blame.attach(f.flags.data(), f.flags.size()); // attaching again starts counting
        CHECK(f.blame.active());
        f.run_to(11, { 0x100 });
        CHECK(f.blame.suspects(0, 11) == 1);
    }

    TEST_CASE("policy names")
    {
        CHECK(std::string(rom_policy_name(RomPolicy::Revert)) == "revert");
        CHECK(parse_rom_policy("reroll") == RomPolicy::Reroll);
        CHECK(!parse_rom_policy("skip"));
    }
}
