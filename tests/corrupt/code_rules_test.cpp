#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_map.h"
#include "corrupt/engine.h"
#include "corrupt/recipe_json.h"

#include <set>

using namespace corrupt;
namespace cf = codeflag;

namespace
{
constexpr uint32_t kCode = 0x1000;   // 100 x LDA #$nn: opcodes at even offsets, operands odd
constexpr uint32_t kCount = 100;

struct Rig
{
    FakeMemory mem;
    std::vector<uint8_t> flags;
    Engine engine;

    Rig()
    {
        auto &rom = mem.of(Target::ROM);
        flags.assign(rom.size(), 0);
        for (uint32_t i = 0; i < kCount; i++)
        {
            rom[kCode + 2 * i] = 0xA9;
            rom[kCode + 2 * i + 1] = uint8_t(i);
            flags[kCode + 2 * i] = cf::Opcode | cf::Op8;
            flags[kCode + 2 * i + 1] = cf::Operand | cf::Op8;
        }
        engine.set_code_flags([this] { return std::span<const uint8_t>(flags); });
    }
    void start(Rule rule, uint64_t seed = 7)
    {
        Recipe r;
        r.name = "code";
        r.seed = seed;
        r.rules = { rule };
        engine.set_recipe(r);
        engine.set_active(true);
    }
    void frames(int n)
    {
        for (int i = 0; i < n; i++)
            engine.on_frame(mem);
    }
};

Rule rom_rule(OpType type, Aim aim = {})
{
    Rule r;
    r.name = "aimed";
    r.range = { Target::ROM, 0, 0xFFFF };
    r.op = { .type = type, .min = 0, .max = 255 };
    r.when = { .type = TriggerType::EveryFrames, .every = 1 };
    r.count = 3;
    r.aim = aim;
    return r;
}
} // namespace

