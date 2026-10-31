#include "opcodes.h"

#include <array>
#include <cstdio>

namespace corrupt
{

namespace
{

constexpr std::array<const char *, kOpClassCount> kClassNames = {
    "arith", "logic", "shift", "compare", "load", "store", "transfer", "branch",
    "jump", "return", "stack", "flags", "width", "block", "misc",
};

// Hex digits for `count` bytes of operand starting at bytes[1], high byte first;
// missing bytes show as ??.
std::string hex_operand(std::span<const uint8_t> bytes, int first, int count)
{
    std::string out = "$";
    for (int i = first + count - 1; i >= first; i--)
    {
        if (size_t(i) < bytes.size())
        {
            char b[3];
            std::snprintf(b, sizeof b, "%02X", bytes[i]);
            out += b;
        }
        else
            out += "??";
    }
    return out;
}

} // namespace

const char *class_name(OpClass c)
{
    return kClassNames[size_t(c)];
}

std::optional<OpClass> class_from_name(std::string_view name)
{
    for (size_t i = 0; i < kClassNames.size(); i++)
        if (name == kClassNames[i])
            return OpClass(i);
    return std::nullopt;
}

Disassembly disassemble(std::span<const uint8_t> bytes, bool m8, bool x8, std::optional<uint32_t> address)
{
    Disassembly d;
    if (bytes.empty())
        return d;
    uint8_t op = bytes[0];
    const OpInfo &info = op_info(op);
    d.length = op_length(op, m8, x8);
    int n = d.length - 1;
    std::string m = info.mnemonic;
    auto arg = [&] { return hex_operand(bytes, 1, n); };
    auto branch = [&]() -> std::string {
        bool have = size_t(d.length) <= bytes.size();
        if (!have)
            return arg();
        int offset = n == 1 ? int(int8_t(bytes[1])) : int(int16_t(bytes[1] | (bytes[2] << 8)));
        if (!address)
            return (offset >= 0 ? "+" : "") + std::to_string(offset);
        uint16_t target = uint16_t((*address & 0xFFFF) + d.length + offset);
        char b[8];
        std::snprintf(b, sizeof b, "$%04X", target);
        return b;
    };

    switch (info.mode)
    {
    case Mode::Implied: d.text = m; break;
    case Mode::Accumulator: d.text = m + " A"; break;
    case Mode::ImmM:
    case Mode::ImmX:
    case Mode::Imm8: d.text = m + " #" + arg(); break;
    case Mode::Direct:
    case Mode::Absolute:
    case Mode::Long: d.text = m + " " + arg(); break;
    case Mode::DirectX:
    case Mode::AbsoluteX:
    case Mode::LongX: d.text = m + " " + arg() + ",X"; break;
    case Mode::DirectY:
    case Mode::AbsoluteY: d.text = m + " " + arg() + ",Y"; break;
    case Mode::DirectInd:
    case Mode::AbsInd: d.text = m + " (" + arg() + ")"; break;
    case Mode::DirectIndX:
    case Mode::AbsIndX: d.text = m + " (" + arg() + ",X)"; break;
    case Mode::DirectIndY: d.text = m + " (" + arg() + "),Y"; break;
    case Mode::DirectLong:
    case Mode::AbsLongInd: d.text = m + " [" + arg() + "]"; break;
    case Mode::DirectLongY: d.text = m + " [" + arg() + "],Y"; break;
    case Mode::Stack: d.text = m + " " + arg() + ",S"; break;
    case Mode::StackIndY: d.text = m + " (" + arg() + ",S),Y"; break;
    case Mode::Relative:
    case Mode::RelLong: d.text = m + " " + branch(); break;
    case Mode::Block:
        // The bytes are the destination bank, then the source; written source first.
        d.text = m + " " + hex_operand(bytes, 2, 1) + "," + hex_operand(bytes, 1, 1);
        break;
    }
    return d;
}

} // namespace corrupt
