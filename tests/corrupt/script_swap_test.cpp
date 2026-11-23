#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/crash_map.h"
#include "corrupt/engine.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule.h"
#include "corrupt/rule_text.h"
#include "corrupt/script_swap.h"

using namespace corrupt;

namespace
{

// Two engines. Events (dispatched from 0x0700): commands 0x35 and 0x41 are 2 bytes, 0x37 and 0x48
// 3, 0x49 1, 0xB0 is a branch, 0x6B (6 bytes) is the only one of its length. Actions (0x0800):
// 0xCC and 0xCD 1 byte.
std::vector<ScriptEngine> engines()
{
    ScriptEngine ev;
    ev.site = 0x0700;
    ev.through_pointer = true;
    auto cmd = [&](ScriptEngine &e, uint8_t v, int len, bool branchy = false) {
        e.commands[v] = { v, 0xC09000u + v, 10, len, 9, 0, branchy };
    };
    cmd(ev, 0x35, 2);
    cmd(ev, 0x41, 2);
    cmd(ev, 0x37, 3);
    cmd(ev, 0x48, 3);
    cmd(ev, 0x49, 1);
    cmd(ev, 0xB0, 2, true);
    cmd(ev, 0x6B, 6);
    ev.places = { { 0x1000, 0x35, 4, true },  { 0x1002, 0x37, 4, true },  { 0x1005, 0x49, 4, true },
                  { 0x1006, 0xB0, 4, false }, { 0x1008, 0x6B, 4, true },  { 0x100E, 0x41, 4, false },
                  { 0x2000, 0x48, 4, true } };
    ScriptEngine act;
    act.site = 0x0800;
    cmd(act, 0xCC, 1);
    cmd(act, 0xCD, 1);
    act.places = { { 0x3000, 0xCC, 4, true }, { 0x3001, 0xCD, 4, true } };
    return { ev, act };
}

void lay(FakeMemory &mem)
{
    for (auto &e : engines())
        for (auto &p : e.places)
            mem.of(Target::ROM)[p.offset] = p.value;
}

} // namespace

