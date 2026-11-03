#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace corrupt
{

// Memory the engine can corrupt. Offsets are relative to the start of each
// region, so wram:0x0400 is $7E:0400.
enum class Target
{
    WRAM,  // 128 KB work RAM
    SRAM,  // battery save RAM, size depends on the cartridge
    VRAM,  // 64 KB video RAM
    OAM,   // 544 B sprite table
    CGRAM, // 512 B palette, little-endian 15-bit colours
    ARAM,  // 64 KB sound CPU RAM
    ROM,   // the game itself, as a clean .sfc (no copier header), up to 6 MB
};

constexpr size_t kTargetCount = 7;
constexpr std::array<Target, kTargetCount> kAllTargets = {
    Target::WRAM, Target::SRAM, Target::VRAM, Target::OAM, Target::CGRAM, Target::ARAM, Target::ROM
};

// Whether the memory holds the game's code (on the SNES, the ROM). New engine code asks this
// rather than comparing with Target::ROM, so the day another platform comes (#75, parked)
// the question is already in one place. Existing comparisons stay until then.
constexpr bool is_code_memory(Target t) { return t == Target::ROM; }

const char *target_name(Target t);
std::optional<Target> parse_target(std::string_view name);
size_t nominal_size(Target t); // SRAM: the common 8 KB; ROM: 1 MB

// Addresses are written with as many hex digits as the memory can ever need (at least 4),
// so lists, rules and the viewer's gutter line up: wram 0x01A00, rom 0x0C8000, vram 0x1A00.
int address_digits(Target t);
std::string hex_address(uint32_t offset, Target t); // "0x01A00"

// The engine never touches the emulator directly; everything goes through this.
// The emulator's implementation owns cache invalidation (tiles, sprites, colours).
struct MemoryAccess
{
    virtual size_t size(Target t) const = 0;
    virtual uint8_t read(Target t, uint32_t offset) const = 0;
    virtual void write(Target t, uint32_t offset, uint8_t value) = 0;
    // A write that belongs to the run's start (painted patches): the ROM keeper never undoes
    // it on a rollback. Elsewhere it's a plain write.
    virtual void write_at_start(Target t, uint32_t offset, uint8_t value) { write(t, offset, value); }
    // The target's bytes in one piece, when they are (for reading a whole ROM quickly).
    virtual const uint8_t *data(Target) const { return nullptr; }
    virtual ~MemoryAccess() = default;
};

struct AddressRange
{
    Target target = Target::WRAM;
    uint32_t first = 0;
    uint32_t last = 0; // inclusive

    uint32_t length() const { return last - first + 1; }
};

// An error message if the range doesn't fit the loaded game's memory.
std::optional<std::string> validate_range(const AddressRange &range, const MemoryAccess &mem);

std::string hex(uint32_t value);

} // namespace corrupt
