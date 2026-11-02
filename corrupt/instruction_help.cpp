#include "instruction_help.h"
#include "opcodes.h"

#include <array>
#include <optional>
#include <utility>

namespace corrupt
{

namespace
{

constexpr std::pair<std::string_view, std::string_view> kWords[] = {
    // Sums and counting.
    { "ADC", "Adds a number to A, the main register, plus the carry. Health, scores and positions often go through it." },
    { "SBC", "Subtracts a number from A, minus the borrow (the carry turned around)." },
    { "INC", "Adds 1, to A or to a value in memory: a counter going up." },
    { "DEC", "Subtracts 1, from A or from a value in memory: a counter or a timer going down." },
    { "INX", "Adds 1 to X, an index register: often the next entry of a table, or a loop counter." },
    { "INY", "Adds 1 to Y, an index register: often the next entry of a table, or a loop counter." },
    { "DEX", "Subtracts 1 from X: often a loop counting down." },
    { "DEY", "Subtracts 1 from Y: often a loop counting down." },
    // Bits.
    { "AND", "Keeps only the bits of A that are also set in the value: masks bits off." },
    { "ORA", "Turns on, in A, the bits that are set in the value." },
    { "EOR", "Flips, in A, the bits that are set in the value." },
    { "BIT", "Tests bits of a value against A without changing either; the next branch decides on the result." },
    { "TRB", "Turns off, in memory, the bits that are set in A." },
    { "TSB", "Turns on, in memory, the bits that are set in A." },
    { "ASL", "Shifts the bits left: doubles the value." },
    { "LSR", "Shifts the bits right: halves the value." },
    { "ROL", "Rotates the bits left, through the carry." },
    { "ROR", "Rotates the bits right, through the carry." },
    // Comparing.
    { "CMP", "Compares A with a value; the next branch decides on the result (equal, smaller, bigger)." },
    { "CPX", "Compares X with a value, for the next branch: often a loop's end or a limit." },
    { "CPY", "Compares Y with a value, for the next branch: often a loop's end or a limit." },
    // Loading and storing.
    { "LDA", "Loads a value into A, the main register." },
    { "LDX", "Loads a value into X, an index register (often a counter, or which entry of a table)." },
    { "LDY", "Loads a value into Y, an index register (often a counter, or which entry of a table)." },
    { "STA", "Stores A into memory." },
    { "STX", "Stores X into memory." },
    { "STY", "Stores Y into memory." },
    { "STZ", "Stores zero into memory: clears it." },
    // Copying between registers.
    { "TAX", "Copies A into X." },
    { "TAY", "Copies A into Y." },
    { "TXA", "Copies X into A." },
    { "TYA", "Copies Y into A." },
    { "TXY", "Copies X into Y." },
    { "TYX", "Copies Y into X." },
    { "TCD", "Copies A into the direct page register: moves where the fast variables are." },
    { "TDC", "Copies the direct page register into A." },
    { "TCS", "Copies A into the stack pointer." },
    { "TSC", "Copies the stack pointer into A." },
    { "TSX", "Copies the stack pointer into X." },
    { "TXS", "Copies X into the stack pointer." },
    { "XBA", "Swaps the two bytes of A." },
    // Decisions.
    { "BCC", "Jumps if the carry is clear: after a compare, if the value was smaller." },
    { "BCS", "Jumps if the carry is set: after a compare, if the value was bigger or the same." },
    { "BEQ", "Jumps if the last result was zero: after a compare, if they were equal." },
    { "BNE", "Jumps if the last result wasn't zero: after a compare, if they were not equal." },
    { "BMI", "Jumps if the last result was negative (its top bit set)." },
    { "BPL", "Jumps if the last result was positive or zero (its top bit clear)." },
    { "BVC", "Jumps if the overflow flag is clear." },
    { "BVS", "Jumps if the overflow flag is set." },
    { "BRA", "Always jumps (a short jump that ignores any decision)." },
    { "BRL", "Always jumps, further than BRA can." },
    // Jumps and calls.
    { "JMP", "Jumps to another place in the code and carries on there; it doesn't come back." },
    { "JML", "Jumps to another place, in any bank; it doesn't come back." },
    { "JSR", "Calls a function in this bank; it comes back here when the function returns (RTS)." },
    { "JSL", "Calls a function anywhere in the game; it comes back here when it returns (RTL)." },
    { "RTS", "Returns from a function called with JSR." },
    { "RTL", "Returns from a function called with JSL." },
    { "RTI", "Returns from an interrupt, like the once-a-frame NMI." },
    // The stack.
    { "PHA", "Pushes A onto the stack: saves it for later." },
    { "PLA", "Pulls A back from the stack." },
    { "PHX", "Pushes X onto the stack: saves it for later." },
    { "PLX", "Pulls X back from the stack." },
    { "PHY", "Pushes Y onto the stack: saves it for later." },
    { "PLY", "Pulls Y back from the stack." },
    { "PHB", "Pushes the data bank (which bank plain addresses read from) onto the stack." },
    { "PLB", "Pulls the data bank back from the stack: changes which bank plain addresses read from." },
    { "PHD", "Pushes the direct page register onto the stack." },
    { "PLD", "Pulls the direct page register back from the stack." },
    { "PHK", "Pushes the bank the code is running in onto the stack." },
    { "PHP", "Pushes the flags onto the stack: saves them for later." },
    { "PLP", "Pulls the flags back from the stack." },
    { "PEA", "Pushes a 16-bit number onto the stack." },
    { "PEI", "Pushes a 16-bit value from a fast variable onto the stack." },
    { "PER", "Pushes an address near this instruction onto the stack." },
    // Flags and modes.
    { "CLC", "Clears the carry, usually just before an add." },
    { "SEC", "Sets the carry, usually just before a subtract." },
    { "CLI", "Lets interrupts happen again." },
    { "SEI", "Stops interrupts from happening." },
    { "CLD", "Turns decimal mode off: sums are plain binary again." },
    { "SED", "Turns decimal mode on: sums count in decimal digits (scores, clocks)." },
    { "CLV", "Clears the overflow flag." },
    { "REP", "Turns flags off: usually makes A, or X and Y, 16-bit for what follows." },
    { "SEP", "Turns flags on: usually makes A, or X and Y, 8-bit for what follows." },
    { "XCE", "Switches between the old 6502 mode and the 65816's own mode." },
    // Blocks and the rest.
    { "MVN", "Copies a block of memory from one place to another, counting up." },
    { "MVP", "Copies a block of memory from one place to another, counting down." },
    { "NOP", "Does nothing." },
    { "WDM", "Reserved: does nothing." },
    { "BRK", "Breaks into the game's break handler: in a running game, usually a crash." },
    { "COP", "Calls the coprocessor handler: rare in games." },
    { "WAI", "Waits for the next interrupt: often waiting for the next frame." },
    { "STP", "Stops the CPU: the game freezes for good." },
};

bool is_call(std::string_view m)
{
    return m == "JSR" || m == "JSL";
}

bool is_jump(std::string_view m)
{
    return m == "JMP" || m == "JML" || is_call(m);
}

// "$012C" -> 300; nothing for text that isn't hex.
std::optional<uint32_t> hex_value(std::string_view digits)
{
    if (digits.empty() || digits.size() > 6)
        return std::nullopt;
    uint32_t v = 0;
    for (char c : digits)
    {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0)
            return std::nullopt;
        v = v * 16 + uint32_t(d);
    }
    return v;
}

std::string operand_words(Mode mode, std::string_view m, std::string_view operand)
{
    std::string op(operand);
    switch (mode)
    {
    case Mode::Implied: return {};
    case Mode::Accumulator: return "It works on A itself.";
    case Mode::ImmM:
    case Mode::ImmX:
    case Mode::Imm8:
    {
        std::string out = op + " is the number itself";
        if (operand.size() > 2 && operand.substr(0, 2) == "#$")
            if (auto v = hex_value(operand.substr(2)))
                out += ": " + std::to_string(*v);
        if (m == "REP" || m == "SEP")
            out += ", the flags to change";
        return out + ".";
    }
    case Mode::Direct: return op + " is one of the game's fast variables (the direct page).";
    case Mode::DirectX: return op + " is an entry in a small table of fast variables; X picks the entry.";
    case Mode::DirectY: return op + " is an entry in a small table of fast variables; Y picks the entry.";
    case Mode::DirectInd: return op + " goes through a pointer: the address stored in a fast variable.";
    case Mode::DirectIndX: return op + " goes through a pointer picked by X from a table of fast variables.";
    case Mode::DirectIndY: return op + " goes through a pointer stored in a fast variable, then Y steps along from it.";
    case Mode::DirectLong: return op + " goes through a long pointer (any bank) stored in fast variables.";
    case Mode::DirectLongY: return op + " goes through a long pointer stored in fast variables, then Y steps along.";
    case Mode::Stack: return op + " is a value on the stack, often something a function was handed.";
    case Mode::StackIndY: return op + " goes through a pointer kept on the stack, then Y steps along.";
    case Mode::Absolute:
        if (is_call(m))
            return op + " is the function it calls.";
        if (is_jump(m))
            return op + " is where it jumps to.";
        return op + " is a value in memory at that address.";
    case Mode::AbsoluteX: return op + " is an entry of a table at that address; X picks the entry.";
    case Mode::AbsoluteY: return op + " is an entry of a table at that address; Y picks the entry.";
    case Mode::AbsInd: return op + " goes wherever the address stored there points.";
    case Mode::AbsIndX: return op + " is a jump table: a list of addresses, and X picks which one it goes to.";
    case Mode::AbsLongInd: return op + " goes wherever the long address stored there points.";
    case Mode::Long:
        if (is_call(m))
            return op + " is the function it calls (bank and address).";
        if (is_jump(m))
            return op + " is where it jumps to (bank and address).";
        return op + " is a value in memory at that full address (bank and address).";
    case Mode::LongX: return op + " is an entry of a table at that full address; X picks the entry.";
    case Mode::Relative:
    case Mode::RelLong: return m == "PER" ? op + " is the address it pushes." : op + " is where it jumps to.";
    case Mode::Block: return op + " are the banks it copies from and to.";
    }
    return {};
}

} // namespace

