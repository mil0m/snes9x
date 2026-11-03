#include "doctest/doctest.h"

#include "corrupt/change_risk.h"

#include <initializer_list>

using namespace corrupt;

namespace
{

// A LoROM bank $00 game; each case writes one function at offset 0 and asks where a change goes.
struct Game
{
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x8000, 0xFF);
    std::vector<uint8_t> flags = std::vector<uint8_t>(0x8000, 0);
    RomAddresses where;
    uint32_t next = 0;

    Game()
    {
        for (uint32_t c = 0; c < 8; c++)
            where.chunks.push_back(0x8000 + c * 0x1000);
    }
    // Instructions laid one after another from offset 0, run 8-bit.
    Game &code(std::initializer_list<std::initializer_list<uint8_t>> list)
    {
        for (auto &bytes : list)
        {
            uint32_t i = next;
            for (auto b : bytes)
            {
                rom[i] = b;
                flags[i] |= (i == next ? codeflag::Opcode : codeflag::Operand) | codeflag::Op8;
                i++;
            }
            next = i;
        }
        return *this;
    }
    FeedReading at(uint32_t offset)
    {
        auto body = function_body(rom, flags, 0, where);
        for (auto &o : change_options(body, rom, flags, where))
            if (o.at == offset)
                return feed_of(o, body, rom, flags, where);
        FAIL("no option at " << offset);
        return {};
    }
    FeedReading skip_at(uint32_t offset)
    {
        auto body = function_body(rom, flags, 0, where);
        for (auto o : change_options(body, rom, flags, where))
            if (o.at == offset && o.kind == ChangeKind::Skip)
                return feed_of(o, body, rom, flags, where);
        FAIL("no call at " << offset);
        return {};
    }
};

} // namespace

