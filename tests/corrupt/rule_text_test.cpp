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
          "swap 0x00F00 <-> 0x00F02, 16-bit");
    CHECK(describe_op(with_op({ .type = OpType::Swap }, 3)) == "swap with random x3");
    CHECK(describe_op(with_op({ .type = OpType::Copy, .from = AddressRange{ Target::VRAM, 0, 0xFFF }, .length = 16 })) ==
          "copy 16 bytes from vram x1");
    CHECK(describe_op(with_op({ .type = OpType::Copy, .length = 1 })) == "copy 1 byte from wram x1");
    CHECK(describe_op(with_op({ .type = OpType::Freeze, .value = uint8_t(0) }, 2)) == "freeze at 0x00 x2");
    CHECK(describe_op(with_op({ .type = OpType::Freeze })) == "freeze as hit x1");
    CHECK(describe_op(with_op({ .type = OpType::Shuffle, .window = 8 })) == "shuffle 8 bytes x1");
    // Area ops work on the whole area: no count.
    CHECK(describe_op(with_op({ .type = OpType::Shift, .dx = 3, .dy = -1 }, 5)) == "shift 3, -1");
    CHECK(describe_op(with_op({ .type = OpType::Shift, .dx = 2, .wrap = false })) == "shift 2, 0, no wrap");
    CHECK(describe_op(with_op({ .type = OpType::Flip, .axis = Axis::Both })) == "flip both ways");
    CHECK(describe_op(with_op({ .type = OpType::Flip, .axis = Axis::Vertical })) == "flip vertical");
    CHECK(describe_op(with_op({ .type = OpType::Levels, .curve = Curve::Invert })) == "invert");
    CHECK(describe_op(with_op({ .type = OpType::Levels, .curve = Curve::Posterize, .amount = 4 })) == "posterize 4");
    CHECK(describe_op(with_op({ .type = OpType::Levels, .curve = Curve::Gamma, .amount = 1.5 })) == "gamma 1.5");
    CHECK(describe_op(with_op({ .type = OpType::Filter, .kernel = Kernel::Median })) == "median filter");
    CHECK(describe_op(with_op({ .type = OpType::Stamp, .source = 0x800 })) == "stamp from 0x00800");
    CHECK(describe_op(with_op({ .type = OpType::Fill, .value = uint8_t(0xEA) })) == "fill 0xEA");
    CHECK(describe_op(with_op({ .type = OpType::OpcodeSwap }, 3)) == "swap opcodes x3");
    CHECK(describe_op(with_op({ .type = OpType::BranchFlip }, 2)) == "flip branches x2");
    CHECK(describe_op(with_op({ .type = OpType::NopOut })) == "NOP out x1");
}

TEST_CASE("an aimed rule says what it aims at")
{
    Rule r = with_op({ .type = OpType::Random, .min = 0, .max = 255 });
    r.range = { Target::ROM, 0, 0xFFFF };
    r.when = { .type = TriggerType::AtLoad };
    r.aim = { .role = Role::Operand, .classes = { OpClass::Arith, OpClass::Compare } };
    CHECK(describe_rule(r) == "rom 0x000000..0x00FFFF: random 0..255 x1, once at load, operands of arith, compare");
    r.aim = { .role = Role::Code, .except = { OpClass::Width, OpClass::Stack } };
    CHECK(describe_rule(r) == "rom 0x000000..0x00FFFF: random 0..255 x1, once at load, code except width, stack");
    r.op = { .type = OpType::NopOut };
    r.aim = { .classes = { OpClass::Jump } };
    CHECK(describe_rule(r) == "rom 0x000000..0x00FFFF: NOP out x1, once at load, only jump");
    r.aim = { .role = Role::Data };
    r.op = { .type = OpType::Add, .min = -1, .max = 1 };
    CHECK(describe_rule(r) == "rom 0x000000..0x00FFFF: add -1..1 x1, once at load, data");
}

TEST_CASE("a rectangle reads as its corner and size")
{
    Rule r;
    set_rect(r, Target::ROM, 0x1000, Rect{ 64, 16, 32 });
    CHECK(describe_range(r) == "rom 16x32 at 0x001000 (rows of 64)");
}