TEST_SUITE("script ops (#44)")
{
    TEST_CASE("slots: steady places of usable commands with another of the same length; nudge needs arguments")
    {
        auto es = engines();
        auto swaps = script_slots(es, 0, 0xFFFFF, std::nullopt, false);
        // 0x35 (2: 0x41), 0x37 (3: 0x48), 0x48 (3: 0x37), 0xCC, 0xCD. Not 0x49 (no other 1-byte
        // event command), 0xB0 (a branch), 0x6B (alone at 6), 0x41 at 0x100E (not steady).
        std::vector<uint32_t> at;
        for (auto &s : swaps)
            at.push_back(s.offset);
        CHECK(at == std::vector<uint32_t>{ 0x1000, 0x1002, 0x2000, 0x3000, 0x3001 });
        CHECK(swaps[0].others == std::vector<uint8_t>{ 0x41 });
        CHECK(swaps[1].others == std::vector<uint8_t>{ 0x48 });
        CHECK(swaps[3].others == std::vector<uint8_t>{ 0xCD }); // actions swap among their own

        auto nudges = script_slots(es, 0, 0xFFFFF, std::nullopt, true);
        at.clear();
        for (auto &s : nudges)
            at.push_back(s.offset);
        CHECK(at == std::vector<uint32_t>{ 0x1000, 0x1002, 0x1008, 0x2000 }); // 0x6B has arguments

        // One engine only, and the range must hold the whole command.
        CHECK(script_slots(es, 0, 0xFFFFF, 0x0800, false).size() == 2);
        CHECK(script_slots(es, 0x1000, 0x1003, std::nullopt, false).size() == 1); // 0x37 at 0x1002 ends at 0x1004
    }

    TEST_CASE("a swap writes another command of the same length; a nudge one argument, never by 0")
    {
        FakeMemory mem;
        lay(mem);
        auto slots = script_slots(engines(), 0, 0xFFFFF, std::nullopt, false);
        Rng rng(4);
        auto events = apply_script_swap(slots, { 0, 1, 3 }, mem, rng);
        REQUIRE(events.size() == 3);
        CHECK(mem.of(Target::ROM)[0x1000] == 0x41);
        CHECK(mem.of(Target::ROM)[0x1002] == 0x48);
        CHECK(mem.of(Target::ROM)[0x3000] == 0xCD);
        CHECK(mem.of(Target::ROM)[0x1001] == 0); // arguments untouched

        auto nudges = script_slots(engines(), 0, 0xFFFFF, std::nullopt, true);
        for (int seed = 0; seed < 50; seed++)
        {
            FakeMemory m;
            lay(m);
            Rng r{ uint64_t(seed) };
            auto ev = apply_script_nudge(nudges, { 2 }, -2, 2, m, r); // 0x6B at 0x1008: arguments 0x1009-0x100D
            REQUIRE(ev.size() == 1);
            CHECK(ev[0].offset >= 0x1009);
            CHECK(ev[0].offset <= 0x100D);
            int delta = int8_t(uint8_t(ev[0].new_value - ev[0].old_value));
            CHECK(delta != 0);
            CHECK(delta >= -2);
            CHECK(delta <= 2);
            CHECK(m.of(Target::ROM)[0x1008] == 0x6B); // the command stays
        }
    }

    TEST_CASE("a locked byte is left alone")
    {
        FakeMemory mem;
        lay(mem);
        HotMask locked;
        locked.add(Target::ROM, 0x1000);
        auto slots = script_slots(engines(), 0, 0xFFFFF, std::nullopt, false);
        Rng rng(1);
        auto events = apply_script_swap(slots, { 0, 1 }, mem, rng, &locked);
        REQUIRE(events.size() == 1);
        CHECK(events[0].offset == 0x1002);
    }

    TEST_CASE("the engine swaps from the engines it's given, and a run without any does nothing")
    {
        FakeMemory mem;
        lay(mem);
        Engine engine;
        bool known = false;
        engine.set_script_engines([&] { return known ? engines() : std::vector<ScriptEngine>{}; });
        Rule r;
        r.name = "script";
        r.range = { Target::ROM, 0, 0xFFFF };
        r.op.type = OpType::ScriptSwap;
        r.when.type = TriggerType::AtLoad;
        r.count = 2;
        Recipe recipe;
        recipe.seed = 3;
        recipe.rules = { r };
        engine.set_recipe(recipe);
        engine.set_active(true);
        CHECK(engine.fire_at_load(mem) == 0);
        known = true;
        engine.set_active(false);
        engine.set_active(true);
        CHECK(engine.fire_at_load(mem) > 0);
        CHECK(engine.aimed_count(0) == 5);
        int changed = 0;
        for (auto &e : engines())
            for (auto &p : e.places)
                changed += mem.of(Target::ROM)[p.offset] != p.value;
        CHECK(changed == 2);
    }

    TEST_CASE("the recipe keeps the op, its engine and a nudge's range; words; ROM only")
    {
        std::string text = R"({"format": 1, "name": "s", "rules": [
            {"name": "swap", "target": "rom", "range": ["0x0", "0xFFFFF"], "op": {"type": "script_swap", "engine": "0x009A8E"},
             "when": {"every_frames": 60}, "count": 2},
            {"name": "nudge", "target": "rom", "range": ["0x0", "0xFFFFF"], "op": {"type": "script_nudge", "min": -3, "max": 3},
             "when": {"every_frames": 60}, "count": 1}]})";
        auto parsed = parse_recipe(text, {});
        std::string why = parsed.errors.empty() ? std::string() : parsed.errors.front();
        CAPTURE(why);
        REQUIRE(parsed.recipe);
        auto &swap = parsed.recipe->rules[0].op;
        CHECK(swap.type == OpType::ScriptSwap);
        REQUIRE(swap.engine);
        CHECK(*swap.engine == 0x009A8E);
        auto &nudge = parsed.recipe->rules[1].op;
        CHECK(nudge.type == OpType::ScriptNudge);
        CHECK(nudge.min == -3);
        CHECK(nudge.max == 3);
        CHECK_FALSE(nudge.engine);
        auto again = parse_recipe(write_recipe(*parsed.recipe), {});
        REQUIRE(again.recipe);
        CHECK(again.recipe->rules[0].op.engine == swap.engine);
        CHECK(again.recipe->rules[1].op.max == 3);
        CHECK(describe_rule(parsed.recipe->rules[0]).find("swap script commands (engine at 0x009A8E)") != std::string::npos);
        CHECK(describe_rule(parsed.recipe->rules[1]).find("nudge script arguments -3..3") != std::string::npos);
        CHECK(describe_strength(parsed.recipe->rules[0], 1.0).find("swaps 2 script commands") != std::string::npos);

        // A nudge with no default range still nudges.
        auto bare = parse_recipe(R"({"format": 1, "name": "s", "rules": [{"name": "n", "target": "rom", "range": ["0x0", "0xF"],
            "op": {"type": "script_nudge"}, "when": {"every_frames": 60}}]})", {});
        CAPTURE((bare.errors.empty() ? std::string() : bare.errors.front()));
        REQUIRE(bare.recipe);
        CHECK(bare.recipe->rules[0].op.min == -1);
        CHECK(bare.recipe->rules[0].op.max == 1);

        CHECK_FALSE(parse_recipe(R"({"format": 1, "name": "s", "rules": [{"name": "s", "target": "rom", "range": ["0x0", "0xF"],
            "op": {"type": "script_swap", "engine": "events"}}]})", {}).recipe);

        FakeMemory mem;
        Rule wram;
        wram.range = { Target::WRAM, 0, 0xFF };
        wram.op.type = OpType::ScriptSwap;
        CHECK(validate_rule(wram, mem));
        Rule zero;
        zero.range = { Target::ROM, 0, 0xFF };
        zero.op.type = OpType::ScriptNudge;
        zero.op.min = zero.op.max = 0;
        CHECK(validate_rule(zero, mem));
    }

    TEST_CASE("a gentler nudge still nudges")
    {
        Op op;
        op.type = OpType::ScriptNudge;
        op.min = -2;
        op.max = 2;
        auto half = scaled_op(op, 0.1);
        CHECK((half.min != 0 || half.max != 0));
    }
}

TEST_CASE("aimed ROM rules past the end of a small ROM are trimmed to it; others aren't")
{
    std::vector<Rule> rules(4);
    for (auto &r : rules)
        r.range = { Target::ROM, 0, 0x7FFFFF };
    rules[0].op.type = OpType::ScriptSwap;
    rules[1].op.type = OpType::Kill;
    rules[2].op.type = OpType::PointerSwap;
    rules[3].op.type = OpType::Random; // plain bytes: a range past the end stays an error
    CHECK(fit_aimed_ranges(rules, 0x80000) == 3);
    for (int i = 0; i < 3; i++)
        CHECK(rules[size_t(i)].range.last == 0x7FFFF);
    CHECK(rules[3].range.last == 0x7FFFFF);
    CHECK(fit_aimed_ranges(rules, 0x80000) == 0); // already fitting
}
