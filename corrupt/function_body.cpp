#include "function_body.h"

#include "code_aim.h"

#include <algorithm>
#include <unordered_set>

namespace corrupt
{

namespace
{

uint8_t flag_at(std::span<const uint8_t> flags, uint32_t o)
{
    return o < flags.size() ? flags[o] : 0;
}

// Where a jump to `target16` from the instruction at `from` lands in the ROM: in the same
// bank, found through the CPU address the map knows for `from`, and checked by mapping the
// result back.
std::optional<uint32_t> in_bank(uint32_t from, uint16_t target16, const RomAddresses &where)
{
    auto pc = where.cpu_address(from);
    if (!pc)
        return std::nullopt;
    int64_t to = int64_t(from) + int64_t(target16) - int64_t(*pc & 0xFFFF);
    if (to < 0)
        return std::nullopt;
    auto check = where.cpu_address(uint32_t(to));
    if (!check || *check != ((*pc & 0xFF0000) | target16))
        return std::nullopt;
    return uint32_t(to);
}

// A branch's target: relative to the next instruction, inside the bank. Through the CPU
// address when it's known, else plain ROM arithmetic.
std::optional<uint32_t> branch_target(uint32_t at, int length, int distance, const RomAddresses &where)
{
    if (auto pc = where.cpu_address(at))
        return in_bank(at, uint16_t((*pc & 0xFFFF) + length + distance), where);
    int64_t to = int64_t(at) + length + distance;
    return to < 0 ? std::nullopt : std::optional<uint32_t>(uint32_t(to));
}

} // namespace

bool FunctionBody::contains(uint32_t offset) const
{
    auto it = std::upper_bound(code.begin(), code.end(), offset,
                               [](uint32_t o, const BodyInstruction &i) { return o < i.at; });
    // A longer instruction a few bytes back may cover it too.
    for (int back = 0; back < 4 && it != code.begin(); back++)
    {
        --it;
        if (offset >= it->at && offset < it->at + it->length)
            return true;
    }
    return false;
}

std::vector<std::pair<uint32_t, uint32_t>> FunctionBody::runs() const
{
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (auto &i : code)
    {
        uint32_t first = i.at, last = i.at + i.length - 1;
        if (!out.empty() && first <= out.back().second + 1)
            out.back().second = std::max(out.back().second, last);
        else
            out.push_back({ first, last });
    }
    return out;
}

FunctionBody function_body(std::span<const uint8_t> rom, std::span<const uint8_t> flags, uint32_t entry,
                           const RomAddresses &where, size_t max_bytes)
{
    FunctionBody body;
    body.entry = entry;
    std::vector<uint32_t> todo{ entry };
    std::unordered_set<uint32_t> seen;
    std::unordered_set<uint32_t> covered;

    while (!todo.empty())
    {
        uint32_t at = todo.back();
        todo.pop_back();
        if (at >= rom.size() || !(flag_at(flags, at) & codeflag::Opcode) || !seen.insert(at).second)
            continue;

        uint8_t f = flag_at(flags, at);
        uint8_t op = rom[at];
        WidthBy by = op_width(op);
        bool ran8 = (f & codeflag::Op8) || !(f & codeflag::Op16);
        bool ran16 = (f & codeflag::Op16) != 0;
        int len8 = op_length(op, true, true);
        int len16 = op_length(op, by != WidthBy::M, by != WidthBy::X);
        int length = ran16 ? len16 : len8;
        if (at + size_t(length) > rom.size())
            continue;

        size_t grows = 0;
        for (int i = 0; i < length; i++)
            grows += !covered.count(at + i);
        if (body.bytes + grows > max_bytes)
        {
            body.truncated = true;
            continue;
        }
        for (int i = 0; i < length; i++)
            covered.insert(at + i);
        body.bytes += grows;
        body.code.push_back({ at, uint8_t(length), ran8 && ran16 && by != WidthBy::None });

        // Where it can go next.
        auto next = [&](uint32_t o) { todo.push_back(o); };
        auto fall = [&] { // each width it ran at (the same place twice is walked once)
            if (ran8)
                next(at + uint32_t(len8));
            if (ran16)
                next(at + uint32_t(len16));
        };
        auto operand16 = [&] { return uint16_t(rom[at + 1] | (rom[at + 2] << 8)); };
        switch (op)
        {
        case 0x60: // RTS
        case 0x6B: // RTL
        case 0x40: // RTI
        case 0x00: // BRK
        case 0xDB: // STP
        case 0x42: // WDM
        case 0x5C: // JML $long: a tail jump elsewhere
            break;
        case 0x6C: // JMP ($abs)
        case 0x7C: // JMP ($abs,X)
        case 0xDC: // JML [$abs]
            body.open_ended = true;
            break;
        case 0x4C: // JMP $abs
            if (auto to = in_bank(at, operand16(), where))
                next(*to);
            break;
        case 0x80: // BRA
            if (auto to = branch_target(at, 2, int8_t(rom[at + 1]), where))
                next(*to);
            break;
        case 0x82: // BRL
            if (auto to = branch_target(at, 3, int16_t(operand16()), where))
                next(*to);
            break;
        default:
            if (is_conditional_branch(op))
            {
                if (auto to = branch_target(at, 2, int8_t(rom[at + 1]), where))
                    next(*to);
            }
            fall(); // calls too: only if what follows them ran
            break;
        }
    }
    std::sort(body.code.begin(), body.code.end(),
              [](const BodyInstruction &a, const BodyInstruction &b) { return a.at < b.at; });
    return body;
}

FunctionBodies function_bodies(std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                               const std::vector<Function> &functions, const RomAddresses &where)
{
    FunctionBodies out;
    for (auto &f : functions)
        out.emplace(f.entry, function_body(rom, flags, f.entry, where));
    return out;
}

} // namespace corrupt
