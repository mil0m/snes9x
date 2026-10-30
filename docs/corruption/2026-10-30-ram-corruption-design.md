# RAM corruption engine — design

Status: open questions answered, ready for the phase 1 plan · 2026-10-30

## Why

Mikael and Erik corrupt SNES games for fun: first ROM corruption, lately RAM corruption
through Lua scripts that offset, randomise and swap memory. Two things make that tedious:

1. **Robustness.** Corrupted games crash constantly, and recovering by hand kills the fun.
2. **Bookkeeping.** Every good corruption becomes its own script, so a game ends up with
   hundreds of near-identical files and no way to replay "that one great run" exactly.

This fork builds corruption into Snes9x itself: a data-driven engine, crash detection with
automatic rollback, and a live UI. RAM comes first; ROM corruption (phase 5) reuses the
same engine with a different target.

## Decisions already made

| | |
|---|---|
| Platforms | Mac, Windows, Linux. Engine in the shared core, UI in the **Qt** front end (now builds on macOS, commit `c87e91b4`). |
| Lua | Not carried over. **Recipes** (data files) replace scripts; favourite old scripts get ported into recipes by hand. |
| Controls | Live panel **and** hotkeys **and** a memory viewer/heatmap. |
| Library | Any folder can be the library root. The intended setup is a **shared GitHub repo** for Mikael and Erik, cloned locally and used as the root. |
| Crash policy | Not one fixed answer: **pluggable strategies** chosen per recipe (or per game), with a crash journal to compare them. The default is **re-roll** the suspect rule's values. |
| SRAM | Allowed freely. Save files are **backed up automatically and continuously**, with no confirmation prompts. |

## Architecture

```
corrupt/                     core, no UI, no Qt — compiled into snes9x-core
  targets.{h,cpp}            memory regions + safe writes (cache invalidation)
  recipe.{h,cpp}             recipe model, JSON load/save, validation
  engine.{h,cpp}             per-frame rule evaluation, seeded RNG, event log
  crash_detector.{h,cpp}     signals → crash verdict
  rollback.{h,cpp}           checkpoint ring, culprit attribution, retry policy
  crashmap.{h,cpp}           learned per-game address danger scores
qt/src/Corruption*.{cpp,ui}  dock panel, hotkeys, memory viewer
tests/corrupt/               headless tests (see Testing)
```

**One hook into the emulator.** `cpuexec.cpp` already has a single point where VBlank starts
(right after `S9xEndScreenRefresh()`). We add one call there, `S9xCorruptFrame()`, which
runs the engine, the detector and the checkpointing. Everything else stays outside the
original core files, which keeps upstream merges painless.

**Threading.** The Qt front end runs emulation on its own thread (`EmuThread`). The engine
lives on that thread only. The UI sends commands through the existing
`EmuApplication::runOnThread`, and reads a small status snapshot that the engine publishes
a few times per second. There are no locks inside the per-frame path.

## Targets (where we can write)

| Target | Source in the core | Size | Notes |
|---|---|---|---|
| `wram` | `Memory.RAM` | 128 KB | Main work RAM; the default and most fun target. |
| `sram` | `Memory.SRAM` | per cart | Battery save RAM. Allowed freely; see *Save-file safety*. |
| `vram` | `Memory.VRAM` | 64 KB | Tiles/tilemaps. Must mark the tile caches (`IPPU.TileCached`) dirty. |
| `oam` | `PPU.OAMData` | 544 B | Sprite table. Must set `IPPU.OBJChanged`. |
| `cgram` | `PPU.CGDATA` | 512 B | Palette. Must set `IPPU.ColorsChanged`. |
| `aram` | `SMP::apuram` | 64 KB | Sound CPU RAM; mangles music and sound effects. |

All writes go through `targets.cpp`, which owns the cache-invalidation rules. Without
this, VRAM, OAM and palette corruptions look like they do nothing until the game happens
to redraw. Coprocessor RAM (SA-1, Super FX) comes later.

