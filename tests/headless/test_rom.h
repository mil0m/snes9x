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
};

// No "runaway" program: every jump into I/O space or open bus we tried found its way back
// into ROM within a frame (open-bus bytes happen to branch), so a once-per-frame PC
// sample never sees it. Such crashes surface as STP, NMI off, a frozen picture, or a
// self-reboot - the last of which isn't detected yet.

constexpr uint32_t kTriggerAddr = 0x10; // WRAM
constexpr uint8_t kTriggerValue = 0x42;
constexpr uint32_t kFrameCounterAddr = 0x11; // WRAM, incremented by NMI

std::vector<uint8_t> build(Crash crash);

} // namespace testrom
