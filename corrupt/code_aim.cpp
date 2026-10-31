#include "code_aim.h"
#include "code_map.h"
#include "crash_map.h"

#include <algorithm>
#include <array>

namespace corrupt
{

namespace
{

constexpr std::array<Role, 7> kRoles = { Role::Any, Role::Opcode, Role::Operand, Role::Code,
                                         Role::Data, Role::Dma, Role::Used };

// Instructions that do different things with the same operand. A swap stays inside one
// family and one addressing mode.
const std::vector<std::vector<std::string_view>> kFamilies = {
    { "ORA", "AND", "EOR", "ADC", "CMP", "SBC", "LDA", "BIT" },
    { "ASL", "LSR", "ROL", "ROR", "INC", "DEC" },
    { "BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA" },
    { "INX", "INY", "DEX", "DEY" },
    { "CLC", "SEC", "CLI", "SEI", "CLD", "SED", "CLV" },
    { "TAX", "TAY", "TXA", "TYA", "TXY", "TYX" },
    { "STA", "STZ" },
    { "LDX", "LDY", "CPX", "CPY" },
    { "STX", "STY" },
};

int family_of(uint8_t op)
{
    std::string_view m = op_info(op).mnemonic;
    for (size_t f = 0; f < kFamilies.size(); f++)
        if (std::find(kFamilies[f].begin(), kFamilies[f].end(), m) != kFamilies[f].end())
            return int(f);
    return -1;
}

const std::array<std::vector<uint8_t>, 256> &groups()
{
    static const auto table = [] {
        std::array<std::vector<uint8_t>, 256> out;
        for (int op = 0; op < 256; op++)
        {
            int family = family_of(uint8_t(op));
            for (int other = 0; other < 256; other++)
                if (other == op || (family >= 0 && family_of(uint8_t(other)) == family &&
                                    op_info(uint8_t(other)).mode == op_info(uint8_t(op)).mode))
                    out[size_t(op)].push_back(uint8_t(other));
        }
        return out;
    }();
    return table;
}

bool runs_both_widths(uint8_t f)
{
    return (f & codeflag::Code) && (f & codeflag::Op8) && (f & codeflag::Op16);
}

// The opcode of the instruction the code byte at `o` belongs to, if the map knows it.
std::optional<uint8_t> owner(std::span<const uint8_t> rom, std::span<const uint8_t> flags, uint32_t o)
{
    auto flag = [&](uint32_t i) { return uint8_t(i < flags.size() ? flags[i] : 0); };
    if (flag(o) & codeflag::Opcode)
        return rom[o];
    for (uint32_t back = 1; back <= 3 && back <= o; back++)
    {
        uint8_t f = flag(o - back);
        if (!(f & codeflag::Opcode))
            continue;
        uint8_t op = rom[o - back];
        bool wide = (f & codeflag::Op16) && !(f & codeflag::Op8);
        if (op_length(op, !wide, !wide) > int(back))
            return op;
    }
    return std::nullopt;
}

bool in(const std::vector<OpClass> &list, OpClass c)
{
    return std::find(list.begin(), list.end(), c) != list.end();
}

struct Writer
{
    MemoryAccess &mem;
    const HotMask *skip;
    std::vector<WriteEvent> events;