Addresses in recipes are **target-relative offsets**, so `wram:0x0400` means `$7E:0400`.
The UI shows both forms.

## Recipes

A recipe is one JSON file. Example:

```json
{
  "format": 1,
  "name": "Purple Link chaos",
  "game": { "title": "ZELDA", "sha256": "…", "crc32": "…" },
  "seed": 1337,
  "notes": "Palette drift + slightly drunk enemies. Great in the Lost Woods.",
  "anchor_state": "purple-link-chaos.anchor.frz",
  "rules": [
    {
      "name": "palette drift",
      "target": "cgram", "range": ["0x000", "0x1FF"],
      "op": { "type": "add", "min": -2, "max": 2 },
      "when": { "every_frames": 30 },
      "count": 4
    },
    {
      "name": "enemy chaos",
      "target": "wram", "range": "enemy_table",
      "op": { "type": "random", "min": "0x00", "max": "0x3F" },
      "when": { "chance_per_frame": 0.02 },
      "count": 1
    },
    {
      "name": "swap two stats",
      "target": "wram",
      "op": { "type": "swap", "a": "0x0F00", "b": "0x0F02", "width": 2 },
      "when": { "hotkey": 1 }
    }
  ]
}
```

### Rule fields

- **`target` + `range`**: a target from the table, and either an inclusive offset pair or a
  *named region* from the game's `regions.json` (for example `"player_stats"`).
- **`op`**, the operation:
  - `add`: add a random delta in `[min, max]`, with `wrap` or `clamp`.
  - `random`: write a random value in `[min, max]`.
  - `bitflip`: flip `bits` random bits, or XOR with `mask`.
  - `swap`: swap address `a` with `b` (`width` 1–2), or swap two random addresses in range.
  - `copy`: copy a block from one place to another, inside or across targets.
  - `freeze`: hold an address at a value (or at the corrupted value) every frame.
  - `shuffle`: permute the bytes inside a small window.
- **`when`**, the trigger: `once_at_frame`, `every_frames`, `chance_per_frame`, `hotkey`
  (1–9), with an optional `window` of `[start, end]` frames counted from activation.
- **`count`**: how many addresses each firing hits.
- **`pick`**: `fresh` (new addresses on every firing, the default) or `sticky` (pick once,
  then keep hitting the same addresses).
- **`enabled`**, **`intensity`** (0–1 multiplier on `count` and the op ranges, driven by
  the panel's sliders), and **`avoid_hot`** (skip addresses the crash map has learned are
  dangerous; see below).

### Determinism and replays

Every rule draws from its own RNG (SplitMix64 seeded from `seed`, rule index and firing
number), so changing one rule doesn't reshuffle the others. Given the same starting state,
a recipe replays identically. That's what `anchor_state` is for: saving a recipe can bundle
the save state it was started from, so "load recipe" puts you back at exactly that moment
with exactly that chaos. **Re-roll** just changes `seed`.

