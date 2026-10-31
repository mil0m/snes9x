#include "doctest/doctest.h"
#include "corrupt/recipe_json.h"
#include "corrupt/rule_text.h"

using namespace corrupt;

namespace
{
Rule with_op(Op op, uint32_t count = 1)
{
    Rule r;
    r.range = { Target::WRAM, 0x2000, 0x2FFF };
    r.op = op;
    r.count = count;
    return r;
}
} // namespace

TEST_CASE("each op has a short description")
{
    CHECK(describe_op(with_op({ .type = OpType::Add, .min = -2, .max = 2 }, 4)) == "add -2..2 x4");
    CHECK(describe_op(with_op({ .type = OpType::Add, .min = 0, .max = 9, .clamp = true })) == "add 0..9 clamped x1");
    CHECK(describe_op(with_op({ .type = OpType::Random, .min = 0, .max = 255 })) == "random 0..255 x1");
    CHECK(describe_op(with_op({ .type = OpType::Bitflip, .bits = 1 })) == "flip 1 bit x1");
    CHECK(describe_op(with_op({ .type = OpType::Bitflip, .bits = 3 }, 2)) == "flip 3 bits x2");
    CHECK(describe_op(with_op({ .type = OpType::Bitflip, .mask = uint8_t(0x81) })) == "xor 0x81 x1");
    CHECK(describe_op(with_op({ .type = OpType::Swap, .width = 2, .a = 0x0F00u, .b = 0x0F02u })) ==
          "swap 0x0F00 <-> 0x0F02, 16-bit");
    CHECK(describe_op(with_op({ .type = OpType::Swap }, 3)) == "swap with random x3");
    CHECK(describe_op(with_op({ .type = OpType::Copy, .from = AddressRange{ Target::VRAM, 0, 0xFFF }, .length = 16 })) ==
          "copy 16 bytes from vram x1");
    CHECK(describe_op(with_op({ .type = OpType::Copy, .length = 1 })) == "copy 1 byte from wram x1");
    CHECK(describe_op(with_op({ .type = OpType::Freeze, .value = uint8_t(0) }, 2)) == "freeze at 0x00 x2");
    CHECK(describe_op(with_op({ .type = OpType::Freeze })) == "freeze as hit x1");
    CHECK(describe_op(with_op({ .type = OpType::Shuffle, .window = 8 })) == "shuffle 8 bytes x1");
}

TEST_CASE("each trigger has a short description")
{
    CHECK(describe_trigger({ .type = TriggerType::OnceAtFrame, .frame = 300 }) == "once at frame 300");
    CHECK(describe_trigger({ .type = TriggerType::EveryFrames, .every = 30 }) == "every 30 frames");
    CHECK(describe_trigger({ .type = TriggerType::EveryFrames, .every = 1 }) == "every frame");
    CHECK(describe_trigger({ .type = TriggerType::ChancePerFrame, .chance = 0.02 }) == "2% per frame");
    CHECK(describe_trigger({ .type = TriggerType::ChancePerFrame, .chance = 0.005 }) == "0.5% per frame");
    CHECK(describe_trigger({ .type = TriggerType::Hotkey, .hotkey = 3 }) == "hotkey 3");
    CHECK(describe_trigger({ .type = TriggerType::AtLoad }) == "once at load");
    Trigger windowed{ .type = TriggerType::EveryFrames, .every = 60, .window = FrameWindow{ 600, 1200 } };
    CHECK(describe_trigger(windowed) == "every 60 frames, frames 600-1200");
}

TEST_CASE("a rule's summary names where, what and when")
{
    Rule r = with_op({ .type = OpType::Random, .min = 0, .max = 255 }, 2);
    r.when = { .type = TriggerType::EveryFrames, .every = 120 };
    CHECK(describe_range(r) == "wram 0x2000..0x2FFF");
    r.region = "enemy_table";
    CHECK(describe_range(r) == "wram enemy_table");
    CHECK(describe_rule(r) == "wram enemy_table: random 0..255 x2, every 120 frames");
    r.avoid_hot = true;
    CHECK(describe_rule(r) == "wram enemy_table: random 0..255 x2, every 120 frames, avoiding danger");
}

TEST_CASE("the default new rule is gentle and valid")
{
    Rule r = default_rule();
    SizedMemory nominal;
    CHECK_FALSE(validate_rule(r, nominal));
    CHECK(r.range.target == Target::WRAM);
    CHECK(r.range.first == 0x2000u);
    CHECK(r.range.last == 0x1FFFFu);
    CHECK(r.op.type == OpType::Random);
    CHECK(r.when.type == TriggerType::EveryFrames);
    CHECK(r.when.every == 120u);
    CHECK(r.count == 1u);
    CHECK_FALSE(r.name.empty());
}

TEST_CASE("sized memory validates against the sizes it was given")
{
    SizedMemory nominal;
    for (auto t : kAllTargets)
        CHECK(nominal.size(t) == nominal_size(t));

    SizedMemory no_save;
    no_save.sizes[size_t(Target::SRAM)] = 0;
    Rule r = default_rule();
    r.range = { Target::SRAM, 0, 0xFF };
    CHECK(validate_rule(r, nominal) == std::nullopt);
    auto err = validate_rule(r, no_save);
    REQUIRE(err);
    CHECK(err->find("sram") != std::string::npos);
    CHECK(no_save.read(Target::WRAM, 5) == 0);
}