TEST_SUITE("code-aware rules")
{
    TEST_CASE("an operand rule only ever changes operand bytes")
    {
        Rig rig;
        rig.start(rom_rule(OpType::Random, { .role = Role::Operand }));
        rig.frames(50);
        REQUIRE(!rig.engine.event_log().empty());
        for (auto &e : rig.engine.event_log())
        {
            CHECK(e.write.target == Target::ROM);
            CHECK(e.write.offset >= kCode);
            CHECK(e.write.offset < kCode + 2 * kCount);
            CHECK(e.write.offset % 2 == 1);
        }
        CHECK(rig.engine.aimed_count(0) == kCount);
    }

    TEST_CASE("opcode swap changes opcodes into members of their group")
    {
        Rig rig;
        rig.start(rom_rule(OpType::OpcodeSwap));
        rig.frames(5);
        REQUIRE(!rig.engine.event_log().empty());
        for (auto &e : rig.engine.event_log())
        {
            CHECK(e.write.offset % 2 == 0);
            auto group = swap_group(e.write.old_value);
            CHECK(std::find(group.begin(), group.end(), e.write.new_value) != group.end());
        }
    }

    TEST_CASE("with no code map known, an aimed rule fires on nothing")
    {
        Rig rig;
        rig.engine.set_code_flags({});
        rig.start(rom_rule(OpType::Random, { .role = Role::Operand }));
        rig.frames(20);
        CHECK(rig.engine.event_log().empty());
        CHECK(rig.engine.status().firings == 0);
        CHECK(rig.engine.aimed_count(0) == 0);
    }

    TEST_CASE("what a rule aims at is fixed for the run, and taken afresh by the next one")
    {
        Rig rig;
        for (uint32_t i = kCount / 2; i < kCount; i++) // the second half isn't explored yet
            rig.flags[kCode + 2 * i] = rig.flags[kCode + 2 * i + 1] = 0;
        rig.start(rom_rule(OpType::Random, { .role = Role::Operand }));
        rig.frames(1);
        for (uint32_t i = kCount / 2; i < kCount; i++)
        {
            rig.flags[kCode + 2 * i] = cf::Opcode | cf::Op8;
            rig.flags[kCode + 2 * i + 1] = cf::Operand | cf::Op8;
        }
        rig.frames(40);
        for (auto &e : rig.engine.event_log())
            CHECK(e.write.offset < kCode + kCount);
        CHECK(rig.engine.aimed_count(0) == kCount / 2);
        rig.engine.set_active(false);
        rig.engine.set_active(true);
        rig.frames(1);
        CHECK(rig.engine.aimed_count(0) == kCount);
    }

    TEST_CASE("an aimed recipe replays identically from the same ROM")
    {
        std::vector<LoggedEvent> runs[2];
        for (auto &run : runs)
        {
            Rig rig;
            rig.start(rom_rule(OpType::Add, { .role = Role::Operand }), 3);
            rig.frames(30);
            run.assign(rig.engine.event_log().begin(), rig.engine.event_log().end());
        }
        REQUIRE(runs[0].size() == runs[1].size());
        for (size_t i = 0; i < runs[0].size(); i++)
        {
            CHECK(runs[0][i].write.offset == runs[1][i].write.offset);
            CHECK(runs[0][i].write.new_value == runs[1][i].write.new_value);
        }
    }

    TEST_CASE("a replaced rule aims afresh")
    {
        Rig rig;
        rig.start(rom_rule(OpType::Random, { .role = Role::Operand }));
        rig.frames(2);
        rig.engine.replace_rule(0, rom_rule(OpType::Random, { .role = Role::Opcode }));
        rig.frames(5);
        CHECK(rig.engine.event_log().back().write.offset % 2 == 0);
    }

    TEST_CASE("aim and code ops in recipes: round trip and errors")
    {
        auto r = parse_recipe(R"({"format":1,"rules":[
            {"target":"rom","range":["0x0","0xFFFF"],"op":{"type":"add","min":-2,"max":2},"when":{"every_frames":60},
             "role":"operand","classes":["arith","compare"],"except":["width"]},
            {"target":"rom","range":["0x0","0xFFFF"],"op":{"type":"branch_flip"},"when":{"at_load":true},"count":3}]})",
                              {});
        REQUIRE_MESSAGE(r.recipe, (r.errors.empty() ? "" : r.errors[0]));
        auto &a = r.recipe->rules[0].aim;
        CHECK(a.role == Role::Operand);
        CHECK(a.classes == std::vector<OpClass>{ OpClass::Arith, OpClass::Compare });
        CHECK(a.except == std::vector<OpClass>{ OpClass::Width });
        CHECK(r.recipe->rules[1].op.type == OpType::BranchFlip);
        CHECK(!r.recipe->rules[1].aim.active());
        auto again = parse_recipe(write_recipe(*r.recipe), {});
        REQUIRE(again.recipe);
        CHECK(again.recipe->rules[0].aim.classes == a.classes);
        CHECK(again.recipe->rules[0].aim.except == a.except);
        CHECK(again.recipe->rules[0].aim.role == Role::Operand);
        CHECK(again.recipe->rules[1].op.type == OpType::BranchFlip);
        CHECK(write_recipe(*parse_recipe(R"({"format":1,"rules":[]})", {}).recipe).find("role") == std::string::npos);

        CHECK(!parse_recipe(R"({"format":1,"rules":[{"target":"rom","range":["0x0","0xF"],"op":{"type":"add"},
            "when":{"every_frames":1},"role":"pointer"}]})", {}).recipe);
        CHECK(!parse_recipe(R"({"format":1,"rules":[{"target":"rom","range":["0x0","0xF"],"op":{"type":"add"},
            "when":{"every_frames":1},"classes":["jumps"]}]})", {}).recipe);
    }

    TEST_CASE("aiming is for ROM rules, and class filters only pick code")
    {
        FakeMemory mem;
        Rule wram = rom_rule(OpType::Random, { .role = Role::Data });
        wram.range = { Target::WRAM, 0, 0xFF };
        CHECK(validate_rule(wram, mem));
        Rule swap = rom_rule(OpType::OpcodeSwap);
        swap.range = { Target::WRAM, 0, 0xFF };
        CHECK(validate_rule(swap, mem));
        Rule data_classes = rom_rule(OpType::Random, { .role = Role::Data, .classes = { OpClass::Load } });
        CHECK(validate_rule(data_classes, mem));
        Rule area = rom_rule(OpType::Fill, { .role = Role::Operand });
        CHECK(validate_rule(area, mem));
        CHECK(!validate_rule(rom_rule(OpType::Random, { .role = Role::Operand, .except = { OpClass::Width } }), mem));
    }
}
