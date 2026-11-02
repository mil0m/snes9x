#pragma once
#include "memory.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace corrupt
{

// Bytes set by hand: painted in the memory viewer, or imported from an edited picture.
// A recipe keeps them as runs of consecutive bytes; they apply when a run starts, like
// at_load rules, so ROM paint is in the game from its first frame.
struct Patch
{
    Target target = Target::ROM;
    uint32_t at = 0;
    std::vector<uint8_t> bytes;
};

// A ROM change kept on purpose, usually a survivor: applied as every run starts, before
// the rules, never blamed or undone by the crash guard. `was` is the clean value, so a pin
// made on another version of the game is skipped instead of scrambling it.
struct Pin
{
    uint32_t offset = 0;
    uint8_t value = 0;
    uint8_t was = 0;
    std::string note;
};

// Merges (offset, value) pairs in; a byte set again takes the new value. Keeps the list
// sorted by target and offset, with runs that never overlap or touch.
// A kill switch (#34): a function's entry, the return that skips it (RTS for a function
// called near, RTL for one called long) and the clean byte it replaces. Its state is the ROM
// byte itself: off while the entry holds the return. With a hotkey (1-9) a press flips it;
// start_off writes the return as every run starts. `was` skips it on another ROM version.
struct Switch
{
    uint32_t entry = 0;
    uint8_t ret = 0x60;
    uint8_t was = 0;
    int hotkey = 0; // 0: none
    bool start_off = false;
    std::string note;
};
inline bool switch_off(const Switch &s, const MemoryAccess &mem)
{
    return s.entry < mem.size(Target::ROM) && mem.read(Target::ROM, s.entry) == s.ret;
}

// A function kept clean on purpose (#58): its body is guarded, so no rule and no paint
// changes it. Deep: the functions it calls are guarded too.
struct Protect
{
    uint32_t entry = 0;
    bool deep = false;
};

void add_patch_bytes(std::vector<Patch> &patches, Target t, const std::vector<std::pair<uint32_t, uint8_t>> &bytes);
void remove_patch_bytes(std::vector<Patch> &patches, Target t, const std::vector<uint32_t> &offsets);
size_t patched_bytes(const std::vector<Patch> &patches);

} // namespace corrupt
