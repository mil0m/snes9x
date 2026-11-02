#include "doctest/doctest.h"

#include "corrupt/buttons.h"

#include <algorithm>

using namespace corrupt;

namespace
{

Rule hotkey_rule(const std::string &name, int key)
{
    Rule r;
    r.name = name;
    r.when.type = TriggerType::Hotkey;
    r.when.hotkey = key;
    return r;
}

ButtonState state(std::vector<uint32_t> off = {}, std::vector<uint32_t> changed = {})
{
    ButtonState s;
    s.function_name = [](uint32_t entry) { return entry == 0x8000 ? std::string("gravity") : std::string(); };
    s.switch_off = [off](const Switch &sw) { return std::find(off.begin(), off.end(), sw.entry) != off.end(); };
    s.change_on = [changed](const Change &c) {
        return std::find(changed.begin(), changed.end(), c.at) != changed.end();
    };
    return s;
}

} // namespace

TEST_SUITE("buttons")
{
    TEST_CASE("no slots in use: no buttons")
    {
        Recipe r;
        Rule timed;
        timed.name = "roulette"; // every N frames: not on a button
        r.rules.push_back(timed);
        Switch quiet;
        quiet.entry = 0x8000; // kept, but on no button
        r.switches.push_back(quiet);
        CHECK(recipe_buttons(r, state()).empty());
    }

    TEST_CASE("a rule is a plain press named after the rule")
    {
        Recipe r;
        r.rules.push_back(hotkey_rule("Burst", 2));
        auto b = recipe_buttons(r, state());
        REQUIRE(b.size() == 1);
        CHECK(b[0].key == 2);
        CHECK(b[0].label == "Burst");
        CHECK_FALSE(b[0].toggle);
        CHECK_FALSE(b[0].on);
    }

    TEST_CASE("a switch is a toggle named after its function, on while the function is off")
    {
        Recipe r;
        Switch sw;
        sw.entry = 0x8000;
        sw.hotkey = 1;
        r.switches.push_back(sw);
        Switch unnamed;
        unnamed.entry = 0x9123;
        unnamed.hotkey = 3;
        r.switches.push_back(unnamed);
        auto b = recipe_buttons(r, state({ 0x8000 }));
        REQUIRE(b.size() == 2);
        CHECK(b[0].label == "gravity off");
        CHECK(b[0].toggle);
        CHECK(b[0].on);
        CHECK(b[1].key == 3);
        CHECK(b[1].label == "0x009123 off");
        CHECK_FALSE(b[1].on);
    }

    TEST_CASE("a note names it before the function does")
    {
        Recipe r;
        Switch sw;
        sw.entry = 0x8000;
        sw.hotkey = 1;
        sw.note = "moon gravity";
        r.switches.push_back(sw);
        Change c;
        c.function = 0x8000;
        c.at = 0x8004;
        c.hotkey = 4;
        r.changes.push_back(c);
        auto b = recipe_buttons(r, state({}, { 0x8004 }));
        REQUIRE(b.size() == 2);
        CHECK(b[0].label == "moon gravity");
        CHECK(b[1].label == "gravity changed");
        CHECK(b[1].toggle);
        CHECK(b[1].on);
    }

    TEST_CASE("several things on one slot: the first one's name and how many more")
    {
        Recipe r;
        r.rules.push_back(hotkey_rule("Burst", 5));
        r.rules.push_back(hotkey_rule("Sparkle", 5));
        Switch sw;
        sw.entry = 0x8000;
        sw.hotkey = 5;
        r.switches.push_back(sw);
        auto b = recipe_buttons(r, state({ 0x8000 }));
        REQUIRE(b.size() == 1);
        CHECK(b[0].label == "Burst + 2 more");
        CHECK(b[0].details == "Burst\nSparkle\ngravity off");
        CHECK_FALSE(b[0].toggle); // a rule fires each press: not something that stays on
    }

    TEST_CASE("a toggle with several switches is on only when all of them are")
    {
        Recipe r;
        for (uint32_t e : { 0x8000u, 0x9000u })
        {
            Switch sw;
            sw.entry = e;
            sw.hotkey = 7;
            r.switches.push_back(sw);
        }
        CHECK_FALSE(recipe_buttons(r, state({ 0x8000 }))[0].on);
        CHECK(recipe_buttons(r, state({ 0x8000, 0x9000 }))[0].on);
    }

    TEST_CASE("slots outside 1-9 and disabled rules are left out; keys come in order")
    {
        Recipe r;
        r.rules.push_back(hotkey_rule("late", 9));
        r.rules.push_back(hotkey_rule("bad", 12));
        auto off = hotkey_rule("resting", 2);
        off.enabled = false;
        r.rules.push_back(off);
        r.rules.push_back(hotkey_rule("early", 1));
        auto b = recipe_buttons(r, state());
        REQUIRE(b.size() == 2);
        CHECK(b[0].key == 1);
        CHECK(b[1].key == 9);
    }
}

TEST_SUITE("what a recipe keeps")
{
    TEST_CASE("nothing kept: an empty list")
    {
        Recipe r;
        r.rules.push_back(hotkey_rule("Burst", 1)); // rules are listed elsewhere
        CHECK(recipe_kept(r, state()).empty());
    }

    TEST_CASE("switches, changes and protects in that order, then pins and paint")
    {
        Recipe r;
        Switch sw;
        sw.entry = 0x8000;
        sw.hotkey = 2;
        sw.start_off = true;
        sw.every = 600;
        sw.for_frames = 60;
        r.switches.push_back(sw);
        Change c;
        c.function = 0x9123;
        c.at = 0x9130;
        c.kind = ChangeKind::Branch;
        c.note = "never lands";
        c.start_on = false;
        r.changes.push_back(c);
        r.protects.push_back({ 0x8000, true });
        r.pins = { { 0x500, 0xEA, 0x20, "" }, { 0x501, 0xEA, 0x20, "" } };
        Patch p;
        p.target = Target::ROM;
        p.at = 0x1000;
        p.bytes = { 1, 2, 3 };
        r.patches.push_back(p);

        auto k = recipe_kept(r, state());
        REQUIRE(k.size() == 5);
        CHECK(k[0].kind == KeptItem::Kind::Switch);
        CHECK(k[0].at == 0x8000);
        CHECK(k[0].what == "gravity off");
        CHECK(k[0].details == "button 2 · starts off · off 1 s of every 10 s");
        CHECK(k[1].kind == KeptItem::Kind::Change);
        CHECK(k[1].at == 0x9130);
        CHECK(k[1].what == "never lands");
        CHECK(k[1].details == "a branch in 0x009123 · starts as it was");
        CHECK(k[2].kind == KeptItem::Kind::Protect);
        CHECK(k[2].what == "gravity protected");
        CHECK(k[2].details == "and what it calls");
        CHECK(k[3].kind == KeptItem::Kind::Pins);
        CHECK(k[3].what == "2 pinned bytes");
        CHECK(k[4].kind == KeptItem::Kind::Paint);
        CHECK(k[4].what == "3 painted bytes");
    }

    TEST_CASE("one pin, one painted byte")
    {
        Recipe r;
        r.pins = { { 0x500, 0xEA, 0x20, "" } };
        Patch p;
        p.target = Target::WRAM;
        p.at = 0x10;
        p.bytes = { 9 };
        r.patches.push_back(p);
        auto k = recipe_kept(r, state());
        REQUIRE(k.size() == 2);
        CHECK(k[0].what == "1 pinned byte");
        CHECK(k[1].what == "1 painted byte");
    }
}
