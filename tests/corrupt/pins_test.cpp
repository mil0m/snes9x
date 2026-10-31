#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/engine.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rom_blame.h"
#include "corrupt/survivors.h"

using namespace corrupt;
namespace cf = codeflag;

namespace
{
struct Rom
{
    FakeMemory mem;
    RomKeeper rom{ mem };
    RomBlame blame{ rom };
    std::vector<uint8_t> flags;
    uint64_t frame = 0;

    Rom()
    {
        auto &bytes = mem.of(Target::ROM);
        for (size_t i = 0; i < bytes.size(); i++)
            bytes[i] = uint8_t(i * 7);
        rom.set_clock([this] { return frame; });
        rom.game_loaded({ { Target::ROM, 0xFFB0, 0xFFFF } });
        flags.assign(bytes.size(), 0);
        blame.attach(flags.data(), flags.size());
    }
    uint8_t clean(uint32_t o) const { return uint8_t(o * 7); }
    uint8_t now(uint32_t o) { return mem.of(Target::ROM)[o]; }
    void run_to(uint64_t last, std::initializer_list<uint32_t> used = {})
    {
        while (frame < last)
        {
            frame++;
            for (auto o : used)
                flags[o] |= cf::Touched;
            blame.collect(frame);
        }
    }
};

Pin pin(uint32_t offset, uint8_t value, uint8_t was, std::string note = {})
{
    return { offset, value, was, std::move(note) };
}
} // namespace

