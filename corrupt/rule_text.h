#pragma once
#include "rule.h"

#include <array>
#include <string>

namespace corrupt
{

// Short, readable descriptions for the panel and the rule editor.
std::string describe_op(const Rule &rule);        // "add -2..2 x4"
std::string describe_trigger(const Trigger &t);   // "every 30 frames"
std::string describe_range(const Rule &rule);     // "wram 0x2000..0x2FFF" or "wram enemy_table"
std::string describe_rule(const Rule &rule);      // all three: "wram ...: add -2..2 x4, every 30 frames"

std::string describe_aim(const Aim &aim); // "operands of arith, compare"; empty without an aim

// What "Add Rule" starts from: one random byte every 2 s in upper work RAM,
// clear of the stack and direct page (the same range gentle-wram-noise uses).
Rule default_rule();

// Memory that only knows its sizes, for validating rules outside the emulation
// thread. Starts at the nominal sizes; reads are 0 and writes are dropped.
struct SizedMemory : MemoryAccess
{
    std::array<size_t, kTargetCount> sizes;

    SizedMemory();
    size_t size(Target t) const override { return sizes[size_t(t)]; }
    uint8_t read(Target, uint32_t) const override { return 0; }
    void write(Target, uint32_t, uint8_t) override {}
};

} // namespace corrupt
