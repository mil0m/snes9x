#pragma once
#include <cstdint>
#include <vector>

// Tiny hand-assembled LoROM test programs. Each one turns the screen and the
// frame interrupt on, counts frames in $7E:0011 from its NMI handler, and waits
// for $7E:0010 to become $42, which is when it crashes in its own way.
namespace testrom
{

enum class Crash
{
    None,    // never crashes, even with $42
    Stop,    // STP
    NmiOff,  // switches NMI off and spins
    Reboot,  // jumps back to its own reset code, in native mode, and boots again
};

// No "runaway" program: every jump into I/O space or open bus we tried found its way back
// into ROM within a frame (open-bus bytes happen to branch), so a once-per-frame PC
// sample never sees it. Such crashes surface as STP, NMI off, a frozen picture, or a
// self-reboot - the last of which isn't detected yet.

constexpr uint32_t kTriggerAddr = 0x10; // WRAM
constexpr uint8_t kTriggerValue = 0x42;
constexpr uint32_t kFrameCounterAddr = 0x11; // WRAM, incremented by NMI

std::vector<uint8_t> build(Crash crash);

// Crash::None, whose NMI handler also runs what the code map test looks for: REP, a
// 16-bit immediate, a 16-bit read from a ROM table, SEP, an 8-bit immediate, an 8-bit
// read, and a DMA of 32 bytes from ROM to VRAM. ROM offsets (the ROM starts at $8000).
std::vector<uint8_t> build_traced();
namespace traced
{
constexpr uint32_t kNmi = 0x0040;       // the handler's REP
constexpr uint32_t kTable = 0x1000;     // $9000: 16-bit read at +0, 8-bit at +0x10
constexpr uint32_t kDmaSource = 0x2000; // $A000
constexpr int kDmaBytes = 32;
} // namespace traced

// Crash::None, whose main loop calls a routine only while $7E:0010 is $42: code the game
// rarely reaches, like a new area. The routine counts its runs in $7E:0012 and clears
// the trigger. ROM offsets.
std::vector<uint8_t> build_rare();
namespace rare
{
constexpr uint32_t kRoutine = 0x0060; // INC $12 (E6 12); STZ $10; RTS
constexpr uint32_t kRunsAddr = 0x12;  // WRAM
} // namespace rare

// Crash::None, whose NMI handler dispatches both ways, picking entry ($11 & 3) each frame:
// JSR ($9100,X) straight through a ROM table of 4 routines (each INC $20+i; RTS), then
// LDA $9200,X / STA $14 / ... / JMP ($0014) through a copy in RAM of another table of 4
// (each INC $30+i, then JMP back). The ROM table has a fifth entry the game never uses,
// pointing at routine 0 (a likely entry); both tables end with $0000. ROM offsets.
std::vector<uint8_t> build_dispatch();
namespace dispatch
{
constexpr uint32_t kSite = 0x0046;        // JSR ($9100,X)
constexpr uint32_t kTable = 0x1100;       // $9100
constexpr uint32_t kRoutines = 0x0100;    // $8100 + 0x10 * i
constexpr uint32_t kRamSite = 0x0053;     // JMP ($0014)
constexpr uint32_t kRamTable = 0x1200;    // $9200
constexpr uint32_t kRamRoutines = 0x0180; // $8180 + 0x10 * i
constexpr uint32_t kCounts = 0x20;        // WRAM: runs of routine i at $20 + i
constexpr uint32_t kRamCounts = 0x30;     // and $30 + i
constexpr int kEntries = 4;
} // namespace dispatch

// Crash::None, whose main loop calls a routine every frame (instant revert, #51): it bumps
// $7E:0014, counts down X from $50 (160 instructions, so a change it ran is long out of the
// window), then BRA +0, LDA $DB00 (whose operand's second byte is STP), INC $13, RTS. A
// branch offset of 2 lands on that STP: the middle of an instruction. ROM offsets.
std::vector<uint8_t> build_derail();
namespace derail
{
constexpr uint32_t kRoutine = 0x0060;   // INC $14 (E6 14)
constexpr uint32_t kInnocent = 0x0061;  // its operand: $14 -> $15 is harmless
constexpr uint32_t kBranch = 0x0068;    // BRA's offset byte (80 00)
constexpr uint32_t kStp = 0x006B;       // LDA $DB00's high byte
constexpr uint32_t kRunsAddr = 0x13;    // WRAM: runs of the routine's end
} // namespace derail

} // namespace testrom
