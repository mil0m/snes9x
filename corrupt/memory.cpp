#include "memory.h"

#include <cctype>
#include <cstdio>

namespace corrupt
{

const char *target_name(Target t)
{
    switch (t)
    {
    case Target::WRAM: return "wram";
    case Target::SRAM: return "sram";
    case Target::VRAM: return "vram";
    case Target::OAM: return "oam";
    case Target::CGRAM: return "cgram";
    case Target::ARAM: return "aram";
    }
    return "?";
}

std::optional<Target> parse_target(std::string_view name)
{
    std::string lower;
    for (char c : name)
        lower += char(std::tolower(static_cast<unsigned char>(c)));

    for (auto t : kAllTargets)
        if (lower == target_name(t))
            return t;
    return std::nullopt;
}

size_t nominal_size(Target t)
{
    switch (t)
    {
    case Target::WRAM: return 0x20000;
    case Target::SRAM: return 0x2000;
    case Target::VRAM: return 0x10000;
    case Target::OAM: return 512 + 32;
    case Target::CGRAM: return 512;
    case Target::ARAM: return 0x10000;
    }
    return 0;
}

std::string hex(uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%X", value);
    return buf;
}

std::optional<std::string> validate_range(const AddressRange &range, const MemoryAccess &mem)
{
    auto size = mem.size(range.target);
    std::string name = target_name(range.target);

    if (size == 0)
        return "this game has no " + name;
    if (range.first > range.last)
        return name + " range " + hex(range.first) + ".." + hex(range.last) + " is reversed";
    if (range.last >= size)
        return name + " range ends at " + hex(range.last) + " but " + name + " is only " + hex(uint32_t(size)) + " bytes";
    return std::nullopt;
}

} // namespace corrupt
