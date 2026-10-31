# ROM corruption: plan (roadmap #22)

**Status (2026-10-31):** reviewed by Mikael (answers at the end). Phases 1-4 done, pending QA.md sections 20-23; phase 5 (code-aware ops) next.

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

Findings while building phase 2:

- **Paint can't go through the ROM journal**: the journal is in time order and a rollback
  pops from the end, so a frame-0 entry pushed after live edits would stop the undo early.
  Painted bytes are written without a journal entry (`write_at_start`); only
  `restore_clean` undoes them, and the next start re-applies them from the recipe.
- **A rectangle keeps its own row width**: it's stored as drawn, so changing the view's row
  width afterwards doesn't reshape it (the outline is only drawn while the widths match).
- **The PNG remembers its place** in a text chunk (`snes9x-area`); Qt keeps it through a save
  and load. Image editors may drop it, so Import falls back to the selection, and says the
  size it needs when that doesn't match.

## Phase 3 in detail

**The code map: what every ROM byte is, learned by watching the game run.** Nothing here
changes what corruption does yet; it's the knowledge phases 4-6 aim with.

Per ROM byte, one byte of flags:

| Bit | Flag | Set by |
|---|---|---|
| 0x01 | `opcode` | the tracer: an instruction started here |
| 0x02 | `operand` | the tracer: inside an instruction, after its opcode |
| 0x04 | `op8` | the tracer: part of an instruction whose width depends on M or X, run with that flag set (8-bit) |
| 0x08 | `op16` | the same, run 16-bit. Both bits: the code runs at both widths, a classic crash source |
| 0x10 | `data` | the core: a CPU read that isn't the running instruction's own bytes (tables, text, pointers, vectors) |
| 0x20 | `dma` | the core: a CPU-to-PPU DMA read it (graphics, maps, palettes) |

Built in four steps, each pushed:

1. **The instruction table and disassembler** (`corrupt/opcodes`, no emulator). All 256
   opcodes: mnemonic, addressing mode, class (`arith`, `logic`, `shift`, `compare`, `load`,
   `store`, `transfer`, `branch`, `jump`, `return`, `stack`, `flags`, `width`, `block`,
   `misc`) and what decides their width (M, X or nothing). `op_length(op, m8, x8)` is
   `constexpr`, so the tracer's wrappers get each instruction's length at compile time.
   `disassemble(bytes, m8, x8, address)` gives `BNE $1A4D`, `LDA [$12],Y`, `MVN $7E,$7F`.
   The lengths are checked against the core's own `S9xOpLengths*` tables in the
   real-core tests, so the table can't drift from the emulator.
2. **The code map** (`corrupt/code_map`, no emulator): the flags, coverage counts (opcode,
   operand, code, data, DMA, both widths), merging (a union), `instruction_at(offset)` for
   hover (walks back to the opcode that covers the byte, at the width it ran), and a file
   format: magic, ROM size, then runs of (flags, length as a varint). A 6 MB ROM that's
   mostly unexplored is a few KB. `Library::code_map_path` is `codemap.<user>.bin` next
   to the crash map; loading reads everyone's, skips files for another ROM size, and keeps
   this person's separate (the only one written). `CodeMapKeeper` holds mine and the
   others, saves every minute while mine grows, and on close.
