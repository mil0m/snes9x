#pragma once
#include "opcodes.h"
#include "ops.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace corrupt
{

struct HotMask;

// Which ROM bytes a rule picks from, by what the code map knows about them.
enum class Role
{
    Any,     // every byte in the range (no aim)
    Opcode,  // where an instruction starts
    Operand, // inside an instruction: its value or address
    Code,    // either
    Data,    // read by the CPU as data
    Dma,     // copied by DMA
    Used,    // anything the game touched
};
const char *role_name(Role r);
std::optional<Role> parse_role(std::string_view name);

struct Aim
{
    Role role = Role::Any;
    std::vector<OpClass> classes; // only bytes of instructions of these classes (implies code)
    std::vector<OpClass> except;  // never these
    // Only bytes in the bodies of the game's busiest functions (#60): changes that land in
    // code the game is running all the time, inside functions.
    bool in_busy_functions = false;
    // Only bytes where a change is low risk (#77): what the changed value goes into next was
    // measured to stop the game rarely (corrupt/change_risk.h). Inside known functions only.
    bool low_risk = false;

    bool active() const
    {
        return role != Role::Any || !classes.empty() || !except.empty() || in_busy_functions || low_risk;
    }
};

// The opcodes `op` may be swapped with, itself included, in opcode order: the same
// addressing mode (so the same length at every width) and the same family (ADC SBC AND
// ORA EOR CMP LDA BIT; the shifts with INC DEC; the branches; ...). Alone: just itself.
std::span<const uint8_t> swap_group(uint8_t op);

constexpr bool is_code_op(OpType t)
{
    return t == OpType::OpcodeSwap || t == OpType::BranchFlip || t == OpType::NopOut;
}

constexpr bool is_conditional_branch(uint8_t op) { return (op & 0x1F) == 0x10; } // BPL BMI ... BEQ
constexpr uint8_t flipped_branch(uint8_t op) { return uint8_t(op ^ 0x20); }

// The ROM offsets in [first, last] that `aim` picks, in order. Code run at both widths
// is never picked: changing it is the classic instant crash. No aim: every offset.
std::vector<uint32_t> aimed_bytes(std::span<const uint8_t> rom, std::span<const uint8_t> flags, uint32_t first,
                                  uint32_t last, const Aim &aim);

// What a code op picks from: opcodes (conditional branches for branch_flip), filtered by
// the aim's classes.
std::vector<uint32_t> code_op_targets(OpType type, std::span<const uint8_t> rom, std::span<const uint8_t> flags,
                                      uint32_t first, uint32_t last, const Aim &aim);

// Applies a code op to the ROM at the given opcode offsets. Bytes `skip` marks are left
// alone (a NOP-out skips the whole instruction).
std::vector<WriteEvent> apply_code_op(const Op &op, MemoryAccess &mem, std::span<const uint8_t> flags,
                                      const std::vector<uint32_t> &opcodes, Rng &rng, const HotMask *skip = nullptr);

} // namespace corrupt
