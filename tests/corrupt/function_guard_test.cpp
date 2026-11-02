#include "doctest/doctest.h"

#include "corrupt/engine.h"
#include "corrupt/function_guard.h"
#include "corrupt/recipe_json.h"
#include "fake_memory.h"

using namespace corrupt;

namespace
{

FunctionBody body(uint32_t entry, std::vector<std::pair<uint32_t, uint8_t>> code)
{
    FunctionBody b;
    b.entry = entry;
    for (auto [at, len] : code)
    {
        b.code.push_back({ at, len, false });
        b.bytes += len;
    }
    return b;
}

std::vector<uint32_t> marked(const HotMask &m)
{
    std::vector<uint32_t> out;
    auto &v = m.hot[size_t(Target::ROM)];
    for (size_t i = 0; i < v.size(); i++)
        if (v[i])
            out.push_back(uint32_t(i));
    return out;
}

Rule rom_rule(OpType type, uint32_t first, uint32_t last, uint32_t count)
{
    Rule r;
    r.range = { Target::ROM, first, last };
    r.op.type = type;
    r.op.min = 0;
    r.op.max = 255;
    r.op.value = 0x77;
    r.when.type = TriggerType::EveryFrames;
    r.when.every = 1;
    r.count = count;
    return r;
}

} // namespace

