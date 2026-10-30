#include "doctest/doctest.h"
#include "fake_memory.h"
#include "corrupt/ops.h"

#include <algorithm>
#include <bit>

using namespace corrupt;

namespace
{
const AddressRange kWram{ Target::WRAM, 0x0000, 0x00FF };

// Every event must describe a real change that is now in memory.
void check_events_match(const std::vector<WriteEvent> &events, FakeMemory &mem)
{
    for (auto &e : events)
    {
        CHECK(e.old_value != e.new_value);
        CHECK(mem.read(e.target, e.offset) == e.new_value);
    }
}
} // namespace

TEST_SUITE("add")
{
    TEST_CASE("adds an exact delta")
    {
        FakeMemory mem;
        mem.of(Target::WRAM)[0x10] = 0x20;
        Op op{ .type = OpType::Add, .min = 5, .max = 5 };
        OpState state;
        Rng rng(1);
        auto events = apply_op(op, state, mem, kWram, { 0x10 }, rng);
        CHECK(mem.of(Target::WRAM)[0x10] == 0x25);
        REQUIRE(events.size() == 1);
        CHECK(events[0].old_value == 0x20);
        CHECK(events[0].new_value == 0x25);
    }

    TEST_CASE("wraps by default and clamps on request")
    {
        FakeMemory mem;
        mem.of(Target::WRAM)[0] = 0xFE;
        mem.of(Target::WRAM)[1] = 0xFE;
        mem.of(Target::WRAM)[2] = 0x01;
        OpState state;
        Rng rng(1);
        apply_op({ .type = OpType::Add, .min = 5, .max = 5 }, state, mem, kWram, { 0 }, rng);
        apply_op({ .type = OpType::Add, .min = 5, .max = 5, .clamp = true }, state, mem, kWram, { 1 }, rng);
        apply_op({ .type = OpType::Add, .min = -5, .max = -5, .clamp = true }, state, mem, kWram, { 2 }, rng);
        CHECK(mem.of(Target::WRAM)[0] == 0x03);
        CHECK(mem.of(Target::WRAM)[1] == 0xFF);
        CHECK(mem.of(Target::WRAM)[2] == 0x00);
    }

    TEST_CASE("a zero delta writes nothing and logs nothing")
    {
        FakeMemory mem;
        OpState state;
        Rng rng(1);
        auto events = apply_op({ .type = OpType::Add, .min = 0, .max = 0 }, state, mem, kWram, { 3 }, rng);
        CHECK(events.empty());
        CHECK(mem.writes == 0);
    }
}

TEST_SUITE("random")
{
    TEST_CASE("writes values inside the range")
    {
        FakeMemory mem;
        OpState state;
        Rng rng(5);
        std::vector<uint32_t> addrs;
        for (uint32_t a = 0; a < 200; a++)
            addrs.push_back(a);
        auto events = apply_op({ .type = OpType::Random, .min = 0x10, .max = 0x1F }, state, mem, kWram, addrs, rng);
        for (uint32_t a = 0; a < 200; a++)
        {
            CHECK(mem.of(Target::WRAM)[a] >= 0x10);
            CHECK(mem.of(Target::WRAM)[a] <= 0x1F);
        }
        CHECK(events.size() == 200); // all started at 0, outside the range
        check_events_match(events, mem);
    }
}

