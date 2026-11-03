#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/engine.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"

#include <set>

using namespace corrupt;

namespace
{

constexpr uint32_t kNearEntry = 0x200; // a near function: its first byte is $E6 (INC dp)
constexpr uint32_t kLongEntry = 0x300; // a long function
constexpr uint8_t kClean = 0xE6;

Engine running(Recipe recipe = {})
{
    Engine engine;
    engine.set_recipe(std::move(recipe));
    engine.set_active(true);
    return engine;
}

FakeMemory rom_with_functions()
{
    FakeMemory mem;
    mem.of(Target::ROM).assign(0x10000, 0xEA);
    for (uint32_t e = 0x100; e < 0x10000; e += 0x100)
        mem.of(Target::ROM)[e] = kClean;
    return mem;
}

Switch near_switch(int hotkey = 0)
{
    Switch s;
    s.entry = kNearEntry;
    s.ret = kRts;
    s.was = kClean;
    s.hotkey = hotkey;
    return s;
}

} // namespace

TEST_SUITE("kill switches")
{
    TEST_CASE("a queued flip says what the entry will be after the next frame; two toggles cancel")
    {
        auto mem = rom_with_functions();
        auto engine = running();
        CHECK_FALSE(engine.queued_state(kNearEntry, false).has_value()); // nothing queued
        engine.flip_function(kNearEntry, kRts, kClean);
        CHECK(engine.queued_state(kNearEntry, false) == true); // off at the next frame
        CHECK_FALSE(engine.queued_state(kLongEntry, false).has_value());
        engine.flip_function(kNearEntry, kRts, kClean);
        CHECK(engine.queued_state(kNearEntry, false) == false); // ticked and unticked while paused
        engine.flip_bytes({ kNearEntry, { kRts }, { kClean }, Engine::Flip::How::On });
        CHECK(engine.queued_state(kNearEntry, false) == true);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kRts);
        CHECK_FALSE(engine.queued_state(kNearEntry, true).has_value()); // done: the ROM says it now
    }

    TEST_CASE("a flip puts the return at the entry, the next puts the clean byte back, never as a rule's write")
    {
        auto mem = rom_with_functions();
        auto engine = running();
        engine.flip_function(kNearEntry, kRts, kClean);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kRts);
        CHECK(engine.event_log().empty());
        engine.on_frame(mem); // a flip happens once
        CHECK(mem.of(Target::ROM)[kNearEntry] == kRts);
        engine.flip_function(kNearEntry, kRts, kClean);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
        CHECK(engine.event_log().empty());
    }

    TEST_CASE("a recipe's switch flips on its own hotkey only")
    {
        auto mem = rom_with_functions();
        Recipe r;
        r.switches = { near_switch(3) };
        auto engine = running(r);
        engine.press_hotkey(2);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
        engine.press_hotkey(3);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kRts);
        CHECK(switch_off(engine.recipe().switches[0], mem));
        engine.press_hotkey(3);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
        CHECK(!switch_off(engine.recipe().switches[0], mem));
    }

    TEST_CASE("its state is the ROM byte: put back from outside, it's on again, and the next press turns it off")
    {
        auto mem = rom_with_functions();
        Recipe r;
        r.switches = { near_switch(1) };
        auto engine = running(r);
        engine.press_hotkey(1);
        engine.on_frame(mem);
        CHECK(switch_off(engine.recipe().switches[0], mem));
        mem.of(Target::ROM)[kNearEntry] = kClean; // a rollback, or the crash guard putting it back
        CHECK(!switch_off(engine.recipe().switches[0], mem));
        engine.press_hotkey(1);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kRts);
    }

    TEST_CASE("nothing flips while the engine is off, and flips asked for meanwhile are dropped")
    {
        auto mem = rom_with_functions();
        Engine engine;
        engine.set_recipe({});
        engine.flip_function(kNearEntry, kRts, kClean);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
        engine.set_active(true);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
    }

    TEST_CASE("start-off switches are written as a run starts; one made on another version is skipped")
    {
        auto mem = rom_with_functions();
        Recipe r;
        Switch on = near_switch();
        Switch off = near_switch();
        off.entry = kLongEntry;
        off.ret = kRtl;
        off.start_off = true;
        Switch elsewhere = off;
        elsewhere.entry = 0x400;
        elsewhere.was = 0x22; // the byte there is $E6 in this ROM
        r.switches = { on, off, elsewhere };
        auto engine = running(r);
        auto applied = engine.apply_switches(mem);
        CHECK(applied.written == 1);
        CHECK(applied.mismatched == 1);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
        CHECK(mem.of(Target::ROM)[kLongEntry] == kRtl);
        CHECK(mem.of(Target::ROM)[0x400] == kClean);
    }

    TEST_CASE("a locked entry is never flipped")
    {
        auto mem = rom_with_functions();
        auto engine = running();
        HotMask locked;
        locked.add_block(Target::ROM, kNearEntry);
        engine.set_locked(locked);
        engine.flip_function(kNearEntry, kRts, kClean);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
    }

    TEST_CASE("switches are added, replaced by entry and removed without a restart")
    {
        auto engine = running();
        engine.add_switch(near_switch(2));
        Switch again = near_switch(5);
        again.note = "gravity?";
        engine.add_switch(again);
        REQUIRE(engine.recipe().switches.size() == 1);
        CHECK(engine.recipe().switches[0].hotkey == 5);
        CHECK(engine.recipe().switches[0].note == "gravity?");
        CHECK(engine.active());
        engine.remove_switch(kNearEntry);
        CHECK(engine.recipe().switches.empty());
    }

    TEST_CASE("a run ahead's preview leaves the queued flips for the real frame")
    {
        auto mem = rom_with_functions();
        auto engine = running();
        engine.flip_function(kNearEntry, kRts, kClean);
        engine.preview_next(mem, Target::ROM);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kClean);
        auto pending = engine.pending_flips();
        CHECK(pending.size() == 1);
        engine.set_pending_flips({});
        engine.set_pending_flips(pending);
        engine.on_frame(mem);
        CHECK(mem.of(Target::ROM)[kNearEntry] == kRts);
    }
}