Player input still makes runs diverge. Bit-exact replays (recording the inputs too, using
Snes9x's movie support) are a possible later extra, not part of v1.

## The corruption library

```
<library root>/                       default: ~/Documents/Snes9x Corruptions
  Super Mario World [a1b2c3d4]/       folder per game: title + first 8 hex of the ROM SHA-256
    regions.json                      named address ranges for this game
    crashmap.json                     learned danger scores (written by the engine)
    recipes/*.json
    recipes/*.anchor.frz
```

The root can be any folder. The intended setup is a shared GitHub repo that Mikael and
Erik both clone and point Snes9x at. The library is designed to stay **easy to merge in
git**:
- JSON is written in a canonical form (a fixed key order, 2-space indent, one rule per block),
  so diffs stay readable.
- Learned data is written **per person** (`crashmap.<user>.json`, `journal.<user>.jsonl`),
  and the engine merges every file it finds when it reads them. Two people playing the same
  game never touch the same file, so there are no merge conflicts.
- Snes9x never runs git itself in v1. You commit and pull as usual. A "sync library"
  button is a possible later extra. Loading a ROM shows that game's recipes in the panel. A recipe
whose SHA-256 doesn't match the loaded ROM opens with a warning, because the addresses may
not mean the same thing in another version.

## Crash detection

The detector evaluates signals at the frame hook. Each one can be switched on or off and
tuned per recipe, because sometimes a frozen screen *is* the joke.

| Signal | How it's detected | Default |
|---|---|---|
| CPU stopped | `STP` sets `HALTED_FLAG` | on, instant |
| CPU deadlock | the core's own "CPU is deadlocked" fatal error / `Settings.StopEmulation` | on, instant |
| Runaway execution | PC inside open bus or the I/O registers, or an endless `BRK`/`COP` loop | on, 30 frames |
| No frame interrupt | NMI disabled for more than N frames (normal games re-enable it within frames) | on, 3 s |
| Frozen picture | frame hash unchanged for N seconds while the CPU keeps running | on, 5 s |
| Stuck black screen | forced blank or brightness 0 for more than N seconds | on, 8 s |
| Sound CPU hang | the sound CPU's program counter stuck in a tight loop with the ports frozen | off at first, needs tuning |

There's also a **watchdog** in the front end: if no frame completes for 2 s of wall-clock
time, that counts as a crash too. It catches the cases where the frame hook stops running
at all.

Each crash gets a **reason** (shown on screen, for example "CPU stopped (STP at $80:1234)").
The panel can also mark the current state as a crash by hand ("this is broken, roll back").

## Rollback

- **Checkpoints:** every K frames (default 60, so 1 s), the engine takes an in-memory save
  state (`S9xFreezeGameMem`) into a ring holding the last N (default 30), each a few hundred
  KB. Each checkpoint records the engine's **event log** since the previous one: which rule
  fired, at which addresses, with which old and new values.
- **On a crash:**
  1. *Attribute:* the suspects are the events between the newest checkpoint before the crash
     and the crash itself, weighted towards the most recent.
  2. *Restore:* load the checkpoint from just before the suspects, so you lose about 1–2 s of
     play.
  3. *Retry with a policy.* Policies are **pluggable strategies** behind one small
     interface (`suspects + history → action`). The policy is chosen per recipe, with a
     per-game default, because what works will differ between games and needs lots of
     iteration. The starting set:
     - **re-roll** (the default): replay from the checkpoint with fresh random values for
       the suspect rules only
     - **skip**: drop the suspect events and carry on
     - **dampen**: lower the suspect rule's intensity a step on each crash
     - **disable**: turn the suspect rule off for the rest of the session
     - **escalate**: re-roll first, dampen after two crashes, disable after four
  4. *Repeat crashes* from the same place step further back and **bisect** the suspect set
     to find the guilty event.
  5. Give up after M attempts (default 5): pause, show what kept crashing, and let the player
     decide.
- The existing rewind hotkey stays separate. Rollback has its own ring and its own hotkey.
- **Crash journal:** every crash is appended to `journal.<user>.jsonl` with its time,
  recipe, suspect rule(s), detector signal, chosen policy and outcome (recovered, or crashed
  again within N seconds). Per-rule crash counts show in the panel, so a crash always points
  at a specific rule, and the journal lets us compare policies across games with real data.

## Save-file safety

Corrupting is never allowed to cost a real save, and this is handled with no prompts:
- **On ROM load**, the existing `.srm` is copied to `<library>/<game>/save-backups/` with
  a timestamp before anything runs.
- **Continuously:** while a corruption session is active, the in-memory SRAM is checked once
  per second. If it changed, a backup is written (at most one per minute, plus always the
  last state before the game is closed).
- **Retention:** the last 50 backups per game, plus one per day for 30 days.
- **Restore:** the panel lists the backups with time and size, and restores one with a
  single click (it pauses, loads it, and resets the game).
- The backups are local only, never in the shared library repo (`save-backups/` is
  git-ignored), because save files are personal.
- Checkpoints already include SRAM (`S9xFreezeGameMem` stores it), so a rollback also
  restores save RAM to its value before the crash.

## Crash map (learning safe ranges)

Every attributed crash adds weight to the addresses involved; every firing that *doesn't*
crash adds survival weight. The score is crash rate with a small prior, kept per target
address in `crashmap.json`. Rules with `avoid_hot` skip addresses above a threshold. Over
a session, the engine learns which bytes are "the stack", "the program counter's
neighbours" and so on, and starts avoiding them by itself. That's the robustness you've
been building by hand in the Lua scripts, done automatically. The map is per game and
shared through the library.

## Qt UI

**Corruption panel** (a dock beside the game window):
- recipe picker for the loaded game, plus *new / save / save as / duplicate*
- rule list with enable toggles; a rule editor (target, range or named region, op, trigger,
  count); per-rule intensity sliders; a global intensity slider
- seed field with **Re-roll** and *lock seed*
- status: frames since activation, firings, crashes caught, last crash reason
- crash policy and detector toggles (collapsed by default)

**Hotkeys** (through the existing binding system, so they're rebindable):
toggle engine · fire hotkey-rules 1–9 · roll back now · re-roll seed · save current
recipe · mark crash.

**Memory viewer:** a hex grid per target with three overlays:
- *activity*: how often each byte changes per second (it shows where the game keeps its
  live data)
- *corruption*: which bytes the engine has hit
- *danger*: the crash map

Selecting a range creates a rule or saves a named region from it. The viewer refreshes
about 10 times per second from a snapshot, so it never slows emulation down.

## Testing

- **Engine unit tests** run against synthetic memory buffers: each op, triggers and windows,
  RNG determinism (same seed → same writes), `sticky` vs `fresh`, intensity scaling,
  recipe JSON round-trips and validation errors.
- **Detector and rollback tests** use tiny ROMs **generated by the test itself** (a few
  bytes of 65816 code): one that runs `STP`, one that disables NMI and spins, one that
  jumps into open bus, and one healthy loop. Each test boots the generated ROM in the
  headless core, applies a rule, and asserts on the verdict, the rollback target and the
  policy outcome. No commercial ROMs in the repo.
- **Manual QA** with real games from the OpenEmu library on Mikael's machine.

The test harness is a small CMake target linked against `snes9x-core`, so it runs the same
way on all three platforms.

## Phases

1. **Engine + recipes**: targets, ops, triggers, RNG, JSON library, hotkey to toggle and
   fire, a minimal panel (load recipe, toggle, re-roll, save). *You can corrupt and keep
   recipes from here on.*
2. **Crash detection + automatic rollback**: detector, checkpoint ring, attribution,
   policies, watchdog, on-screen crash reasons.
3. **Full live panel**: rule editor, sliders, named regions, `anchor_state` saving.
4. **Crash map + memory viewer**.
5. **ROM corruption**: a ROM target applied at load time (or live, with cache flushing)
   with the same recipes, seeds and library.

Each phase is test-first and ends in a playable build.

## Out of scope for now

Lua scripting · netplay · coprocessor RAM · bit-exact input replays · the Cocoa Mac app
(only the Qt front end gets the UI) · pushing anything to the upstream Snes9x project.

## Answered questions (2026-10-30)

1. **Library:** any folder, with a shared GitHub repo between Mikael and Erik as the
   intended setup. See *The corruption library*.
2. **Crash policy:** many strategies to iterate on, possibly different per game. Default:
   re-roll the suspect rule, so every crash points at a specific rule. See *Rollback*.
3. **SRAM:** handled automatically, with continuous backups. See *Save-file safety*.
4. **Lua scripts:** not needed; phase 1 starts from simple test recipes.
