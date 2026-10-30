#pragma once
#include "crash_map.h"
#include "memory.h"
#include "ops.h"
#include "trigger.h"

#include <string>
#include <vector>

namespace corrupt
{

enum class Pick
{
    Fresh,  // new random addresses on every firing
    Sticky, // pick once, then keep hitting the same addresses
};

struct Rule
{
    std::string name;
    AddressRange range;
    std::string region; // named region the range came from, kept for saving; empty if literal
    Op op;
    Trigger when;
    uint32_t count = 1;
    Pick pick = Pick::Fresh;
    bool enabled = true;
    double intensity = 1.0; // 0..1, scales count and the op's strength
    bool avoid_hot = false; // skip addresses the crash map has learned are dangerous
    uint64_t salt = 0;      // mixed into this rule's seed; re-rolling one rule changes it
};

// What a rule remembers between frames. Reset when the recipe is (re)started.
struct RuleRuntime
{
    OpState op_state;
    std::vector<uint32_t> sticky;
    uint64_t firings = 0;
};

// Runs one rule for one frame. `frame` counts from activation, starting at 1.
// `scale` multiplies the rule's intensity (the recipe's intensity). With rule.avoid_hot,
// picked addresses that `hot` marks are dropped (not redrawn), so rules without it, or
// an empty mask, replay exactly as before.
std::vector<WriteEvent> tick_rule(const Rule &rule, RuleRuntime &rt, MemoryAccess &mem,
                                  uint64_t seed, uint32_t rule_index, uint64_t frame,
                                  uint16_t pressed_hotkeys, double scale = 1.0, const HotMask *hot = nullptr);

std::optional<std::string> validate_rule(const Rule &rule, const MemoryAccess &mem);

} // namespace corrupt
