#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/code_aim.h"
#include "corrupt/code_map.h"
#include "corrupt/crash_map.h"

#include <algorithm>
#include <set>

using namespace corrupt;
namespace cf = codeflag;

namespace
{
bool has(std::span<const uint8_t> group, uint8_t op)
{
    return std::find(group.begin(), group.end(), op) != group.end();
}

// A tiny program with its flags, as the tracer would leave them:
//   0: LDA #$05      A9 05     (8-bit)
//   2: STA $0450     8D 50 04
//   5: BNE -16       D0 F0
//   7: LDA #$1234    A9 34 12  (16-bit)
//  10: a data byte, 11: a DMA byte, 12: never touched
//  13: ADC #..       69 01     run at both widths
struct Program
{
    std::vector<uint8_t> rom = { 0xA9, 0x05, 0x8D, 0x50, 0x04, 0xD0, 0xF0, 0xA9, 0x34, 0x12, 0x77, 0x88, 0x99, 0x69, 0x01, 0x00 };
    std::vector<uint8_t> flags = std::vector<uint8_t>(16, 0);
    Program()
    {
        flags[0] = cf::Opcode | cf::Op8;
        flags[1] = cf::Operand | cf::Op8;
        flags[2] = cf::Opcode | cf::Op8;
        flags[3] = flags[4] = cf::Operand | cf::Op8;
        flags[5] = cf::Opcode;
        flags[6] = cf::Operand;
        flags[7] = cf::Opcode | cf::Op16;
        flags[8] = flags[9] = cf::Operand | cf::Op16;
        flags[10] = cf::Data;
        flags[11] = cf::Dma;
        flags[13] = cf::Opcode | cf::Op8 | cf::Op16;
        flags[14] = cf::Operand | cf::Op8 | cf::Op16;
    }
    std::vector<uint32_t> aim(Aim a, uint32_t first = 0, uint32_t last = 15) const
    {
        return aimed_bytes(rom, flags, first, last, a);
    }
};
} // namespace

