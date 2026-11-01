# Safety first: plan (roadmap phase 7, #50-#52)

**Status (2026-11-01):** #50 done (QA.md section 27). From Erik's list (sorted in
[the horizons doc](./2026-10-31-horizons.md#eriks-list-2026-11-01)): see what the game uses
live, catch a bad change sooner, and test a change before it reaches the game.

## #50 ROM live heat

**Goal.** In the memory viewer, on the ROM: what the game runs and reads *right now*
lights up and fades over a second and a half, so you can watch which code and tables a
scene uses, and see a corrupted area come into play. Today's Code, Data and DMA overlays
show everything ever seen; WRAM already has fading Reads and Writes (#23).

**How.** The code map's tracer and the core's read hooks already set a *touched* bit on
every ROM byte the game uses (the crash blame reads it for changed bytes, then clears it).
While the viewer shows the ROM:

- Every frame, after the crash blame has collected, `RomHeat::observe` scans the map for
  touched bits (8 bytes at a time), stamps each touched byte with the frame and clears the
  bit. The blame only reads the bits of changed bytes, and it has already done so for the
  frame, so nothing is lost to it.
- The first scan after the viewer starts watching the ROM only clears: bits set while
  nobody watched would otherwise light up everything at once.
- When the viewer pulls a snapshot (10 times a second), the stamps become one level per
  byte (0-255), fading linearly over `kFadeFrames` (90, 1.5 s). Only then: no per-frame
  work beyond the scan.
- **Live** overlay (ROM only), a bright yellow; hover says *in use right now* or *used in
  the last second or two*.

Cost: only while the viewer shows the ROM. The scan is one pass over the map a frame
(6 MB for FF6, about 0.3 ms with 8-byte words); stamps are 4 bytes per ROM byte.

**Not in it:** live heat for VRAM, OAM, CGRAM and ARAM. The PPU reads VRAM all the time to
draw, so "what's read" there means little; what the CPU writes there comes from WRAM and
ROM, which already show it.

**Tests.** `RomHeat`: stamps and clears touched bytes, fades to nothing after the fade,
the first scan after a pause lights nothing, the blame's bits on changed bytes still work
when it collects first. Real core: on the test ROM, the NMI handler and its table glow
after a frame and the padding doesn't. Viewer: the Live overlay is offered for the ROM only,
paints from the snapshot's levels, and the hover says so.

**How it went.** As planned, plus one catch: the crash blame clears the touched bits of
*changed* bytes before the heat scan runs, so corrupted bytes in use wouldn't have glowed;
`RomBlame::used_this_frame()` hands them over and the heat stamps them too. On Goof
Troop's title and demo the running code and tables glow, and graphics fade after each DMA
(tour shots `07b-viewer-rom-live`, `07c-viewer-rom-jumps`).

## #51 Instant revert, #52 Test before applying

Planned in their own sections before they're built.
