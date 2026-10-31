#include "doctest/doctest.h"
#include "corrupt/opcodes.h"

#include <set>
#include <string>
#include <vector>

using namespace corrupt;

namespace
{
std::string dis(std::vector<uint8_t> bytes, bool m8 = true, bool x8 = true, std::optional<uint32_t> at = std::nullopt)
{
    return disassemble(bytes, m8, x8, at).text;
}
} // namespace

TEST_SUITE("opcodes")
{
    TEST_CASE("lengths follow the addressing mode, and M or X for immediates")
    {
        CHECK(op_length(0xEA, true, true) == 1);   // NOP
        CHECK(op_length(0xA9, true, true) == 2);   // LDA #
        CHECK(op_length(0xA9, false, true) == 3);  // LDA # with a 16-bit A
        CHECK(op_length(0xA9, true, false) == 2);  // X doesn't matter for LDA
        CHECK(op_length(0xA2, true, false) == 3);  // LDX # with 16-bit index registers
        CHECK(op_length(0xA2, false, true) == 2);
        CHECK(op_length(0xC2, false, false) == 2); // REP is always 8-bit
        CHECK(op_length(0x5C, true, true) == 4);   // JML long
        CHECK(op_length(0x54, true, true) == 3);   // MVN
        CHECK(op_length(0x82, true, true) == 3);   // BRL
        CHECK(op_length(0xF4, true, true) == 3);   // PEA
        CHECK(op_length(0x00, true, true) == 2);   // BRK has a signature byte
        static_assert(op_length(0x69, false, false) == 3, "usable at compile time");
    }

    TEST_CASE("what decides an instruction's width")
    {
        CHECK(op_width(0xA9) == WidthBy::M);  // LDA
        CHECK(op_width(0x8D) == WidthBy::M);  // STA abs
        CHECK(op_width(0x9C) == WidthBy::M);  // STZ abs
        CHECK(op_width(0x0A) == WidthBy::M);  // ASL A
        CHECK(op_width(0x48) == WidthBy::M);  // PHA
        CHECK(op_width(0x8A) == WidthBy::M);  // TXA: the result is A
        CHECK(op_width(0xA2) == WidthBy::X);  // LDX
        CHECK(op_width(0xE8) == WidthBy::X);  // INX
        CHECK(op_width(0xAA) == WidthBy::X);  // TAX: the result is X
        CHECK(op_width(0xDA) == WidthBy::X);  // PHX
        CHECK(op_width(0x4C) == WidthBy::None); // JMP
        CHECK(op_width(0xC2) == WidthBy::None); // REP
        CHECK(op_width(0xEB) == WidthBy::None); // XBA
        CHECK(op_width(0x54) == WidthBy::None); // MVN
    }

    TEST_CASE("every opcode has a mnemonic and a class")
    {
        std::set<std::string> names;
        for (int op = 0; op < 256; op++)
        {
            auto &info = op_info(uint8_t(op));
            CHECK(std::string(info.mnemonic).size() == 3);
            names.insert(info.mnemonic);
        }
        CHECK(names.size() == 92); // the 65816's instruction set (JML and JSL counted on their own)
        CHECK(op_class(0x69) == OpClass::Arith);  // ADC
        CHECK(op_class(0xE8) == OpClass::Arith);  // INX
        CHECK(op_class(0x29) == OpClass::Logic);  // AND
        CHECK(op_class(0x24) == OpClass::Logic);  // BIT
        CHECK(op_class(0x0A) == OpClass::Shift);  // ASL
        CHECK(op_class(0xC9) == OpClass::Compare);
        CHECK(op_class(0xA9) == OpClass::Load);
        CHECK(op_class(0x9C) == OpClass::Store);  // STZ
        CHECK(op_class(0xAA) == OpClass::Transfer);
        CHECK(op_class(0xEB) == OpClass::Transfer); // XBA
        CHECK(op_class(0xD0) == OpClass::Branch);
        CHECK(op_class(0x82) == OpClass::Branch); // BRL
        CHECK(op_class(0x22) == OpClass::Jump);   // JSL
        CHECK(op_class(0x6B) == OpClass::Return); // RTL
        CHECK(op_class(0x48) == OpClass::Stack);
        CHECK(op_class(0xF4) == OpClass::Stack);  // PEA
        CHECK(op_class(0x18) == OpClass::Flags);  // CLC
        CHECK(op_class(0xC2) == OpClass::Width);  // REP
        CHECK(op_class(0xFB) == OpClass::Width);  // XCE
        CHECK(op_class(0x44) == OpClass::Block);  // MVP
        CHECK(op_class(0xDB) == OpClass::Misc);   // STP
        CHECK(std::string(class_name(OpClass::Compare)) == "compare");
        CHECK(class_from_name("branch") == OpClass::Branch);
        CHECK(!class_from_name("nonsense"));
    }

    TEST_CASE("disassembly of each addressing mode")
    {
        CHECK(dis({ 0xEA }) == "NOP");
        CHECK(dis({ 0x0A }) == "ASL A");
        CHECK(dis({ 0xA9, 0x05 }) == "LDA #$05");
        CHECK(dis({ 0xA9, 0x34, 0x12 }, false) == "LDA #$1234");
        CHECK(dis({ 0xA2, 0x34, 0x12 }, true, false) == "LDX #$1234");
        CHECK(dis({ 0xC2, 0x30 }) == "REP #$30");
        CHECK(dis({ 0xA5, 0x12 }) == "LDA $12");
        CHECK(dis({ 0xB5, 0x12 }) == "LDA $12,X");
        CHECK(dis({ 0xB6, 0x12 }) == "LDX $12,Y");
        CHECK(dis({ 0xB2, 0x12 }) == "LDA ($12)");
        CHECK(dis({ 0xA1, 0x12 }) == "LDA ($12,X)");
        CHECK(dis({ 0xB1, 0x12 }) == "LDA ($12),Y");
        CHECK(dis({ 0xA7, 0x12 }) == "LDA [$12]");
        CHECK(dis({ 0xB7, 0x12 }) == "LDA [$12],Y");
        CHECK(dis({ 0xA3, 0x03 }) == "LDA $03,S");
        CHECK(dis({ 0xB3, 0x03 }) == "LDA ($03,S),Y");
        CHECK(dis({ 0xAD, 0x50, 0x04 }) == "LDA $0450");
        CHECK(dis({ 0xBD, 0x50, 0x04 }) == "LDA $0450,X");
        CHECK(dis({ 0xB9, 0x50, 0x04 }) == "LDA $0450,Y");
        CHECK(dis({ 0xAF, 0x56, 0x34, 0x12 }) == "LDA $123456");
        CHECK(dis({ 0xBF, 0x56, 0x34, 0x12 }) == "LDA $123456,X");
        CHECK(dis({ 0x6C, 0x34, 0x12 }) == "JMP ($1234)");
        CHECK(dis({ 0x7C, 0x34, 0x12 }) == "JMP ($1234,X)");
        CHECK(dis({ 0xDC, 0x34, 0x12 }) == "JML [$1234]");
        CHECK(dis({ 0x22, 0x56, 0x34, 0x12 }) == "JSL $123456");
        CHECK(dis({ 0xF4, 0x34, 0x12 }) == "PEA $1234");
        CHECK(dis({ 0xD4, 0x12 }) == "PEI ($12)");
        CHECK(dis({ 0x54, 0x7F, 0x7E }) == "MVN $7E,$7F"); // source first, as written; the bytes are dest, source
        CHECK(dis({ 0x00, 0x00 }) == "BRK #$00");
    }

    TEST_CASE("branches show their target when the address is known")
    {
        CHECK(dis({ 0xD0, 0x0C }) == "BNE +12");
        CHECK(dis({ 0xD0, 0xF9 }) == "BNE -7");
        CHECK(dis({ 0xD0, 0x0C }, true, true, 0xC21A3F) == "BNE $1A4D");
        CHECK(dis({ 0xD0, 0xF9 }, true, true, 0x008016) == "BNE $8011");
        CHECK(dis({ 0x80, 0xFE }, true, true, 0x00FFFF) == "BRA $FFFF");
        CHECK(dis({ 0x82, 0x00, 0x10 }, true, true, 0x80FFF0) == "BRL $0FF3"); // wraps in the bank
        CHECK(dis({ 0x62, 0x10, 0x00 }, true, true, 0x808000) == "PER $8013");
    }

    TEST_CASE("missing bytes don't read past the end")
    {
        auto d = disassemble(std::vector<uint8_t>{ 0xAD, 0x50 }, true, true, std::nullopt);
        CHECK(d.length == 3);
        CHECK(d.text == "LDA $??50");
        CHECK(disassemble(std::vector<uint8_t>{}, true, true, std::nullopt).text.empty());
    }
}