TEST_SUITE("bitflip")
{
    TEST_CASE("mask XORs exactly")
    {
        FakeMemory mem;
        mem.of(Target::WRAM)[7] = 0b1010'0000;
        OpState state;
        Rng rng(1);
        apply_op({ .type = OpType::Bitflip, .mask = 0b1000'0001 }, state, mem, kWram, { 7 }, rng);
        CHECK(mem.of(Target::WRAM)[7] == 0b0010'0001);
    }

    TEST_CASE("flips exactly `bits` distinct bits")
    {
        for (int bits = 1; bits <= 8; bits++)
        {
            FakeMemory mem;
            OpState state;
            Rng rng{ uint64_t(bits) };
            apply_op({ .type = OpType::Bitflip, .bits = bits }, state, mem, kWram, { 0 }, rng);
            CHECK(std::popcount(mem.of(Target::WRAM)[0]) == bits);
        }
    }
}

TEST_SUITE("swap")
{
    TEST_CASE("fixed addresses, width 2")
    {
        FakeMemory mem;
        auto &w = mem.of(Target::WRAM);
        w[0x0F00] = 0x11; w[0x0F01] = 0x22;
        w[0x0F02] = 0x33; w[0x0F03] = 0x44;
        Op op{ .type = OpType::Swap, .width = 2, .a = 0x0F00u, .b = 0x0F02u };
        OpState state;
        Rng rng(1);
        auto events = apply_op(op, state, mem, { Target::WRAM, 0, 0x1FFFF }, {}, rng);
        CHECK(w[0x0F00] == 0x33);
        CHECK(w[0x0F01] == 0x44);
        CHECK(w[0x0F02] == 0x11);
        CHECK(w[0x0F03] == 0x22);
        CHECK(events.size() == 4);
        check_events_match(events, mem);
    }

    TEST_CASE("random swaps keep the bytes, only move them")
    {
        FakeMemory mem;
        auto &w = mem.of(Target::WRAM);
        for (uint32_t a = 0; a <= 0xFF; a++)
            w[a] = uint8_t(a);
        auto before = std::vector<uint8_t>(w.begin(), w.begin() + 0x100);
        OpState state;
        Rng rng(77);
        auto events = apply_op({ .type = OpType::Swap }, state, mem, kWram, { 1, 50, 99 }, rng);
        auto after = std::vector<uint8_t>(w.begin(), w.begin() + 0x100);
        CHECK(after != before);
        std::sort(after.begin(), after.end());
        std::sort(before.begin(), before.end());
        CHECK(after == before);
        check_events_match(events, mem);
    }

    TEST_CASE("swapping an address with itself changes nothing")
    {
        FakeMemory mem;
        mem.of(Target::WRAM)[5] = 9;
        OpState state;
        Rng rng(1);
        auto events = apply_op({ .type = OpType::Swap, .a = 5u, .b = 5u }, state, mem, kWram, {}, rng);
        CHECK(events.empty());
        CHECK(mem.of(Target::WRAM)[5] == 9);
    }
}

TEST_SUITE("copy")
{
    TEST_CASE("copies a block, across targets")
    {
        FakeMemory mem;
        auto &cg = mem.of(Target::CGRAM);
        cg[0x10] = 0xAA; cg[0x11] = 0xBB; cg[0x12] = 0xCC;
        Op op{ .type = OpType::Copy, .from = AddressRange{ Target::CGRAM, 0x10, 0x12 }, .length = 3 };
        OpState state;
        Rng rng(1);
        auto events = apply_op(op, state, mem, kWram, { 0x40 }, rng);
        auto &w = mem.of(Target::WRAM);
        CHECK(w[0x40] == 0xAA);
        CHECK(w[0x41] == 0xBB);
        CHECK(w[0x42] == 0xCC);
        check_events_match(events, mem);
    }

    TEST_CASE("overlapping copy reads the source before writing")
    {
        FakeMemory mem;
        auto &w = mem.of(Target::WRAM);
        w[0] = 1; w[1] = 2; w[2] = 3; w[3] = 4;
        Op op{ .type = OpType::Copy, .from = AddressRange{ Target::WRAM, 0, 3 }, .length = 4 };
        OpState state;
        Rng rng(1);
        apply_op(op, state, mem, kWram, { 1 }, rng);
        CHECK(w[1] == 1);
        CHECK(w[2] == 2);
        CHECK(w[3] == 3);
        CHECK(w[4] == 4);
    }

    TEST_CASE("a copy that would run past the target is clipped")
    {
        FakeMemory mem;
        Op op{ .type = OpType::Copy, .from = AddressRange{ Target::WRAM, 0, 7 }, .length = 8 };
        mem.of(Target::WRAM).assign(mem.of(Target::WRAM).size(), 0);
        for (int i = 0; i < 8; i++)
            mem.of(Target::WRAM)[i] = uint8_t(i + 1);
        OpState state;
        Rng rng(1);
        apply_op(op, state, mem, { Target::WRAM, 0, 0x1FFFF }, { 0x1FFFD }, rng); // no throw
        CHECK(mem.of(Target::WRAM)[0x1FFFF] == 3);
    }
}

TEST_SUITE("freeze")
{
    TEST_CASE("holds a fixed value")
    {
        FakeMemory mem;
        OpState state;
        Rng rng(1);
        Op op{ .type = OpType::Freeze, .value = uint8_t(0x63) };
        apply_op(op, state, mem, kWram, { 0x20 }, rng);
        CHECK(mem.of(Target::WRAM)[0x20] == 0x63);
        mem.of(Target::WRAM)[0x20] = 0x01; // the game changes it
        apply_op(op, state, mem, kWram, { 0x20 }, rng);
        CHECK(mem.of(Target::WRAM)[0x20] == 0x63);
    }

    TEST_CASE("without a value it holds whatever was there when first hit")
    {
        FakeMemory mem;
        mem.of(Target::WRAM)[0x30] = 0x07;
        OpState state;
        Rng rng(1);
        Op op{ .type = OpType::Freeze };
        auto first = apply_op(op, state, mem, kWram, { 0x30 }, rng);
        CHECK(first.empty());
        mem.of(Target::WRAM)[0x30] = 0x99;
        apply_op(op, state, mem, kWram, { 0x30 }, rng);
        CHECK(mem.of(Target::WRAM)[0x30] == 0x07);
    }
}

TEST_SUITE("shuffle")
{
    TEST_CASE("permutes a window, clipped to the range")
    {
        FakeMemory mem;
        auto &w = mem.of(Target::WRAM);
        for (int i = 0; i < 8; i++)
            w[0xF8 + i] = uint8_t(i + 1);
        w[0x100] = 0xEE; // just outside the range
        OpState state;
        Rng rng(3);
        auto events = apply_op({ .type = OpType::Shuffle, .window = 16 }, state, mem, kWram, { 0xF8 }, rng);
        std::vector<uint8_t> win(w.begin() + 0xF8, w.begin() + 0x100);
        auto sorted = win;
        std::sort(sorted.begin(), sorted.end());
        CHECK(sorted == std::vector<uint8_t>{ 1, 2, 3, 4, 5, 6, 7, 8 });
        CHECK(w[0x100] == 0xEE);
        check_events_match(events, mem);
    }
}

// Like real CGRAM: the high byte of each 15-bit colour has no bit 7.
struct MaskingMemory : FakeMemory
{
    void write(Target t, uint32_t offset, uint8_t value) override
    {
        if (t == Target::CGRAM && (offset & 1))
            value &= 0x7F;
        FakeMemory::write(t, offset, value);
    }
};

TEST_CASE("events record what memory actually holds, not what was attempted")
{
    MaskingMemory mem;
    OpState state;
    Rng rng(1);
    const AddressRange cg{ Target::CGRAM, 0, 0x1FF };

    auto events = apply_op({ .type = OpType::Random, .min = 0xFF, .max = 0xFF }, state, mem, cg, { 1 }, rng);
    REQUIRE(events.size() == 1);
    CHECK(events[0].new_value == 0x7F);

    // A write the hardware drops entirely is not a change at all.
    events = apply_op({ .type = OpType::Random, .min = 0x80, .max = 0x80 }, state, mem, cg, { 3 }, rng);
    CHECK(events.empty());
}

TEST_CASE("same seed gives the same result")
{
    FakeMemory a, b;
    OpState sa, sb;
    Rng ra(99), rb(99);
    Op op{ .type = OpType::Add, .min = -40, .max = 40 };
    apply_op(op, sa, a, kWram, { 1, 2, 3, 4, 5 }, ra);
    apply_op(op, sb, b, kWram, { 1, 2, 3, 4, 5 }, rb);
    CHECK(a.of(Target::WRAM) == b.of(Target::WRAM));
}

TEST_SUITE("validation")
{
    FakeMemory mem;

    TEST_CASE("valid ops pass")
    {
        CHECK_FALSE(validate_op({ .type = OpType::Add, .min = -3, .max = 3 }, kWram, mem));
        CHECK_FALSE(validate_op({ .type = OpType::Random, .min = 0, .max = 255 }, kWram, mem));
        CHECK_FALSE(validate_op({ .type = OpType::Swap, .width = 2, .a = 0u, .b = 0x10u }, kWram, mem));
    }

    TEST_CASE("bad parameters are named")
    {
        auto err = validate_op({ .type = OpType::Random, .min = 0, .max = 300 }, kWram, mem);
        REQUIRE(err);
        CHECK(err->find("max") != std::string::npos);

        CHECK(validate_op({ .type = OpType::Add, .min = 3, .max = -3 }, kWram, mem));
        CHECK(validate_op({ .type = OpType::Bitflip, .bits = 9 }, kWram, mem));
        CHECK(validate_op({ .type = OpType::Swap, .width = 3 }, kWram, mem));
        CHECK(validate_op({ .type = OpType::Swap, .a = 0u }, kWram, mem)); // a without b
        CHECK(validate_op({ .type = OpType::Shuffle, .window = 1 }, kWram, mem));
        CHECK(validate_op({ .type = OpType::Copy }, kWram, mem)); // no source
        CHECK(validate_op({ .type = OpType::Copy, .from = AddressRange{ Target::WRAM, 0, 3 }, .length = 5 }, kWram, mem));
    }

    TEST_CASE("width-2 swap at the last byte is rejected")
    {
        auto err = validate_op({ .type = OpType::Swap, .width = 2, .a = 0u, .b = 0x1FFFFu },
                               { Target::WRAM, 0, 0x1FFFF }, mem);
        CHECK(err.has_value());
    }
}