    bool blocked(uint32_t o) const { return skip && skip->is_hot(Target::ROM, o); }
    void put(uint32_t o, uint8_t value)
    {
        if (blocked(o))
            return;
        uint8_t old = mem.read(Target::ROM, o);
        if (old == value)
            return;
        mem.write(Target::ROM, o, value);
        events.push_back({ Target::ROM, o, old, value });
    }
};

} // namespace

const char *role_name(Role r)
{
    switch (r)
    {
    case Role::Any: return "any";
    case Role::Opcode: return "opcode";
    case Role::Operand: return "operand";
    case Role::Code: return "code";
    case Role::Data: return "data";
    case Role::Dma: return "dma";
    case Role::Used: return "used";
    }
    return "any";
}

std::optional<Role> parse_role(std::string_view name)
{
    for (auto r : kRoles)
        if (name == role_name(r))
            return r;
    return std::nullopt;
}

std::span<const uint8_t> swap_group(uint8_t op)
{
    return groups()[op];
}

std::vector<uint32_t> aimed_bytes(std::span<const uint8_t> rom, std::span<const uint8_t> flags, uint32_t first,
                                  uint32_t last, const Aim &aim)
{
    std::vector<uint32_t> out;
    if (rom.empty() || first >= rom.size())
        return out;
    last = std::min<uint32_t>(last, uint32_t(rom.size() - 1));
    bool filtered = !aim.classes.empty() || !aim.except.empty();
    for (uint32_t o = first; o <= last; o++)
    {
        if (!aim.active())
        {
            out.push_back(o);
            continue;
        }
        uint8_t f = uint8_t((o < flags.size() ? flags[o] : 0) & codeflag::All);
        bool ok = false;
        bool code_role = true;
        switch (aim.role)
        {
        case Role::Any: ok = (f & codeflag::Code) != 0; break; // only class filters: code implied
        case Role::Opcode: ok = (f & codeflag::Opcode) != 0; break;
        case Role::Operand: ok = (f & codeflag::Operand) && !(f & codeflag::Opcode); break;
        case Role::Code: ok = (f & codeflag::Code) != 0; break;
        case Role::Data: ok = (f & codeflag::Data) != 0, code_role = false; break;
        case Role::Dma: ok = (f & codeflag::Dma) != 0, code_role = false; break;
        case Role::Used: ok = f != 0; break;
        }
        if (!ok || (code_role && runs_both_widths(f)))
            continue;
        if (filtered)
        {
            auto op = (f & codeflag::Code) ? owner(rom, flags, o) : std::nullopt;
            if (!op)
                continue;
            OpClass c = op_class(*op);
            if ((!aim.classes.empty() && !in(aim.classes, c)) || in(aim.except, c))
                continue;
        }
        out.push_back(o);
    }
    return out;
}

std::vector<uint32_t> code_op_targets(OpType type, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                                      uint32_t first, uint32_t last, const Aim &aim)
{
    Aim opcodes = aim;
    opcodes.role = Role::Opcode;
    auto out = aimed_bytes(rom, flags, first, last, opcodes);
    if (type == OpType::BranchFlip)
        std::erase_if(out, [&](uint32_t o) { return !is_conditional_branch(rom[o]); });
    return out;
}

std::vector<WriteEvent> apply_code_op(const Op &op, MemoryAccess &mem, std::span<const uint8_t> flags,
                                      const std::vector<uint32_t> &opcodes, Rng &rng, const HotMask *skip)
{
    Writer w{ mem, skip, {} };
    size_t size = mem.size(Target::ROM);
    for (uint32_t o : opcodes)
    {
        if (o >= size)
            continue;
        uint8_t v = mem.read(Target::ROM, o);
        switch (op.type)
        {
        case OpType::OpcodeSwap:
        {
            auto group = swap_group(v);
            // Always one draw per opcode, so the other picks don't shift when a group is a single.
            auto pick = rng.range(0, int64_t(std::max<size_t>(group.size(), 2)) - 2);
            if (group.size() < 2)
                break;
            std::vector<uint8_t> others;
            for (uint8_t g : group)
                if (g != v)
                    others.push_back(g);
            w.put(o, others[size_t(pick) % others.size()]);
            break;
        }
        case OpType::BranchFlip:
            if (is_conditional_branch(v))
                w.put(o, flipped_branch(v));
            break;
        case OpType::NopOut:
        {
            uint8_t f = o < flags.size() ? flags[o] : 0;
            bool wide = (f & codeflag::Op16) && !(f & codeflag::Op8);
            uint32_t length = uint32_t(op_length(v, !wide, !wide));
            if (o + length > size)
                break;
            bool any_blocked = false;
            for (uint32_t i = 0; i < length; i++)
                any_blocked |= w.blocked(o + i);
            if (any_blocked)
                break;
            for (uint32_t i = 0; i < length; i++)
                w.put(o + i, 0xEA);
            break;
        }
        default:
            break;
        }
    }
    return std::move(w.events);
}

} // namespace corrupt
