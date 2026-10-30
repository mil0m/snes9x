#pragma once

// The corruption tooling's one hook into the emulator core. cpuexec.cpp defines
// the pointer and calls it at the start of every frame (V=1, right after
// S9xStartScreenRefresh); front ends that don't corrupt leave it null, so every
// Snes9x build links without corrupt/.
//
// Why frame start and not VBlank start: during VBlank most games copy their
// sprite table and palette from WRAM into OAM/CGRAM (and upload tiles), which
// would overwrite PPU corruption before it is ever drawn. At V=1 the game's
// VBlank work is done and nothing of the new frame has been drawn yet.
extern void (*S9xCorruptFrameHook)(void);
