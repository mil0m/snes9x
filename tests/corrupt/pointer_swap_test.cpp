#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/crash_map.h"
#include "corrupt/engine.h"
#include "corrupt/pointer_swap.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"

using namespace corrupt;

namespace
{

// Site 0x50 dispatches through a table at 0x100: two entries used ($C0:8200, $C0:8210), one
// likely ($C0:8220), and a second table of its own at 0x400 (one used, $C0:8300). Site 0x60
// has its own table at 0x200 with one entry used.
std::vector<JumpTable> tables()
{
    JumpTable a, b, c;
    a.site = 0x50;
    a.entries = { { 0x100, 0xC08200, 5, true }, { 0x102, 0xC08210, 2, true }, { 0x104, 0xC08220, 0, false } };
    b.site = 0x50;
    b.entries = { { 0x400, 0xC08300, 1, true } };
    c.site = 0x60;
    c.entries = { { 0x200, 0xC09000, 1, true } };
    return { a, b, c };
}

void put(FakeMemory &mem, uint32_t offset, uint32_t target, int stride = 2)
{
    for (int i = 0; i < stride; i++)
        mem.of(Target::ROM)[offset + uint32_t(i)] = uint8_t(target >> (8 * i));
}

uint32_t entry(FakeMemory &mem, uint32_t offset)
{
    auto &rom = mem.of(Target::ROM);
    return 0xC00000 | rom[offset] | (rom[offset + 1] << 8);
}

std::vector<uint32_t> donor_targets(const SwapSlot &s)
{
    std::vector<uint32_t> out;
    for (auto &d : s.donors)
        out.push_back(d.target);
    return out;
}

} // namespace

