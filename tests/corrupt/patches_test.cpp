#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/engine.h"
#include "corrupt/patches.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rom_keeper.h"
#include "corrupt/run_control.h"

using namespace corrupt;

TEST_SUITE("patches")
{
    TEST_CASE("painted bytes merge into runs; painting a byte again replaces it")
    {
        std::vector<Patch> p;
        add_patch_bytes(p, Target::ROM, { { 0x10, 1 }, { 0x11, 2 }, { 0x13, 4 } });
        add_patch_bytes(p, Target::ROM, { { 0x12, 3 }, { 0x10, 9 } });
        add_patch_bytes(p, Target::WRAM, { { 0x5, 7 } });
        REQUIRE(p.size() == 2);
        CHECK(p[0].target == Target::WRAM); // sorted by target, then offset
        CHECK(p[1].target == Target::ROM);
        CHECK(p[1].at == 0x10);
        CHECK(p[1].bytes == std::vector<uint8_t>{ 9, 2, 3, 4 });
        CHECK(patched_bytes(p) == 5);
        remove_patch_bytes(p, Target::ROM, { 0x11, 0x12 });
        REQUIRE(p.size() == 3);
        CHECK(p[1].bytes == std::vector<uint8_t>{ 9 });
        CHECK(p[2].at == 0x13);
    }

    TEST_CASE("patches round-trip through JSON as runs of hex")
    {
        Recipe r;
        r.name = "painted";
        add_patch_bytes(r.patches, Target::ROM, { { 0x1000, 0xEA }, { 0x1001, 0xEA }, { 0x2000, 0x00 } });
        auto text = write_recipe(r);
        CHECK(text.find(R"("bytes": "EAEA")") != std::string::npos);
        auto back = parse_recipe(text, {});
        REQUIRE(back.recipe);
        CHECK(back.recipe->patches.size() == 2);
        CHECK(write_recipe(*back.recipe) == text);

        CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[],"patches":[{"target":"rom","at":"0x10","bytes":"E"}]})", {}).recipe);
        CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[],"patches":[{"target":"rom","at":"0x10","bytes":"ZZ"}]})", {}).recipe);
        CHECK_FALSE(parse_recipe(R"({"format":1,"rules":[],"patches":[{"target":"oam","at":"0x21F","bytes":"0000"}]})", {}).recipe);
    }

    TEST_CASE("a run applies them at its start; ROM patches make it a classic start")
    {
        FakeMemory mem;
        RomKeeper rom(mem);
        rom.game_loaded({ { Target::ROM, 0xFFB0, 0xFFFF } });
        Recipe r;
        add_patch_bytes(r.patches, Target::ROM, { { 0x100, 0xAA }, { 0xFFFC, 0x00 } }); // the second is locked
        add_patch_bytes(r.patches, Target::WRAM, { { 0x200, 0xBB } });

        struct Core : CoreAccess
        {
            int resets = 0;
            std::vector<uint8_t> save_state() override { return { 1 }; }
            bool load_state(const std::vector<uint8_t> &) override { return true; }
            FrameSignals sample() override { return {}; }
            void reset_console() override { resets++; }
        } core;

        Engine e;
        e.set_locked(rom.locked_mask());
        e.hold_starts(true);
        e.set_recipe(r);
        e.set_active(true);
        RunControl control;
        control.between_frames(e, core, &rom);
        CHECK(core.resets == 1);
        CHECK(mem.of(Target::ROM)[0x100] == 0xAA);
        CHECK(mem.of(Target::ROM)[0xFFFC] == 0x00); // untouched: locked (it was 0 in the fake anyway)
        CHECK(mem.of(Target::WRAM)[0x200] == 0xBB);
        CHECK(rom.changed() == 1);
        CHECK(e.event_log().empty()); // patches aren't rule writes

        rom.revert_after(0);
        CHECK(mem.of(Target::ROM)[0x100] == 0xAA); // a start edit: rollbacks don't undo it
    }

    TEST_CASE("painting while running adds patches without restarting the run")
    {
        Engine e;
        Recipe r;
        e.set_recipe(r);
        e.set_active(true);
        FakeMemory mem;
        for (int i = 0; i < 5; i++)
            e.on_frame(mem);
        e.add_patches(Target::ROM, { { 0x40, 1 } });
        CHECK(e.current_frame() == 5);
        CHECK(e.recipe().patches.size() == 1);
        e.clear_patches(Target::ROM);
        CHECK(e.recipe().patches.empty());
    }

    TEST_CASE("a start edit isn't undone by a rollback")
    {
        FakeMemory mem;
        RomKeeper rom(mem);
        uint64_t frame = 100;
        rom.set_clock([&] { return frame; });
        rom.game_loaded({});
        rom.write(Target::ROM, 0x10, 5);        // live, frame 100
        rom.write_at_start(Target::ROM, 0x20, 6); // painted
        frame = 120;
        rom.write(Target::ROM, 0x30, 7);
        rom.revert_after(50);
        CHECK(mem.of(Target::ROM)[0x10] == 0);
        CHECK(mem.of(Target::ROM)[0x30] == 0);
        CHECK(mem.of(Target::ROM)[0x20] == 6);
        CHECK(rom.changed() == 1);
        rom.restore_clean();
        CHECK(mem.of(Target::ROM)[0x20] == 0);
    }
}
