#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Jump tables (roadmap #25): the indirect jumps a game takes, recorded by the code map's
// tracer, and the tables of routine addresses behind them. A pointer swap (#32) replaces
// one entry with another entry of the same table, so the jump still lands on a real routine.
// Plan: docs/corruption/2026-11-01-pointer-swap-plan.md.
namespace corrupt
{

constexpr uint32_t kNoEntry = 0xFFFFFFFF;

// The four indirect jumps the tracer notes.
namespace jumpop
{
constexpr uint8_t JmpIndexed = 0x7C; // JMP (a,X): the pointer at PB:a+X
constexpr uint8_t JsrIndexed = 0xFC; // JSR (a,X)
constexpr uint8_t JmpPointer = 0x6C; // JMP (a): the pointer at $00:a, usually RAM
constexpr uint8_t JmlPointer = 0xDC; // JML [a]: a 3-byte pointer at $00:a
constexpr bool noted(uint8_t op) { return op == JmpIndexed || op == JsrIndexed || op == JmpPointer || op == JmlPointer; }
} // namespace jumpop

// One indirect jump as it ran: where the instruction is (a ROM offset), where the pointer
// it used was in ROM (kNoEntry when it was in RAM), and where it went (a 24-bit address).
struct JumpKey
{
    uint32_t site = 0;
    uint32_t entry = kNoEntry;
    uint32_t target = 0;
    bool operator==(const JumpKey &) const = default;
};

struct JumpKeyHash
{
    size_t operator()(const JumpKey &k) const
    {
        uint64_t h = (uint64_t(k.site) << 40) ^ (uint64_t(k.entry) << 20) ^ k.target;
        return size_t(h * 0x9E3779B97F4A7C15ULL);
    }
};

struct JumpSeen
{
    uint8_t opcode = 0;
    uint32_t hits = 0;
};

// Every indirect jump seen, with how often. Lives on the emulation thread; the tracer
// notes into it while frames run. Only ever grows, so logs from several people and
// sessions combine by adding them up.
class JumpLog
{
  public:
    void note(uint32_t site, uint8_t opcode, uint32_t entry, uint32_t target)
    {
        auto &seen = jumps[JumpKey{ site, entry, target }];
        if (!seen.hits++)
            seen.opcode = opcode;
    }
    void add(const JumpKey &key, JumpSeen seen); // hits added up
    void merge(const JumpLog &other);
    void clear() { jumps.clear(); }

    size_t size() const { return jumps.size(); } // distinct jumps
    bool empty() const { return jumps.empty(); }
    const std::unordered_map<JumpKey, JumpSeen, JumpKeyHash> &all() const { return jumps; }
    // Grows when a new jump is seen; hits alone don't change it.
    uint64_t fingerprint() const { return jumps.size(); }

  private:
    std::unordered_map<JumpKey, JumpSeen, JumpKeyHash> jumps;
};

// The file: JSON, {"format": "snes9x-jumps", "version": 1, "rom_size": N, "jumps": [{"site":
// "0x050160", "op": "7C", "entry": "0x0501A0" or null, "target": "0xC51234", "hits": 12}]},
// in site order, so a person's file diffs well in the shared library.
std::string encode_jump_log(const JumpLog &log, size_t rom_size);
std::optional<JumpLog> decode_jump_log(std::string_view file, size_t rom_size, std::string &error);

// Where each 4 KB block of the CPU's 24-bit address space is in the ROM (kNone: not ROM).
// The reverse of RomAddresses: every mirror maps, not only the bank code normally runs from.
struct RomOffsets
{
    static constexpr uint32_t kBlock = 0x1000;
    static constexpr uint32_t kNone = 0xFFFFFFFF;
    std::vector<uint32_t> blocks; // 4096 entries, or empty

    std::optional<uint32_t> offset(uint32_t address) const
    {
        size_t block = (address & 0xFFFFFF) / kBlock;
        if (block >= blocks.size() || blocks[block] == kNone)
            return std::nullopt;
        return blocks[block] + (address % kBlock);
    }
};

enum class JumpKind
{
    Direct,     // the jump read its pointer from the ROM table itself
    ThroughRam, // the game copied the entry into RAM first; the entry is inferred
};

struct JumpEntry
{
    uint32_t offset = 0; // the entry's first byte in the ROM
    uint32_t target = 0; // the 24-bit address it holds (bank from the table for 2-byte entries)
    uint32_t hits = 0;   // 0 for likely entries
    bool seen = false;   // the game jumped through it; false: likely (it points at a routine that ran)
};

// The routine addresses one dispatcher jumps through.
struct JumpTable
{
    uint32_t site = 0; // the jump instruction's ROM offset
    uint8_t opcode = 0;
    JumpKind kind = JumpKind::Direct;
    int stride = 2;                 // bytes per entry: 2, or 3 with the bank
    std::vector<JumpEntry> entries; // in ROM order, seen and likely

    size_t seen() const;
    size_t likely() const { return entries.size() - seen(); }
    uint32_t first() const { return entries.empty() ? 0 : entries.front().offset; }
    uint32_t last() const { return entries.empty() ? 0 : entries.back().offset + uint32_t(stride) - 1; }
    // The bytes an entry holds for `target`: low, high (, bank).
    std::vector<uint8_t> bytes_for(uint32_t target) const;
};

struct JumpFindings
{
    std::vector<JumpTable> tables; // by site, then ROM order
    size_t pointer_jumps = 0;      // targets through RAM no table explains (pointers built in code)
    size_t ambiguous = 0;          // targets whose value is in more than one data-read spot
};

// Walk out at most this many entries past the seen ones, either way.
constexpr int kLikelyReach = 64;
// Entries of one site farther apart than this are separate tables.
constexpr uint32_t kTableGap = 0x400;

// The tables behind the log, from the ROM as it was when recorded (the clean ROM) and the
// code map's flags (everyone's). Direct entries are known; entries through RAM are the
// ROM bytes holding the target's value that the CPU has read as data, when exactly one spot
// does, or exactly one of several lines up with the site's other entries. Likely entries
// fill the gaps and continue a table while each points at a routine that has run (just
// not through this entry), in the same bank, and their own bytes aren't code. Anything
// looser walks off through plain data: in HiROM nearly any 16-bit value lands in ROM.
JumpFindings find_jump_tables(const JumpLog &log, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                              const RomOffsets &offsets);

} // namespace corrupt
