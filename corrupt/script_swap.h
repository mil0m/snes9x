#pragma once
#include "ops.h"
#include "script_engines.h"

#include <optional>
#include <vector>

// Script ops (roadmap #44): corrupt the game's scripts in their own vocabulary. A swap gives a
// command byte another command of the same engine and the same length, so the next command
// still starts where it did; a nudge adds a little to one of a command's arguments. Both only
// touch places #43 trusts: seen run, steady (every step from there was the command's length),
// of a command with a known length that doesn't branch. Plan:
// docs/corruption/2026-11-23-script-engines-plan.md.
namespace corrupt
{

struct HotMask;

// One place a script rule may change.
struct ScriptSlot
{
    uint32_t offset = 0;         // the command byte
    uint8_t value = 0;           // the command in the clean ROM
    int length = 0;              // bytes, the command included
    std::vector<uint8_t> others; // same engine, same length, usable, not `value`
};

// The usable places lying wholly in [first, last], of the engine dispatched from `engine` (a
// site's ROM offset; all engines when empty). For a swap only places with another command to
// become; for a nudge only places with arguments. By offset, each once.
std::vector<ScriptSlot> script_slots(const std::vector<ScriptEngine> &engines, uint32_t first, uint32_t last,
                                     std::optional<uint32_t> engine, bool nudge);

// Gives each picked slot (an index into `slots`) another of its commands, drawn at random.
std::vector<WriteEvent> apply_script_swap(const std::vector<ScriptSlot> &slots, const std::vector<uint32_t> &picks,
                                          MemoryAccess &mem, Rng &rng, const HotMask *skip = nullptr);

// Adds a delta in [min, max], never 0, to one argument byte of each picked slot (wrapping).
std::vector<WriteEvent> apply_script_nudge(const std::vector<ScriptSlot> &slots, const std::vector<uint32_t> &picks,
                                           int min, int max, MemoryAccess &mem, Rng &rng,
                                           const HotMask *skip = nullptr);

} // namespace corrupt
