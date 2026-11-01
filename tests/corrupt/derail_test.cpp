#include "doctest/doctest.h"
#include "corrupt/derail.h"

using namespace corrupt;
namespace cf = codeflag;

TEST_SUITE("derail")
{
    TEST_CASE("where the CPU can't be running the game's own code")
    {
        // Open bus and I/O, whatever the byte.
        CHECK(landing(false, false, 0, 0xEA) == Landing::OpenBus);
        // A known opcode, still unchanged: fine, even a BRK the game uses on purpose.
        CHECK(landing(true, true, cf::Opcode, 0xA9) == Landing::None);
        CHECK(landing(true, true, cf::Opcode | cf::Data, 0x00) == Landing::None);
        // Known only as an operand: the middle of an instruction. Known as both: fine.
        CHECK(landing(true, true, cf::Operand | cf::Op16, 0xA9) == Landing::MidInstruction);
        CHECK(landing(true, true, cf::Operand | cf::Opcode, 0xA9) == Landing::None);
        // Known only as a DMA source: not enough (FE4 DMAs code to RAM and runs it from ROM too).
        CHECK(landing(true, true, cf::Dma, 0x12) == Landing::None);
        // Known only as data, or never seen: the game reads its code as data, and visits new
        // code all the time, so only the opcodes no game runs by accident count.
        CHECK(landing(true, true, cf::Data, 0xA9) == Landing::None);
        CHECK(landing(true, true, 0, 0xA9) == Landing::None);
        CHECK(landing(true, true, 0, 0x00) == Landing::Brk);
        CHECK(landing(true, true, cf::Data, 0xDB) == Landing::Stop);
        CHECK(landing(true, true, 0, 0x42) == Landing::Wdm);
        CHECK(landing(true, true, 0, 0x02) == Landing::None); // COP: some games use it
        // In RAM there's no map: the same three opcodes.
        CHECK(landing(true, false, 0, 0x00) == Landing::Brk);
        CHECK(landing(true, false, 0, 0xA9) == Landing::None);
        // An opcode that ran here but has been changed since is not the game's any more.
        CHECK(landing(true, true, cf::Opcode | cf::Changed, 0x00) == Landing::Brk);
        CHECK(landing(true, true, cf::Opcode | cf::Changed, 0xDB) == Landing::Stop);
        CHECK(landing(true, true, cf::Opcode | cf::Changed, 0xEA) == Landing::None);
        CHECK(std::string(landing_name(Landing::MidInstruction)) == "mid_instruction");
    }

    TEST_CASE("a landing counts only just after a changed byte was used")
    {
        DerailWatch w;
        w.step();
        CHECK(!w.open()); // nothing changed was used yet
        w.land(Landing::Brk, 0x808000);
        CHECK(!w.derailed());

        w.used(0x1234);
        CHECK(w.open());
        for (uint32_t i = 0; i < DerailWatch::kWindow; i++)
            w.step();
        CHECK(w.open()); // still within the window
        w.step();
        CHECK(!w.open());
        w.land(Landing::MidInstruction, 0x808000);
        CHECK(!w.derailed());
        CHECK(!w.take());
    }

    TEST_CASE("the note names the changed bytes used in the window, newest first, once each")
    {
        DerailWatch w;
        w.used(0x10);
        for (uint32_t i = 0; i < DerailWatch::kWindow + 1; i++)
            w.step(); // 0x10 drops out of the window
        w.used(0x20);
        w.step();
        w.used(0x30);
        w.used(0x20);
        w.step();
        w.land(Landing::MidInstruction, 0x1C0ABCD);
        CHECK(w.derailed());
        CHECK(!w.open());
        w.land(Landing::Brk, 0x808000); // only the first landing in a frame
        auto n = w.take();
        REQUIRE(n);
        CHECK(n->kind == Landing::MidInstruction);
        CHECK(n->pc == 0xC0ABCD);
        CHECK(n->culprits == std::vector<uint32_t>{ 0x20, 0x30 });
        // Taken: the window is gone with it.
        CHECK(!w.derailed());
        CHECK(!w.open());
        CHECK(!w.take());
    }

    TEST_CASE("the ring keeps the last few changed bytes")
    {
        DerailWatch w;
        for (uint32_t i = 0; i < DerailWatch::kRing + 3; i++)
            w.used(i);
        w.step();
        w.land(Landing::Stop, 0);
        auto n = w.take();
        REQUIRE(n);
        REQUIRE(n->culprits.size() == DerailWatch::kRing);
        CHECK(n->culprits.front() == DerailWatch::kRing + 2);
        CHECK(n->culprits.back() == 3);
    }

    TEST_CASE("clear forgets the window and the note")
    {
        DerailWatch w;
        w.used(5);
        w.land(Landing::Brk, 0);
        CHECK(w.derailed());
        w.clear();
        CHECK(!w.derailed());
        CHECK(!w.open());
    }
}

TEST_CASE("the changed bit is never saved or counted")
{
    CodeMap map(16);
    map.mark(3, cf::Changed);
    map.mark(4, cf::Opcode | cf::Changed);
    CHECK(map.coverage().seen == 1);
    std::string error;
    auto back = decode_code_map(encode_code_map(map), error);
    REQUIRE(back);
    CHECK(back->at(3) == 0);
    CHECK(back->at(4) == cf::Opcode);
}
