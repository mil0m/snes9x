#pragma once
#include "crash_map.h"
#include "function_body.h"
#include "recipe.h"

#include <cstdint>
#include <optional>
#include <string>
#include <span>
#include <vector>

// Guards (#58): the functions you play with are kept clean of every other rule and of paint.
// A function is in play while the recipe has a switch, a change or a protect for it, or while
// it's switched off from the Functions window. Plan: docs/corruption/2026-11-02-function-changes-plan.md.
namespace corrupt
{

struct InPlay
{
    uint32_t entry = 0;
    bool deep = false; // the functions it calls are guarded too
};

// The recipe's functions in play, plus `window_off` (switched off from the window, not kept),
// each once, in entry order; deep if any of its reasons says so.
std::vector<InPlay> functions_in_play(const Recipe &recipe, const std::vector<uint32_t> &window_off);

// The bytes guarded: the bodies of the functions in play, and for deep ones the bodies of the
// functions their bodies call (through `calls`, transitively, at most `deep_limit` more each).
HotMask guard_mask(const std::vector<InPlay> &in_play, const FunctionBodies &bodies, const CallLog *calls,
                   size_t deep_limit = 32);

// The bytes your own play wrote, which a guard never cleans: pins, switches' entries and
// changes' bytes.
HotMask own_bytes(const Recipe &recipe);

// What going up cleans: guarded bytes that differ from the clean ROM and aren't your own,
// as (offset, clean value).
std::vector<std::pair<uint32_t, uint8_t>> guard_cleanup(const HotMask &guarded, const HotMask &own,
                                                        std::span<const uint8_t> clean, std::span<const uint8_t> now);

// The Functions overlay (#58): a funcmark per ROM byte, and what hover says about a byte.
namespace funcmark
{
constexpr uint8_t Body = 1;    // in a known function's body
constexpr uint8_t Guarded = 2; // in a guarded body
constexpr uint8_t Off = 3;     // the entry of a function that's off now
} // namespace funcmark

struct ShownFunction
{
    uint32_t entry = 0;
    std::optional<uint32_t> address; // where it's called, for showing
    std::string label;               // its names (#59), empty if nobody named it
    size_t bytes = 0;
    bool guarded = false;
    bool off = false;
    std::vector<std::pair<uint32_t, uint32_t>> runs;
};

// Marks for every byte: bodies, guarded bytes over them, off entries over those.
std::vector<uint8_t> function_marks(const std::vector<ShownFunction> &functions, const HotMask &guarded,
                                    size_t rom_size);
// The function a byte belongs to: a guarded one first, then the smallest. Null if none.
const ShownFunction *function_at(const std::vector<ShownFunction> &functions, uint32_t offset);

} // namespace corrupt
