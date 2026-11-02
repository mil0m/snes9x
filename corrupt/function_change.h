#pragma once
#include "calls.h"
#include "code_map.h"
#include "function_body.h"
#include "patches.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Change a function (#60, with #35 redirection): instead of turning it off, change how it
// works. Every instruction of its body that can become something of the same length that
// still runs is an option: its number, its branch, its math, or one of its calls (skipped, or
// sent to another function). Plan: docs/corruption/2026-11-02-function-changes-plan.md.
namespace corrupt
{

// One instruction of a body that can change, and how.
struct ChangeOption
{
    uint32_t at = 0; // the instruction's opcode
    std::optional<uint32_t> address; // its CPU address, for showing
    uint8_t length = 0;
    uint8_t opcode = 0;
    ChangeKind kind = ChangeKind::Number;
    std::string text; // "ADC #$01"
    // Number: the value's bytes, after the opcode (1 or 2, as it ran) and its value now.
    int width = 1;
    uint32_t value = 0;
    // Math: the opcode it swaps with (the same addressing mode); a number's too when it has one
    // (ADC #$01 can also become SBC #$01). 0: none.
    uint8_t other = 0;
    // A call (kind Skip; it can also be redirected): where it goes (a ROM offset, if the ROM
    // holds it) and how it returns.
    std::optional<uint32_t> target;
    CallType call = CallType::Near;
};

// Every instruction of `body` that can change, in offset order, read from the clean ROM.
// Numbers: the immediates of LDA/LDX/LDY/ADC/SBC/AND/ORA/EOR/CMP/CPX/CPY/BIT (not REP, SEP
// and the like), but not ones the map saw at both widths. Branches: the conditional ones.
// Math: the pairs below. Calls: JSR $abs and JSL $long (their targets found through `where`).
std::vector<ChangeOption> change_options(const FunctionBody &body, std::span<const uint8_t> rom,
                                         std::span<const uint8_t> flags, const RomAddresses &where);

// The math pairs: ADC/SBC, INC/DEC, INX/DEX, INY/DEY, ASL/LSR, ROL/ROR, CLC/SEC, AND/ORA. The
// partner of `op` in the same addressing mode, if it has one.
std::optional<uint8_t> math_partner(uint8_t op);

// The bytes each change writes, starting at the option's opcode.
std::vector<uint8_t> number_bytes(const ChangeOption &o, uint32_t value); // the opcode kept, the value after
enum class BranchTo
{
    Opposite, // BNE <-> BEQ...
    Always,   // BRA, the same distance
    Never,    // NOP NOP
};
std::vector<uint8_t> branch_bytes(const ChangeOption &o, BranchTo to);
std::vector<uint8_t> math_bytes(const ChangeOption &o);
std::vector<uint8_t> skip_bytes(const ChangeOption &o); // NOPs over the whole call
// The call sent to `entry`: a near call needs the new function in the same bank as the call
// (nullopt if it isn't, or `entry` has no CPU address); a long call takes any.
std::optional<std::vector<uint8_t>> redirect_bytes(const ChangeOption &o, uint32_t entry, const RomAddresses &where);

// A change kept for a recipe: the option, the bytes it writes now, and what was there.
Change make_change(uint32_t function, const ChangeOption &o, ChangeKind kind, std::vector<uint8_t> value,
                   std::span<const uint8_t> clean_rom);

// "number", "branch", "math", "skip", "redirect".
const char *change_kind_name(ChangeKind k);
std::optional<ChangeKind> parse_change_kind(std::string_view name);

// A change is on while the ROM holds its value.
bool change_on(const Change &c, const MemoryAccess &mem);

} // namespace corrupt
