# Phase 1 — engine + recipes: implementation plan

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md).
Goal: by the end, you load a game in the Qt build, pick or make a recipe, toggle corruption
with a hotkey, re-roll the seed, save the recipe to the library, and your save files are
backed up automatically. Crash detection and rollback are phase 2.

**Ground rules:** test-first for all engine logic (red → green → commit); one commit per
task; `corrupt/` never includes Qt; the core gets exactly one new call (`S9xCorruptFrame`).

## Key seam: the engine never touches the emulator directly

```cpp
// corrupt/memory.h
enum class Target { WRAM, SRAM, VRAM, OAM, CGRAM, ARAM };
struct MemoryAccess {
    virtual size_t size(Target) const = 0;
    virtual uint8_t read(Target, uint32_t offset) const = 0;
    virtual void write(Target, uint32_t offset, uint8_t value) = 0;   // owns cache invalidation
    virtual ~MemoryAccess() = default;
};
```

The tests use a `FakeMemory` (plain vectors). The game uses `CoreMemory` (task 9). This keeps
the engine tests fast, independent of any ROM, and the same on every OS.

## Tasks

### 0. Scaffolding
- Vendor `external/nlohmann/json.hpp` (3.11.3) and `external/doctest/doctest.h` (2.4.11),
  with their licenses.
- Create `corrupt/` and `tests/corrupt/`, plus `tests/CMakeLists.txt` building
  `corrupt-tests` from `corrupt/*.cpp` (all of the engine except `core_memory.cpp`)
  and the test files.
- `./build-mac.sh test` builds and runs the tests.
- One trivial passing test proves the harness works. Commit.

### 1. Seeded RNG: `corrupt/rng.{h,cpp}`
SplitMix64, `Rng::for_firing(seed, rule_index, firing_no)`, `next_u64`, `range(min, max)`
inclusive, `chance(p)`.
Tests: same inputs give the same sequence; changing the rule index or firing number changes
it; `range` stays within bounds over 10k draws, hitting both ends; `chance(0)` is never true
and `chance(1)` always is.

### 2. Targets: `corrupt/memory.{h,cpp}`
The `Target` enum, `parse_target("wram") ↔ name()`, `FakeMemory` (test support, so it lives
in `tests/corrupt/`), and an address-range type with validation against the target size.
Tests: parsing every name in both directions; an unknown name is an error; a range out of
bounds is an error, with the target size in the message.

### 3. Operations: `corrupt/ops.{h,cpp}`
One function per op over `(MemoryAccess&, Target, addresses, Rng&) → vector<WriteEvent>`.
Each `WriteEvent` records `{target, offset, old, new}`.
- `add` (wrap / clamp), `random`, `bitflip` (`bits` | `mask`), `swap` (fixed `a`/`b` or
  random pair, `width` 1–2), `copy` (block, possibly across targets), `freeze`, `shuffle`.

Tests, per op: the exact bytes after applying it to a known buffer with a fixed seed; events
that match the actual changes; edge cases (clamp at 0x00/0xFF, swapping an address with
itself, overlapping copies, a width-2 swap at the last byte rejected).

### 4. Triggers: `corrupt/trigger.{h,cpp}`
`should_fire(trigger, frames_since_activation, rng, pressed_hotkeys)` for `once_at_frame`,
`every_frames`, `chance_per_frame`, `hotkey`, with an optional `window`.
Tests: the firing frames over 300 frames for each kind; window edges; a hotkey only fires
on the frame it was pressed.

### 5. Rules: `corrupt/rule.{h,cpp}`
A `Rule` holds its target, range, op, trigger, count, pick (`fresh`/`sticky`), enabled and
intensity. `Rule::tick(...)` picks addresses, scales `count` and the op range by intensity
(at least 1 address when intensity > 0), and returns events.
Tests: `sticky` hits the same addresses on every firing and `fresh` doesn't; intensity 0
writes nothing; a disabled rule writes nothing; addresses never fall outside the range.

### 6. Engine: `corrupt/engine.{h,cpp}`
`Engine::set_recipe`, `set_active`, `reroll(new_seed)`, `press_hotkey(n)`,
`on_frame(MemoryAccess&)`. The engine keeps the frame count since activation and a bounded
event log (ring of the last 10k events, which is what phase 2 needs for attribution), and
publishes a status struct (frames, firings, active).
Tests: running the same recipe twice from the same buffer gives identical bytes after 600
frames; re-rolling changes them; inactive means no writes; a hotkey rule fires only on its
key; the event log is bounded.

### 7. Recipe JSON: `corrupt/recipe_json.{h,cpp}`
`parse_recipe(json, regions) → Recipe | errors` and `write_recipe(Recipe) → string`.
- Numbers can be written as `"0x…"` or decimal. Named ranges resolve through the game's
  regions.