TEST_SUITE("pins")
{
    TEST_CASE("pins round-trip through recipes, and bad ones are errors")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[],"pins":[
            {"rom":"0x21A3F","value":"0xF0","was":"0xD0","note":"doors stay shut"},
            {"rom":"0x100","value":1,"was":2}]})",
                              {});
        REQUIRE_MESSAGE(r.recipe, (r.errors.empty() ? "" : r.errors[0]));
        REQUIRE(r.recipe->pins.size() == 2);
        CHECK(r.recipe->pins[0].offset == 0x21A3F);
        CHECK(r.recipe->pins[0].value == 0xF0);
        CHECK(r.recipe->pins[0].was == 0xD0);
        CHECK(r.recipe->pins[0].note == "doors stay shut");
        auto text = write_recipe(*r.recipe);
        CHECK(text.find("\"rom\": \"0x21A3F\"") != std::string::npos);
        auto again = parse_recipe(text, {});
        REQUIRE(again.recipe);
        CHECK(again.recipe->pins.size() == 2);
        CHECK(again.recipe->pins[1].note.empty());
        CHECK(again.recipe->pins[0].note == "doors stay shut");
        CHECK(write_recipe(*parse_recipe(R"({"format":1,"rules":[]})", {}).recipe).find("pins") == std::string::npos);

        CHECK(!parse_recipe(R"({"format":1,"rules":[],"pins":[{"rom":"0x1","value":"0x1"}]})", {}).recipe); // no was
        CHECK(!parse_recipe(R"({"format":1,"rules":[],"pins":[{"rom":"0x1","value":256,"was":0}]})", {}).recipe);
        CHECK(!parse_recipe(R"({"format":1,"rules":[],"pins":[{"wram":"0x1","value":1,"was":0}]})", {}).recipe);
    }

    TEST_CASE("pins apply as the run starts, only onto the ROM version they were made for")
    {
        Rom r;
        Engine engine;
        Recipe recipe;
        recipe.pins = { pin(0x10, 0xEA, r.clean(0x10)), pin(0x20, 0xEA, uint8_t(r.clean(0x20) + 1)),
                        pin(0xFFFC, 0x00, r.clean(0xFFFC)) }; // the last one is locked
        engine.set_recipe(recipe);
        engine.set_locked(r.rom.locked_mask());
        CHECK(engine.has_pins());
        auto applied = engine.apply_pins(r.rom);
        CHECK(applied.written == 1);
        CHECK(applied.mismatched == 1);
        CHECK(r.now(0x10) == 0xEA);
        CHECK(r.now(0x20) == r.clean(0x20));
        CHECK(r.now(0xFFFC) == r.clean(0xFFFC));
        r.rom.revert_after(0); // a rollback can't undo a pin
        CHECK(r.now(0x10) == 0xEA);
    }

    TEST_CASE("pinning replaces a pin on the same byte; unpinning removes it")
    {
        Engine engine;
        engine.set_recipe({});
        engine.add_pins({ pin(0x10, 1, 0), pin(0x20, 2, 0) });
        engine.add_pins({ pin(0x10, 3, 0, "again") });
        REQUIRE(engine.recipe().pins.size() == 2);
        CHECK(engine.recipe().pins[0].offset == 0x10);
        CHECK(engine.recipe().pins[0].value == 3);
        CHECK(engine.recipe().pins[0].note == "again");
        engine.remove_pins({ 0x10 });
        REQUIRE(engine.recipe().pins.size() == 1);
        CHECK(engine.recipe().pins[0].offset == 0x20);
    }

    TEST_CASE("the blame never watches a pinned byte")
    {
        Rom r;
        r.rom.write(Target::ROM, 0x10, 0x99);
        r.rom.write(Target::ROM, 0x20, 0x99);
        r.blame.set_pinned({ 0x10 });
        CHECK(r.blame.watched() == 1);
        r.run_to(10, { 0x10, 0x20 });
        CHECK(r.blame.act(0, 10, 3).offsets == std::vector<uint32_t>{ 0x20 });
        CHECK(r.now(0x10) == 0x99);
        r.rom.write(Target::ROM, 0x10, 0x98); // changed again later: still pinned, still not watched
        CHECK(r.blame.watched() == 0);
    }

    TEST_CASE("survivors: used often enough, 15 s without a crash, newest first, with the instruction before and after")
    {
        Rom r;
        // 0x100: BNE +4 (D0 04), run; becomes BEQ. 0x200: a data byte. 0x300: barely used.
        auto &bytes = r.mem.of(Target::ROM);
        bytes[0x100] = 0xD0;
        bytes[0x101] = 0x04;
        r.rom.game_loaded({}); // the clean copy with the program in it
        r.blame.attach(r.flags.data(), r.flags.size());
        r.flags[0x100] = cf::Opcode;
        r.flags[0x101] = cf::Operand;
        r.flags[0x200] = cf::Data;
        r.flags[0x300] = cf::Data;
        r.rom.write(Target::ROM, 0x100, 0xF0);
        r.rom.write(Target::ROM, 0x200, 0x11);
        r.rom.write(Target::ROM, 0x300, 0x22);
        r.run_to(50, { 0x100 });
        r.run_to(150, { 0x100, 0x200 });
        r.run_to(155, { 0x100, 0x200, 0x300 });
        r.run_to(1100, { 0x100, 0x200 });

        RomAddresses where;
        where.chunks.assign(r.flags.size() / RomAddresses::kChunk, RomAddresses::kNone);
        where.chunks[0] = 0xC00000;
        auto list = find_survivors(r.blame, r.rom.clean(), bytes, r.flags, where);
        REQUIRE(list.size() == 2); // 0x300 was used in 5 frames only
        CHECK(list[0].offset == 0x200); // first used later: newest first
        CHECK(list[0].before.empty());  // data
        CHECK(list[0].was == r.clean(0x200));
        CHECK(list[0].now == 0x11);
        CHECK(list[1].offset == 0x100);
        CHECK(list[1].address == 0xC00100);
        CHECK(list[1].was == 0xD0);
        CHECK(list[1].now == 0xF0);
        CHECK(list[1].before == "BNE $0106");
        CHECK(list[1].after == "BEQ $0106");
        CHECK(list[1].frames_used == 1100);
        CHECK((list[1].flags & cf::Opcode));
        CHECK(find_survivors(r.blame, r.rom.clean(), bytes, r.flags, where, 2000).empty());
    }
}