TEST_SUITE("function guards")
{
    TEST_CASE("in play: switches, protects and window flips, each once, deep if any says so")
    {
        Recipe r;
        r.switches.push_back({ .entry = 0x300, .ret = 0x60, .was = 0xA5 });
        r.protects.push_back({ 0x100, false });
        r.protects.push_back({ 0x300, true });
        auto in = functions_in_play(r, { 0x200, 0x300 });
        REQUIRE(in.size() == 3);
        CHECK(in[0].entry == 0x100);
        CHECK_FALSE(in[0].deep);
        CHECK(in[1].entry == 0x200);
        CHECK(in[2].entry == 0x300);
        CHECK(in[2].deep);
    }

    TEST_CASE("the mask is the bodies; deep adds what they call, transitively")
    {
        FunctionBodies bodies;
        bodies[0x100] = body(0x100, { { 0x100, 3 }, { 0x103, 1 } }); // JSR $0200 at 0x100
        bodies[0x200] = body(0x200, { { 0x200, 3 }, { 0x203, 1 } }); // JSR $0300 at 0x200
        bodies[0x300] = body(0x300, { { 0x300, 1 } });
        CallLog calls;
        calls.note(0x100, callop::Jsr, 0x200);
        calls.note(0x200, callop::Jsr, 0x300);
        calls.note(0x900, callop::Jsr, 0x100); // a caller outside: not followed

        auto shallow = guard_mask({ { 0x100, false } }, bodies, &calls);
        CHECK(marked(shallow) == std::vector<uint32_t>{ 0x100, 0x101, 0x102, 0x103 });
        auto deep = guard_mask({ { 0x100, true } }, bodies, &calls);
        CHECK(marked(deep).size() == 9);
        CHECK(deep.is_hot(Target::ROM, 0x300));
        auto capped = guard_mask({ { 0x100, true } }, bodies, &calls, 1);
        CHECK(marked(capped).size() == 8);
        CHECK_FALSE(capped.is_hot(Target::ROM, 0x300));
        // A function with no body known guards nothing.
        CHECK(guard_mask({ { 0x500, false } }, bodies, &calls).empty());
    }

    TEST_CASE("going up cleans what others changed, never pins or switches")
    {
        Recipe r;
        r.pins.push_back({ 0x101, 0x11, 0x01, "" });
        r.switches.push_back({ .entry = 0x100, .ret = 0x60, .was = 0x00 });
        HotMask g;
        for (uint32_t o = 0x100; o < 0x104; o++)
            g.add(Target::ROM, o);
        std::vector<uint8_t> clean{ 0, 1, 2, 3 }, now{ 0x60, 0x11, 0x99, 3 };
        clean.insert(clean.begin(), 0x100, 0);
        now.insert(now.begin(), 0x100, 0);
        auto fix = guard_cleanup(g, own_bytes(r), clean, now);
        CHECK(fix == std::vector<std::pair<uint32_t, uint8_t>>{ { 0x102, 2 } });
    }

    TEST_CASE("rules skip guarded bytes; switches and pins still land")
    {
        for (auto type : { OpType::Random, OpType::Fill, OpType::Bitflip })
        {
            FakeMemory mem;
            mem.of(Target::ROM).assign(0x400, 0);
            Engine e;
            Recipe r;
            r.rules.push_back(rom_rule(type, 0x100, 0x1FF, 256));
            r.switches.push_back({ .entry = 0x140, .ret = 0x60, .was = 0x00, .hotkey = 1 });
            e.set_recipe(r);
            HotMask g;
            for (uint32_t o = 0x140; o < 0x160; o++)
                g.add(Target::ROM, o);
            e.set_guarded(g);
            e.set_active(true);
            e.press_hotkey(1);
            e.on_frame(mem);
            CAPTURE(op_name(type));
            CHECK(mem.read(Target::ROM, 0x140) == 0x60); // the switch went off
            for (uint32_t o = 0x141; o < 0x160; o++)
                CHECK(mem.read(Target::ROM, o) == 0);
            for (auto &ev : e.event_log())
                CHECK_FALSE(g.is_hot(Target::ROM, ev.write.offset));
            size_t outside = 0;
            for (uint32_t o = 0x100; o < 0x140; o++)
                outside += mem.read(Target::ROM, o) != 0;
            CHECK(outside > 0);
        }
    }

    TEST_CASE("patches skip guarded bytes; pins don't")
    {
        FakeMemory mem;
        mem.of(Target::ROM).assign(0x400, 0);
        Engine e;
        Recipe r;
        r.patches.push_back({ Target::ROM, 0x0FE, { 1, 2, 3, 4 } });
        r.pins.push_back({ 0x101, 0x55, 0x00, "" });
        e.set_recipe(r);
        HotMask g;
        g.add(Target::ROM, 0x100);
        g.add(Target::ROM, 0x101);
        e.set_guarded(g);
        CHECK(e.apply_patches(mem) == 2);
        CHECK(mem.read(Target::ROM, 0x0FE) == 1);
        CHECK(mem.read(Target::ROM, 0x100) == 0);
        CHECK(e.apply_pins(mem).written == 1);
        CHECK(mem.read(Target::ROM, 0x101) == 0x55);
    }

    TEST_CASE("protect in the recipe: JSON round trip and errors")
    {
        Recipe r;
        r.name = "p";
        r.protects.push_back({ 0x9000, false });
        r.protects.push_back({ 0xA000, true });
        auto text = write_recipe(r);
        CHECK(text.find("\"protect\"") != std::string::npos);
        auto back = parse_recipe(text, {});
        REQUIRE(back.errors.empty());
        REQUIRE(back.recipe->protects.size() == 2);
        CHECK(back.recipe->protects[1].entry == 0xA000);
        CHECK(back.recipe->protects[1].deep);

        auto bad = parse_recipe(R"({"format": 1, "name": "x", "rules": [], "protect": [{"deep": 1}]})", {});
        REQUIRE_FALSE(bad.errors.empty());
        CHECK(bad.errors[0].find("protect[0]") != std::string::npos);
    }

    TEST_CASE("engine: protects added and removed without a restart")
    {
        Engine e;
        e.set_recipe({});
        e.add_protect({ 0x200, false });
        e.add_protect({ 0x100, true });
        e.add_protect({ 0x200, true });
        REQUIRE(e.recipe().protects.size() == 2);
        CHECK(e.recipe().protects[0].entry == 0x100);
        CHECK(e.recipe().protects[1].deep);
        e.remove_protect(0x100);
        CHECK(e.recipe().protects.size() == 1);
    }
}

TEST_SUITE("function guards")
{
    TEST_CASE("overlay marks: bodies, guarded over them, off entries over those; hover finds the function")
    {
        std::vector<ShownFunction> fs(2);
        fs[0].entry = 0x10;
        fs[0].runs = { { 0x10, 0x17 } };
        fs[0].bytes = 8;
        fs[0].off = true;
        fs[1].entry = 0x14; // a shared tail, smaller
        fs[1].runs = { { 0x14, 0x17 } };
        fs[1].bytes = 4;
        HotMask g;
        g.add(Target::ROM, 0x16);
        auto m = function_marks(fs, g, 0x20);
        CHECK(m[0x0F] == 0);
        CHECK(m[0x10] == funcmark::Off);
        CHECK(m[0x11] == funcmark::Body);
        CHECK(m[0x16] == funcmark::Guarded);
        CHECK(function_at(fs, 0x11) == &fs[0]);
        CHECK(function_at(fs, 0x15) == &fs[1]); // the smaller one
        fs[0].guarded = true;
        CHECK(function_at(fs, 0x15) == &fs[0]); // a guarded one first
        CHECK(function_at(fs, 0x18) == nullptr);
    }
}