TEST_SUITE("turn functions off (rule)")
{
    std::vector<Function> learned(int n)
    {
        std::vector<Function> fs;
        for (int i = 0; i < n; i++)
            fs.push_back({ .entry = uint32_t(0x100 * (i + 1)),
                           .type = i % 2 ? CallType::Long : CallType::Near,
                           .frames = uint64_t(1000 - i) });
        return fs;
    }

    Rule kill_rule(uint32_t count, bool any = false)
    {
        Rule r;
        r.name = "kill";
        r.range = { Target::ROM, 0, 0xFFFF };
        r.op.type = OpType::Kill;
        r.op.any = any;
        r.when.type = TriggerType::EveryFrames;
        r.when.every = 1;
        r.count = count;
        return r;
    }

    TEST_CASE("each firing turns functions off with the right return, logged as the rule's writes")
    {
        auto mem = rom_with_functions();
        Recipe r;
        r.seed = 7;
        r.rules = { kill_rule(2) };
        auto fs = learned(10);
        fs.push_back({ .entry = 0xF000, .type = CallType::Mixed, .frames = 5000 });
        Engine engine;
        engine.set_functions([&] { return fs; });
        engine.set_recipe(r);
        engine.set_active(true);
        engine.on_frame(mem);
        CHECK(engine.aimed_count(0) == 10); // the mixed one isn't offered
        REQUIRE(engine.event_log().size() == 2);
        for (auto &e : engine.event_log())
        {
            uint32_t i = e.write.offset / 0x100 - 1;
            CHECK(e.write.new_value == (i % 2 ? kRtl : kRts));
            CHECK(e.write.old_value == kClean);
            CHECK(e.rule_index == 0);
        }
        CHECK(mem.of(Target::ROM)[0xF000] == kClean);
    }

    TEST_CASE("busiest picks from the 64 that ran in the most frames; any from all of them")
    {
        auto fs = learned(70); // 0x4100..0x4600 ran in the fewest frames
        for (bool any : { false, true })
        {
            auto mem = rom_with_functions();
            Recipe r;
            r.seed = 11;
            r.rules = { kill_rule(1, any) };
            Engine engine;
            engine.set_functions([&] { return fs; });
            engine.set_recipe(r);
            engine.set_active(true);
            std::set<uint32_t> hit;
            for (int i = 0; i < 2000; i++)
            {
                engine.on_frame(mem);
                if (!engine.event_log().empty())
                    hit.insert(engine.event_log().back().write.offset);
            }
            CHECK(engine.aimed_count(0) == (any ? 70u : 64u));
            bool rare = hit.count(0x4500) || hit.count(0x4600);
            CHECK(rare == any);
        }
    }

    TEST_CASE("the same seed turns off the same functions; nothing learned yet, nothing happens")
    {
        auto fs = learned(30);
        auto picks = [&](uint64_t seed, bool known) {
            auto mem = rom_with_functions();
            Recipe r;
            r.seed = seed;
            r.rules = { kill_rule(3) };
            Engine engine;
            engine.set_functions([&] { return known ? fs : std::vector<Function>{}; });
            engine.set_recipe(r);
            engine.set_active(true);
            engine.on_frame(mem);
            std::vector<uint32_t> out;
            for (auto &e : engine.event_log())
                out.push_back(e.write.offset);
            return out;
        };
        CHECK(picks(5, true) == picks(5, true));
        CHECK(picks(5, true) != picks(6, true));
        CHECK(picks(5, false).empty());
    }

    TEST_CASE("only functions inside the rule's range")
    {
        auto mem = rom_with_functions();
        Recipe r;
        r.seed = 3;
        Rule k = kill_rule(50);
        k.range = { Target::ROM, 0x300, 0x5FF };
        r.rules = { k };
        auto fs = learned(20);
        Engine engine;
        engine.set_functions([&] { return fs; });
        engine.set_recipe(r);
        engine.set_active(true);
        engine.on_frame(mem);
        CHECK(engine.aimed_count(0) == 3);
        for (auto &e : engine.event_log())
            CHECK((e.write.offset >= 0x300 && e.write.offset <= 0x5FF));
    }
}

