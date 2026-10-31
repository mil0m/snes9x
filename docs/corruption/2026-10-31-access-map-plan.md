# WRAM access map: implementation plan

**Status (2026-10-31):** done (tasks 1–6), pending QA.md section 19.

Goal: show how often the *game* reads and writes each byte of WRAM, as overlays on the
memory viewer (pixels, hex, tiles) and numbers on hover. The Activity overlay can't: it
compares memory between frames, so it misses writes of the same value and every read.
Knowing what the game reads all the time (and what it only writes) is the best hint
for where corruption will show up.

## Decisions

- **WRAM first** (Mikael, 2026-10-31). VRAM, OAM, CGRAM and ARAM later if this proves useful.
- **Counted in the core, guarded by one null pointer.** `S9xWramReads` and
  `S9xWramWrites` (`uint32[0x20000]` each, null unless the viewer watches WRAM), defined
  in cpuexec.cpp next to `S9xCorruptFrameHook`. Off, every access pays one load and a
  branch; measured below.
- **Where it counts:**
  - `getset.h`: the direct-pointer paths of `S9xGetByte`, `S9xGetWord`, `S9xSetByte` and
    `S9xSetWord`. All CPU reads and writes of WRAM (and its mirrors in banks 00-3F)
    go through these, and so do DMA transfers *into* WRAM from the PPU.
  - `dma.cpp`: CPU-to-PPU DMA reads its source through a base pointer, not
    `S9xGetByte`, so `S9xDoDMA` counts the source range as reads once per transfer. This is the
    big one: the per-frame copy of the sprite table and palette from WRAM.
  - `ppu.cpp`: the WMDATA port (`$2180`), which games use to fill WRAM or decompress into it,
    often by DMA from ROM.
- **Not counted (documented):** instruction fetches (code running from WRAM is read
  through `CPU.PCBase`), HDMA tables, and the SA-1's own accesses.
- **Rates, not totals.** Between frames, `MemoryWatch` folds the counters into a decaying
  average of accesses per frame (the Activity overlay's 0.98 decay), then zeroes them. So
  the overlay shows what the game does *now*, and the hover reads "read 3.2/frame".
- **Two overlays, Reads (blue) and Writes (green)**, logarithmic against the memory's
  busiest byte, like Hits. Empty for other targets: the hover says so.

## Tasks

1. Core: the counters and the hooks in getset.h, dma.cpp, ppu.cpp. A headless test on
   the test ROM: its main loop polls $10 and its NMI increments $11.
2. Glue (`corrupt/core_access`): `WramAccessCounter` that turns counting on and off and
   hands its counts to the watch. `MemoryWatch::observe_accesses` (rates, zeroing).
   Tests.
3. Session: count while the viewer watches WRAM, fold between frames. `corrupt-smoke
   --access` prints the busiest blocks on a real game (expect the OAM shadow read by DMA).
4. Qt: `Overlay::Reads`, `Overlay::Writes`, hover. Tests and PNGs.
5. Speed: corrupt-smoke fps on a real game, master vs hooks off vs hooks on.
6. Docs: README, QA.md section 19, findings here.

## Findings while building

- **DMA into WRAM through WMDATA skips `S9xSetPPU`**: dma.cpp calls `REGISTER_2180`
  directly, so the write count lives in `REGISTER_2180` (ppu.h), not in the port's case.
- **Speed with the hooks off: no measurable cost.** 12,000 frames, 3 runs each: master
  FF6 15.35 s / Pocky 12.51 s; hooks in but off 15.72 / 12.76; hooks compiled out
  (`S9X_WRAM_COUNTING` defined to `false`) 15.99 / 12.93. The machine drifted
  more than the hooks cost. Marked `__builtin_expect` unlikely anyway.
- **Speed with counting on:** `--access` over 12,000 frames took FF6 26.3 s (vs 15.8 off)
  and Pocky 23.3 s (vs 12.9): about 0.9 ms a frame, which includes corrupt-smoke's own
  summing next to the watch's folding of 2 x 128K counters. Fine against a 16.7 ms frame;
  if it ever matters, fold only every few frames.
- **Real games (`corrupt-smoke ... 600 1800 --access`).** FF6's sprite table copy at
  `0x300..0x51F` is read exactly 1.00 times a byte a frame and its palette copies too: the
  DMA counting works. Pocky & Rocky reads its sprite copy 0.73 times a frame (it skips
  the DMA on lag frames). FF6's title scene touches only ~2,400 bytes of WRAM in 10 s;
  Pocky's gameplay touches nearly all of it (46,566 bytes only written: buffers
  filled and sent elsewhere).
- At 1× a single blue dot on full-brightness grey was hard to see, so every overlay now
  dims the bytes to 45% under it (the pixel view only).
