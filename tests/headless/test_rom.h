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

// Crash::None, whose main loop turns overscan ($2133 bit 2) on while the beam is on lines
// 200-232 and off elsewhere, as a corrupted game once did: VBlank should start at line 225
// with overscan off and at 240 with it on, and this dodges both. The NMI still counts frames.
std::vector<uint8_t> build_overscan_flip();

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

// Crash::None with a fuse (made for #55, since removed; the guard's tests still use it). Each frame the main loop ticks the fuse
// ($7E:0015: while it's set it counts down, and STP when it reaches 0), and while $7E:0010
// is $42 calls a rarely run routine: LDA #$00 / STA $15 (the fuse stays out), INC $16 (a
// harmless count), INC $12, STZ $10. A changed immediate lights the fuse: the CPU stops that
// many frames later, far from the change (no landing). ROM offsets.
std::vector<uint8_t> build_fuse();
namespace fuse
{
constexpr uint32_t kRoutine = 0x0060;  // LDA #$00 (A9 00)
constexpr uint32_t kLight = 0x0061;    // its immediate: nonzero lights the fuse for that many frames
constexpr uint32_t kInnocent = 0x0065; // INC $16's operand: $16 -> $17 is harmless
constexpr uint32_t kFuseAddr = 0x15;   // WRAM
constexpr uint32_t kRunsAddr = 0x12;   // WRAM: runs of the routine
} // namespace fuse

// Crash::None for the blame after a landing (#57). Each frame the main loop calls two
// routines: BRA +0 / LDA #$EA / RTS (a branch offset of 1 lands on the immediate, a NOP: the
// middle of an instruction, harmless), then INC $16 / INC $12 / RTS. ROM offsets.
std::vector<uint8_t> build_landing();
namespace landing
{
constexpr uint32_t kBranch = 0x0061;    // BRA's offset byte (80 00)
constexpr uint32_t kLanding = 0x0063;   // LDA #$EA's immediate: a NOP when run
constexpr uint32_t kInnocent = 0x0081;  // INC $16's operand: $16 -> $17 is harmless
constexpr uint32_t kRunsAddr = 0x12;    // WRAM: runs of the second routine
} // namespace landing

// Crash::None for the call graph and kill switches (#26, #34). Each frame the main loop
// calls: a near function (INC $20; RTS), a long one (INC $21; RTL), a mixed one twice
// (PHK / JSR and JSL, to INC $22; RTL), and one that skips a byte of data after its JSR
// (REP #$20 / PLA / INC A / PHA / SEP #$20 / INC $23 / RTS; the byte is $00, a BRK if run).
// ROM offsets.
std::vector<uint8_t> build_calls();
namespace calls
{
constexpr uint32_t kNear = 0x0060;   // $8060
constexpr uint32_t kLong = 0x0070;   // $8070
constexpr uint32_t kMixed = 0x00A0;  // $80A0
constexpr uint32_t kInline = 0x00B0; // $80B0
constexpr uint32_t kNearSite = 0x0012, kLongSite = 0x0015, kMixedNearSite = 0x001A, kMixedLongSite = 0x001D,
                   kInlineSite = 0x0021;
constexpr uint32_t kCounts = 0x20; // WRAM: runs of near, long, mixed (twice a frame), inline at $20-$23
} // namespace calls

// A main loop calling a function with a body worth changing (#58, #60): step adds 1 to a
// counter with ADC #$01, counts every other frame past a BEQ, and calls a helper; another
// near function is called beside it, for redirecting the helper's call.
std::vector<uint8_t> build_bodies();
namespace bodies
{
constexpr uint32_t kStep = 0x0060;   // $8060
constexpr uint32_t kAdc = 0x0063;    // ADC #$01: the number is at kAdc + 1
constexpr uint32_t kBeq = 0x0069;
constexpr uint32_t kCall = 0x006D;   // JSR $80A0
constexpr uint32_t kHelper = 0x00A0; // $80A0
constexpr uint32_t kOther = 0x00C0;  // $80C0
constexpr uint32_t kStepBytes = 17;
// WRAM: $20 step's counter, $21 helper runs, $22 the branch's count, $23 other runs.
constexpr uint32_t kCounter = 0x20, kHelperRuns = 0x21, kBranchRuns = 0x22, kOtherRuns = 0x23;
} // namespace bodies

} // namespace testrom
