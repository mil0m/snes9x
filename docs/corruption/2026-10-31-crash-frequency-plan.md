# Crash frequency map: implementation plan (follow-up to roadmap #19)

**Status (2026-10-31):** done (tasks 1–5), pending QA.md section 18.

The Danger overlay shows a crash *rate*, so a block that crashed once and was never
calm looks as alarming as one that crashed 40 times, and nothing shows which blocks are
actually hot (what Avoid dangerous bytes skips). Mikael asked for a frequency map to
lay over the pixel view of memory.

## Decisions

- **A Crashes overlay**, next to Danger: how many crashes each block was blamed for,
  logarithmic against the memory's busiest block, starting at a quarter strength so a
  single crash still shows (like Hits). Gold, so it doesn't read as Danger's red.
- **The snapshot carries the counts per block** (`crash_blocks`, `hot_blocks`, one entry
  per 16-byte block) besides the per-byte `danger` it already has. Every view can use them
  through `MemoryPane`.
- **Hot blocks are marked in the pixel view** under Danger and Crashes: a red tick in the
  address gutter beside each row that holds one (visible at 1×), and an outline around
  each hot block from 4× zoom up.
- **The hover label shows the block's numbers** whenever the map knows it, whatever
  the overlay: `0x1F35 = 0x3A · block 0x1F30: 5 crashes, 12 calm, danger 0.26, hot`.
- The engine and the files don't change.

## Tasks

1. `CrashMapKeeper::block_counts(t, size)` and `hot_blocks(t, size)`; the snapshot fields;
   the session fills them. Tests.
2. `MemoryPane`: `Overlay::Crashes`, an `OverlayScale` (busiest hits and crashes)
   replacing `max_hits`. Pixel, hex and tile views. Tests.
3. Pixel view: hot ticks and outlines. Tests and PNGs.
4. Viewer: the overlay entry, the hover text. Tests.
5. Docs: README, QA.md section 18, findings here.

## Findings while building

- `pixelRect()` is empty for bytes scrolled off sideways, so the outlines are computed
  from the row and column directly, or a block at the window's edge would lose its frame.
- The tile view picks up Crashes through `overlayLevel` like the other overlays, but has
  no hot marks or crash hover yet: a tile spans 16–64 bytes, so it would need to sum
  blocks. Left for when someone asks.
- Mikael's "frequency map" turned out to mean how often the *game* reads and writes
  memory, not crashes: that's the next plan, `2026-10-31-access-map-plan.md`.
