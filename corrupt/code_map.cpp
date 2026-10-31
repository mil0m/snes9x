#include "code_map.h"

#include <bit>
#include <cstring>

namespace corrupt
{

namespace
{
constexpr char kMagic[8] = { 'S', '9', 'X', 'C', 'M', 'A', 'P', '1' };
constexpr size_t kMaxSize = 16 * 1024 * 1024; // the biggest ROM Snes9x loads, with room

void put_varint(std::string &out, uint64_t v)
{
    do
    {
        uint8_t b = v & 0x7F;
        v >>= 7;
        out += char(b | (v ? 0x80 : 0));
    } while (v);
}
} // namespace

bool CodeMap::merge(const CodeMap &other)
{
    if (other.size() != size())
        return false;
    for (size_t i = 0; i < bytes.size(); i++)
        bytes[i] |= other.bytes[i];
    return true;
}

CodeCoverage CodeMap::coverage() const
{
    CodeCoverage c;
    c.size = bytes.size();
    for (uint8_t f : bytes)
    {
        if (!f)
            continue;
        c.seen++;
        c.opcode += (f & codeflag::Opcode) != 0;
        c.operand += (f & codeflag::Operand) != 0;
        c.code += (f & codeflag::Code) != 0;
        c.both_widths += (f & codeflag::Code) && (f & codeflag::Op8) && (f & codeflag::Op16);
        c.data += (f & codeflag::Data) != 0;
        c.dma += (f & codeflag::Dma) != 0;
    }
    return c;
}

uint64_t CodeMap::fingerprint() const
{
    uint64_t bits = 0;
    size_t i = 0;
    for (; i + 8 <= bytes.size(); i += 8)
    {
        uint64_t word;
        std::memcpy(&word, bytes.data() + i, 8);
        bits += std::popcount(word);
    }
    for (; i < bytes.size(); i++)
        bits += std::popcount(bytes[i]);
    return bits;
}

std::string encode_code_map(const CodeMap &map)
{
    std::string out(kMagic, sizeof kMagic);
    uint32_t size = uint32_t(map.size());
    for (int i = 0; i < 4; i++)
        out += char((size >> (8 * i)) & 0xFF);
    auto &f = map.flags();
    for (size_t i = 0; i < f.size();)
    {
        size_t j = i + 1;
        while (j < f.size() && f[j] == f[i])
            j++;
        out += char(f[i]);
        put_varint(out, j - i);
        i = j;
    }
    return out;
}

std::optional<CodeMap> decode_code_map(std::string_view file, std::string &error)
{
    if (file.size() < 12 || std::memcmp(file.data(), kMagic, sizeof kMagic) != 0)
    {
        error = "not a code map";
        return std::nullopt;
    }
    uint32_t size = 0;
    for (int i = 0; i < 4; i++)
        size |= uint32_t(uint8_t(file[8 + i])) << (8 * i);
    if (size > kMaxSize)
    {
        error = "bigger than any ROM";
        return std::nullopt;
    }
    CodeMap map(size);
    uint8_t *out = map.data();
    size_t at = 0;
    size_t i = 12;
    while (i < file.size())
    {
        uint8_t flags = uint8_t(file[i++]);
        uint64_t run = 0;
        int shift = 0;
        bool ended = false;
        while (i < file.size() && shift < 35)
        {
            uint8_t b = uint8_t(file[i++]);
            run |= uint64_t(b & 0x7F) << shift;
            shift += 7;
            if (!(b & 0x80))
            {
                ended = true;
                break;
            }
        }
        if (!ended)
        {
            error = "a run length is cut off";
            return std::nullopt;
        }
        if (run == 0 || run > size - at)
        {
            error = run ? "the runs go past the ROM size" : "a run of no bytes";
            return std::nullopt;
        }
        std::memset(out + at, flags & codeflag::All, size_t(run));
        at += size_t(run);
    }
    if (at != size)
    {
        error = "the runs stop short of the ROM size";
        return std::nullopt;
    }
    return map;
}

std::optional<Instruction> instruction_at(std::span<const uint8_t> rom, const CodeMap &map, uint32_t offset,
                                          const RomAddresses &where)
{
    return instruction_at(rom, std::span<const uint8_t>(map.flags()), offset, where);
}

std::optional<Instruction> instruction_at(std::span<const uint8_t> rom, std::span<const uint8_t> flags, uint32_t offset,
                                          const RomAddresses &where)
{
    struct
    {
        std::span<const uint8_t> f;
        uint8_t at(size_t i) const { return i < f.size() ? f[i] : 0; }
    } map{ flags };
    if (offset >= rom.size() || !(map.at(offset) & codeflag::Code))
        return std::nullopt;
    for (uint32_t back = 0; back < 4 && back <= offset; back++)
    {
        uint32_t start = offset - back;
        uint8_t f = map.at(start);
        if (!(f & codeflag::Opcode))
        {
            if (!(f & codeflag::Operand))
                return std::nullopt; // walked off the code
            continue;
        }
        uint8_t op = rom[start];
        WidthBy by = op_width(op);
        bool ran8 = (f & codeflag::Op8) || !(f & codeflag::Op16);
        bool ran16 = (f & codeflag::Op16) != 0;
        // Prefer the 8-bit form; the 16-bit one if only it reaches this byte.
        bool narrow = ran8 && op_length(op, true, true) > int(back);
        if (!narrow && !(ran16 && op_length(op, by != WidthBy::M, by != WidthBy::X) > int(back)))
        {
            if (back == 0)
                return std::nullopt;
            continue;
        }
        bool m8 = by != WidthBy::M || narrow;
        bool x8 = by != WidthBy::X || narrow;
        Instruction in;
        in.start = start;
        in.opcode = op;
        in.cls = op_class(op);
        in.both_widths = ran8 && ran16 && by != WidthBy::None;
        in.address = where.cpu_address(start);
        auto d = disassemble(rom.subspan(start), m8, x8, in.address);
        in.length = d.length;
        in.text = d.text;
        return in;
    }
    return std::nullopt;
}

void CodeMapKeeper::reset(size_t rom_size, CodeMaps loaded)
{
    own = loaded.mine.size() == rom_size ? std::move(loaded.mine) : CodeMap(rom_size);
    others = loaded.others.size() == rom_size ? std::move(loaded.others) : CodeMap(rom_size);
    saved_print = own.fingerprint();
    frames = 0;
}

void CodeMapKeeper::tick()
{
    frames++;
}

CodeMap CodeMapKeeper::combined() const
{
    CodeMap all = own;
    all.merge(others);
    return all;
}

bool CodeMapKeeper::save_due()
{
    if (frames < kSaveEveryFrames)
        return false;
    frames = 0;
    return unsaved();
}

void CodeMapKeeper::saved()
{
    saved_print = own.fingerprint();
}

} // namespace corrupt