TEST_SUITE("code aim")
{
    TEST_CASE("swap groups: same mode and length at every width, different meaning")
    {
        for (int op = 0; op < 256; op++)
        {
            CAPTURE(op);
            auto group = swap_group(uint8_t(op));
            REQUIRE(has(group, uint8_t(op)));
            for (uint8_t other : group)
            {
                CAPTURE(int(other));
                CHECK(op_info(other).mode == op_info(uint8_t(op)).mode);
                for (bool m8 : { false, true })
                    for (bool x8 : { false, true })
                        CHECK(op_length(other, m8, x8) == op_length(uint8_t(op), m8, x8));
            }
        }
        auto adc_dp = swap_group(0x65);
        CHECK(has(adc_dp, 0xE5)); // SBC $12
        CHECK(has(adc_dp, 0x25)); // AND $12
        CHECK(has(adc_dp, 0xA5)); // LDA $12
        CHECK(!has(adc_dp, 0x85)); // not STA: it writes instead of reading
        CHECK(has(swap_group(0x69), 0xE9)); // ADC # <-> SBC #
        CHECK(!has(swap_group(0x69), 0xA2)); // not LDX #: its width follows X
        CHECK(has(swap_group(0xD0), 0xF0)); // BNE <-> BEQ
        CHECK(has(swap_group(0xD0), 0x80)); // ... and BRA
        CHECK(has(swap_group(0x85), 0x64)); // STA $12 <-> STZ $12
        CHECK(has(swap_group(0xE8), 0xCA)); // INX <-> DEX
        CHECK(swap_group(0x20).size() == 1); // JSR: alone
        CHECK(swap_group(0x48).size() == 1); // PHA: alone (a different push unbalances the stack)
        CHECK(swap_group(0xC2).size() == 1); // REP: alone
    }

    TEST_CASE("branch flips")
    {
        CHECK(is_conditional_branch(0xD0));
        CHECK(is_conditional_branch(0x10));
        CHECK(!is_conditional_branch(0x80)); // BRA
        CHECK(!is_conditional_branch(0x82)); // BRL
        CHECK(!is_conditional_branch(0xA9));
        CHECK(flipped_branch(0xD0) == 0xF0);
        CHECK(flipped_branch(0xF0) == 0xD0);
        CHECK(flipped_branch(0x10) == 0x30);
        CHECK(flipped_branch(0x90) == 0xB0);
        CHECK(flipped_branch(0x50) == 0x70);
    }

    TEST_CASE("roles pick bytes by what the code map knows, skipping code run at both widths")
    {
        Program p;
        CHECK(p.aim({ .role = Role::Opcode }) == std::vector<uint32_t>{ 0, 2, 5, 7 });
        CHECK(p.aim({ .role = Role::Operand }) == std::vector<uint32_t>{ 1, 3, 4, 6, 8, 9 });
        CHECK(p.aim({ .role = Role::Code }).size() == 10);
        CHECK(p.aim({ .role = Role::Data }) == std::vector<uint32_t>{ 10 });
        CHECK(p.aim({ .role = Role::Dma }) == std::vector<uint32_t>{ 11 });
        CHECK(p.aim({ .role = Role::Used }).size() == 12); // not the untouched bytes, nor code run at both widths
        CHECK(p.aim({ .role = Role::Operand }, 3, 6) == std::vector<uint32_t>{ 3, 4, 6 });
        CHECK(p.aim({}).size() == 16); // no aim: every byte, as before
    }

    TEST_CASE("class filters go by the instruction a byte belongs to")
    {
        Program p;
        CHECK(p.aim({ .role = Role::Operand, .classes = { OpClass::Store } }) == std::vector<uint32_t>{ 3, 4 });
        CHECK(p.aim({ .role = Role::Opcode, .except = { OpClass::Branch } }) == std::vector<uint32_t>{ 0, 2, 7 });
        CHECK(p.aim({ .role = Role::Code, .classes = { OpClass::Branch } }) == std::vector<uint32_t>{ 5, 6 });
        CHECK(p.aim({ .classes = { OpClass::Load } }) == std::vector<uint32_t>{ 0, 1, 7, 8, 9 }); // code implied
    }

    TEST_CASE("the ops' own targets: opcodes, conditional branches")
    {
        Program p;
        CHECK(code_op_targets(OpType::OpcodeSwap, p.rom, p.flags, 0, 15, {}) == std::vector<uint32_t>{ 0, 2, 5, 7 });
        CHECK(code_op_targets(OpType::BranchFlip, p.rom, p.flags, 0, 15, {}) == std::vector<uint32_t>{ 5 });
        CHECK(code_op_targets(OpType::NopOut, p.rom, p.flags, 0, 15, { .except = { OpClass::Load } }) ==
              std::vector<uint32_t>{ 2, 5 });
    }

    struct Rom
    {
        FakeMemory mem;
        Program p;
        Rom()
        {
            auto &bytes = mem.of(Target::ROM);
            std::copy(p.rom.begin(), p.rom.end(), bytes.begin());
            p.flags.resize(bytes.size(), 0);
        }
        uint8_t at(uint32_t o) { return mem.of(Target::ROM)[o]; }
    };

    TEST_CASE("opcode swap writes another member of the group")
    {
        Rom r;
        Rng rng(5);
        Op op{ .type = OpType::OpcodeSwap };
        std::set<uint8_t> seen;
        for (int i = 0; i < 40; i++)
        {
            r.mem.of(Target::ROM)[0] = 0xA9;
            auto w = apply_code_op(op, r.mem, r.p.flags, { 0 }, rng);
            REQUIRE(w.size() == 1);
            CHECK(w[0].old_value == 0xA9);
            CHECK(w[0].new_value != 0xA9);
            CHECK(has(swap_group(0xA9), w[0].new_value));
            seen.insert(w[0].new_value);
        }
        CHECK(seen.size() > 3);
        r.mem.of(Target::ROM)[0] = 0x20; // JSR: nothing to swap with
        CHECK(apply_code_op(op, r.mem, r.p.flags, { 0 }, rng).empty());
    }

    TEST_CASE("branch flip and NOP-out")
    {
        Rom r;
        Rng rng(1);
        auto w = apply_code_op({ .type = OpType::BranchFlip }, r.mem, r.p.flags, { 5 }, rng);
        REQUIRE(w.size() == 1);
        CHECK(r.at(5) == 0xF0);

        w = apply_code_op({ .type = OpType::NopOut }, r.mem, r.p.flags, { 0, 7 }, rng);
        CHECK(w.size() == 5); // LDA #$05 (two bytes) and LDA #$1234 as it ran, 16-bit (three)
        for (uint32_t o : { 0u, 1u, 7u, 8u, 9u })
            CHECK(r.at(o) == 0xEA);
        CHECK(r.at(2) == 0x8D);
        CHECK(r.at(10) == 0x77);
    }

    TEST_CASE("bytes nobody may touch are skipped, the whole instruction for a NOP-out")
    {
        Rom r;
        Rng rng(1);
        HotMask locked;
        locked.add_block(Target::ROM, 0); // bytes 0-15
        CHECK(apply_code_op({ .type = OpType::NopOut }, r.mem, r.p.flags, { 2 }, rng, &locked).empty());
        CHECK(r.at(2) == 0x8D);
    }

    TEST_CASE("role names")
    {
        for (auto role : { Role::Any, Role::Opcode, Role::Operand, Role::Code, Role::Data, Role::Dma, Role::Used })
            CHECK(parse_role(role_name(role)) == role);
        CHECK(!parse_role("pointer"));
    }
}
