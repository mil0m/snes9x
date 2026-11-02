#include "function_change.h"

#include "code_aim.h"

#include <algorithm>
#include <array>
#include <string>

namespace corrupt
{

namespace
{

uint8_t flag_at(std::span<const uint8_t> flags, uint32_t o)
{
    return o < flags.size() ? flags[o] : 0;
}

bool is_number_op(uint8_t op)
{
    auto &info = op_info(op);
    if (info.mode != Mode::ImmM && info.mode != Mode::ImmX)
        return false;
    std::string_view m = info.mnemonic;
    return m == "LDA" || m == "LDX" || m == "LDY" || m == "ADC" || m == "SBC" || m == "AND" || m == "ORA" ||
           m == "EOR" || m == "CMP" || m == "CPX" || m == "CPY" || m == "BIT";
}

// A JSL's CPU address as a ROM offset: the chunk the map shows it in, or a mirror of it
// (LoROM's $80 banks mirror $00).
std::optional<uint32_t> rom_offset_of(uint32_t cpu, const RomAddresses &where)
{
    for (uint32_t mirror : { cpu, cpu ^ 0x800000u })
        for (size_t c = 0; c < where.chunks.size(); c++)
            if (where.chunks[c] != RomAddresses::kNone && where.chunks[c] == (mirror & ~(RomAddresses::kChunk - 1)))
                return uint32_t(c * RomAddresses::kChunk + (mirror & (RomAddresses::kChunk - 1)));
    return std::nullopt;
}

constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kMathPairs{ {
    { "ADC", "SBC" },
    { "INC", "DEC" },
    { "INX", "DEX" },
    { "INY", "DEY" },
    { "ASL", "LSR" },
    { "ROL", "ROR" },
    { "CLC", "SEC" },
    { "AND", "ORA" },
} };

} // namespace

std::optional<uint8_t> math_partner(uint8_t op)
{
    std::string_view m = op_info(op).mnemonic;
    Mode mode = op_info(op).mode;
    for (auto &[a, b] : kMathPairs)
    {
        std::string_view want = m == a ? b : m == b ? a : std::string_view();
        if (want.empty())
            continue;
        for (int o = 0; o < 256; o++)
            if (op_info(uint8_t(o)).mnemonic == want && op_info(uint8_t(o)).mode == mode)
                return uint8_t(o);
    }
    return std::nullopt;
}

std::vector<ChangeOption> change_options(const FunctionBody &body, std::span<const uint8_t> rom,
                                         std::span<const uint8_t> flags, const RomAddresses &where)
{
    std::vector<ChangeOption> out;
    for (auto &in : body.code)
    {
        if (in.at >= rom.size())
            continue;
        uint8_t op = rom[in.at];
        ChangeOption o;
        o.at = in.at;
        o.length = in.length;
        o.opcode = op;
        o.address = where.cpu_address(in.at);
        bool ran16 = (flag_at(flags, in.at) & codeflag::Op16) != 0;
        if (auto d = instruction_at(rom, flags, in.at, where))
            o.text = d->text;
        if (is_number_op(op))
        {
            if (in.both_widths)
                continue; // its length depends on a width that changes: leave it alone
            o.kind = ChangeKind::Number;
            o.width = ran16 ? 2 : 1;
            if (in.at + 1 + size_t(o.width) > rom.size())
                continue;
            o.value = rom[in.at + 1] | (o.width == 2 ? rom[in.at + 2] << 8 : 0);
            o.other = math_partner(op).value_or(0); // ADC #: its number, or SBC
        }
        else if (is_conditional_branch(op))
            o.kind = ChangeKind::Branch;
        else if (auto other = math_partner(op))
        {
            o.kind = ChangeKind::Math;
            o.other = *other;
        }
        else if ((op == callop::Jsr || op == callop::Jsl) && in.at + size_t(op == callop::Jsl ? 4 : 3) <= rom.size())
        {
            o.kind = ChangeKind::Skip;
            o.call = call_type_of(op);
            uint16_t lo = uint16_t(rom[in.at + 1] | (rom[in.at + 2] << 8));
            if (op == callop::Jsl)
                o.target = rom_offset_of((uint32_t(rom[in.at + 3]) << 16) | lo, where);
            else if (auto pc = where.cpu_address(in.at))
                o.target = rom_offset_of((*pc & 0xFF0000) | lo, where);
        }
        else
            continue;
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<uint8_t> number_bytes(const ChangeOption &o, uint32_t value)
{
    std::vector<uint8_t> out{ o.opcode, uint8_t(value) };
    if (o.width == 2)
        out.push_back(uint8_t(value >> 8));
    return out;
}

std::vector<uint8_t> branch_bytes(const ChangeOption &o, BranchTo to)
{
    switch (to)
    {
    case BranchTo::Opposite:
        return { flipped_branch(o.opcode) };
    case BranchTo::Always:
        return { 0x80 }; // BRA: the same distance follows
    case BranchTo::Never:
        return { 0xEA, 0xEA };
    }
    return {};
}

std::vector<uint8_t> math_bytes(const ChangeOption &o)
{
    return { o.other };
}

std::vector<uint8_t> skip_bytes(const ChangeOption &o)
{
    return std::vector<uint8_t>(o.call == CallType::Long ? 4 : 3, 0xEA);
}

std::optional<std::vector<uint8_t>> redirect_bytes(const ChangeOption &o, uint32_t entry, const RomAddresses &where)
{
    auto to = where.cpu_address(entry);
    if (!to)
        return std::nullopt;
    if (o.call == CallType::Long)
        return std::vector<uint8_t>{ callop::Jsl, uint8_t(*to), uint8_t(*to >> 8), uint8_t(*to >> 16) };
    auto from = where.cpu_address(o.at);
    if (!from || (*from >> 16) != (*to >> 16))
        return std::nullopt; // JSR stays in its bank
    return std::vector<uint8_t>{ callop::Jsr, uint8_t(*to), uint8_t(*to >> 8) };
}

Change make_change(uint32_t function, const ChangeOption &o, ChangeKind kind, std::vector<uint8_t> value,
                   std::span<const uint8_t> clean_rom)
{
    Change c;
    c.function = function;
    c.at = o.at;
    c.kind = kind;
    for (size_t i = 0; i < value.size() && o.at + i < clean_rom.size(); i++)
        c.was.push_back(clean_rom[o.at + i]);
    c.value = std::move(value);
    c.value.resize(c.was.size());
    return c;
}

const char *change_kind_name(ChangeKind k)
{
    switch (k)
    {
    case ChangeKind::Number: return "number";
    case ChangeKind::Branch: return "branch";
    case ChangeKind::Math: return "math";
    case ChangeKind::Skip: return "skip";
    case ChangeKind::Redirect: return "redirect";
    }
    return "number";
}

std::optional<ChangeKind> parse_change_kind(std::string_view name)
{
    for (auto k : { ChangeKind::Number, ChangeKind::Branch, ChangeKind::Math, ChangeKind::Skip, ChangeKind::Redirect })
        if (name == change_kind_name(k))
            return k;
    return std::nullopt;
}

bool change_on(const Change &c, const MemoryAccess &mem)
{
    if (c.value.empty() || c.at + c.value.size() > mem.size(Target::ROM))
        return false;
    for (size_t i = 0; i < c.value.size(); i++)
        if (mem.read(Target::ROM, uint32_t(c.at + i)) != c.value[i])
            return false;
    return true;
}

} // namespace corrupt
