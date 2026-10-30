# Memory viewer with a heatmap: implementation plan (roadmap #18)

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md),
*Qt UI*, "Memory viewer" (design doc phase 4).

**Status (2026-10-30):** done (tasks 1–6), pending QA.md section 12. Map and pixel view follow-ups done too (sections 14 and 15).

Goal: see where a game keeps its live data, where the recipe has been writing, and where
the game keeps copies of its sprites and palette (where corruption sticks), then turn a
selected range into a rule without typing addresses.

## Decisions

- **Only while open, only one target.** The viewer watches one target at a time, and only
  while its window is open, so a closed viewer costs nothing. Watching WRAM means one
  128 KB compare per frame, between frames.
- **Activity is a decaying heat.** Each frame, a byte that changed gets +1 and every heat
  decays by 2 %, so a byte that changes every frame settles near 50 and a quiet one fades
  in about 2 seconds. The colour scale is logarithmic, so rare changes still show.
- **Hits are a running count since the viewer opened** (or *Clear*), taken from the
  engine's event log, so the recipe's footprint stays visible after it moves on.
- **The UI reads a snapshot**: bytes, heat and hits, copied under the session lock about
  10 times a second.
- **Copies of sprites and palette** are found by matching the current OAM and CGRAM
  contents against WRAM: runs of 32 bytes or more, ignoring stretches with fewer than 3
  different byte values (WRAM and palettes are full of zeros, which would match anything).
- **Danger (the crash map) waits for #19**, and naming a selection waits for #16; the
  viewer leaves room for both.

## Tasks

1. `corrupt/memory_watch.{h,cpp}`: `MemoryWatch` with `watch(target, size)`,
   `observe(mem, events)`, `clear_hits()` and `snapshot()`. Tests: a byte changing every
   frame heats toward 50 and fades when it stops; untouched bytes stay at 0; hits count
   only this target's events and only new ones; switching target resets; snapshot sizes.
2. `find_copies(haystack, needle, min_run)`: maximal matching runs, longest first. Tests:
   an embedded copy is found at the right offsets; a partial copy; zero and single-value
   stretches don't match; overlapping candidates merge into one run.
3. Session: `watchMemory(target)` / `stopWatching()`, fed from `betweenFrames`;
   `memorySnapshot()`; `findCopies()` for OAM and CGRAM in WRAM.
4. `MemoryView` widget: a hex grid painting only the visible rows, cells coloured by the
   chosen overlay, an overview strip of the whole target, mouse selection (drag,
   shift-click), `scrollTo(offset)`. Offscreen tests: the offset under a point, drag and
   shift-click selections, the overview click, and PNGs.
5. `MemoryViewer` window: target, overlay (Activity / Hits / None), go to address,
   *Clear hits*, *Find sprite and palette copies* (a list; a click selects the copy), and
   *New rule from selection* (the rule editor with the range filled in). Opened from the
   panel.
6. Docs and QA.

## Findings while building

- **Copies are real and worth it.** `corrupt-smoke --copies` (added for this) found the
  sprite table copy in Pocky & Rocky, FE4, FF6 and Goof Troop, and full palette copies in
  all four (two in Pocky & Rocky, likely the live palette and a fade target). Palette drift
  on Goof Troop's WRAM copy changed 791 of 900 frames, against 19 on CGRAM itself.
- **Repetitive tables match themselves.** Sprite tables are rows of similar 4-byte entries,
  so FF6's OAM matched its WRAM copy at 207 shifts. Runs mostly inside a longer kept run
  are now dropped: one result per real copy.
- **AddressSanitizer earned its keep.** The widget tests crashed now and then inside GLib;
  ASan showed an out-of-bounds write in the test's own sample helper (writing hits at
  `0x1000` into a 512-byte palette snapshot). `./build-mac.sh asan` now runs every suite
  under it, and all are clean, including the real-core tests that patch the opcode tables.
- **The overview strip needs room.** At a narrow width the hex cells painted over it;
  the view now has a minimum width that fits the grid beside the strip, and a test checks it.
- **Watching costs about 0.4 ms a frame** for WRAM in a release build, and nothing while
  the viewer is closed.

## Follow-up: the memory map (2026-10-30)

Asked for after #18: a list of what lives where, like other emulators' memory viewers.