TEST_SUITE("pointer swap")
{
    TEST_CASE("slots are the used entries in range; donors are the other targets of the same dispatcher")
    {
        auto slots = swap_slots(tables(), 0, 0xFFFF, false);
        REQUIRE(slots.size() == 3); // 0x100, 0x102, 0x400; the site 0x60 table has nothing to swap with
        CHECK(slots[0].offset == 0x100);
        CHECK(slots[0].target == 0xC08200);
        CHECK(donor_targets(slots[0]) == std::vector<uint32_t>{ 0xC08210, 0xC08300 });
        CHECK(slots[1].offset == 0x102);
        CHECK(donor_targets(slots[1]) == std::vector<uint32_t>{ 0xC08200, 0xC08300 });
        CHECK(slots[2].offset == 0x400);
        CHECK(donor_targets(slots[2]) == std::vector<uint32_t>{ 0xC08200, 0xC08210 });

        auto with_likely = swap_slots(tables(), 0, 0xFFFF, true);
        CHECK(donor_targets(with_likely[0]) == std::vector<uint32_t>{ 0xC08210, 0xC08220, 0xC08300 });
        CHECK(with_likely.size() == 3); // likely entries are donors, never changed themselves

        auto in_range = swap_slots(tables(), 0x101, 0x3FF, false);
        REQUIRE(in_range.size() == 1); // 0x100 starts outside, 0x400 is past it
        CHECK(in_range[0].offset == 0x102);
    }

    TEST_CASE("a 2-byte entry only takes targets in its own bank; the same target twice is one donor")
    {
        auto t = tables();
        t[1].entries[0].target = 0xC18300; // another bank
        t[1].entries.push_back({ 0x402, 0xC08210, 1, true }); // a target the first table has too
        auto slots = swap_slots(t, 0, 0x3FF, false);
        REQUIRE(slots.size() == 2);
        CHECK(donor_targets(slots[0]) == std::vector<uint32_t>{ 0xC08210 });

        t[0].stride = t[1].stride = 3; // 3-byte entries carry their bank
        t[0].entries = { { 0x100, 0xC08200, 5, true } };
        t[1].entries = { { 0x400, 0xC18300, 1, true } };
        auto long_slots = swap_slots(t, 0, 0x3FF, false);
        REQUIRE(long_slots.size() == 1);
        CHECK(donor_targets(long_slots[0]) == std::vector<uint32_t>{ 0xC18300 });
    }

    TEST_CASE("applying writes another target into the entry, and both ways with mutual")
    {
        FakeMemory mem;
        put(mem, 0x100, 0xC08200);
        put(mem, 0x102, 0xC08210);
        put(mem, 0x400, 0xC08300);
        auto slots = swap_slots(tables(), 0, 0xFFFF, false);
        Rng rng(42);
        auto events = apply_pointer_swap(slots, { 0 }, false, mem, rng);
        uint32_t now = entry(mem, 0x100);
        CHECK((now == 0xC08210 || now == 0xC08300));
        CHECK_FALSE(events.empty());
        for (auto &e : events)
        {
            CHECK(e.target == Target::ROM);
            CHECK(e.offset >= 0x100);
            CHECK(e.offset <= 0x101);
            CHECK(e.old_value != e.new_value);
        }
        CHECK(entry(mem, 0x102) == 0xC08210); // the donor keeps its own

        FakeMemory both;
        put(both, 0x100, 0xC08200);
        put(both, 0x102, 0xC08210);
        put(both, 0x400, 0xC08300);
        Rng same(42);
        apply_pointer_swap(slots, { 0 }, true, both, same);
        uint32_t donor = entry(both, 0x100) == 0xC08210 ? 0x102 : 0x400;
        CHECK(entry(both, donor) == 0xC08200); // the donor got the entry's target
    }

    TEST_CASE("an entry with a locked byte is left whole")
    {
        FakeMemory mem;
        put(mem, 0x100, 0xC08200);
        auto slots = swap_slots(tables(), 0, 0xFFFF, false);
        HotMask locked;
        locked.add_block(Target::ROM, 0x101); // the block 0x100-0x10F
        Rng rng(1);
        auto events = apply_pointer_swap(slots, { 0, 1 }, false, mem, rng, &locked);
        CHECK(events.empty());
        CHECK(entry(mem, 0x100) == 0xC08200);
    }

    TEST_CASE("the engine swaps at load from the tables it's given, and a run without tables does nothing")
    {
        FakeMemory mem;
        put(mem, 0x100, 0xC08200);
        put(mem, 0x102, 0xC08210);
        put(mem, 0x400, 0xC08300);
        Engine engine;
        bool known = false;
        engine.set_jump_tables([&] { return known ? tables() : std::vector<JumpTable>{}; });
        Rule r;
        r.name = "pointers";
        r.range = { Target::ROM, 0, 0xFFFF };
        r.op.type = OpType::PointerSwap;
        r.when.type = TriggerType::AtLoad;
        r.count = 2;
        Recipe recipe;
        recipe.seed = 3;
        recipe.rules = { r };
        engine.set_recipe(recipe);
        engine.set_active(true);
        CHECK(engine.fire_at_load(mem) == 0); // nothing recorded yet
        known = true;
        engine.set_active(false);
        engine.set_active(true);
        CHECK(engine.fire_at_load(mem) > 0);
        CHECK(engine.aimed_count(0) == 3);
        int changed = (entry(mem, 0x100) != 0xC08200) + (entry(mem, 0x102) != 0xC08210) + (entry(mem, 0x400) != 0xC08300);
        CHECK(changed == 2);
        for (uint32_t o : { 0x100u, 0x102u, 0x400u }) // every entry still goes to a real routine
        {
            auto t = entry(mem, o);
            CHECK((t == 0xC08200 || t == 0xC08210 || t == 0xC08300));
        }
    }

    TEST_CASE("the recipe keeps reach and mutual; it's described in words; it's for the ROM only")
    {
        std::string text = R"({"format": 1, "name": "p", "rules": [{"name": "swap", "target": "rom", "range": ["0x0", "0xFFFFF"],
            "op": {"type": "pointer_swap", "reach": "likely", "mutual": true}, "when": {"at_load": true}, "count": 3}]})";
        auto parsed = parse_recipe(text, {});
        std::string why = parsed.errors.empty() ? std::string() : parsed.errors.front();
        CAPTURE(why);
        REQUIRE(parsed.recipe);
        auto &op = parsed.recipe->rules[0].op;
        CHECK(op.type == OpType::PointerSwap);
        CHECK(op.likely);
        CHECK(op.mutual);
        auto again = parse_recipe(write_recipe(*parsed.recipe), {});
        REQUIRE(again.recipe);
        CHECK(again.recipe->rules[0].op.likely);
        CHECK(again.recipe->rules[0].op.mutual);
        auto words = describe_rule(parsed.recipe->rules[0]);
        CHECK(words.find("swap pointers") != std::string::npos);
        CHECK(words.find("both ways") != std::string::npos);
        CHECK(words.find("unused entries too") != std::string::npos);

        auto bad = parse_recipe(R"({"format": 1, "name": "p", "rules": [{"name": "s", "target": "rom", "range": ["0x0", "0xF"],
            "op": {"type": "pointer_swap", "reach": "everywhere"}}]})", {});
        CHECK_FALSE(bad.recipe);

        FakeMemory mem;
        Rule wram;
        wram.range = { Target::WRAM, 0, 0xFF };
        wram.op.type = OpType::PointerSwap;
        CHECK(validate_rule(wram, mem));
    }
}
