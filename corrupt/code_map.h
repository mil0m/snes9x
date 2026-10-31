#pragma once
#include "opcodes.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The code map: what each ROM byte is, learned by watching the game run. One byte of
// flags per ROM byte, set by the tracer (code) and the core's read hooks (data, DMA).
// Flags are only ever added, so maps from several people and sessions are a union.
namespace corrupt
{

namespace codeflag
{
constexpr uint8_t Opcode = 0x01;  // an instruction started here
constexpr uint8_t Operand = 0x02; // inside an instruction, after its opcode
constexpr uint8_t Op8 = 0x04;     // in an M- or X-dependent instruction, run 8-bit
constexpr uint8_t Op16 = 0x08;    // the same, run 16-bit (both: runs at both widths)
constexpr uint8_t Data = 0x10;    // the CPU read it as data
constexpr uint8_t Dma = 0x20;     // a DMA read it
constexpr uint8_t Code = Opcode | Operand;
constexpr uint8_t All = 0x3F;
} // namespace codeflag

struct CodeCoverage
{
    size_t size = 0;
    size_t opcode = 0, operand = 0, code = 0; // code: either
    size_t both_widths = 0;                   // code bytes run 8- and 16-bit
    size_t data = 0, dma = 0;
    size_t seen = 0; // any flag at all
};

class CodeMap
{
  public:
    CodeMap() = default;
    explicit CodeMap(size_t size) : bytes(size, 0) {}

    size_t size() const { return bytes.size(); }
    bool empty() const { return bytes.empty(); }
    uint8_t at(size_t offset) const { return offset < bytes.size() ? bytes[offset] : 0; }
    void mark(size_t offset, uint8_t flags)
    {
        if (offset < bytes.size())
            bytes[offset] |= flags;
    }
    // Where the tracer writes. Valid until the map is resized or replaced.
    uint8_t *data() { return bytes.data(); }
    const std::vector<uint8_t> &flags() const { return bytes; }

    bool merge(const CodeMap &other); // false (and nothing changes) when the sizes differ
    CodeCoverage coverage() const;
    // Grows whenever a flag is added anywhere (flags are never removed): the set bits.
    uint64_t fingerprint() const;

  private:
    std::vector<uint8_t> bytes;
};

// The file: "S9XCMAP1", the ROM size (u32, little-endian), then runs of
// (flags byte, run length as a LEB128 varint) covering exactly that size.
std::string encode_code_map(const CodeMap &map);
std::optional<CodeMap> decode_code_map(std::string_view file, std::string &error);

// Where ROM offsets show up in the CPU's address space: one address per 4 KB chunk of
// ROM (the bank a game's code normally runs it from), kNone where it isn't mapped.
struct RomAddresses
{
    static constexpr uint32_t kChunk = 0x1000;
    static constexpr uint32_t kNone = 0xFFFFFFFF;
    std::vector<uint32_t> chunks;

    std::optional<uint32_t> cpu_address(uint32_t offset) const
    {
        size_t chunk = offset / kChunk;
        if (chunk >= chunks.size() || chunks[chunk] == kNone)
            return std::nullopt;
        return chunks[chunk] + (offset % kChunk);
    }
};

// The instruction a code byte is part of, as it ran.
struct Instruction
{
    uint32_t start = 0; // the opcode's offset
    int length = 0;
    std::optional<uint32_t> address; // the opcode's CPU address, when mapped
    uint8_t opcode = 0;
    OpClass cls = OpClass::Misc;
    bool both_widths = false; // the map saw it run 8- and 16-bit; this is the 8-bit form, unless only 16 covers the byte
    std::string text;         // "BNE $1A4D"
};

// Walks back from `offset` to the opcode whose instruction covers it. Nothing for bytes
// the map doesn't know as code.
std::optional<Instruction> instruction_at(std::span<const uint8_t> rom, const CodeMap &map, uint32_t offset,
                                          const RomAddresses &where);

// A game's code maps: this person's (the only one they write) and everyone else's, merged.
struct CodeMaps
{
    CodeMap mine;
    CodeMap others;
    std::vector<std::string> errors; // files that couldn't be read or are for another ROM size
};

// The loaded game's code map while playing: this person's, which the tracer records into,
// and everyone else's from the library. Lives on the emulation thread, between frames.
class CodeMapKeeper
{
  public:
    static constexpr uint64_t kSaveEveryFrames = 3600; // a minute, while the map grows

    // A game was loaded: maps of another size start over.
    void reset(size_t rom_size, CodeMaps loaded = {});
    void tick(); // every frame

    CodeMap &mine() { return own; }
    const CodeMap &mine() const { return own; }
    CodeMap combined() const; // everyone's
    CodeCoverage coverage() const { return combined().coverage(); }

    bool save_due(); // once a minute, when mine grew since the last save; starts the minute over
    bool unsaved() const { return own.fingerprint() != saved_print; }
    void saved(); // call after writing mine()

  private:
    CodeMap own, others;
    uint64_t saved_print = 0;
    uint64_t frames = 0;
};

} // namespace corrupt