3. **The tracer in the real core** (`corrupt/core_access`):
   - `RomTracer` wraps all 256 opcodes in the six `S9xOpcodes*` tables with
     `template <int Table, int Op>` wrappers (1,536 functions). The table fixes M and X
     (E1, M1X1, M1X0, M0X1, M0X0), so length and width are constants; the slow table
     reads them from P at run time and looks every byte up through the memory map,
     since it's the one used where an instruction crosses a 4 KB block. The fast tables
     find the opcode as `CPU.PCBase + PC - 1`, no map lookup.
   - Chained with the reset watch: the tracer saves whatever is in the table (maybe the
     reset watch's wrapper) and calls it. Installing or moving the reset watch lifts the
     tracer first and puts it back after, so neither ever saves the other's stale entry.
   - Core change, tiny and null-guarded like the WRAM counters: `uint8 *S9xRomFlags`
     (null when off) and the running instruction's ROM span, set by the wrapper.
     `getset.h`'s plain-memory read paths mark `data` unless the byte is inside that span
     (operand fetches through `S9xGetByte` in the slow paths, `JMP (a,X)`, would
     otherwise look like data). `dma.cpp` marks `dma` in the loop it already has for WRAM.
   - The test ROM gets a variant whose NMI runs `REP`, a 16-bit immediate, a 16-bit and an
     8-bit data read from a ROM table and a DMA from ROM, so the real-core tests can check
     exact flags per byte. Also: installing and removing restores every table entry, and
     the reset watch still counts with the tracer on.
   - `corrupt-smoke <rom> <recipe> 0 <frames> --codemap [file]` runs plain, then traced,
     prints frames per second for both, the coverage and the busiest code banks, and can
     write the map. Budget: tracing costs under 15%.
4. **Session and viewer.** The session records whenever a game is loaded (if the speed
   allows; otherwise only while the viewer shows the ROM) into this person's map. The
   snapshot carries the combined map's flags and each 4 KB ROM chunk's CPU address (the
   mapping's canonical bank: `$C0-$FF` for HiROM, `$80-$FF` for LoROM). Overlays **Code**
   (opcodes strong, operands lighter), **Data** and **DMA**; code run at both widths says so
   on hover.
   Hovering a ROM byte in the pixel view adds `$C2:1A3F  D0 0C  BNE $1A4D (branch)` or
   `data`/`dma`. The panel's status line shows the coverage.

Not traced in v1: HDMA tables (read through their own pointers), code run from WRAM or
SRAM (not ROM), and the SA-1's own CPU.

Findings while building phase 3:

- **Tracing is cheap**: 4.3% on FF6 (5 minutes of its intro), 4.7% FE4, 6.4% Pocky & Rocky,
  7.0% Goof Troop, against the 15% budget. So the session records whenever a game is loaded,
  not only while the viewer shows the ROM, and the map grows as people simply play.
- **FF6 T-Edition's title screen runs from WRAM** (a wait loop at `$7E:7195`): 1.7 million
  instructions in two seconds, none of them ROM. The tracer was right; a WRAM trace would
  show such code later if wanted.
- **Coverage after 5 minutes of FF6's intro**: 24,886 code bytes (11,283 opcodes), 156,903 data
  bytes and 115,008 DMA bytes of 6 MB; the map file is 40 KB. Bank `$C0` holds 22,000 of the
  code bytes. No code byte ran at both widths in any of the four games so far.
- **FE4 and Goof Troop DMA almost nothing from ROM**: they unpack graphics into WRAM and DMA
  from there, so for them the DMA overlay is nearly empty and the graphics are found as
  data reads.
- **The core's `pint` is signed**: the first data hook compared a negative offset (a WRAM
  pointer below the ROM) against the ROM size and wrote out of bounds. Offsets are unsigned
  now; the real-core test caught it on the first run.
- **A launch check without a scratch library writes to the real one**: the first app run
  (to generate the scratch config) saved a code map into `~/Documents/Snes9x Corruptions`.
  Removed; generate the config with `LibraryRoot` pointed at scratch next time.

## Phase 4 in detail

**Blame by execution: only changed bytes the game actually ran or read can be guilty.**
Today the guard blames *rules* whose writes fall in the crash window. That fails for ROM:
a classic run writes everything at frame 0, long before any window, so the guard blames
every rule, re-rolls them (which changes nothing already written), crashes again, and
gives up, throwing the whole corruption away. A changed ROM byte can only do harm when the
CPU runs it or reads it, and the tracer already sees exactly that.

**How a use is noticed, for free.** The code map gets a seventh bit, `touched` (0x80):
set alongside the others on every opcode, operand, data read and DMA read, never saved.
Between frames, the blame looks only at the *changed* bytes (the RomKeeper tells it when a
byte starts or stops differing from the clean ROM): touched means used this frame, so it
stamps the byte and clears the bit. No extra work for the CPU, and the per-frame cost is
one check per changed byte.

Per changed byte: when it changed, first and last frame it was used, and in how many frames.

**Suspects** are the changed bytes used in the guard's blame window, ranked:

1. *Newly reached*: first used inside the window (code the game just got to). Newest first.
2. *Already running*: used before the window too. Least-proven first (fewest frames used).

**ROM policies** (recipe `rom_policy`, separate from the rule policy):

- `revert` (default): put suspects back to the clean value.
- `reroll`: give suspects a fresh random value instead. (Keeping a byte's role, operand
  stays operand, needs phase 5's ops.)

Escalating, so as much corruption as possible survives: the first crash in a row acts on
the newest half of the newly reached suspects (all suspects if none are new), the second
on all newly reached ones, the third and later on every suspect in the (by then wider)
window. Acted-on bytes drop out of the suspects, so repeat crashes narrow down like a
bisection. The fix is *settled*: written outside the journal and with the byte's journal
entries dropped, so a later, deeper rollback can't bring the bad value back.

**The guard** gets the ROM blame as a second source of suspects: a checkpoint with used
changed bytes after it counts as having something to blame, and when only ROM bytes are to
blame no rule is punished. For a *guess* (frame interrupt off, frozen or black picture) only
newly reached bytes count: a changed byte that has run fine all along is no evidence that a
quiet screen is a crash. Journal lines and the on-screen message say what happened
("back 1.0 s, reverted 2 ROM bytes").

**The crash map learns from it**: blamed bytes count as crashes for their 16-byte ROM
block, and a changed byte that kept being used for 15 s without a crash counts as a
survival, so `avoid_hot` works for ROM rules too. The per-frame learner stops counting ROM
writes once use-based blame is on (it would blame the write, not the use). Per-byte
counts for code are left for phase 5, if blocks turn out too coarse.

Built in four steps, each pushed:

1. **The touched bit**: the flag, never saved (the file and the fingerprint mask it), set by
   the tracer and the core's data and DMA marks.
2. **`RomBlame`** (`corrupt/rom_blame`, no emulator): watched bytes from the RomKeeper's
   change notices, `collect(frame)`, ranked suspects, the two policies with escalation,
   settling through the RomKeeper, survivals. Unit tests with a fake ROM and flags.
3. **Guard and real core**: the guard's second suspect source, `rom_policy` in recipes,
   journal and summary. A test ROM with a rarely run routine: a classic run corrupts it to
   `STP`, flips the NMI's `INC` to `DEC` and changes bytes nothing reads; the game reaches
   the routine, crashes, and only the `STP` is reverted.
4. **Session, viewer, smoke**: wired in the session (collect after each frame, before the
   guard). A **Changes** overlay on the ROM (changed bytes, brighter when they ran), hover
   says "changed from $E6, ran in 340 frames". The panel's ROM line says how many changed
   bytes have run. `corrupt-smoke --guard` on FF6 with a classic ROM recipe, blame on and
   off: crashes, give-ups, and how many changed bytes survive.

Findings while building phase 4 (FF6, 300 random bytes in bank `$C0` at load, 5 minutes
headless from power-on, `corrupt-smoke ... 18000 1 --guard [noblame]`, six seeds):

| | alive to the end | changes kept | picture changed in | code bytes run |
|---|---|---|---|---|
| clean FF6, no corruption | yes | - | 6,779 frames | 24,886 |
| old guard (blame rules only) | 0 of 6 | all ~300 | 1-6 frames | - |
| blame by use, 5 crashes in a row and give up | 2 of 6 | 0 (gave up) or 229-261 | 7-3,611 | 4,778-19,226 |
| blame by use, only fruitless crashes count | **6 of 6** | **41-261** | 519-6,302 | 14,172-27,005 |

- **The old guard was blind to classic runs**: every corrupted boot hung (frame interrupt
  off, picture still) and it reported *nothing*, because a hang is a guess and no rule had
  written anything recently. All six runs were dead for the full five minutes.
- **Boot code needs the run's first checkpoint at frame 0** (the app takes it there; the
  smoke harness now does too), or bytes the game ran while booting can't be blamed.
- **Giving up after 5 crashes in a row threw everything away** while the blame was still
  making progress: corrupted boot code takes many rounds. Now a crash that puts bytes back
  for good doesn't count towards giving up (there are only so many changed bytes); only
  fruitless ones do, plus a cap of 40 in a row. Re-roll never fixes for good, so it keeps
  the old limit.
- **Guesses blame only newly reached bytes**, so a harmless change in code that runs every
  frame isn't reverted the first time the screen goes quiet.
- macOS `bash` 3.2 has no `wait -n`; batch runs of three with a plain `wait`.

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