std::string explain_mnemonic(std::string_view mnemonic)
{
    for (auto &[m, words] : kWords)
        if (m == mnemonic)
            return std::string(words);
    return {};
}

std::string explain_instruction(uint8_t opcode, std::string_view text)
{
    std::string_view m = op_info(opcode).mnemonic;
    std::string out = explain_mnemonic(m);
    auto space = text.find(' ');
    std::string_view operand = space == std::string_view::npos ? std::string_view() : text.substr(space + 1);
    while (!operand.empty() && operand.front() == ' ')
        operand.remove_prefix(1);
    if (!operand.empty())
        if (auto words = operand_words(op_info(opcode).mode, m, operand); !words.empty())
            out += "\n" + words;
    return out;
}

std::string explain_change(ChangeKind kind)
{
    switch (kind)
    {
    case ChangeKind::Number:
        return "Changes the number it uses: a different speed, damage, timer, count or limit. The code runs the same, "
               "with another value.";
    case ChangeKind::Branch:
        return "Changes a decision: make it go the other way, always, or never. Doors that never open, checks that "
               "always pass, enemies that never turn.";
    case ChangeKind::Math:
        return "Turns a sum or a bit operation around: add becomes subtract, more becomes less, and the like.";
    case ChangeKind::Skip:
        return "Skips the call: the function it calls doesn't run from here (it still can from elsewhere).";
    case ChangeKind::Redirect:
        return "Sends the call to another function of the game: that one runs here instead.";
    }
    return {};
}

} // namespace corrupt
