#include "doctest/doctest.h"

#include "corrupt/function_body.h"

#include <initializer_list>

using namespace corrupt;

namespace
{

// A 64 KB ROM mapped like LoROM bank $00 then $01 ($8000-$FFFF each), with a code map the
// test fills in one instruction at a time, as the tracer would.
struct Game
{
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x10000, 0xFF);
    std::vector<uint8_t> flags = std::vector<uint8_t>(0x10000, 0);
    RomAddresses where;

    Game()
    {
        for (uint32_t c = 0; c < 16; c++)
            where.chunks.push_back(((c / 8) << 16) | (0x8000 + (c % 8) * 0x1000));
    }

    // An instruction at `at` that ran: its opcode and operand bytes marked. `wide`: an M/X
    // instruction run 16-bit (Op16), else run 8-bit (Op8) when it depends on a width.
    uint32_t put(uint32_t at, std::initializer_list<uint8_t> bytes, uint8_t width_flag = 0)
    {
        uint32_t i = at;
        for (auto b : bytes)
        {
            rom[i] = b;
            flags[i] |= (i == at ? codeflag::Opcode : codeflag::Operand) | width_flag;
            i++;
        }
        return i;
    }

    FunctionBody body(uint32_t entry, size_t max = FunctionBody::kMaxBytes) const
    {
        return function_body(rom, flags, entry, where, max);
    }
};

std::vector<uint32_t> starts(const FunctionBody &b)
{
    std::vector<uint32_t> out;
    for (auto &i : b.code)
        out.push_back(i.at);
    return out;
}

} // namespace