TEST_SUITE("change risk")
{
    TEST_CASE("a number stored into RAM is a variable; into a register, hardware")
    {
        Game g;
        g.code({ { 0xA9, 0x04 }, { 0x8D, 0x40, 0x0D }, { 0xA9, 0x80 }, { 0x8D, 0x00, 0x21 }, { 0x60 } });
        auto speed = g.at(0);
        CHECK(speed.feed == Feed::Variable);
        CHECK(speed.why == "stored into $7E:0D40");
        auto screen = g.at(5);
        CHECK(screen.feed == Feed::Hardware);
        CHECK(screen.why.find("INIDISP") != std::string::npos);
    }

    TEST_CASE("a number that becomes an index, a loop count, or is replaced unused")
    {
        Game a;
        a.code({ { 0xA9, 0x04 }, { 0xAA }, { 0xBD, 0x00, 0x90 }, { 0x60 } }); // LDA #4, TAX, LDA $9000,X
        CHECK(a.at(0).feed == Feed::Index);
        Game b;
        b.code({ { 0xA2, 0x08 }, { 0xBD, 0x00, 0x90 }, { 0x60 } }); // LDX #8, LDA $9000,X
        CHECK(b.at(0).feed == Feed::Index);
        Game c;
        c.code({ { 0xA0, 0x10 }, { 0x88 }, { 0xD0, 0xFD }, { 0x60 } }); // LDY #$10, DEY, BNE back
        CHECK(c.at(0).feed == Feed::LoopCount);
        Game d;
        d.code({ { 0xA9, 0x04 }, { 0xA9, 0x05 }, { 0x8D, 0x40, 0x0D }, { 0x60 } });
        CHECK(d.at(0).feed == Feed::Overwritten);
        Game e;
        e.code({ { 0xA9, 0x04 }, { 0x20, 0x00, 0x81 }, { 0x60 } }); // handed to a call
        CHECK(e.at(0).feed == Feed::Argument);
    }

    TEST_CASE("a compared number: a loop's bound when the branch goes back, a condition when forward")
    {
        Game a;
        a.code({ { 0xEA }, { 0xC9, 0x20 }, { 0x90, 0xFB }, { 0x60 } }); // NOP, CMP #$20, BCC back to the NOP
        CHECK(a.at(1).feed == Feed::LoopBound);
        Game b;
        b.code({ { 0xC9, 0x20 }, { 0xF0, 0x01 }, { 0xEA }, { 0x60 } }); // CMP #$20, BEQ over the NOP
        CHECK(b.at(0).feed == Feed::Condition);
    }

    TEST_CASE("a temporary byte, a DMA address, a value kept in the high byte")
    {
        Game a;
        a.code({ { 0xA9, 0x04 }, { 0x85, 0x02 }, { 0x60 } }); // STA $02
        CHECK(a.at(0).feed == Feed::Scratch);
        Game b;
        b.code({ { 0xA9, 0x04 }, { 0x85, 0x40 }, { 0x60 } }); // STA $40: a direct-page variable
        CHECK(b.at(0).feed == Feed::Variable);
        Game c;
        c.code({ { 0xA9, 0x04 }, { 0x8D, 0x02, 0x43 }, { 0x60 } }); // STA $4302
        CHECK(c.at(0).feed == Feed::Pointer);
        Game d;
        d.code({ { 0xA9, 0x04 }, { 0xEB }, { 0xA9, 0x05 }, { 0x60 } }); // LDA #4, XBA, LDA #5
        CHECK(d.at(0).feed != Feed::Overwritten);
    }

    TEST_CASE("a number stored where the function reads a pointer is a pointer")
    {
        Game g;
        g.code({ { 0xA9, 0x00 }, { 0x85, 0x10 }, { 0xB1, 0x10 }, { 0x60 } }); // LDA #0, STA $10, LDA ($10),Y
        CHECK(g.at(0).feed == Feed::Pointer);
    }

    TEST_CASE("math: memory changed in place, a loop's counter, the carry into a sum")
    {
        Game a;
        a.code({ { 0xEE, 0x40, 0x0D }, { 0x60 } }); // INC $0D40
        CHECK(a.at(0).feed == Feed::Variable);
        Game b;
        b.code({ { 0xEA }, { 0xE8 }, { 0xD0, 0xFC }, { 0x60 } }); // NOP, INX, BNE back
        CHECK(b.at(1).feed == Feed::LoopCount);
        Game c;
        c.code({ { 0x18 }, { 0x69, 0x02 }, { 0x8D, 0x40, 0x0D }, { 0x60 } }); // CLC, ADC #2, STA $0D40
        CHECK(c.at(0).feed == Feed::Variable);
    }

    TEST_CASE("branches: back is a loop, forward skips a block")
    {
        Game g;
        g.code({ { 0xEA }, { 0xD0, 0xFD }, { 0xF0, 0x01 }, { 0xEA }, { 0x60 } });
        CHECK(g.at(1).feed == Feed::LoopBranch);
        CHECK(g.at(3).feed == Feed::ForwardBranch);
    }

    TEST_CASE("a skipped call: does the caller read its result?")
    {
        Game a;
        a.code({ { 0x20, 0x00, 0x81 }, { 0xB0, 0x01 }, { 0xEA }, { 0x60 } }); // JSR, BCS: reads the carry
        CHECK(a.skip_at(0).feed == Feed::ResultUsed);
        Game b;
        b.code({ { 0x20, 0x00, 0x81 }, { 0x8D, 0x40, 0x0D }, { 0x60 } }); // JSR, STA: reads A
        CHECK(b.skip_at(0).feed == Feed::ResultUsed);
        Game c; // JSR, then everything set again
        c.code({ { 0x20, 0x00, 0x81 }, { 0xA9, 0x00 }, { 0xA2, 0x00 }, { 0xA0, 0x00 }, { 0x18 }, { 0x8D, 0x40, 0x0D },
                 { 0x60 } });
        CHECK(c.skip_at(0).feed == Feed::ResultUnused);
    }
}
