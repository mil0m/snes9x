#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The 65816's instruction set, for the code map: how long each instruction is at the
// width it runs at, what kind of instruction it is, and a small disassembler for the
// memory viewer's hover text. Everything the tracer needs is constexpr, so its
// per-opcode wrappers get lengths as compile-time constants.
namespace corrupt
{

enum class Mode : uint8_t
{
    Implied,     // NOP
    Accumulator, // ASL A
    ImmM,        // LDA #$12 / #$1234: 8 or 16 bits by M
    ImmX,        // LDX #$12 / #$1234: by X
    Imm8,        // REP #$30, SEP, BRK, COP, WDM: always one byte
    Direct,      // $12
    DirectX,     // $12,X
    DirectY,     // $12,Y
    DirectInd,   // ($12)
    DirectIndX,  // ($12,X)
    DirectIndY,  // ($12),Y
    DirectLong,  // [$12]
    DirectLongY, // [$12],Y
    Stack,       // $12,S
    StackIndY,   // ($12,S),Y
    Absolute,    // $1234
    AbsoluteX,   // $1234,X
    AbsoluteY,   // $1234,Y
    AbsInd,      // ($1234)   JMP
    AbsIndX,     // ($1234,X) JMP, JSR
    AbsLongInd,  // [$1234]   JML
    Long,        // $123456
    LongX,       // $123456,X
    Relative,    // BNE: one signed byte
    RelLong,     // BRL, PER: two
    Block,       // MVN, MVP: two banks
};

// What kind of instruction it is: what a code-aware rule may swap it for, and what the
// class filters pick by.
enum class OpClass : uint8_t
{
    Arith,    // ADC SBC INC DEC INX INY DEX DEY
    Logic,    // AND ORA EOR BIT TRB TSB
    Shift,    // ASL LSR ROL ROR
    Compare,  // CMP CPX CPY
    Load,     // LDA LDX LDY
    Store,    // STA STX STY STZ
    Transfer, // TAX TXA ... XBA TCD
    Branch,   // BNE BEQ ... BRA BRL
    Jump,     // JMP JML JSR JSL
    Return,   // RTS RTL RTI
    Stack,    // PHA PLA ... PEA PEI PER
    Flags,    // CLC SEC CLI SEI CLD SED CLV
    Width,    // REP SEP XCE: change how everything after them runs
    Block,    // MVN MVP
    Misc,     // NOP WDM BRK COP WAI STP
};
constexpr int kOpClassCount = 15;

// What decides an instruction's width: the accumulator's M flag, the index registers' X
// flag, or nothing (jumps, branches, REP...).
enum class WidthBy : uint8_t
{
    None,
    M,
    X,
};

struct OpInfo
{
    const char *mnemonic;
    Mode mode;
};

// clang-format off
inline constexpr OpInfo kOpcodes[256] = {
    // 0x00
    {"BRK", Mode::Imm8}, {"ORA", Mode::DirectIndX}, {"COP", Mode::Imm8}, {"ORA", Mode::Stack},
    {"TSB", Mode::Direct}, {"ORA", Mode::Direct}, {"ASL", Mode::Direct}, {"ORA", Mode::DirectLong},
    {"PHP", Mode::Implied}, {"ORA", Mode::ImmM}, {"ASL", Mode::Accumulator}, {"PHD", Mode::Implied},
    {"TSB", Mode::Absolute}, {"ORA", Mode::Absolute}, {"ASL", Mode::Absolute}, {"ORA", Mode::Long},
    // 0x10
    {"BPL", Mode::Relative}, {"ORA", Mode::DirectIndY}, {"ORA", Mode::DirectInd}, {"ORA", Mode::StackIndY},
    {"TRB", Mode::Direct}, {"ORA", Mode::DirectX}, {"ASL", Mode::DirectX}, {"ORA", Mode::DirectLongY},
    {"CLC", Mode::Implied}, {"ORA", Mode::AbsoluteY}, {"INC", Mode::Accumulator}, {"TCS", Mode::Implied},
    {"TRB", Mode::Absolute}, {"ORA", Mode::AbsoluteX}, {"ASL", Mode::AbsoluteX}, {"ORA", Mode::LongX},
    // 0x20
    {"JSR", Mode::Absolute}, {"AND", Mode::DirectIndX}, {"JSL", Mode::Long}, {"AND", Mode::Stack},
    {"BIT", Mode::Direct}, {"AND", Mode::Direct}, {"ROL", Mode::Direct}, {"AND", Mode::DirectLong},
    {"PLP", Mode::Implied}, {"AND", Mode::ImmM}, {"ROL", Mode::Accumulator}, {"PLD", Mode::Implied},
    {"BIT", Mode::Absolute}, {"AND", Mode::Absolute}, {"ROL", Mode::Absolute}, {"AND", Mode::Long},
    // 0x30
    {"BMI", Mode::Relative}, {"AND", Mode::DirectIndY}, {"AND", Mode::DirectInd}, {"AND", Mode::StackIndY},
    {"BIT", Mode::DirectX}, {"AND", Mode::DirectX}, {"ROL", Mode::DirectX}, {"AND", Mode::DirectLongY},
    {"SEC", Mode::Implied}, {"AND", Mode::AbsoluteY}, {"DEC", Mode::Accumulator}, {"TSC", Mode::Implied},
    {"BIT", Mode::AbsoluteX}, {"AND", Mode::AbsoluteX}, {"ROL", Mode::AbsoluteX}, {"AND", Mode::LongX},
    // 0x40
    {"RTI", Mode::Implied}, {"EOR", Mode::DirectIndX}, {"WDM", Mode::Imm8}, {"EOR", Mode::Stack},
    {"MVP", Mode::Block}, {"EOR", Mode::Direct}, {"LSR", Mode::Direct}, {"EOR", Mode::DirectLong},
    {"PHA", Mode::Implied}, {"EOR", Mode::ImmM}, {"LSR", Mode::Accumulator}, {"PHK", Mode::Implied},
    {"JMP", Mode::Absolute}, {"EOR", Mode::Absolute}, {"LSR", Mode::Absolute}, {"EOR", Mode::Long},
    // 0x50
    {"BVC", Mode::Relative}, {"EOR", Mode::DirectIndY}, {"EOR", Mode::DirectInd}, {"EOR", Mode::StackIndY},
    {"MVN", Mode::Block}, {"EOR", Mode::DirectX}, {"LSR", Mode::DirectX}, {"EOR", Mode::DirectLongY},
    {"CLI", Mode::Implied}, {"EOR", Mode::AbsoluteY}, {"PHY", Mode::Implied}, {"TCD", Mode::Implied},
    {"JML", Mode::Long}, {"EOR", Mode::AbsoluteX}, {"LSR", Mode::AbsoluteX}, {"EOR", Mode::LongX},
    // 0x60
    {"RTS", Mode::Implied}, {"ADC", Mode::DirectIndX}, {"PER", Mode::RelLong}, {"ADC", Mode::Stack},
    {"STZ", Mode::Direct}, {"ADC", Mode::Direct}, {"ROR", Mode::Direct}, {"ADC", Mode::DirectLong},
    {"PLA", Mode::Implied}, {"ADC", Mode::ImmM}, {"ROR", Mode::Accumulator}, {"RTL", Mode::Implied},
    {"JMP", Mode::AbsInd}, {"ADC", Mode::Absolute}, {"ROR", Mode::Absolute}, {"ADC", Mode::Long},
    // 0x70
    {"BVS", Mode::Relative}, {"ADC", Mode::DirectIndY}, {"ADC", Mode::DirectInd}, {"ADC", Mode::StackIndY},
    {"STZ", Mode::DirectX}, {"ADC", Mode::DirectX}, {"ROR", Mode::DirectX}, {"ADC", Mode::DirectLongY},
    {"SEI", Mode::Implied}, {"ADC", Mode::AbsoluteY}, {"PLY", Mode::Implied}, {"TDC", Mode::Implied},
    {"JMP", Mode::AbsIndX}, {"ADC", Mode::AbsoluteX}, {"ROR", Mode::AbsoluteX}, {"ADC", Mode::LongX},
    // 0x80
    {"BRA", Mode::Relative}, {"STA", Mode::DirectIndX}, {"BRL", Mode::RelLong}, {"STA", Mode::Stack},
    {"STY", Mode::Direct}, {"STA", Mode::Direct}, {"STX", Mode::Direct}, {"STA", Mode::DirectLong},
    {"DEY", Mode::Implied}, {"BIT", Mode::ImmM}, {"TXA", Mode::Implied}, {"PHB", Mode::Implied},
    {"STY", Mode::Absolute}, {"STA", Mode::Absolute}, {"STX", Mode::Absolute}, {"STA", Mode::Long},
    // 0x90
    {"BCC", Mode::Relative}, {"STA", Mode::DirectIndY}, {"STA", Mode::DirectInd}, {"STA", Mode::StackIndY},
    {"STY", Mode::DirectX}, {"STA", Mode::DirectX}, {"STX", Mode::DirectY}, {"STA", Mode::DirectLongY},
    {"TYA", Mode::Implied}, {"STA", Mode::AbsoluteY}, {"TXS", Mode::Implied}, {"TXY", Mode::Implied},
    {"STZ", Mode::Absolute}, {"STA", Mode::AbsoluteX}, {"STZ", Mode::AbsoluteX}, {"STA", Mode::LongX},
    // 0xA0
    {"LDY", Mode::ImmX}, {"LDA", Mode::DirectIndX}, {"LDX", Mode::ImmX}, {"LDA", Mode::Stack},
    {"LDY", Mode::Direct}, {"LDA", Mode::Direct}, {"LDX", Mode::Direct}, {"LDA", Mode::DirectLong},
    {"TAY", Mode::Implied}, {"LDA", Mode::ImmM}, {"TAX", Mode::Implied}, {"PLB", Mode::Implied},
    {"LDY", Mode::Absolute}, {"LDA", Mode::Absolute}, {"LDX", Mode::Absolute}, {"LDA", Mode::Long},
    // 0xB0
    {"BCS", Mode::Relative}, {"LDA", Mode::DirectIndY}, {"LDA", Mode::DirectInd}, {"LDA", Mode::StackIndY},
    {"LDY", Mode::DirectX}, {"LDA", Mode::DirectX}, {"LDX", Mode::DirectY}, {"LDA", Mode::DirectLongY},
    {"CLV", Mode::Implied}, {"LDA", Mode::AbsoluteY}, {"TSX", Mode::Implied}, {"TYX", Mode::Implied},
    {"LDY", Mode::AbsoluteX}, {"LDA", Mode::AbsoluteX}, {"LDX", Mode::AbsoluteY}, {"LDA", Mode::LongX},
    // 0xC0
    {"CPY", Mode::ImmX}, {"CMP", Mode::DirectIndX}, {"REP", Mode::Imm8}, {"CMP", Mode::Stack},
    {"CPY", Mode::Direct}, {"CMP", Mode::Direct}, {"DEC", Mode::Direct}, {"CMP", Mode::DirectLong},
    {"INY", Mode::Implied}, {"CMP", Mode::ImmM}, {"DEX", Mode::Implied}, {"WAI", Mode::Implied},
    {"CPY", Mode::Absolute}, {"CMP", Mode::Absolute}, {"DEC", Mode::Absolute}, {"CMP", Mode::Long},
    // 0xD0
    {"BNE", Mode::Relative}, {"CMP", Mode::DirectIndY}, {"CMP", Mode::DirectInd}, {"CMP", Mode::StackIndY},
    {"PEI", Mode::DirectInd}, {"CMP", Mode::DirectX}, {"DEC", Mode::DirectX}, {"CMP", Mode::DirectLongY},
    {"CLD", Mode::Implied}, {"CMP", Mode::AbsoluteY}, {"PHX", Mode::Implied}, {"STP", Mode::Implied},
    {"JML", Mode::AbsLongInd}, {"CMP", Mode::AbsoluteX}, {"DEC", Mode::AbsoluteX}, {"CMP", Mode::LongX},
    // 0xE0
    {"CPX", Mode::ImmX}, {"SBC", Mode::DirectIndX}, {"SEP", Mode::Imm8}, {"SBC", Mode::Stack},
    {"CPX", Mode::Direct}, {"SBC", Mode::Direct}, {"INC", Mode::Direct}, {"SBC", Mode::DirectLong},
    {"INX", Mode::Implied}, {"SBC", Mode::ImmM}, {"NOP", Mode::Implied}, {"XBA", Mode::Implied},
    {"CPX", Mode::Absolute}, {"SBC", Mode::Absolute}, {"INC", Mode::Absolute}, {"SBC", Mode::Long},
    // 0xF0
    {"BEQ", Mode::Relative}, {"SBC", Mode::DirectIndY}, {"SBC", Mode::DirectInd}, {"SBC", Mode::StackIndY},
    {"PEA", Mode::Absolute}, {"SBC", Mode::DirectX}, {"INC", Mode::DirectX}, {"SBC", Mode::DirectLongY},
    {"SED", Mode::Implied}, {"SBC", Mode::AbsoluteY}, {"PLX", Mode::Implied}, {"XCE", Mode::Implied},
    {"JSR", Mode::AbsIndX}, {"SBC", Mode::AbsoluteX}, {"INC", Mode::AbsoluteX}, {"SBC", Mode::LongX},
};
// clang-format on

constexpr const OpInfo &op_info(uint8_t op)
{
    return kOpcodes[op];
}

// Operand bytes for a mode; immediates by width.
constexpr int operand_bytes(Mode mode, bool m8, bool x8)
{
    switch (mode)
    {
    case Mode::Implied:
    case Mode::Accumulator:
        return 0;
    case Mode::ImmM:
        return m8 ? 1 : 2;
    case Mode::ImmX:
        return x8 ? 1 : 2;
    case Mode::Imm8:
    case Mode::Direct:
    case Mode::DirectX:
    case Mode::DirectY:
    case Mode::DirectInd:
    case Mode::DirectIndX:
    case Mode::DirectIndY:
    case Mode::DirectLong:
    case Mode::DirectLongY:
    case Mode::Stack:
    case Mode::StackIndY:
    case Mode::Relative:
        return 1;
    case Mode::Absolute:
    case Mode::AbsoluteX:
    case Mode::AbsoluteY:
    case Mode::AbsInd:
    case Mode::AbsIndX:
    case Mode::AbsLongInd:
    case Mode::RelLong:
    case Mode::Block:
        return 2;
    case Mode::Long:
    case Mode::LongX:
        return 3;
    }
    return 0;
}

// The whole instruction, opcode included. m8/x8: the M and X flags are set (8-bit);
// emulation mode is both.
constexpr int op_length(uint8_t op, bool m8, bool x8)
{
    return 1 + operand_bytes(kOpcodes[op].mode, m8, x8);
}

namespace detail
{
constexpr bool is_any(std::string_view m, std::initializer_list<std::string_view> names)
{
    for (auto n : names)
        if (m == n)
            return true;
    return false;
}
} // namespace detail

constexpr WidthBy op_width(uint8_t op)
{
    std::string_view m = kOpcodes[op].mnemonic;
    if (detail::is_any(m, { "ADC", "AND", "BIT", "CMP", "EOR", "LDA", "ORA", "SBC", "STA", "STZ", "ASL", "LSR",
                            "ROL", "ROR", "INC", "DEC", "TRB", "TSB", "PHA", "PLA", "TXA", "TYA" }))
        return WidthBy::M;
    if (detail::is_any(m, { "CPX", "CPY", "LDX", "LDY", "STX", "STY", "INX", "INY", "DEX", "DEY", "PHX", "PHY",
                            "PLX", "PLY", "TAX", "TAY", "TSX", "TXY", "TYX" }))
        return WidthBy::X;
    return WidthBy::None;
}

constexpr OpClass op_class(uint8_t op)
{
    std::string_view m = kOpcodes[op].mnemonic;
    using detail::is_any;
    if (is_any(m, { "ADC", "SBC", "INC", "DEC", "INX", "INY", "DEX", "DEY" }))
        return OpClass::Arith;
    if (is_any(m, { "AND", "ORA", "EOR", "BIT", "TRB", "TSB" }))
        return OpClass::Logic;
    if (is_any(m, { "ASL", "LSR", "ROL", "ROR" }))
        return OpClass::Shift;
    if (is_any(m, { "CMP", "CPX", "CPY" }))
        return OpClass::Compare;
    if (is_any(m, { "LDA", "LDX", "LDY" }))
        return OpClass::Load;
    if (is_any(m, { "STA", "STX", "STY", "STZ" }))
        return OpClass::Store;
    if (is_any(m, { "TAX", "TAY", "TXA", "TYA", "TSX", "TXS", "TXY", "TYX", "TCD", "TDC", "TCS", "TSC", "XBA" }))
        return OpClass::Transfer;
    if (is_any(m, { "BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA", "BRL" }))
        return OpClass::Branch;
    if (is_any(m, { "JMP", "JML", "JSR", "JSL" }))
        return OpClass::Jump;
    if (is_any(m, { "RTS", "RTL", "RTI" }))
        return OpClass::Return;
    if (is_any(m, { "PHA", "PHX", "PHY", "PHP", "PHB", "PHD", "PHK", "PLA", "PLX", "PLY", "PLP", "PLB", "PLD",
                    "PEA", "PEI", "PER" }))
        return OpClass::Stack;
    if (is_any(m, { "CLC", "SEC", "CLI", "SEI", "CLD", "SED", "CLV" }))
        return OpClass::Flags;
    if (is_any(m, { "REP", "SEP", "XCE" }))
        return OpClass::Width;
    if (is_any(m, { "MVN", "MVP" }))
        return OpClass::Block;
    return OpClass::Misc;
}

const char *class_name(OpClass c); // "arith", "branch"...
std::optional<OpClass> class_from_name(std::string_view name);

struct Disassembly
{
    int length = 0;   // the whole instruction, even when fewer bytes were given
    std::string text; // "LDA $0450,X"; missing bytes show as ??; empty when no bytes
};

// The instruction at the start of `bytes`. With its CPU address, branches show their
// target ($1A4D, in the same bank); without, the offset (+12).
Disassembly disassemble(std::span<const uint8_t> bytes, bool m8, bool x8, std::optional<uint32_t> address);

} // namespace corrupt