- `corrupt/memory_map.{h,cpp}`: `fixed_map(target)` for the hardware layout every game
  shares; `video_map(VideoLayout)` and `sound_map(SoundLayout)` for what the game uses now.
  Unit-tested, including mode 7, clipping at the end of VRAM, a wrapped second sprite table,
  an echo buffer of length 0, and FF6's real layout.
- `SnesCoreAccess::video_layout()` / `sound_layout()` read the PPU (layer bases are word
  addresses in the core, sprite bases bytes) and the DSP (ESA, EDL, FLG, DIR).
- **A layer's tiles end where the next area starts.** 1024 tiles is only the most a layer
  can use, and games pack VRAM tightly; listing the maximum made BG1's tiles in FF6 run
  over its sprite tiles.
- `corrupt-smoke --map` prints the map with how much of each area holds data. On Goof
  Troop and FF6 every area does (FF6: BG1 tiles 0x6000-0xBFFF, echo buffer 0xCD00-0xF4FF).
- The viewer shows the map as a sidebar, live areas in italics; the session re-reads the
  live layout between frames while VRAM or ARAM is watched. QA.md section 14.


## Follow-up: the pixel view (2026-10-30)

Asked for after the map: see a whole memory at once, each byte one pixel coloured by its
value, zoomable. (Decoding VRAM tiles with the palette is a later, separate view.)

- **A toggle, not a new window.** *Hex / Pixels* in the viewer switches between the two
  views of the same snapshot. The selection, the Map, named areas, copies, Go to and
  New Rule from Selection work the same in both; switching keeps the selection and the
  place you were looking at. Both views implement one small interface (`MemoryPane`),
  so the viewer talks to whichever is showing.
- **Zoom is pixels per byte**: 1, 2, 3, 4, 6, 8, 12, 16. One row holds as many bytes as
  fit, rounded down to a power of two (16 at least), so 4-, 8- and 16-byte tables line up
  in columns. At 1:1 a 600-pixel-wide view shows 512 bytes a row and all of WRAM in 256
  rows. Zoom with **+ / −** buttons or **Ctrl+wheel**, which keeps the byte under the
  mouse in place; the plain wheel scrolls.
- **Colours.** `corrupt/byte_colour.{h,cpp}` (no Qt): 0x00 black, 0xFF white, the rest
  along a hue ramp whose brightness rises with the value, so runs of equal bytes, fills,
  gradients and repeating tables show as bands and stripes, and close values look alike.
  With the Activity or Hits overlay on, bytes drop to a dim grey and the heat is painted
  over them in the overlay's colour, so the overlay reads at a glance; *Plain* shows the
  full colours.
- **Addresses** on the left every few rows; hovering shows the byte's address and value.
- **Painting** builds a small image of the visible rows (one pixel per byte) and draws it
  scaled without smoothing, so cost follows the window, not the target.

Tasks:

1. `byte_colour(value)` and `byte_grey(value)`: 0 black, 0xFF white, every value
   distinct, brightness rising, neighbours close. Unit tests.
2. `PixelView` widget: zoom, columns from the width, `offsetAt`/`pixelRect` both ways at
   several zooms, drag and shift-click selection, `select`/`scrollTo`, Ctrl+wheel zoom
   around the cursor, hover. Offscreen tests and PNGs.
3. Viewer: the Hex / Pixels toggle and zoom buttons; the selection and position carry
   over; the Map, regions, copies and Go to select in the view that's showing. Tests and
   a PNG of the whole window in pixel mode.
4. Docs: README, QA.md section 15, findings here.

Done 2026-10-30 (QA.md section 15). Findings:

- **The colours are cubehelix**, a ramp with a turning hue whose brightness rises
  steadily, so 0x00 is black and 0xFF white and every value is distinct. On real dumps
  (`corrupt-smoke --dump`, drawn by the widget tests) FF6's WRAM shows its fills, tables
  and compressed buffers as clearly different bands, and VRAM its tile bitplanes.
- **A row is no wider than square.** Sizing rows only by the window made a 512-byte
  palette 4 rows high at 4×. Capping the row at the power of two at or above √size gives
  cgram and oam 32 bytes a row (one palette, eight sprite entries), and leaves WRAM and
  VRAM alone.
- **A selection can be one pixel high at 1:1**, so a bar beside the selected rows marks it.
  The image also gets a thin frame, or a 0xFF fill melts into a white window.
- **1:1 is the default**, because that is where all of WRAM fits in the window.
- Not done, maybe later: a chosen row width (stride), to line up a tile map's rows, and
  the VRAM tile view that decodes graphics with the palette.
