#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/rom_keeper.h"

using namespace corrupt;

namespace
{
struct Fixture
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    uint64_t frame = 0;

    Fixture()
    {
        auto &bytes = mem.of(Target::ROM);
        for (size_t i = 0; i < bytes.size(); i++)
            bytes[i] = uint8_t(i * 7);
        rom.set_clock([this] { return frame; });
        rom.game_loaded({ { Target::ROM, 0xFFB0, 0xFFFF } });
    }
    uint8_t clean(uint32_t offset) const { return uint8_t(offset * 7); }
};
} // namespace

TEST_SUITE("rom keeper")
{
    TEST_CASE("ROM writes land and are counted; other targets pass straight through")
    {
        Fixture f;
        f.rom.write(Target::ROM, 0x100, 0xAA);
        f.rom.write(Target::WRAM, 0x10, 0x55);
        CHECK(f.mem.of(Target::ROM)[0x100] == 0xAA);
        CHECK(f.rom.read(Target::ROM, 0x100) == 0xAA);
        CHECK(f.mem.of(Target::WRAM)[0x10] == 0x55);
        CHECK(f.rom.size(Target::ROM) == f.mem.size(Target::ROM));
        CHECK(f.rom.data(Target::ROM) == f.mem.data(Target::ROM));
        CHECK(f.rom.changed() == 1);
        f.rom.write(Target::ROM, 0x100, f.clean(0x100)); // back to clean by hand
        CHECK(f.rom.changed() == 0);
    }

    TEST_CASE("locked bytes never change, and the engine gets them as a mask")
    {
        Fixture f;
        f.rom.write(Target::ROM, 0xFFFC, 0x00);
        f.rom.write(Target::ROM, 0xFFB0, 0x00);
        CHECK(f.mem.of(Target::ROM)[0xFFFC] == f.clean(0xFFFC));
        CHECK(f.mem.of(Target::ROM)[0xFFB0] == f.clean(0xFFB0));
        CHECK(f.rom.changed() == 0);
        CHECK(f.rom.locked_mask().is_hot(Target::ROM, 0xFFC0));
        CHECK_FALSE(f.rom.locked_mask().is_hot(Target::ROM, 0xFFAF));
        CHECK_FALSE(f.rom.locked_mask().is_hot(Target::WRAM, 0xFFC0));
        f.rom.write(Target::ROM, 0xFFAF, 0x00); // just outside
        CHECK(f.mem.of(Target::ROM)[0xFFAF] == 0x00);
    }

    TEST_CASE("a rollback undoes the edits made after its frame, newest first")
    {
        Fixture f;
        f.frame = 10;
        f.rom.write(Target::ROM, 0x200, 0x01); // kept
        f.frame = 20;
        f.rom.write(Target::ROM, 0x300, 0x02);
        f.rom.write(Target::ROM, 0x200, 0x03);
        f.frame = 25;
        f.rom.write(Target::ROM, 0x300, 0x04); // the same byte twice after the rollback point
        f.rom.revert_after(15);
        CHECK(f.mem.of(Target::ROM)[0x200] == 0x01);
        CHECK(f.mem.of(Target::ROM)[0x300] == f.clean(0x300));
        CHECK(f.rom.changed() == 1);
        f.frame = 16;
        f.rom.write(Target::ROM, 0x400, 0x05); // the run goes on from there
        f.rom.revert_after(15);
        CHECK(f.mem.of(Target::ROM)[0x400] == f.clean(0x400));
        CHECK(f.mem.of(Target::ROM)[0x200] == 0x01);
    }

    TEST_CASE("edits at frame 0 (at_load) survive any rollback")
    {
        Fixture f;
        f.rom.write(Target::ROM, 0x500, 0x09);
        f.rom.revert_after(0);
        CHECK(f.mem.of(Target::ROM)[0x500] == 0x09);
    }

    TEST_CASE("restoring the clean ROM undoes everything")
    {
        Fixture f;
        for (uint32_t a = 0x1000; a < 0x1100; a++)
            f.rom.write(Target::ROM, a, uint8_t(f.clean(a) + 1));
        CHECK(f.rom.changed() == 0x100);
        f.rom.restore_clean();
        CHECK(f.rom.changed() == 0);
        CHECK(f.mem.of(Target::ROM)[0x1050] == f.clean(0x1050));
        f.rom.revert_after(0); // nothing left to revert
        CHECK(f.mem.of(Target::ROM)[0x1050] == f.clean(0x1050));
    }

    TEST_CASE("guard outcomes: a rollback reverts, giving up restores the clean ROM")
    {
        Fixture f;
        f.frame = 0;
        f.rom.write(Target::ROM, 0x600, 0x11);
        f.frame = 50;
        f.rom.write(Target::ROM, 0x700, 0x22);

        GuardOutcome nothing;
        f.rom.after_guard(nothing);
        CHECK(f.rom.changed() == 2);

        GuardOutcome back;
        back.kind = GuardOutcome::RolledBack;
        back.restored_frame = 40;
        f.rom.after_guard(back);
        CHECK(f.mem.of(Target::ROM)[0x700] == f.clean(0x700));
        CHECK(f.mem.of(Target::ROM)[0x600] == 0x11);

        GuardOutcome gave_up;
        gave_up.kind = GuardOutcome::GaveUp;
        f.rom.after_guard(gave_up);
        CHECK(f.rom.changed() == 0);
    }

    TEST_CASE("before a game is loaded, or after it closes, it only passes writes through")
    {
        FakeMemory mem;
        RomKeeper rom(mem);
        rom.write(Target::ROM, 0xFFFC, 0x12);
        CHECK(mem.of(Target::ROM)[0xFFFC] == 0x12);
        CHECK(rom.changed() == 0);
        rom.restore_clean(); // harmless
        CHECK(mem.of(Target::ROM)[0xFFFC] == 0x12);

        Fixture f;
        f.rom.game_closed();
        f.rom.write(Target::ROM, 0x10, 0x99);
        CHECK(f.rom.changed() == 0);
        CHECK(f.rom.locked_mask().empty());
    }

    TEST_CASE("a long run's journal stays bounded but recent edits still revert")
    {
        Fixture f;
        for (uint64_t i = 0; i < RomKeeper::kJournalLimit + 1000; i++)
        {
            f.frame = i + 1;
            f.rom.write(Target::ROM, uint32_t(i % 0x8000), uint8_t(i));
        }
        CHECK(f.rom.journal_size() <= RomKeeper::kJournalLimit);
        f.rom.revert_after(f.frame - 1); // the very last edit
        uint32_t last = uint32_t((f.frame - 1) % 0x8000);
        CHECK(f.mem.of(Target::ROM)[last] == uint8_t(f.frame - 1 - 0x8000));
    }
}
