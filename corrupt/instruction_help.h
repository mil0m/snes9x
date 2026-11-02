#pragma once
#include "patches.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace corrupt
{

// Plain words for the 65816's instructions (#71), for players who don't read assembly: the
// Change It… window's tooltips, and anywhere else an instruction is shown.

// What a mnemonic does ("LDA": "Loads a value into A, the main register."), or empty for
// one the 65816 doesn't have.
std::string explain_mnemonic(std::string_view mnemonic);

// What the instruction does, then (on a second line) what its operand is: the number, the
// variable, the table entry, the pointer, where a branch or call goes. `text` is how the
// disassembler wrote it ("ADC #$01"); the opcode gives the addressing mode.
std::string explain_instruction(uint8_t opcode, std::string_view text);

// What a kind of change does to the game.
std::string explain_change(ChangeKind kind);

} // namespace corrupt
