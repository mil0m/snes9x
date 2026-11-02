#include "doctest/doctest.h"

#include "corrupt/engine.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"
#include "fake_memory.h"

using namespace corrupt;

namespace
{

uint8_t rom(FakeMemory &m, uint32_t o)
{
    return m.read(Target::ROM, o);
}

} // namespace

TEST_SUITE("timed switches")
{
    TEST_CASE("a switch on a timer is off for its part of every cycle, from the run's start")
    {
        FakeMemory mem;
        mem.of(Target::ROM)[0x100] = 0xA5;
        Engine e;
        Recipe r;
        r.switches.push_back({ .entry = 0x100, .ret = kRts, .was = 0xA5, .every = 10, .for_frames = 3 });
        e.set_recipe(r);
        e.set_active(true);
        std::string seen;
        for (int f = 1; f <= 22; f++)
        {
            e.on_frame(mem);
            seen += rom(mem, 0x100) == kRts ? 'x' : '.';
        }
        CHECK(seen == "xxx.......xxx.......xx");
        CHECK(e.event_log().empty()); // not a rule's write

        // After a rollback to frame 5 the timer goes on from there, the same as before.
        e.restore({ 5, {}, {} });
        e.on_frame(mem); // frame 6
        CHECK(rom(mem, 0x100) == kRts); // still what frame 22 left: timers only act at their edges
        for (int f = 7; f <= 11; f++)
            e.on_frame(mem);
        CHECK(rom(mem, 0x100) == kRts); // frame 11 starts a cycle
    }

    TEST_CASE("a change on a timer is on for its part; a byte someone else changed is left alone by toggles, set by the timer")
    {
        FakeMemory mem;
        auto &bytes = mem.of(Target::ROM);
        bytes[0x200] = 0x69;
        bytes[0x201] = 0x01;
        Engine e;
        Recipe r;
        r.changes.push_back({ .function = 0x1F0, .at = 0x200, .kind = ChangeKind::Number, .value = { 0x69, 0x09 },
                              .was = { 0x69, 0x01 }, .every = 4, .for_frames = 1 });
        e.set_recipe(r);
        e.set_active(true);
        std::string seen;
        for (int f = 1; f <= 9; f++)
        {
            e.on_frame(mem);
            seen += bytes[0x201] == 0x09 ? 'x' : '.';
        }
        CHECK(seen == "x...x...x");
    }

    TEST_CASE("a timer leaves bytes alone that hold neither its on nor its off (another ROM version)")
    {
        FakeMemory mem;
        mem.of(Target::ROM)[0x100] = 0x42;
        Engine e;
        Recipe r;
        r.switches.push_back({ .entry = 0x100, .ret = kRts, .was = 0xA5, .every = 4, .for_frames = 2 });
        e.set_recipe(r);
        e.set_active(true);
        for (int f = 1; f <= 8; f++)
            e.on_frame(mem);
        CHECK(rom(mem, 0x100) == 0x42);
    }

    TEST_CASE("timers in the recipe: JSON round trip and errors")
    {
        Recipe r;
        r.name = "t";
        r.switches.push_back({ .entry = 0x9000, .ret = kRts, .was = 0x22, .every = 600, .for_frames = 120 });
        r.changes.push_back({ .function = 0x9000, .at = 0x9012, .kind = ChangeKind::Math, .value = { 0xE9 }, .was = { 0x69 },
                              .every = 60, .for_frames = 30 });
        auto text = write_recipe(r);
        auto back = parse_recipe(text, {});
        REQUIRE(back.errors.empty());
        CHECK(back.recipe->switches[0].every == 600);
        CHECK(back.recipe->switches[0].for_frames == 120);
        CHECK(back.recipe->changes[0].every == 60);
        CHECK(back.recipe->changes[0].for_frames == 30);
        CHECK(write_recipe(*back.recipe) == text);

        for (auto bad : { R"("switches": [{"entry": "0x1", "return": "0x60", "was": "0x22", "every": 10}])",
                          R"("switches": [{"entry": "0x1", "return": "0x60", "was": "0x22", "every": 10, "for": 10}])",
                          R"("changes": [{"function": "0x1", "at": "0x2", "kind": "math", "value": "E9", "was": "69", "every": 1, "for": 1}])" })
        {
            auto p = parse_recipe(std::string(R"({"format": 1, "name": "x", "rules": [], )") + bad + "}", {});
            CAPTURE(bad);
            CHECK_FALSE(p.errors.empty());
        }
    }

    TEST_CASE("a kill that puts back: each function comes back on after its time, through rollbacks too")
    {
        FakeMemory mem;
        auto &bytes = mem.of(Target::ROM);
        bytes[0x100] = 0xA5;
        bytes[0x200] = 0xA5;
        Engine e;
        Recipe r;
        Rule kill;
        kill.range = { Target::ROM, 0, 0xFFFF };
        kill.op.type = OpType::Kill;
        kill.op.put_back = 5;
        kill.when.type = TriggerType::EveryFrames;
        kill.when.every = 10;
        kill.count = 1;
        r.rules.push_back(kill);
        e.set_recipe(r);
        e.set_functions([] {
            return std::vector<Function>{ { .entry = 0x100, .type = CallType::Near, .frames = 50 },
                                          { .entry = 0x200, .type = CallType::Near, .frames = 40 } };
        });
        CHECK(describe_rule(kill).find("back on after") != std::string::npos);
        e.set_active(true);
        std::string seen;
        for (int f = 1; f <= 20; f++)
        {
            e.on_frame(mem);
            seen += bytes[0x100] == kRts || bytes[0x200] == kRts ? 'x' : '.';
        }
        CHECK(seen == ".........xxxxx.....x");
        CHECK(e.event_log().size() == 2); // the kills; putting back isn't logged

        // A snapshot holds what's still to put back.
        auto snap = e.snapshot();
        uint8_t a = bytes[0x100], b = bytes[0x200];
        for (int f = 0; f < 3; f++)
            e.on_frame(mem);
        e.restore(snap);
        bytes[0x100] = a; // the ROM as it was then (the ROM keeper's job)
        bytes[0x200] = b;
        CHECK(int(a == kRts) + int(b == kRts) == 1);
        for (int f = 0; f < 5; f++)
            e.on_frame(mem);
        CHECK(int(bytes[0x100] == kRts) + int(bytes[0x200] == kRts) == 0);

        auto text = write_recipe(r);
        CHECK(text.find("\"put_back\": 5") != std::string::npos);
        CHECK(parse_recipe(text, {}).recipe->rules[0].op.put_back == 5);
    }

    TEST_CASE("a kill rule keeps learning while its list is short")
    {
        FakeMemory mem;
        Engine e;
        Recipe r;
        Rule kill;
        kill.range = { Target::ROM, 0, 0xFFFF };
        kill.op.type = OpType::Kill;
        kill.when.type = TriggerType::EveryFrames;
        kill.when.every = 1;
        kill.count = 1;
        kill.intensity = 0.0001; // fires almost never: we only want it to aim
        r.rules.push_back(kill);
        e.set_recipe(r);
        size_t known = 1;
        e.set_functions([&] {
            std::vector<Function> fs;
            for (size_t i = 0; i < known; i++)
                fs.push_back({ .entry = uint32_t(0x100 * (i + 1)), .type = CallType::Near, .frames = 10 });
            return fs;
        });
        e.set_active(true);
        e.on_frame(mem);
        CHECK(e.aimed_count(0) == 1);
        known = 5;
        for (int f = 0; f < 299; f++)
            e.on_frame(mem);
        CHECK(e.aimed_count(0) == 5); // taken again at frame 300
    }
}