- Errors carry a JSON path (`rules[2].op.min: expected 0..255, got 300`).
- Writing is stable: sorted keys, 2-space indent.

Tests: the spec's example recipe round-trips byte-identically; every validation error case
is caught; an unknown named region is an error that names it; an unknown `format` version
is rejected.

### 8. Library: `corrupt/library.{h,cpp}`
- `game_folder_name(title, sha256)` gives `"Super Mario World [a1b2c3d4]"`, with characters
  that filesystems dislike replaced.
- `list_recipes`, `load_recipe`, `save_recipe` (atomic: write to a temp file, then rename),
  `load_regions`.
- Creates the folder on first save, plus a `.gitignore` containing `save-backups/`.

Tests run in a temp directory: folder naming including odd titles; save → list → load
round-trip; a SHA mismatch is reported as a warning, not an error; a broken recipe file is
listed as broken instead of aborting the listing.

### 9. Core adapter and the hook
- `corrupt/core_memory.cpp`: `CoreMemory` over `Memory.RAM`, `Memory.SRAM`/`SRAM_SIZE`,
  `Memory.VRAM`, `PPU.OAMData`, `PPU.CGDATA`, `SMP::apuram`.
  - VRAM writes: mark the tile caches dirty (`IPPU.TileCached[...]`).
  - OAM writes: set `IPPU.OBJChanged`.
  - CGRAM writes: set `IPPU.ColorsChanged`, and update the matching entry in
    `IPPU.ScreenColors` so the new colour shows right away.
- `corrupt/hook.{h,cpp}`: a global `S9xCorruptFrame()` that forwards to the engine when one
  is attached, and an attach/detach API for the front end.
- `cpuexec.cpp`: one line after `S9xEndScreenRefresh()`.
- Add the sources to `qt/CMakeLists.txt` and `libretro/Makefile.common`, so the libretro
  build keeps compiling (with no engine attached it does nothing).
- Verification: build all three targets, then a manual check (task 14). This is the only
  task with no automated test, because it's pure glue to emulator globals.

### 10. Save-file backups: `corrupt/save_backup.{h,cpp}`
- Pure logic, fully tested: the `should_backup(now, last_backup, sram_hash, last_hash)`
  rate limit (one per minute when changed), and the retention policy (the last 50, plus one
  per day for 30 days) as a function from a file list to the files to delete.
- File input/output: `backup_now(folder, bytes)` and `list_backups`.
- Qt wiring: back up on ROM load (before the first frame), check once per second while a
  recipe is active, and back up on closing the game.

Tests: the rate limit, retention with timestamps spread across 40 days, and the listing
order.

### 11. Qt hotkeys
New rebindable actions in the existing binding system (`EmuConfig` names plus
`EmuApplication::handleBinding`): *Corruption: toggle*, *re-roll seed*, *fire 1* to
*fire 9*, *save recipe*. Each posts to the emulation thread with `runOnThread`. An on-screen
message shows "Corruption ON · seed 1337" and so on.

### 12. Qt minimal panel
A `CorruptionDock` (a `QDockWidget` on `EmuMainWindow`, toggled from the menu):
- library root picker (stored in the config)
- the recipe dropdown for the loaded game, plus *Reload*
- an *Active* toggle, a seed field with *Re-roll*, and *Save as…* (name + notes)
- a read-only rule list (name, target, op, trigger, enabled checkbox)
- status: frames, firings
- a *Save backups* list with *Restore*

The status refreshes 4 times a second from the engine's published snapshot. Full rule
editing waits until phase 3; until then, rules are edited in the JSON file and the
recipe reloaded.

### 13. Starter recipes
`docs/corruption/examples/`, written for any game:
- `gentle-wram-noise` (2 random bytes every 2 s in `$7E:2000–$7F:FFFF`)
- `palette-drift`
- `sprite-jitter` (OAM `add ±2`)
- `music-mangler` (ARAM bitflip, slow)
- `stat-swap-hotkey` (hotkey 1 swaps two random WRAM bytes)

A *Copy example into this game* button in the panel copies one into the game's folder.

### 14. Manual QA script and docs
- `docs/corruption/QA.md`: load Goof Troop, apply each starter recipe, confirm a visible or
  audible effect, re-roll, save, reload, confirm the save backup appears and restores.
- `docs/corruption/README.md`: how to point Snes9x at the shared library repo, and the
  recipe format reference (generated from the spec's rule-field list).
- Mikael runs the QA.

## Out of this phase
Crash detection, rollback, the crash journal and crash map (phase 2 and later), rule editing
in the UI, `anchor_state`, the memory viewer.

## Order and size
Tasks 0–8 and 10 are pure engine work, test-first and independent of the emulator. Tasks
9, 11 and 12 are integration. That's about 15 commits.
