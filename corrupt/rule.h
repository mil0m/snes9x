#pragma once
#include "area_ops.h"
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
    // A rectangle instead of a plain range: range.first is its top-left byte and range.last
    // its bottom-right one (keep them in step with set_rect).
    std::optional<Rect> rect;
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
// writes to bytes that `hot` marks are skipped, and every other write is the same as without
// it (values are still drawn for the skipped ones). Rules without it, or an empty mask,
// replay exactly as before. Every rule skips the bytes `locked` marks (pass them inside `hot`
// too, for avoiding rules).
std::vector<WriteEvent> tick_rule(const Rule &rule, RuleRuntime &rt, MemoryAccess &mem,
                                  uint64_t seed, uint32_t rule_index, uint64_t frame,
                                  uint16_t pressed_hotkeys, double scale = 1.0, const HotMask *hot = nullptr,
                                  const HotMask *locked = nullptr);

std::optional<std::string> validate_rule(const Rule &rule, const MemoryAccess &mem);

// Makes the rule cover a rectangle with its top-left at `start` (range.last follows).
void set_rect(Rule &rule, Target target, uint32_t start, Rect rect);

} // namespace corrupt
