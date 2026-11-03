#pragma once
#include "area_ops.h"
#include "calls.h"
#include "code_aim.h"
#include "crash_map.h"
#include "memory.h"
#include "ops.h"
#include "pointer_swap.h"
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
    Aim aim;                // ROM only: pick bytes by their role in the code map
    uint64_t salt = 0;      // mixed into this rule's seed; re-rolling one rule changes it
};

// What a rule remembers between frames. Reset when the recipe is (re)started.
struct RuleRuntime
{
    OpState op_state;
    std::vector<uint32_t> sticky;
    uint64_t firings = 0;
    // A kill with put_back (#61): writes to undo at a frame, if the byte still holds them.
    struct PutBack
    {
        uint64_t frame;
        WriteEvent write;
    };
    std::vector<PutBack> put_backs;
};

// Whether the rule picks from what the game was seen doing: the code map (an aim, or a code
// op) or the jump tables (a jump op).
inline bool needs_aim(const Rule &rule)
{
    return rule.aim.active() || is_code_op(rule.op.type) || is_jump_op(rule.op.type) || is_function_op(rule.op.type);
}

// For a rule that needs aim: the ROM offsets it may pick (worked out once per run), and
// the code map, which code ops read widths from. A jump op gets its slots instead, and a
// function op the functions it may turn off.
struct AimedBytes
{
    const std::vector<uint32_t> *bytes = nullptr;
    std::span<const uint8_t> flags;
    const std::vector<SwapSlot> *swaps = nullptr; // not "slots": a Qt macro
    const std::vector<Function> *functions = nullptr;
};

// The offsets the rule may pick from the ROM and code map as they are now. With
// aim.in_busy_functions, only those `busy` marks (none when it's null).
std::vector<uint32_t> aim_rule(const Rule &rule, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                               const HotMask *busy = nullptr);

// Runs one rule for one frame. `frame` counts from activation, starting at 1.
// `scale` multiplies the rule's intensity (the recipe's intensity). With rule.avoid_hot,
// writes to bytes that `hot` marks are skipped, and every other write is the same as without
// it (values are still drawn for the skipped ones). Rules without it, or an empty mask,
// replay exactly as before. Every rule skips the bytes `locked` marks (pass them inside `hot`
// too, for avoiding rules).
std::vector<WriteEvent> tick_rule(const Rule &rule, RuleRuntime &rt, MemoryAccess &mem,
                                  uint64_t seed, uint32_t rule_index, uint64_t frame,
                                  uint16_t pressed_hotkeys, double scale = 1.0, const HotMask *hot = nullptr,
                                  const HotMask *locked = nullptr, const AimedBytes *aimed = nullptr);

std::optional<std::string> validate_rule(const Rule &rule, const MemoryAccess &mem);

// The op as a rule at `intensity` (0..1, its own times the recipe's) runs it: Add's range and
// Flip bits' count shrink toward +-1, never to nothing; other ops are unchanged.
Op scaled_op(const Op &op, double intensity);

// Makes the rule cover a rectangle with its top-left at `start` (range.last follows).
void set_rect(Rule &rule, Target target, uint32_t start, Rect rect);

} // namespace corrupt
