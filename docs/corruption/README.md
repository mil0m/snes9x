# Corruption in Snes9x

This fork of Snes9x corrupts games while you play them: their working memory, graphics,
sound or the ROM itself, from **recipes** you can save, replay and share, with a **crash
guard** that rolls back and plays on when the game falls over.

**How to use it: [the guide](./GUIDE.md).** It's also in the app, under **Guide** in the
Corruption panel. Start with its first section; it takes a minute.

## Build and run

```sh
./build-mac.sh qt        # macOS; needs: brew install qt pkgconf libpng ninja
build/qt/snes9x-qt path/to/game.sfc
```

On Linux and Windows, build `qt/` with CMake as upstream Snes9x does. The corruption code
is plain C++20 with no extra dependencies.

## The library

Recipes, named areas, crash maps, code maps and save backups live in a **library** folder
(the panel's **More > Choose the Library Folder…**). Each game gets a folder named after its
ROM title and the first 8 hex digits of its SHA-256, like `FF6 T-Edition 2016 [6c288894]/`:

| File | What it is |
|---|---|
| `recipes/*.json` | Recipes, and `*.anchor.frz` next to them: their start moments |
| `regions.json` | Named areas |
| `crashmap.<you>.json`, `codemap.<you>.bin` | What the crash guard and the code map learned; one file per person, everyone's added up when the game loads |
| `journal.<you>.jsonl` | Every crash, recovery and give-up, one JSON line each |
| `save-backups/` | Copies of the game's battery save (git-ignored) |

To play with a friend, the library can be a shared git repository (ours is
`github.com/mil0m/snes9x-corruptions`): commit and pull like any repo; Snes9x never runs git
itself. For a single recipe, **More > Share…** is simpler.

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
| `opcode_swap` | | `rom`: an instruction becomes another of the same length and addressing mode |
| `branch_flip` | | `rom`: a conditional branch goes the other way |
| `nop_out` | | `rom`: the whole instruction becomes NOPs |
| `pointer_swap` | `reach` (`seen`, default, or `likely`), `mutual` (default false) | `rom`: a jump table entry gets another routine its dispatcher went to; `count` is entries |

`shift` to `fill` act on the whole area at once and ignore `count` and `pick`.
`opcode_swap`, `branch_flip` and `nop_out` only pick instructions the code map knows;
`pointer_swap` only picks entries of the jump tables found so far (both are learned as you
play).

**`when`**: exactly one of `every_frames`, `chance_per_frame` (0–1), `once_at_frame`,
`hotkey` (1–9), or `at_load: true` (once as the run starts, before frame 1; on the ROM that's
classic corruption), plus an optional `window: [start, end]`. Frames count from activation,
starting at 1.

**Also per rule**: `count` (bytes per firing), `pick` (`fresh`, or `sticky` to keep hitting
the same bytes), `enabled`, `intensity` (0–1, scales count and add/bitflip strength; below one byte per
firing it lowers the chance to fire),
`salt` (re-rolls this rule's randomness; the crash guard sets it), `avoid_hot` (skip bytes
the crash map marks dangerous), and for `rom` rules, aiming by the code map: `role`
(`opcode`, `operand`, `code`, `data`, `dma`, `used`), `classes` and `except` (lists of
`arith`, `logic`, `shift`, `compare`, `load`, `store`, `transfer`, `branch`, `jump`,
`return`, `stack`, `flags`, `width`, `block`, `misc`).

**Per recipe, optional**: `intensity` (0–1, default 1; the Chaos slider), which multiplies
every rule's intensity, `anchor_state`, the file name of the recipe's saved moment, and
`patches`, the painted bytes: `[{ "target": "rom", "at": "0x21A3F", "bytes": "EAEA" }]`, and
`pins`, kept ROM changes: `[{ "rom": "0x21A3F", "value": "0xF0", "was": "0xD0", "note": "…" }]`
(`was` is the clean value; a pin that doesn't match the ROM is skipped).

**Crash settings, per recipe, optional**:

```json
"crash_policy": "escalate",
"rom_policy": "revert",
"instant_revert": true,
"test_first": 3,
"crash_detection": { "enabled": true, "cpu_stopped": true, "runaway_frames": 30,
                     "nmi_off_seconds": 5, "frozen_seconds": 5, "black_seconds": 8, "min_presses": 3,
                     "reboot": true }
```

Any detection field can be left out to keep its default; `0` switches a signal off.
`rom_policy` is what happens to changed ROM bytes blamed for a crash: `revert` (back to
clean, the default) or `reroll` (a fresh random value). `instant_revert` (on by default) fixes
a changed ROM byte the moment it sends the CPU into non-code (the middle of an instruction,
empty ROM, open bus) and plays that frame again, the same way `rom_policy` says; `false` leaves
those crashes to the guard. `test_first` (seconds, default 3, `0` off) plays each live ROM
rule's change that far ahead, unseen, before it lands: a change that crashes the game is
re-rolled, and skipped after three tries.

A shared recipe (`.s9xrecipe`, from **More > Share…**) is `{"format": "snes9x-shared-recipe",
"version": 1, "recipe": {…}, "moment": "<base64 save state>"}`.

**`regions.json`**, per game (the memory viewer writes it; hand edits work too, and
`notes` are kept):

```json
{ "format": 1, "regions": { "player stats": { "target": "wram", "range": [ "0x0F00", "0x0F3F" ], "notes": "HP, MP, level" } } }
```

Region names have 1–40 characters, start with a letter, and use letters, digits, spaces,
`_` and `-`.

## Design and plans

What exists and where (for developers and coding agents): [FEATURES.md](./FEATURES.md).

How it was designed and built, with measurements from real games:
[RAM corruption design](./2026-10-30-ram-corruption-design.md) ·
[engine and recipes](./2026-10-30-phase1-engine-recipes-plan.md) ·
[crash rollback](./2026-10-30-phase2-crash-rollback-plan.md) ·
[live panel](./2026-10-30-phase3-live-panel-plan.md) ·
[reboot detection](./2026-10-30-reboot-detection-plan.md) ·
[crash map](./2026-10-30-crash-map-plan.md) ·
[memory viewer](./2026-10-30-memory-viewer-plan.md) ·
[WRAM access map](./2026-10-31-access-map-plan.md) ·
[crash frequency](./2026-10-31-crash-frequency-plan.md) ·
[ROM corruption](./2026-10-31-rom-corruption-plan.md) ·
[UX pass](./2026-10-31-ux-pass-plan.md) ·
[visual UI pass](./2026-10-31-ui-pass-plan.md) ·
[jump tables and pointer swap](./2026-11-01-pointer-swap-plan.md) ·
[functions: call graph and kill switches](./2026-11-01-functions-plan.md) ·
[safety: live heat, instant revert, testing ahead (and two ideas removed)](./2026-11-01-safety-plan.md) ·
[remote control](./2026-11-01-remote-plan.md) ·
[what could come next](./2026-10-31-horizons.md).
Manual test steps: [QA.md](./QA.md).

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
disassembled instructions, writing the map to `file` if given,
`--jumps [file]` records the indirect jumps too and prints the jump tables found behind them
(writing the jump log to `file`), `--swaps [seeds=10] [count=3]` boots each seed's picked
jump table entries three ways, unguarded (pointer-swapped, random bytes, clean), and counts
which keep running, and
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

**The UI tour** screenshots every corruption window of the real app with a game running:
the main window, the panel without and with a recipe, the rule editor, the memory viewer
(WRAM hex and pixels, the ROM with the Code overlay, VRAM tiles) and the survivors dialog.

```sh
SNES9X_UI_TOUR=/tmp/tour XDG_CONFIG_HOME=<scratch config> QT_QPA_PLATFORM=offscreen \
  timeout -s KILL 30 build/qt/snes9x-qt <copy of a ROM>
```

Use a scratch config (`snes9x/snes9x-qt.conf` with `Mute=true`,
`PauseEmulationWhenUnfocused=false`, `PanelVisible=true` and `LibraryRoot` pointed at a
scratch folder) and a copy of the ROM, so nothing real is written. The pictures land in the
folder with a `done` file at the end.
