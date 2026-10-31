#pragma once
#include "code_map.h"
#include "rom_blame.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace corrupt
{

// A changed ROM byte that has proven itself: the game used it often, and no crash followed.
struct Survivor
{
    uint32_t offset = 0;
    std::optional<uint32_t> address; // where the CPU sees it
    uint8_t was = 0, now = 0;
    uint32_t frames_used = 0;
    uint64_t first_used = 0;
    uint8_t flags = 0;    // its code map flags (codeflag::All)
    std::string before;   // the instruction it belongs to, clean ("BNE $1A4D"); empty for data
    std::string after;    // ...and as it is now ("BEQ $1A4D")
};

constexpr uint32_t kSurvivorFrames = 100;

// Changed bytes used in at least `min_frames` frames with no crash in the 15 s after the
// first use, newest first. `clean` and `now` are the whole ROM.
std::vector<Survivor> find_survivors(const RomBlame &blame, std::span<const uint8_t> clean, std::span<const uint8_t> now,
                                     std::span<const uint8_t> flags, const RomAddresses &where,
                                     uint32_t min_frames = kSurvivorFrames);

} // namespace corrupt
