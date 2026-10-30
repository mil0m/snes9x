#pragma once

// The corruption tooling's one hook into the emulator core. cpuexec.cpp defines
// the pointer and calls it at the start of every VBlank; front ends that don't
// corrupt leave it null, so every Snes9x build links without corrupt/.
extern void (*S9xCorruptFrameHook)(void);
