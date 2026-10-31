# ROM corruption: plan (roadmap #22)

**Status (2026-10-31):** reviewed by Mikael (answers at the end). Phase 1 done, pending QA.md section 20; phase 2 next.

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md),
phase 5 ("a ROM target applied at load time, or live"). Both modes are in (Mikael, 31 Oct).

## Goal

The gems of corruption are in the **code and data**, not the graphics (Mikael, 31 Oct:
"Code, while brittle, can result in golden moments when it works."). So this is not a
plan for steering corruption away from code. It's a plan for **aiming at code and data
and surviving it**:

1. **Know what every ROM byte is**: code the CPU runs (and which part of an instruction:
   opcode or operand, at the width it actually runs at), data the CPU reads (tables,
   text, scripts), or data sent by DMA (graphics, sound).
2. **Corrupt with that knowledge**: change an instruction's operand but not its opcode,
   swap an opcode for another of the same length so the instruction stream stays
   aligned, flip a branch, NOP out a call, nudge a value in a table. These are where the
   golden moments come from, and they keep the game running far more often than random
   bytes do.
3. **Survive the rest**: when a ROM change crashes the game, find out exactly which
   changed byte did it (only bytes the game ran or read can be guilty), undo or re-roll
   that one byte, and keep every other change.
4. **Keep the gems**: a change that ran thousands of times without a crash is a
   *survivor*. List them, and let a player pin one into the recipe so it's always applied,
   while the rest of the recipe keeps re-rolling.
5. **Edit memory like an image**, the way Mikael and Erik worked before: the ROM as a
   greyscale picture at a byte-exact row width, regions picked by eye, then shifted,
   flipped, filtered or painted over. See *Image-style editing*.

## How it differs from RAM

| | RAM | ROM |
|---|---|---|
| What a byte means | Changes with game state; the game rewrites it | **Fixed**: the same byte is the same code or data in every session, for everyone with that ROM |
| Undoing a change | The game often undoes it; a rollback restores it | **Nothing undoes it.** Save states don't hold the ROM, so a rollback leaves the corrupted byte in place and the crash happens again |
| Blame | Any write in the last 3 s (noisy, as the RAM crash map showed) | **Only bytes the game actually ran or read** since the checkpoint can be guilty: precise |
| Learning | Slow, game-state dependent | Fast and shareable: a code byte that crashes, crashes again |

So the RAM crash map's weakness (noisy blame on state that keeps changing) mostly goes
away on ROM, and its job changes from "avoid" to "find the one byte to undo".

## Decisions (proposed)

- **One new target, `rom`**: offsets into the ROM as Snes9x holds it (`Memory.ROM`, with any
  copier header stripped and the image de-interleaved), i.e. offsets in a clean `.sfc`. The
  viewer and rule editor also show the SNES address (`$C0:1234`) via the memory map.
- **Two modes, one recipe format.** A rule's trigger decides:
  - `at_load` (new trigger): **classic**. The rule fires once, right after the ROM is
    mapped and before the console resets, so the game boots from the corrupted ROM. Same
    seed, same corrupted ROM, every time.
  - every existing trigger (every N frames, chance, hotkey, once at a frame): **live**. ROM
    bytes change while the game runs.
  - A recipe can mix both, and RAM rules too.
- **The pristine ROM is kept** (a copy taken at load, before anything is applied). It
  gives undo, "what changed" diffs, exporting a clean or corrupted `.sfc`, and the game's
  identity: the library folder and every map are keyed by the clean ROM, so corrupting
  the header can't move the game to another folder.
- **The header and vectors are always protected** (`$FFB0-$FFFF` of bank 0's view:
  title, mapping, interrupt vectors; required, Mikael 31 Oct). Mapping is decided at load
  from the clean header anyway, and corrupting the vectors just bricks the boot. Writes
  there are dropped like `avoid_hot` drops hot ones, and the viewer shows the area locked.
- **FF6 is the control game** (Mikael, 31 Oct): the T-Edition 2016 ROM on disk, ExHiROM,
  no coprocessor. Every phase is proven on it first, with `corrupt-smoke`, before the
  others.
- **A ROM edit log** (offset, old, new, frame, rule) per session. On rollback the guard
  restores the checkpoint *and* reverts every ROM edit made after it, so live ROM rules
  roll back like RAM ones. Classic edits made at load are never reverted by a plain
  rollback, only by a policy that blames them.