TEST_CASE("each trigger has a short description")
{
    CHECK(describe_trigger({ .type = TriggerType::OnceAtFrame, .frame = 300 }) == "once at frame 300");
    CHECK(describe_trigger({ .type = TriggerType::EveryFrames, .every = 30 }) == "every 30 frames");
    CHECK(describe_trigger({ .type = TriggerType::EveryFrames, .every = 1 }) == "every frame");
    CHECK(describe_trigger({ .type = TriggerType::ChancePerFrame, .chance = 0.02 }) == "2% per frame");
    CHECK(describe_trigger({ .type = TriggerType::ChancePerFrame, .chance = 0.005 }) == "0.5% per frame");
    CHECK(describe_trigger({ .type = TriggerType::Hotkey, .hotkey = 3 }) == "button 3");
    CHECK(describe_trigger({ .type = TriggerType::AtLoad }) == "once at load");
    Trigger windowed{ .type = TriggerType::EveryFrames, .every = 60, .window = FrameWindow{ 600, 1200 } };
    CHECK(describe_trigger(windowed) == "every 60 frames, frames 600-1200");
}

TEST_CASE("a rule's summary names where, what and when")
{
    Rule r = with_op({ .type = OpType::Random, .min = 0, .max = 255 }, 2);
    r.when = { .type = TriggerType::EveryFrames, .every = 120 };
    CHECK(describe_range(r) == "wram 0x02000..0x02FFF");
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

TEST_CASE("describe_strength says what a rule does at its strength, in its own units")
{
    Rule add = with_op({ .type = OpType::Add, .min = -2, .max = 2 }, 8);
    CHECK(describe_strength(add) == "At 100%: changes 8 bytes each time it fires, adding -2..2.");
    add.intensity = 0.5;
    CHECK(describe_strength(add) == "At 50%: changes 4 of its 8 bytes each time it fires, adding -1..1 instead of -2..2.");
    // The two sliders multiply, and the text says so.
    CHECK(describe_strength(add, 0.5) ==
          "At 25% (50% × 50% for all rules): changes 2 of its 8 bytes each time it fires, adding -1..1 instead of -2..2.");

    Rule flip = with_op({ .type = OpType::Bitflip, .bits = 4 }, 2);
    flip.intensity = 0.5;
    CHECK(describe_strength(flip) == "At 50%: changes 1 of its 2 bytes each time it fires, flipping 2 bits instead of 4.");
    Rule mask = with_op({ .type = OpType::Bitflip, .mask = uint8_t(0x81) }, 1);
    CHECK(describe_strength(mask) == "At 100%: changes 1 byte each time it fires.");

    // Under one byte a firing, it fires less often instead.
    Rule one = with_op({ .type = OpType::Random, .min = 0, .max = 255 }, 1);
    one.intensity = 0.1;
    CHECK(describe_strength(one) == "At 10%: it fires on about 1 in 10 of its chances, and then changes 1 byte.");
    one.intensity = 0.7;
    CHECK(describe_strength(one) == "At 70%: it fires on about 70% of its chances, and then changes 1 byte.");
    one.intensity = 0.125; // a crash guard's halving isn't rounded away
    CHECK(describe_strength(one) == "At 12.5%: it fires on about 1 in 8 of its chances, and then changes 1 byte.");

    one.intensity = 1.0;
    CHECK(describe_strength(one, 0.0) == "At 0% (100% × 0% for all rules): it does nothing.");
    one.intensity = 0.0;
    CHECK(describe_strength(one) == "At 0%: it does nothing.");
}

TEST_CASE("describe_strength: areas, a fixed swap, code, jump tables and functions")
{
    Rule shift = with_op({ .type = OpType::Shift, .dx = 1 }, 1);
    CHECK(describe_strength(shift) == "At 100%: changes the whole area each time it fires.");
    shift.intensity = 0.25;
    CHECK(describe_strength(shift) == "At 25%: it fires on about 1 in 4 of its chances, and then changes the whole area.");

    Rule pair = with_op({ .type = OpType::Swap, .a = 0x10u, .b = 0x20u }, 1);
    CHECK(describe_strength(pair) == "At 100%: swaps the pair each time it fires.");

    Rule code = with_op({ .type = OpType::NopOut }, 4);
    code.range = { Target::ROM, 0, 0xFFFF };
    code.intensity = 0.5;
    CHECK(describe_strength(code) == "At 50%: changes 2 of its 4 instructions each time it fires.");

    Rule jumps = with_op({ .type = OpType::PointerSwap }, 1);
    jumps.range = code.range;
    CHECK(describe_strength(jumps) == "At 100%: swaps 1 jump table entry each time it fires.");
    jumps.count = 3;
    CHECK(describe_strength(jumps) == "At 100%: swaps 3 jump table entries each time it fires.");

    Rule kill = with_op({ .type = OpType::Kill }, 2);
    kill.range = code.range;
    kill.intensity = 0.25;
    CHECK(describe_strength(kill) == "At 25%: it fires on about 1 in 2 of its chances, and then turns off 1 function.");
}