TEST_SUITE("kill switches in the recipe")
{
    TEST_CASE("switches and the kill op round-trip")
    {
        Recipe r;
        r.name = "switches";
        Switch s = near_switch(3);
        s.start_off = true;
        s.note = "gravity?";
        Switch l;
        l.entry = 0x12345;
        l.ret = kRtl;
        l.was = 0x8B;
        r.switches = { s, l };
        Rule k;
        k.name = "kill";
        k.range = { Target::ROM, 0, 0x7FFF };
        k.op.type = OpType::Kill;
        k.op.any = true;
        k.when.type = TriggerType::Hotkey;
        k.when.hotkey = 1;
        r.rules = { k };
        std::string text = write_recipe(r);
        CHECK(text.find("\"switches\"") != std::string::npos);
        CHECK(text.find("\"kill\"") != std::string::npos);
        auto back = parse_recipe(text, {});
        REQUIRE(back.recipe);
        REQUIRE(back.recipe->switches.size() == 2);
        auto &b = back.recipe->switches[0];
        CHECK(b.entry == kNearEntry);
        CHECK(b.ret == kRts);
        CHECK(b.was == kClean);
        CHECK(b.hotkey == 3);
        CHECK(b.start_off);
        CHECK(b.note == "gravity?");
        CHECK(back.recipe->switches[1].ret == kRtl);
        CHECK(back.recipe->switches[1].hotkey == 0);
        CHECK(back.recipe->rules[0].op.type == OpType::Kill);
        CHECK(back.recipe->rules[0].op.any);
        CHECK(write_recipe(*back.recipe) == text);
    }

    TEST_CASE("a bad switch is refused with where and why")
    {
        auto bad = [](const char *sw) {
            std::string text = std::string(R"({"format": 1, "name": "x", "switches": [)") + sw + "]}";
            auto r = parse_recipe(text, {});
            return r.errors.empty() ? std::string() : r.errors[0];
        };
        CHECK(bad(R"({"entry": "0x200", "return": "0x60", "was": "0xE6"})").empty());
        CHECK(bad(R"({"entry": "0x200", "return": "0xEA", "was": "0xE6"})").find("switches[0].return") != std::string::npos);
        CHECK(bad(R"({"entry": "0x200", "return": "0x60", "was": "0xE6", "hotkey": 10})").find("switches[0].hotkey") !=
              std::string::npos);
        CHECK(bad(R"({"entry": "0x200", "was": "0xE6"})").find("switches[0]") != std::string::npos);
    }

    TEST_CASE("the kill op is for the ROM only and says what it does")
    {
        FakeMemory mem;
        Rule k;
        k.range = { Target::WRAM, 0, 0xFF };
        k.op.type = OpType::Kill;
        CHECK(validate_rule(k, mem));
        k.range = { Target::ROM, 0, 0xFF };
        CHECK(!validate_rule(k, mem));
        CHECK(describe_rule(k).find("functions") != std::string::npos);
        CHECK(std::string(op_name(OpType::Kill)) == "kill");
        CHECK(parse_op("kill") == OpType::Kill);
    }
}
