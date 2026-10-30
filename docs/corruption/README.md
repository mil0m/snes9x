# Corruption tooling

This fork of Snes9x has built-in RAM corruption: data-driven **recipes** instead of
scripts, a shared **recipe library**, and automatic **save-file backups**. The design is
in [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md).

Phase 1 (this) covers recipes, the panel, hotkeys and save backups. Crash detection with
automatic rollback is phase 2.

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
5. **Save As…** writes the current recipe and seed into the library.

To change rules for now, edit the recipe's JSON and press **Rescan** then **Load**. A rule
editor in the panel comes in phase 3.

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
the same bytes), `enabled`, `intensity` (0–1, scales count and add/bitflip strength).

**`regions.json`**, per game:

```json
{ "format": 1, "regions": { "player_stats": { "target": "wram", "range": [ "0x0F00", "0x0F3F" ] } } }
```

## Things worth knowing

- **Sprites and palettes often bounce back.** Most games copy their sprite table and
  palette from WRAM into OAM/CGRAM every frame, so corrupting OAM or CGRAM directly only
  flashes. For lasting effects, corrupt the game's WRAM copy (finding it is what the phase
  4 memory viewer is for).
- **Low WRAM is dangerous.** `$7E:0000–1FFF` usually holds the stack and direct page, which
  is the fastest way to crash.

## For developers

```sh
./build-mac.sh test                                    # engine unit tests
build/tests/corrupt-smoke game.sfc recipe.json 600 600 # a recipe in the real core, headless
```

`corrupt-smoke` runs the same frames clean and corrupted from one save state, and reports
how many frames look different and which memory changed. It also checks that every write
the engine logged really landed in emulator memory.
