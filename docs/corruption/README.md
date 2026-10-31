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
  wider than the window scroll sideways. With **Activity**
  or **Hits** on, the bytes go grey and the heat is painted over them; **Plain** shows the
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
(64 KB), `oam` (544 B sprite table), `cgram` (512 B palette), `aram` (64 KB sound CPU RAM).

**`range`**: `[first, last]` inclusive, relative to the target (`wram` `0x0400` is
`$7E:0400`), a name from the game's `regions.json`, or left out for the whole target.

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

**`when`**: exactly one of `every_frames`, `chance_per_frame` (0–1), `once_at_frame`, or
`hotkey` (1–9), plus an optional `window: [start, end]`. Frames count from activation,
starting at 1.

**Also per rule**: `count` (bytes per firing), `pick` (`fresh`, or `sticky` to keep hitting
the same bytes), `enabled`, `intensity` (0–1, scales count and add/bitflip strength; below one byte per
firing it lowers the chance to fire),
`salt` (re-rolls this rule's randomness; the crash guard sets it).

**Per recipe, optional**: `intensity` (0–1, default 1; the Chaos slider), which multiplies
every rule's intensity, and `anchor_state`, the file name of the recipe's saved moment.

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
random blocks (a control: if random does as well, the map learned nothing), and
`--dump <dir>` writes every
memory's raw bytes to `<dir>/<target>.bin`. Point `CORRUPT_DUMP_DIR` at such a folder when
running the widget tests and they draw it through the pixel and tile views into the shots folder.

The real-core tests (`tests/headless/guard_integration_test.cpp`) boot tiny generated ROMs
that crash on cue (`STP`, NMI off) and check that the guard catches, rolls back and plays
on.

The panel widget tests (`tests/qt/`, built when Qt 6 is found) run on Qt's offscreen
platform, so they need no screen. They also write PNGs of the rule editor to
`build/tests/shots/`, which shows the layout on a machine without a display.
