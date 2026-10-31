# Corruption tooling

This fork of Snes9x has built-in RAM corruption: data-driven **recipes** instead of
scripts, a shared **recipe library**, and automatic **save-file backups**. The design is
in [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md).

Phase 1 covers recipes, the panel, hotkeys and save backups. Phase 2 adds the **crash
guard**: crashes are detected and rolled back automatically. Phase 3 adds editing rules
in the panel while the game runs.

## Build and run

```sh
./build-mac.sh qt        # macOS; needs: brew install qt pkgconf libpng ninja
build/qt/snes9x-qt path/to/game.sfc
```

On Linux and Windows, build `qt/` with CMake as upstream Snes9x does. The corruption code
is plain C++20 with no extra dependencies.

## The library

Open **Corruption > Show Panel** and **Choose…** a library folder. The intended setup is a
clone of the shared library repo:

```sh
git clone https://github.com/mil0m/snes9x-corruptions.git ~/git/snes9x-corruptions
```

Each game gets a folder named after its ROM title plus the first 8 hex digits of its
SHA-256, for example `FF6 T-Edition 2016 [6c288894]/`, holding `recipes/*.json`, an
optional `regions.json` and the git-ignored `save-backups/`. Commit and pull the library
like any repo; Snes9x never runs git itself.

## Using it

1. Load a game, open the panel, and pick a recipe. With no recipes yet, choose a starter
   from the list and press **Add Example**.
2. **Load**, then tick **Active** (or press **F7**).
3. **Re-roll** (**F8**) for a fresh seed. The same recipe + seed replays identically from
   the moment it was activated.
4. Rules with a `hotkey` trigger fire on **Corruption: Fire Hotkey N** (hotkey 1 is
   **F9**; bind the rest under *Settings > Shortcuts*).
5. **Save** writes the recipe back to its file; **Save As…** writes a new one. The status
   line says *(edited)* while there are unsaved changes, including a new seed and what the
   crash guard changed.

## Editing rules

Edit rules in the panel while the game runs:

- **Add Rule** starts from a gentle default (one random byte every 2 s in upper WRAM).
- **Edit**, or double-click a rule, opens the editor:
  - *Where*: the memory, and a range, or a named region from the game's `regions.json`.
  - *What*: the op and its settings.
  - *When*: the trigger, optionally only within a frame window.
  - *How much*: count, pick, intensity and on/off.
  - Every change is checked against the loaded game, and **OK** stays off until the rule
    is valid. The reason is shown in red.
- **Duplicate** copies the selected rule; **Remove** deletes it (it asks first).
- **New** starts an empty recipe for the game; build it up with **Add Rule**, then
  **Save As…**.

An edit takes effect on the next frame and touches only that rule: the others keep their
frame count, sticky addresses and freezes. The edited rule starts over, picking new
addresses and releasing its freezes. One side effect: removing a rule gives the rules
after it new random values, because a rule's randomness depends on its position.

Loading another recipe or starting a new one asks before discarding unsaved edits.

## The memory viewer

**Memory Viewer…** in the panel opens a hex, pixel or tile view of one target (WRAM, VRAM, the palette…):