- **Tracing without touching the core**, as #20 did: wrap every opcode handler in the
  CPU's six opcode tables (E1, M1X1, M1X0, M0X1, M0X0, Slow). The wrapper knows the
  instruction's address (PB:PC − 1) and, from the table, the M and X flags, so it knows the
  real length of the instruction it's about to run. Installed only while something needs
  it; chains with the reset watch (install after, remove before).
- **Data reads and DMA from ROM** extend the WRAM counters (#23): the same null-guarded
  hooks in `getset.h` and `dma.cpp`, now also checking the ROM range. CPU data reads and DMA
  reads are counted separately: tables and scripts vs graphics and samples.
- **Blame by execution.** A bitmap marks every ROM byte that differs from the pristine ROM.
  The tracer and the data hooks check it on every ROM access (one bit test) and stamp the
  frame the changed byte was last *used*. When the guard catches a crash, the suspects
  are the changed bytes used in the blame window. Not the rules that wrote; the bytes.
- **The ROM crash map is per byte for code, per 16-byte block for data.** A code byte's
  role is known exactly, and neighbouring code bytes are unrelated instructions.
  *Survivals* count uses of a changed byte that were followed by 15 calm seconds.
- **Code maps and crash maps live in the library**, one file per person, summed on load,
  like `crashmap.<user>.json`. Code maps are a union (any byte anyone saw run is code).
  They're binary-ish (a ROM can be 6 MB): one byte of flags per ROM byte, run-length
  encoded, so a mostly-unexplored ROM stays small.
- **Determinism.** Classic rules and pinned patches replay exactly from the seed. Rules
  that pick by role (only operands, only executed code) depend on the code map, so like
  `avoid_hot` they replay exactly only while the map is unchanged. Recipes record which map
  version they were saved with, so a replay can warn.
- **Out of scope for v1:** coprocessors that run their own code from ROM (SA-1, Super FX:
  its instruction cache would also need flushing, `fx_flushCache`), and the
  decompression chips (S-DD1, SPC7110), whose data is compressed in ROM. Corrupting their
  ROM still works, but untraced, so blame falls back to "the rule that wrote".

## What each ROM byte can be (the code map)

Per ROM byte, a set of flags gathered while playing:

| Flag | Seen when | What it means for corruption |
|---|---|---|
| `opcode` | the tracer ran an instruction starting here | Swap for a same-length opcode, or NOP the instruction |
| `operand` | inside a traced instruction, after its opcode | Change the value or address it uses; the instruction stream stays aligned |
| `op8` / `op16` | the operand's width when it ran (M/X flags) | A byte run at both widths is a classic crash source; flag it |
| `data` | the CPU read it with a load instruction | Tables, stats, scripts, text, pointers |
| `dma` | a DMA read it | Graphics, tile maps, palettes, sound samples |
| `pointer` | (later) a data read used as an address | Pointer tables: the juiciest and most fragile data |

Plus, per opcode byte, the **instruction class**, from a 256-entry table: arithmetic,
logic, compare, load/store, branch, jump/call, return, stack, flags/width
(`REP`/`SEP`/`XCE`), block move, misc. The class decides what a code-aware op may do.

## Code-aware operations (the juicy part)

New ops for `rom` rules, each picking only bytes of the right role from the code map:

- **`operand`**: change an operand byte (random, add, bitflip, as today). `LDA #$05` becomes
  `LDA #$63`, `STA $0450` becomes `STA $0458`. Changes constants and where things go;
  never misaligns the code. The workhorse.
- **`opcode_swap`**: replace an opcode with another from the same *swap group*: same
  length and addressing mode, different meaning. `ADC↔SBC↔AND↔ORA↔EOR↔CMP`,
  `INC↔DEC`, `ASL↔LSR↔ROL↔ROR`, `BEQ↔BNE↔BCC↔BCS↔BMI↔BPL↔BVC↔BVS`, `LDA↔LDX↔LDY`
  only where the widths agree, `STA↔STZ`. The groups are a hand-checked table with tests.
- **`branch_flip`**: invert a conditional branch (`BEQ↔BNE` and so on). A small change with
  big logic effects: doors that never open, enemies that never die.
- **`nop_out`**: replace a whole instruction with `NOP`s of the same length, so a call, a
  store or a check simply doesn't happen. `JSR` to a sound effect, an HP subtraction, a
  collision check.
- **`nudge`**: add a small delta (±1..±N) to operands or data bytes, for "almost right"
  values: speeds, timers, table entries.
- **Class filters** on any rule: `classes: ["arith", "compare", "branch"]`, or `except:
  ["stack", "width", "return"]`. Without filters everything is fair game; the editor
  suggests leaving out `width` and `stack`, which almost always crash at once, but never
  forces it.

All of these are plain byte writes in the end, so the edit log, rollback, blame and
replays work the same as for any rule.

## Image-style editing (the Photoshop way)

How Mikael and Erik corrupted ROMs before this project (31 Oct): Vinesauce was too random,
so they opened ROMs in Photoshop as greyscale images, one pixel per byte (sometimes less,
for NES games), with the row length set so the data lined up. Then: select a region by
eye, and offset it, flip it, **shift all of it by X** (the favourite), run filters on it,
or paint over it with exact grey values, and load the ROM again. It worked because you
can *see* where an operation should go.

The pixel view already is that canvas (one dot per byte, a chosen row width, zoom). What
it needs is the tools, working on any target, ROM or RAM, and saved as replayable rules
instead of a one-way edit:

- **Rectangle selections**: drag a box in the pixel view. It's stored as start offset,
  row width, width and height, so it means the same region whatever the window size.
  Today's selections are linear ranges; both kinds work.
- **Area operations** act on a whole region at once, not on picked addresses:
  - **shift** by (dx, dy) in the current row width, wrapping or not: every byte moves X to
    the right and Y rows down. Mikael's favourite, and a plain byte offset when dy = 0.
  - **flip** horizontally (each row reversed) or vertically (row order reversed);
    **rotate 180°**.
  - **levels**: remap values through a curve: invert, posterize, threshold, gamma,
    contrast. 256-entry table, so anything a curves dialog can draw.
  - **filters**: blur, sharpen, median, emboss, as 3×3 kernels over the 2D layout. These
    smear neighbouring bytes into each other in a way that keeps local structure.
  - **noise** and **offset** (add) over the region, which the per-address ops already do.
  - **stamp**: copy a region to another place, as a clone stamp (the 2D version of Copy).
- **Paint**: pick a value (with an eyedropper from the image), a brush size, and paint
  in the pixel view. Strokes become patches in the recipe, undoable.
- **Round trip to a real image editor**: export a region (or the whole ROM) as an 8-bit
  greyscale PNG at the row width; import an edited PNG back, and the differences become
  patches. So Photoshop still works, and what comes back is a recipe.
- **Less than a byte per pixel** (NES-style, and SNES bitplanes): a pixel view mode with
  1, 2 or 4 bits per dot, with the area operations working on those values. After the
  rest, if wanted.
- Area operations take the same triggers: `at_load` gives the classic edited ROM, live
  triggers keep re-applying them (a region that keeps sliding is its own kind of fun on
  WRAM or VRAM too).

## Surviving it

- **Policies for ROM suspects**, added to the existing set (re-roll, skip, dampen,
  disable, escalate):
  - **revert**: put the guilty byte(s) back to the pristine value and keep every other
    change. The default for `rom`.
  - **re-roll byte**: give the guilty byte a fresh value from its rule, keeping its role
    (an operand stays an operand). Searches for a version that survives.
  - Repeat crashes with several suspects **bisect** the suspect set (the #21 idea, which is
    cheap here: only the few bytes used in the window are suspects, and reverting is free).
- **Classic mode survives too**: a rollback goes back a second, reverts the suspects the
  policy picked, and plays on. Purists can turn the guard off for a pure classic run.
- **The crash map** marks code bytes and data blocks that crashed when changed, so later
  seeds can be told to avoid *them*. Not code in general, just the bytes known to be fatal.

## Keeping the gems

- **Survivors**: changed bytes the game used at least N times (default 100) without a crash
  within 15 s, newest first, with their role, class and the instruction before and after
  ("`$C2:1A3F  BNE → BEQ`, ran 4,120 times"). In the panel and in `corrupt-smoke`.
- **Pin**: a survivor (or any change) can be pinned into the recipe as a fixed patch:
  `"patches": [{"rom": "0x21A3F", "value": "0xF0", "was": "0xD0", "note": "doors stay shut"}]`.
  Pinned patches apply at load before the rules, are never re-rolled or reverted, and
  `was` lets a recipe refuse a different ROM version.
- **Export**: write the corrupted ROM as a `.sfc` (with the checksum fixed, optionally) for
  sharing outside Snes9x or playing on hardware.

## Viewer

The memory viewer gets `rom` as a target, in hex, pixels and tiles (graphics in ROM show up
in the tile view like VRAM, with a chosen format). Overlays for the code map (**Code**:
opcode and operand bytes in two shades, **Data**, **DMA**), **Changes** (bytes that
differ from the clean ROM), and the ROM crash map. Hovering a code byte shows the
instruction it's part of, disassembled (`$C2:1A3F  D0 0C  BNE $1A4D`), and how often it ran.

## Phases

Each phase is test-first, ends in a pushed, playable build, and gets its QA.md section.
Phase 2 comes before the code map because it's the workflow Mikael and Erik already know
works, and it needs nothing from the tracer.

1. **ROM target, both modes.** `rom` in `MemoryAccess` (write `Memory.ROM`, size, the pristine
   copy), the `at_load` trigger and the boot-from-corrupted-ROM path, header and vector
   protection, the ROM edit log and revert-on-rollback, `rom` in the viewer (hex, pixels,
   tiles), Export ROM. `corrupt-smoke` with a `rom` recipe on real games. *From here on, classic
   and live ROM corruption work with plain ops, and crashes roll back properly.*
2. **Image-style editing.** Rectangle selections, the area operations (shift, flip, rotate,
   levels, filters, stamp) as rules, painting into patches, PNG export and import. On every
   target, proven on FF6's ROM.
3. **The code map.** The opcode-table tracer (opcode/operand/width per byte), CPU data
   reads and DMA from ROM in the access hooks, the instruction class table, a small
   disassembler for hover, the code map file in the library, Code/Data/DMA overlays,
   `corrupt-smoke --codemap` printing coverage on real games. Speed measured with and
   without tracing.
4. **Blame by execution and ROM policies.** The changed-byte bitmap and last-used stamps,
   suspects = changed bytes used in the window, revert and re-roll byte policies, bisect, the
   ROM crash map (per byte for code). The headless test ROM gets a corruptible code path to
   prove it.
5. **Code-aware ops.** `operand`, `opcode_swap` (with the swap-group table), `branch_flip`,
   `nop_out`, `nudge`, class filters, rule editor support. Real-game runs comparing crash
   rates and how long runs last: random bytes vs operand-only vs opcode swaps.
6. **Survivors and pins.** The survivors list, pinning into `patches`, the panel UI, and a
   starter recipe or two built from what works on the test games.

## Phase 1 in detail

**The model: during a run, the ROM is the clean ROM plus that run's edits.**

- **Starting a run** (on, re-roll, another recipe, the saved moment) restores the clean ROM,
  then applies the rules with the `at_load` trigger. If any of them targets the ROM, and the
  run isn't starting from a saved moment, the console is **power-cycled** so the game boots
  from the corrupted ROM (classic). Then the run's anchor is taken, and then the `at_load`
  rules on other targets fire. From a saved moment it's the same order with "load the moment"
  in place of the power cycle, so a replay gets the same ROM and the same RAM.
- **A rollback** reverts the ROM edits made after the checkpoint it went back to (save
  states don't hold the ROM). `at_load` edits are frame 0, so a rollback never undoes them.
- **Giving up** (the guard's last resort) restores the clean ROM too: a byte that keeps
  crashing would otherwise crash the game forever.
- **Turning a recipe off** keeps the damage, like RAM. The panel has **Restore Clean ROM**.

Tasks:

1. `Target::ROM` (`rom`): `CoreMemory` over `Memory.ROM` and `CalculatedSize`, plus a raw `data()`
   pointer for the viewer's fast path. Every switch over targets.
2. The `at_load` trigger: JSON, the rule text, the editor. `Engine::fire_at_load(mem, filter)`
   runs only `at_load` rules, at frame 0, and logs their writes.
3. `RomKeeper`, a `MemoryAccess` over the emulator's: the clean copy, the locked bytes,
   a journal of ROM edits with their frame, `restore_clean`, `revert_after(frame)`,
   `after_guard(outcome)`, a count of changed bytes. The engine also gets the locked bytes as
   a mask that every rule skips (not only `avoid_hot` ones), so locked writes never reach
   the log.
4. `RunControl` runs the start order above; `CoreAccess::reset_console()` (`S9xReset`, then
   re-arm the reboot watch, whose wrapped opcode may have changed).
5. `SnesCoreAccess::locked_rom()`: the ROM bytes behind `$00:FFB0-$FFFF`, whatever the mapping.
6. Session, headless harness and `corrupt-smoke`: the keeper wraps `CoreMemory`; guard outcomes
   go through `after_guard`; **Export ROM…** and **Restore Clean ROM** in the panel.
7. The viewer on a 4-6 MB target: `MemoryWatch` skips the activity heat for the ROM (only
   rules change it) and copies bytes only when they changed; the crash map's per-byte
   danger stays empty while it knows nothing about a target.
8. Tests on the test ROM: a classic patch boots (`INC $11` becomes `DEC $11`, the frame
   counter runs backwards), a live patch that crashes (`WAI` becomes `STP`) is rolled back
   *and reverted*, locked bytes stay put. Then FF6 with `corrupt-smoke`, classic and live.
9. Docs: README, QA.md section 20, a classic and a live starter recipe.

Findings while building phase 1:

- **A test passed for the wrong reason at first**: without the ROM undo, a live `WAI` → `STP`
  still ended with a clean ROM, because the guard gave up and restored it. The test now
  requires exactly one rollback; with the undo commented out it fails.
- **FF6 boots from a classic corrupted ROM**: 3,000 random bytes in the first 512 KB make
  1,645 of 1,800 frames differ from the clean boot (from frame 156). 40 bytes changed
  nothing visible in the intro: sparse classic corruption needs play to show.
- **Export needs the checksum fixed on a copy**, not in the running ROM: the header is
  locked, and the game shouldn't see its own checksum change.
- **Viewer cost on a 6 MB ROM**: the activity heat (24 MB of floats) and per-byte reads are
  skipped for the ROM, hits are allocated on the first hit, the crash map's per-byte danger
  is empty while it knows nothing about a target, and the viewer refreshes a big target
  twice a second instead of ten times.

## Phase 2 in detail

Built in four steps, each pushed:

1. **Rectangles and area ops in the engine.** A rule can cover a rectangle instead of a
   range: `"rect": { "start": "0x1000", "row": 64, "width": 16, "height": 32 }` (top-left
   byte, row width, width and height in bytes). Every existing op works on it, picking
   bytes inside the rectangle. Six new **area ops** act on the whole region at once
   (a plain range counts as one row):
   - `shift`: `dx`, `dy` (bytes, rows; negative is left or up), `wrap` (default true; false
     fills with `value`, default 0).
   - `flip`: `axis` `horizontal`, `vertical` or `both`.
   - `levels`: `curve` `invert`, `posterize` (`amount` levels, 2-128), `threshold`
     (`amount` 0-255), `gamma` (`amount` 0.1-10), `contrast` (`amount` -1..1).
   - `filter`: `kernel` `blur`, `sharpen`, `median`, `emboss`, `edges`, `max`, `min`
     (3×3 over the 2D layout; the edges repeat).
   - `stamp`: copies the same-sized region starting at `from` onto this one.
   - `fill`: every byte to `value`.
   Area ops ignore `count` and `pick`; intensity below 1 is the chance to fire, as for
   one-byte rules. Their writes go through the same masks (locked bytes, avoid_hot).
2. **Rectangle selection in the viewer.** Alt-drag in the pixel view draws a rectangle in
   the current row width; New Rule from Selection makes a rect rule. The rule editor
   shows the rectangle and the new ops.
3. **Paint.** A paint mode in the pixel view: a value (eyedropper with Alt-click), brush
   size, strokes written at once through the session and kept in the recipe as a
   `patches` list that applies when the run starts, like `at_load`.
4. **PNG round trip.** Export a selection (or all) as an 8-bit greyscale PNG at the row
   width; import a PNG of the same size back, and the bytes that differ become patches.

Sub-byte pixels (1, 2 or 4 bits per dot) come after, if wanted.

## Risks

- **Tracer speed.** An extra indirect call on every instruction. Installed only while
  needed (the viewer shows `rom`, a rule needs the code map, or ROM changes exist and the
  guard needs blame). Budget: under 15%. If it's slower, sample (trace one frame in N) for
  the map and keep only the bitmap check for blame.
- **Coverage.** The code map only knows code that has run. A role-aware rule can only pick
  from explored code, which is also the code that matters right now. The map grows as
  you and Erik play, and is shared.
- **Self-modifying or RAM-run code** isn't ROM and isn't traced as ROM; games that copy
  routines to WRAM need the WRAM trace (a later extra using the same tracer).
- **Opcodes run at two widths** (one routine called with both M settings) get both flags;
  operand ops skip them by default.

## Answered questions (2026-10-31)

1. **Earlier tools:** Vinesauce ROM Corruptor first, but it was too random and
   uncontrolled. Then Photoshop: ROMs as greyscale images with byte-exact rows, region
   shifts, flips, filters and painting. That became *Image-style editing* and phase 2.
2. **First game:** FF6 is the control game.
3. **Header and vector protection:** required, so always on.