TEST_SUITE("function bodies")
{
    TEST_CASE("a straight function runs to its return")
    {
        Game g;
        uint32_t o = g.put(0x100, { 0xA5, 0x20 });        // LDA $20
        o = g.put(o, { 0x18 });                            // CLC
        o = g.put(o, { 0x69, 0x01 }, codeflag::Op8);       // ADC #$01
        o = g.put(o, { 0x85, 0x20 });                      // STA $20
        o = g.put(o, { 0x60 });                            // RTS
        g.put(o, { 0xEA });                                // NOP: after the return, not in it
        auto b = g.body(0x100);
        CHECK(starts(b) == std::vector<uint32_t>{ 0x100, 0x102, 0x103, 0x105, 0x107 });
        CHECK(b.bytes == 8);
        CHECK(b.runs() == std::vector<std::pair<uint32_t, uint32_t>>{ { 0x100, 0x107 } });
        CHECK(b.contains(0x104));
        CHECK_FALSE(b.contains(0x108));
        CHECK_FALSE(b.truncated);
        CHECK_FALSE(b.open_ended);
    }

    TEST_CASE("a branch is followed both ways, BRA only to its target")
    {
        Game g;
        uint32_t o = g.put(0x100, { 0xA5, 0x21 });  // LDA $21
        o = g.put(o, { 0xF0, 0x04 });               // BEQ +4 -> 0x108
        o = g.put(o, { 0xE6, 0x22 });               // INC $22
        o = g.put(o, { 0x80, 0x03 });               // BRA +3 -> 0x10B
        o = g.put(o, { 0xE6, 0x23 });               // 0x108: INC $23
        o = g.put(o, { 0x60 });                     // 0x10A: RTS
        g.put(0x10B, { 0x6B });                     // RTL: another way out
        auto b = g.body(0x100);
        CHECK(starts(b) == std::vector<uint32_t>{ 0x100, 0x102, 0x104, 0x106, 0x108, 0x10A, 0x10B });
        // A branch to a byte that never ran as code isn't followed.
        Game h;
        h.put(0x100, { 0xD0, 0x10 }); // BNE +16: never taken
        h.put(0x102, { 0x60 });
        CHECK(starts(h.body(0x100)) == std::vector<uint32_t>{ 0x100, 0x102 });
    }

    TEST_CASE("a backward branch loops without walking twice")
    {
        Game g;
        g.put(0x100, { 0xCA });       // DEX
        g.put(0x101, { 0xD0, 0xFD }); // BNE -3 -> 0x100
        g.put(0x103, { 0x60 });
        CHECK(starts(g.body(0x100)) == std::vector<uint32_t>{ 0x100, 0x101, 0x103 });
    }

    TEST_CASE("JMP $abs is followed in its bank, through the CPU address")
    {
        Game g;
        g.put(0x100, { 0x4C, 0x00, 0x90 }); // JMP $9000 -> offset 0x1000
        g.put(0x1000, { 0xE6, 0x20 });
        g.put(0x1002, { 0x60 });
        CHECK(starts(g.body(0x100)) == std::vector<uint32_t>{ 0x100, 0x1000, 0x1002 });

        // In bank $01 the same operand means $01:9000.
        g.put(0x8100, { 0x4C, 0x00, 0x90 });
        g.put(0x9000, { 0x6B });
        CHECK(starts(g.body(0x8100)) == std::vector<uint32_t>{ 0x8100, 0x9000 });

        // Without addresses a JMP can't be followed.
        auto b = function_body(g.rom, g.flags, 0x100, RomAddresses{});
        CHECK(starts(b) == std::vector<uint32_t>{ 0x100 });
    }

    TEST_CASE("a call is stepped over when what follows it ran; the callee isn't in the body")
    {
        Game g;
        g.put(0x100, { 0x20, 0x00, 0x82 });       // JSR $8200
        g.put(0x103, { 0x22, 0x00, 0x83, 0x00 }); // JSL $00:8300
        g.put(0x107, { 0x60 });
        g.put(0x200, { 0x60 });
        g.put(0x300, { 0x6B });
        auto b = g.body(0x100);
        CHECK(starts(b) == std::vector<uint32_t>{ 0x100, 0x103, 0x107 });
        CHECK_FALSE(b.contains(0x200));

        // A callee that reads data after its call: that byte never ran, so the walk stops.
        Game h;
        h.put(0x100, { 0x20, 0x00, 0x82 }); // JSR $8200
        h.rom[0x103] = 0x00;                // inline data
        h.put(0x104, { 0x60 });
        CHECK(starts(h.body(0x100)) == std::vector<uint32_t>{ 0x100 });
    }

    TEST_CASE("indirect and long jumps end the walk, open-ended")
    {
        for (auto op : { uint8_t(0x6C), uint8_t(0x7C), uint8_t(0xDC) })
        {
            Game g;
            g.put(0x100, { 0xA2, 0x02 }, codeflag::Op8); // LDX #$02
            g.put(0x102, { op, 0x00, 0x90 });
            auto b = g.body(0x100);
            CHECK(starts(b) == std::vector<uint32_t>{ 0x100, 0x102 });
            CHECK(b.open_ended);
        }
        Game g;
        g.put(0x100, { 0x5C, 0x00, 0x90, 0x01 }); // JML $01:9000: a tail jump, not followed
        auto b = g.body(0x100);
        CHECK(starts(b) == std::vector<uint32_t>{ 0x100 });
        CHECK_FALSE(b.open_ended);
    }

    TEST_CASE("widths: 16-bit immediates are longer; both widths keep the longer form")
    {
        Game g;
        g.put(0x100, { 0xC2, 0x20 });                     // REP #$20
        g.put(0x102, { 0xA9, 0x34, 0x12 }, codeflag::Op16); // LDA #$1234
        g.put(0x105, { 0x60 });
        auto b = g.body(0x100);
        CHECK(starts(b) == std::vector<uint32_t>{ 0x100, 0x102, 0x105 });
        CHECK(b.code[1].length == 3);
        CHECK_FALSE(b.code[1].both_widths);

        Game h; // called with M at 8 and at 16 bits: ADC # is 2 or 3 bytes
        h.put(0x100, { 0x69, 0x01, 0xEA }, codeflag::Op8 | codeflag::Op16);
        h.flags[0x102] = codeflag::Opcode | codeflag::Operand; // the NOP after the 8-bit form
        h.put(0x103, { 0x60 });
        auto hb = h.body(0x100);
        CHECK(hb.code[0].both_widths);
        CHECK(hb.code[0].length == 3);
        CHECK(hb.contains(0x103));
    }

    TEST_CASE("an M-wide instruction of the same length either way, run only 16-bit, is followed")
    {
        Game g;
        g.put(0x100, { 0xC2, 0x20 });                       // REP #$20
        g.put(0x102, { 0x48 }, codeflag::Op16);             // PHA (16-bit push, still 1 byte)
        g.put(0x103, { 0xBD, 0x01, 0x18 }, codeflag::Op16); // LDA $1801,X
        g.put(0x106, { 0x60 });
        CHECK(starts(g.body(0x100)) == std::vector<uint32_t>{ 0x100, 0x102, 0x103, 0x106 });
    }

    TEST_CASE("an unknown entry has no body; the size cap truncates")
    {
        Game g;
        CHECK(g.body(0x100).code.empty());
        for (uint32_t o = 0x100; o < 0x200; o++)
            g.put(o, { 0xEA });
        auto b = g.body(0x100, 64);
        CHECK(b.truncated);
        CHECK(b.bytes <= 64);
    }

    TEST_CASE("a shared tail belongs to both functions; bodies for a list of functions")
    {
        Game g;
        g.put(0x100, { 0xE6, 0x20 }); // A: INC $20, falls into B
        g.put(0x102, { 0xE6, 0x21 }); // B
        g.put(0x104, { 0x60 });
        std::vector<Function> fs{ { 0x100 }, { 0x102 }, { 0x500 } };
        auto bodies = function_bodies(g.rom, g.flags, fs, g.where);
        CHECK(bodies.at(0x100).bytes == 5);
        CHECK(bodies.at(0x102).bytes == 3);
        CHECK(bodies.at(0x500).code.empty());
    }
}
