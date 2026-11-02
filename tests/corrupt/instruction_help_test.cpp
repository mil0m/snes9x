#include "doctest/doctest.h"

#include "corrupt/instruction_help.h"
#include "corrupt/opcodes.h"

#include <set>
#include <string>

using namespace corrupt;

TEST_SUITE("instruction help")
{
    TEST_CASE("every instruction the 65816 has is explained")
    {
        std::set<std::string> seen;
        for (int op = 0; op < 256; op++)
            seen.insert(op_info(uint8_t(op)).mnemonic);
        CHECK(seen.size() == 92);
        for (auto &m : seen)
        {
            INFO(m);
            auto text = explain_mnemonic(m);
            CHECK(text.size() > 10);
            CHECK(text.back() == '.');
        }
        CHECK(explain_mnemonic("XYZ").empty());
    }

    TEST_CASE("an instruction: what it does, then what its operand is")
    {
        auto adc = explain_instruction(0x69, "ADC #$01"); // immediate
        CHECK(adc.find("Adds") != std::string::npos);
        CHECK(adc.find("#$01 is the number itself: 1.") != std::string::npos);
        auto lda = explain_instruction(0xA9, "LDA #$012C"); // 16-bit immediate
        CHECK(lda.find("300") != std::string::npos);
        CHECK(explain_instruction(0x85, "STA $12").find("fast variables") != std::string::npos);
        CHECK(explain_instruction(0xBD, "LDA $1234,X").find("X picks") != std::string::npos);
        CHECK(explain_instruction(0xB1, "LDA ($10),Y").find("pointer") != std::string::npos);
        CHECK(explain_instruction(0x7C, "JMP ($9100,X)").find("jump table") != std::string::npos);
        auto bne = explain_instruction(0xD0, "BNE $9012");
        CHECK(bne.find("not equal") != std::string::npos);
        CHECK(bne.find("$9012") != std::string::npos);
        auto jsl = explain_instruction(0x22, "JSL $C30006");
        CHECK(jsl.find("Calls") != std::string::npos);
        CHECK(jsl.find("$C30006") != std::string::npos);
        // Nothing to say about an operand it doesn't have.
        auto clc = explain_instruction(0x18, "CLC");
        CHECK(clc.find('\n') == std::string::npos);
    }

    TEST_CASE("every kind of change says what it does to the game")
    {
        for (auto k : { ChangeKind::Number, ChangeKind::Branch, ChangeKind::Math, ChangeKind::Skip, ChangeKind::Redirect })
        {
            auto text = explain_change(k);
            CHECK(text.size() > 20);
            CHECK(text.back() == '.');
        }
        CHECK(explain_change(ChangeKind::Branch).find("decision") != std::string::npos);
    }
}