- The **Map** on the left lists what lives where in that memory; click an area to select
  it. Every memory has its fixed hardware layout (WRAM's direct page and stack area, the
  sprite table, background and sprite palettes, the sound CPU's registers…). VRAM and ARAM
  also show what the game uses right now, read from the graphics and sound chips: each
  background layer's tiles and tile map, the sprite tiles, the echo buffer and the sample
  directory. Those are in italics, because they move when the game changes screens.
- **Activity** colours each byte by how often the game changes it, so live data (timers,
  positions, the sprite buffer) glows and unused memory stays plain. A byte that changes
  every frame is fully orange; one that changes once a second is faint but visible.
- **Hits** colours the bytes the recipe has written since the viewer opened (or since
  **Clear Hits**), so you can see a recipe's footprint.
- **Reads** (blue) and **Writes** (green), WRAM only: how often the game itself reads and
  writes each byte, counted by the emulator while the viewer shows WRAM. Unlike Activity
  they see a byte written with the same value and every read, including the DMA that
  copies the sprite table and palette out of WRAM each frame. A byte the game reads all the
  time but rarely writes is where corruption sticks and shows; one it rewrites every frame
  undoes your corruption at once. Hover a byte for `read 3.0, written 0.5 a frame`. Not
  counted: code the game runs from WRAM, HDMA tables, and the SA-1 chip's own accesses.
- The strip on the right is the whole target at a glance; click it to jump there. **Go to**
  takes an address like `0x1C00` or `$1C00`.
- Drag across bytes (or click, then shift-click) to select a range, then **New Rule from
  Selection…** opens the rule editor with that range filled in.
- **Named areas**: select bytes and **Name Selection…** (say `player stats`), and the name
  works as a range in any rule for that game; the rule editor's **Region** list offers it.
  Click a name to jump there. **Rename…** updates every recipe of the game that uses it;
  **Delete…** leaves those recipes the range, just without the name. Names are stored in
  the game's `regions.json`, shared through the library like recipes. Redefining a name
  moves its rules the next time their recipe is loaded.
- **Find Copies in WRAM** lists where WRAM holds copies of the sprite table and the palette. Most
  games rebuild sprites and colours from those copies every frame, so corrupting a copy
  sticks where corrupting `oam` or `cgram` only flashes. In Goof Troop, palette drift on its
  WRAM copy changed 791 of 900 frames, against 19 on `cgram` itself. Click a result to select
  it.

- **Pixels** (the first menu on the toolbar, next to **Hex**) draws every byte as one dot
  coloured by its value, so a whole memory fits on screen: zero is black, 0xFF white, and
  the values in between run through the colours as they get brighter. Fills show as solid
  bands, tables as stripes, compressed data as noise, and VRAM's tiles as their bitplane
  patterns. **−** and **+** (or Ctrl+mouse wheel, which zooms around the mouse) set the
  dot size from 1 to 16 screen pixels; a row holds as many bytes as fit, always a power of
  two, so tables line up in columns. Small memories come out square-ish: the palette is
  one 16-colour palette per row, the sprite table eight sprites per row.
- **Row** sets how many bytes a row of dots holds: **Auto** (as above) or any number from
  1 to 4096. Data laid out in rows only lines up when a row of dots is exactly one row of
  the data, so step it one byte at a time (arrows or the mouse wheel over the box) and
  watch for the moment the picture stops leaning and snaps into columns. A 32-wide tile map
  lines up at 64 (two bytes per tile); in FF6 the BG1 map turns into a picture of the level
  at 64 and shears at 66. Each memory keeps its own width while the viewer is open; rows
  wider than the window scroll sideways. With an overlay
  on, the bytes go dark grey and the overlay is painted over them; **Plain** shows the
  full colours. Hover a dot for its address and value. Selecting, the Map, named areas,
  copies and New Rule from Selection work the same as in hex, and switching keeps the
  selection and the place you were looking at.

- **Tiles** (the third mode) decodes the memory as SNES graphics, 8x8 tiles in the game's
  own colours, so you see the sprites, fonts and backgrounds themselves. It works on any
  memory, not only VRAM: many games unpack graphics into WRAM first. On the second toolbar
  row pick the **format** (2, 4 or 8 bpp, or mode 7), the **palette** (which row of the
  game's colours, or **Grey** for the raw colour numbers; sprites use 8-15 at 4 bpp), and
  how many tiles **per row** (big sprites and fonts often line up at 16 or 32). Easiest:
  click a tile area in the **Map** (BG1 tiles, Sprite tiles…), which picks the right
  format and palette for you. The colours are read live from the game, so corrupting the
  palette shows here at once. Selecting is whole tiles: **New Rule from Selection…** then
  corrupts exactly those graphics. Hover a tile for its number and address.

The viewer only watches while its window is open, about 0.4 ms a frame for WRAM.

## Saved moments

A recipe can carry the moment its run started, so loading it puts the game back there and
the same seed replays the same chaos (until your own button presses differ). That's how
you hand someone exactly the run you saw.

- With **Saved moments** ticked (the default), **Save** and **Save As…** write the moment
  the current run started to `recipes/<name>.anchor.frz`, next to the recipe.
- **Load** on a recipe that has one jumps the game back to it and starts the run there.
  The status line says *from its saved moment*.
- A run starts at the next frame, so the moment and frame 1 line up exactly. While the game
  is paused, a start waits (the status says *starting with the next frame*).
- After a re-roll or switching the recipe off and on, the run has a *new moment, not
  saved*: **Save** writes it over the old one.
- A moment is only loaded into the same ROM version (same SHA-256); for another version the
  recipe starts where you are.
- Untick **Saved moments** to load and save recipes without touching the game's state.

Moments are Snes9x save states, about 800 KB each, uncompressed for now. They're meant to
be shared, so they're not git-ignored.

## Turning it up and down

- **Chaos** (in the run box) sets the whole recipe's intensity, and each rule has its own
  **Intensity** slider in the table. They multiply: a rule at 50 % under 50 % Chaos runs at
  25 %. Both act on the next frame and are saved with the recipe.
- **Corruption: More Chaos / Less Chaos** step Chaos by 10 %. They have no keys by
  default; bind them under *Settings > Shortcuts*.
- Intensity scales how many bytes a rule hits per firing, and how hard `add` and
  `bitflip` hit. Once that drops below one byte, the rule **fires less often** instead:
  a one-byte rule at 30 % fires on about 3 of every 10 chances. That's also what lets the
  crash guard's `dampen` calm a one-byte rule down. When the rule moves, its slider moves.
Editing the JSON by hand still works: **Rescan**, then **Load**.

## The crash guard

While a recipe is active, Snes9x keeps a checkpoint every second (the last 30). When the
game crashes, it goes back to just before the corruption that caused it, changes what
the guilty rules do, and plays on. You lose a second or two.

**What counts as a crash:**

| Signal | Caught when |
|---|---|
| CPU stopped | the game runs `STP`; caught at once |
| Deadlock | the core gives up on the CPU; caught at once |
| Frame interrupt off | NMI stays off for 5 s (games switch it off for up to ~3 s while loading) |
| Frozen picture | the picture doesn't change for 5 s **while you press buttons** |
| Black screen | forced blank or zero brightness for 8 s **while you press buttons** |
| Runaway code | code runs from I/O or unmapped memory for 30 frames (rarely fires; see below) |
| Reboot | the game jumps back into its own reset code and starts over; caught at once |
| Mark Crash | you press **Mark Crash** (rolls back about 2 s) |

A still screen nobody is touching (a text box, a menu, a pause screen) is never a crash.
Heuristic signals (NMI, frozen, black, reboot) are also ignored when no corruption
happened just before them. That keeps a game's own soft-reset button combo from rolling
back a clean game.

**What happens to the guilty rules** depends on the policy: the recipe's
`crash_policy`, or the panel's override.

| Policy | On a crash, the suspect rules… |
|---|---|
| `reroll` (default) | get fresh random values (a new `salt`, saved with the recipe) |
| `skip` | sit out until the crash point, then carry on |
| `dampen` | drop to half intensity |
| `disable` | are switched off |
| `escalate` | re-roll twice, then dampen twice, then switch off |

If the same crash comes back right away, each repeat goes **5 s further back** (about
0.5 s, then 5.5, 10.5, … up to the 30 s of checkpoints). Damage is often written long
before the game trips over it, and every checkpoint after it carries the damage along.

"Suspects" are all rules that wrote anything between the checkpoint and the crash, so a
busy every-frame rule often shares the blame. That's harmless with `reroll` and blunt
with `disable`. After 5 crashes without 10 calm seconds in between, the guard rolls back
once more and switches corruption off.

**The journal.** Every crash, recovery and give-up is appended to
`<game>/journal.<you>.jsonl` in the library, with the time, recipe, seed, signal, suspects,
action and how far back it went. It's one file per person, so a shared library never
conflicts. It's the data for deciding which policy suits which game.

**The crash map.** The guard also learns where corruption crashes each game. Every
memory is split into 16-byte blocks, and each block counts writes followed by a crash
(the last 3 s before the trouble) and writes followed by 15 calm seconds. A block is
**dangerous** when it has at least 2 crashes, a crash rate of 0.4 or more (counted with a
small prior), and a crash rate at least 3 times the rest of that memory's. The last part
matters in games whose crashes blame nearly everything: there the busiest blocks reach 0.4
by chance. Rules with **Avoid dangerous bytes** ticked (the rule editor; `avoid_hot` in
the JSON) skip their writes to those bytes, and every other write stays exactly the same. The memory viewer's
**Danger** overlay shows the map in red (the crash rate) and the **Crashes** overlay in gold
(how many crashes each block was blamed for). Under both, the pixel view puts a red tick
beside rows with a dangerous block and, from 4× zoom, outlines the block; hovering a byte
shows its block's crashes, calm runs and danger. The panel says how many dangerous areas it
knows. It's kept in `<game>/crashmap.<you>.json`, one file per person like the journal;
everyone's files add up when the game loads, so you and Erik teach each other's
recipes. It learns slowly, and only some games have clear dangerous spots. On Fire
Emblem 4, five 5-minute sessions of the low-WRAM roulette marked 6 blocks; on 20 fresh
seeds avoiding them cut crashes from 231 to 208 (fewer on 5 seeds, more on 4: once a run
avoids a crash it plays on differently and can crash elsewhere), while avoiding 6 random
blocks changed nothing. On Pocky & Rocky, Final Fantasy VI and Goof Troop nothing stood
out, so the map stays empty and changes nothing. Avoiding rules don't replay exactly once
the map changes.

**Things worth knowing:**

- While a run is stuck crashing at the same spot (a crash loop), nothing survives long
  enough to count, so the bytes written next to the real culprit collect crashes too. They
  cool down once play moves on and they're written again without trouble.
- Runaway-code detection samples the CPU once a frame, and a runaway CPU nearly always
  wanders back into ROM within that frame. So those crashes usually surface as one of the
  other signals.
- **Reboots** are spotted by watching the first instruction of the game's reset code. A
  reboot that jumps past that instruction, or one made from the CPU's emulation mode, is
  missed. Pressing reset in Snes9x never counts.

## ROM corruption

The `rom` target corrupts the game itself, the way ROM corruption always worked, but with
recipes, seeds and the crash guard. Offsets are into the ROM as a clean `.sfc` (no copier
header). Two ways to use it, mixed freely in one recipe:

- **Classic**: a rule with **Once, when the run starts** (`"when": { "at_load": true }`)
  changes the ROM as the run starts, and the console restarts so the game boots from the
  corrupted ROM. The same seed always gives the same corrupted ROM; Re-roll gives another.
  Starter: **Classic ROM sprinkle**.
- **Live**: any other trigger changes ROM bytes while you play, so code and data change
  under the running game. Starter: **Live ROM drip**.

How the ROM behaves:

- **A run always starts from the clean ROM**: turning a recipe on, Re-roll, loading
  another recipe or its saved moment. Turning a recipe off keeps what it did, like RAM;
  **Restore Clean ROM** in the panel undoes it.
- **The header and interrupt vectors can't be corrupted** (what the CPU sees at
  `$00:FFB0-$FFFF`). Rules skip those bytes.
- **The crash guard undoes ROM changes too.** Save states don't hold the ROM, so after a
  rollback the guard also puts back every ROM byte changed since the checkpoint.
- **It blames the bytes the game actually used.** A changed ROM byte can only do harm once
  the game runs it or reads it, and the code map sees exactly that. So when a crash comes,
  the guard puts back only the changed bytes the game used just before it, and keeps every
  other change: code the game reached for the first time goes first (the newest half on the
  first crash, all of it on the second), then everything used before the crash. Classic
  runs survive this way too, instead of giving up and losing the whole corruption: six
  FF6 runs with 300 random bytes in its main code bank all played five minutes, keeping
  40-260 of the changes, where the old guard left every one of them hung. A crash that puts
  bytes back doesn't count towards giving up (only crashes that fix nothing do). With
  `"rom_policy": "reroll"` in the recipe, blamed bytes get a fresh random value instead of
  the clean one. Blamed bytes count as crashes in the crash map, and changed bytes the game
  kept using for 15 s count as calm, so **Avoid dangerous bytes** works on the ROM too.
- The **Changes** overlay shows which ROM bytes differ from the clean ROM, bright orange
  once the game has used them; hovering one says `changed from 0xE6, used in 340 frames`
  or `not used yet`. The panel's ROM line counts both.
- **Export ROM…** saves the ROM as it is now as a `.sfc`, with its checksum fixed, for
  other emulators or a flash cart.
- The memory viewer shows the `rom` target like any other, in hex, pixels and tiles.
- Not handled yet: games with a Super FX or SA-1 chip may not see live ROM changes to their
  coprocessor's code right away.

### The code map

While a game is loaded, the emulator watches what the game does with its ROM and remembers,
per byte, what it is:

- **code**: an instruction the CPU ran, split into the **opcode** (which instruction) and its
  **operands** (the value or address it uses), at the width it ran at (8 or 16 bits);
- **data**: bytes the CPU read as data: tables, stats, text, scripts, pointers;
- **DMA**: bytes copied straight to the graphics chip: tiles, maps, palettes.

The memory viewer shows it on the `rom` target as three overlays: **Code** (opcodes bright
teal, operands lighter), **Data** (pink) and **DMA** (yellow-green). Hover a code byte for
the instruction it belongs to, as the CPU sees it: `$C0:0053  22 06 00 C3 JSL $C30006 (jump)`.
The panel's ROM line shows how much is known so far.

It only knows what has run or been read, so it grows as you play: five minutes of FF6's
intro find about 25,000 code bytes, 157 KB of data and 115 KB of graphics. Each person's map
is saved in the library as `codemap.<name>.bin` (every minute while it grows, and when the
game closes), and everyone's maps are combined when a game loads, so playing with Erik's
library adds his exploration to yours. Watching costs about 4-7% of emulation speed.

This is what the crash guard blames with (see above), and what code-aware corruption aims
with (below).
Not seen yet: code the game runs from WRAM (FF6 T-Edition's title screen waits in WRAM, for
one), HDMA tables, and the SA-1's own CPU.

### Code-aware corruption

ROM rules can aim at bytes by what the code map knows about them, and three ops change
instructions as instructions. In the rule editor, **Aim** picks the bytes:

- **Bytes**: any byte in the range (the old way), **opcodes** (which instruction),
  **operands** (the value or address an instruction uses: `LDA #$05` becomes `LDA #$63`,
  and the code never goes out of step), **code** (either), **data** the game reads,
  **graphics** it copies by DMA, or **anything the game used**.
- **Instructions**: any, only some classes, or all but some (`arith`, `logic`, `shift`,
  `compare`, `load`, `store`, `transfer`, `branch`, `jump`, `return`, `stack`, `flags`,
  `width`, `block`, `misc`). Choosing "all but" starts with `width` and `stack` ticked:
  `REP`/`SEP`/`XCE` and pushes and pulls almost always crash at once.
- Code the game ran at both 8 and 16 bits is never picked: changing it is the classic
  instant crash.

The ops, in **What**:

- **Swap opcodes**: an instruction becomes another of the same length and addressing mode
  (`ADC` ↔ `SBC` ↔ `AND` ↔ `ORA` ↔ `EOR` ↔ `CMP` ↔ `LDA` ↔ `BIT`, `ASL` ↔ `LSR` ↔ `ROL` ↔ `ROR` ↔
  `INC` ↔ `DEC`, `STA` ↔ `STZ`, the branches, the flags, `INX` ↔ `DEX`…).
- **Flip branches**: `BEQ` ↔ `BNE`, `BCC` ↔ `BCS`, `BPL` ↔ `BMI`, `BVC` ↔ `BVS`.
- **NOP out**: the whole instruction becomes `NOP`s: a call, a store or a check that no
  longer happens.

A "nudge" is just **Add** −2..2 on operands or data. In a recipe file these are
`"role": "operand"`, `"classes": ["arith", "compare"]`, `"except": ["width"]`, and
`"op": {"type": "branch_flip"}` (also `opcode_swap`, `nop_out`).

An aimed rule picks only from bytes the map already knows, so **play the game a while
first**; with nothing known it fires on nothing. What a rule aims at is worked out once
per run, when it first fires, so a recipe and seed replay exactly from a saved moment,
and the next run aims at everything explored since.

On FF6 (classic, its main code bank, five minutes): 100 branch flips crashed 0-2 times,
and a third to a half of them stayed in code the game kept running; 300 random bytes
crashed 5-12 times and only a handful of them were ever used. Starters: **Branch flips**,
**Operand nudge** and **Opcode swap**.

Plan and what's next: [`2026-10-31-rom-corruption-plan.md`](./2026-10-31-rom-corruption-plan.md). Beyond it, game-aware corruption (jump tables, state jumps, game profiles mapped with Claude, a search for golden moments): [`2026-10-31-horizons.md`](./2026-10-31-horizons.md).

## Editing memory like a picture

The way ROMs used to be corrupted in Photoshop, built in: the memory viewer's **Pixels**
view is the canvas (one dot a byte, a row width you choose), and these work on any target,
the ROM included:

- **Rectangles**: Alt-drag (Option on a Mac) to select a rectangle in the current row width.
  **New Rule from Selection** makes a rule for exactly that rectangle; the rule editor shows
  it under **Shape**.
- **Area operations**, for the whole rectangle or range at once: **Shift** (right by X bytes,
  down by Y rows, wrapping round or filling), **Flip** (horizontally, vertically or both),
  **Levels** (invert, posterize, threshold, gamma, contrast), **Filter** (blur, sharpen,
  median, emboss, edges, max, min, over the 2D layout), **Stamp** (copy another region of the
  same size onto it) and **Fill**. With **Once, when the run starts** they make a classic
  corrupted ROM; with any other trigger they keep happening (a region that keeps sliding).
  The per-byte ops (random, add, bit flips…) also work inside a rectangle. Starter: **ROM
  region shift**.
- **Paint**: switch on **Paint**, pick a value (or Alt-click a dot to take its value) and a
  brush size, and drag. The bytes change at once and are kept in the recipe as **patches**,
  which apply every time the run starts, so painted ROM is in the game from its first frame
  and no rollback undoes it. Painting with no recipe loaded makes one. **Clear Paint** in the
  panel forgets them.
- **Round trip to a real image editor**: **Export PNG…** saves the rectangle (or the selected
  range, or the whole memory) as a greyscale PNG in the current row width. Edit it anywhere,
  keep it greyscale and the same size, and **Import PNG…** puts it back: the pixels that
  changed become painted bytes. The PNG remembers where it came from; if an editor strips
  that, it goes onto the current selection when the sizes match.

## Save backups

Nothing to do: the game's save RAM is backed up when it loads, about once a minute while
it changes, and when it closes or you switch games. The newest 50 backups are kept, plus
one per day for 30 days. **Restore** in the panel backs up the current save first, writes
the chosen one and resets the game.

## Recipe format

```json
{
  "format": 1,
  "name": "Palette drift",
  "notes": "Free text.",
  "seed": 1002,
  "rules": [
    {
      "name": "drift",
      "target": "cgram",
      "range": [ "0x0000", "0x01FF" ],
      "op": { "type": "add", "min": -2, "max": 2 },
      "when": { "every_frames": 30 },
      "count": 4
    }
  ]
}
```

Numbers can be JSON numbers or `"0x…"` hex or decimal strings. Unknown keys are errors,
so typos fail loudly.

**`target`**: `wram` (128 KB), `sram` (battery save RAM; needs an explicit range), `vram`
(64 KB), `oam` (544 B sprite table), `cgram` (512 B palette), `aram` (64 KB sound CPU RAM),
`rom` (the game itself, up to 8 MB; needs an explicit range).

**`range`**: `[first, last]` inclusive, relative to the target (`wram` `0x0400` is
`$7E:0400`), a name from the game's `regions.json`, or left out for the whole target.
Or **`rect`** instead: `{ "start": "0x1000", "row": 64, "width": 16, "height": 32 }`, a
rectangle with its top-left at `start`, in rows of `row` bytes.

**`op`**, by `type`:

| type | fields | does |
|---|---|---|
| `add` | `min`, `max`, `mode` (`wrap`/`clamp`) | adds a random delta |
| `random` | `min`, `max` (0–255) | writes a random value |
| `bitflip` | `bits` (1–8) or `mask` | flips random bits, or XORs |
| `swap` | `a`, `b`, `width` (1–2) | swaps a↔b, or each picked byte with a random one |
| `copy` | `from` {`target`, `range`} or region, `length` | copies a block from a random spot in `from` |
| `freeze` | `value` (optional) | holds a value, or whatever was there when first hit |
| `shuffle` | `window` (2–256) | permutes the bytes after each picked address |
| `shift` | `dx`, `dy`, `wrap` (default true), `value` (fill when not wrapping) | moves the whole area |
| `flip` | `axis` (`horizontal`, `vertical`, `both`) | mirrors the whole area |
| `levels` | `curve` (`invert`, `posterize`, `threshold`, `gamma`, `contrast`), `amount` | remaps every value |
| `filter` | `kernel` (`blur`, `sharpen`, `median`, `emboss`, `edges`, `max`, `min`) | 3×3 over the area's rows |
| `stamp` | `from` (the source's top-left) | copies a same-sized region onto the area |
| `fill` | `value` | sets the whole area |

The last six act on the whole area at once and ignore `count` and `pick`.

**`when`**: exactly one of `every_frames`, `chance_per_frame` (0–1), `once_at_frame`,
`hotkey` (1–9), or `at_load: true` (once as the run starts, before frame 1; on the ROM that's
classic corruption), plus an optional `window: [start, end]`. Frames count from activation,
starting at 1.

**Also per rule**: `count` (bytes per firing), `pick` (`fresh`, or `sticky` to keep hitting
the same bytes), `enabled`, `intensity` (0–1, scales count and add/bitflip strength; below one byte per
firing it lowers the chance to fire),
`salt` (re-rolls this rule's randomness; the crash guard sets it).

**Per recipe, optional**: `intensity` (0–1, default 1; the Chaos slider), which multiplies
every rule's intensity, `anchor_state`, the file name of the recipe's saved moment, and
`patches`, the painted bytes: `[{ "target": "rom", "at": "0x21A3F", "bytes": "EAEA" }]`.

**Crash settings, per recipe, optional**:

```json
"crash_policy": "escalate",
"crash_detection": { "enabled": true, "cpu_stopped": true, "runaway_frames": 30,
                     "nmi_off_seconds": 5, "frozen_seconds": 5, "black_seconds": 8, "min_presses": 3,
                     "reboot": true }
```

Any detection field can be left out to keep its default; `0` switches a signal off.

**`regions.json`**, per game (the memory viewer writes it; hand edits work too, and
`notes` are kept):

```json
{ "format": 1, "regions": { "player stats": { "target": "wram", "range": [ "0x0F00", "0x0F3F" ], "notes": "HP, MP, level" } } }
```

Region names have 1–40 characters, start with a letter, and use letters, digits, spaces,
`_` and `-`.

## Things worth knowing

- **Sprites and palettes often bounce back.** Most games copy their sprite table and
  palette from WRAM into OAM/CGRAM every frame, so corrupting OAM or CGRAM directly only
  flashes. For lasting effects, corrupt the game's WRAM copy: the memory viewer's **Find
  Copies** shows where it is.
- **Low WRAM is dangerous.** `$7E:0000–1FFF` usually holds the stack and direct page, which
  is the fastest way to crash.

## For developers

```sh
./build-mac.sh test                                    # engine, real-core and panel widget tests
./build-mac.sh asan                                    # the same, under AddressSanitizer
build/tests/corrupt-smoke game.sfc recipe.json 600 600 # a recipe in the real core, headless
```

`corrupt-smoke` runs the same frames clean and corrupted from one save state, and reports
how many frames look different and which memory changed. It also checks that every write
the engine logged really landed in emulator memory. With `--replay` instead, it starts the
recipe the way the app does, plays on, starts again from the saved moment, and checks the
second run ends with identical memory and picture. With `--copies` it lists where WRAM
holds copies of the sprite table and palette after the warmup, `--map` prints the
VRAM and ARAM map with how much of each area holds data, `--learn [held=3]` learns a crash map
over five seeds and then plays `held` other seeds plain, avoiding it, and avoiding as many
random blocks (a control: if random does as well, the map learned nothing),
`--codemap [file]` records the code map for `frames` frames (twice, alternating with
plain runs) and prints the tracing cost, coverage, the banks with the most code and a few
disassembled instructions, writing the map to `file` if given, and
`--dump <dir>` writes every
memory's raw bytes to `<dir>/<target>.bin`. Point `CORRUPT_DUMP_DIR` at such a folder when
running the widget tests and they draw it through the pixel and tile views into the shots folder. With
a `rom.codemap.bin` there too (from `--codemap`), they also draw the ROM with the Code, Data
and DMA overlays.

The real-core tests (`tests/headless/guard_integration_test.cpp`) boot tiny generated ROMs
that crash on cue (`STP`, NMI off) and check that the guard catches, rolls back and plays
on.

The panel widget tests (`tests/qt/`, built when Qt 6 is found) run on Qt's offscreen
platform, so they need no screen. They also write PNGs of the rule editor to
`build/tests/shots/`, which shows the layout on a machine without a display.
