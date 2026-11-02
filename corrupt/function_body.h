#pragma once
#include "calls.h"
#include "code_map.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

// A function's body (roadmap #58): the bytes of its own code, from its entry to its returns,
// found by walking the code map (no tracer cost). What guards keep other corruption off, what
// "Change a function" (#60) lists, and what the Functions overlay shows.
// Plan: docs/corruption/2026-11-02-function-changes-plan.md.
namespace corrupt
{

// One instruction of a body, as the code map saw it run.
struct BodyInstruction
{
    uint32_t at = 0;
    uint8_t length = 0;       // the longest it ran (an instruction run at both widths: its 16-bit form)
    bool both_widths = false; // ran 8- and 16-bit: changes leave it alone
};

struct FunctionBody
{
    static constexpr size_t kMaxBytes = 4096;

    uint32_t entry = 0;
    std::vector<BodyInstruction> code; // in offset order
    size_t bytes = 0;                  // distinct bytes the instructions cover
    bool truncated = false;            // the walk reached kMaxBytes
    bool open_ended = false;           // it leaves through an indirect jump: some of it may be elsewhere

    bool contains(uint32_t offset) const; // a byte of one of its instructions
    // Its bytes as runs of [first, last], in order.
    std::vector<std::pair<uint32_t, uint32_t>> runs() const;
};

// Walks the code map from `entry`: only bytes the map knows as opcodes are followed;
// conditional branches both ways, BRA/BRL to their target, JMP $abs to its target in the same
// bank (needs `where`); past calls (JSR, JSL) when the next instruction ran; stops at returns,
// indirect and long jumps, BRK, STP and WDM. An entry that isn't a known opcode: an empty body.
FunctionBody function_body(std::span<const uint8_t> rom, std::span<const uint8_t> flags, uint32_t entry,
                           const RomAddresses &where, size_t max_bytes = FunctionBody::kMaxBytes);

// Bodies for a game's functions, by entry.
using FunctionBodies = std::unordered_map<uint32_t, FunctionBody>;
FunctionBodies function_bodies(std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                               const std::vector<Function> &functions, const RomAddresses &where);

} // namespace corrupt
