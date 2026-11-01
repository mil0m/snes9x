#include "rom_heat.h"
#include "code_map.h"

#include <cstring>

namespace corrupt
{

void RomHeat::reset(size_t size)
{
    used_at.assign(size, 0);
    frame = 0;
    primed = false;
}

void RomHeat::observe(uint8_t *flags, size_t size, std::span<const uint32_t> also_used)
{
    if (size != used_at.size())
        reset(size);
    frame++;
    constexpr uint64_t kTouchedEach = 0x0101010101010101ULL * codeflag::Touched;
    size_t i = 0;
    auto byte = [&](size_t o) {
        if (!(flags[o] & codeflag::Touched))
            return;
        flags[o] &= uint8_t(~codeflag::Touched);
        if (primed)
            used_at[o] = frame;
    };
    for (; i + 8 <= size; i += 8)
    {
        uint64_t word;
        std::memcpy(&word, flags + i, 8);
        if (word & kTouchedEach)
            for (size_t k = 0; k < 8; k++)
                byte(i + k);
    }
    for (; i < size; i++)
        byte(i);
    if (primed)
        for (uint32_t o : also_used)
            if (o < size)
                used_at[o] = frame;
    primed = true;
}

uint8_t RomHeat::level(size_t offset) const
{
    if (offset >= used_at.size() || !used_at[offset])
        return 0;
    uint32_t age = frame - used_at[offset];
    if (age >= kFadeFrames)
        return 0;
    return uint8_t(255 - age * 255 / kFadeFrames);
}

std::vector<uint8_t> RomHeat::levels() const
{
    std::vector<uint8_t> out(used_at.size());
    for (size_t i = 0; i < out.size(); i++)
        out[i] = level(i);
    return out;
}

} // namespace corrupt
