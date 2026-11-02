# Snes9x corruption fork: notes for coding agents

This fork adds game corruption to Snes9x: recipes that change a game's memory or ROM while it
runs, a crash guard that rolls back, and tools around both. Upstream Snes9x is untouched
except for a few small, null-guarded hooks in the core.

## Find your way

- **What exists and where: [docs/corruption/FEATURES.md](docs/corruption/FEATURES.md).** One
  entry per feature: code, tests, smoke mode, plan, QA section. Start there.
- How a player uses it: [docs/corruption/GUIDE.md](docs/corruption/GUIDE.md) (also shown in
  the app). Build, library layout, recipe format: [docs/corruption/README.md](docs/corruption/README.md).
- Why it is the way it is: the plan docs in `docs/corruption/` (dated; each ends with
  *How it went*, with measurements). Ideas not built yet: `2026-10-31-horizons.md`.
- Manual test steps, one section per feature: [docs/corruption/QA.md](docs/corruption/QA.md).
- Qt widget house rules: the `qt-widgets-ui` skill in `.claude/skills/`.

## Layers

- `corrupt/`: the engine and everything that can be tested without Qt or the emulator.
  Unit tests in `tests/corrupt/` use fakes (`FakeMemory`, fake `CoreAccess` games).
- `qt/src/`: the app. `CorruptionSession` is the hub; it lives on the emulation thread,
  and `betweenFrames()` runs everything between frames. `CorruptionDock` is the panel.
- The core: `S9xCorruptFrameHook` and a few counters and flags (`S9xRomFlags`,
  `S9xMarkRom`, `S9xWramReads/Writes`). Keep core changes tiny and null-guarded.
- Real-core tests: `tests/headless/` boots tiny hand-assembled test ROMs (`test_rom.cpp`).
  `corrupt-smoke` runs recipes on real games headless and measures them.

## How work is done here

1. A plan section first (in a plan doc, with the design choices spelled out), then a card on
   the roadmap. Before planning a safety or crash-blame idea, read FEATURES.md's *Tried and
   removed*: some ideas were built, measured and taken out, and why is written there.
2. Test first: unit tests with fakes, a real-core case with a test ROM, then `corrupt-smoke`
   numbers on real games (Goof Troop, Pocky & Rocky, FE4; FF6 for ROM work) against a
   baseline. Report what the numbers say, including when a feature doesn't help.
3. `./build-mac.sh test` and `./build-mac.sh asan` must pass. If the app changed:
   `./build-mac.sh qt` and the UI tour (`tests/qt/ui_tour.sh <out> <rom>`); read the PNGs.
4. Update FEATURES.md, GUIDE.md and QA.md, and the plan's *How it went*.
5. Commit straight to `master` as `mil0m <9479577+mil0m@users.noreply.github.com>`, push,
   no attribution lines. `tools/install-hooks.sh` once per clone: the pre-commit privacy
   check.

## Commands

```sh
./build-mac.sh test       # unit, real-core and offscreen Qt tests, plus the privacy check
./build-mac.sh asan       # the same under AddressSanitizer
./build-mac.sh qt         # the app: build/qt/snes9x-qt
build/tests/corrupt-smoke <rom> <recipe.json> <frames> <warmup> [--mode ...]   # see FEATURES.md
tests/qt/ui_tour.sh <out dir> <rom>          # screenshots of every window, offscreen
tests/remote/remote_e2e.sh <out dir> <rom>   # the remote page in a headless browser
tools/privacy_check.sh [--staged]            # nothing private in tracked files
```

## Rules and gotchas

- **No feature may need an LLM to work.** The fork must stand on its own for anyone who
  builds it: per-game knowledge comes from the tracer, from measurements and from what
  players name and share in the library, never from Claude or another model. (Using
  Claude to *write* the code is fine.)
- **Never put private things in the repo**: domains, emails, tokens, tunnel ids. The public
  repo only ever has placeholders (`<your-host>`). `tools/privacy_check.sh` checks against
  an untracked `.private-words` list.
- **The UI tour and any app launch use a scratch config** (`XDG_CONFIG_HOME`), never the real
  `~/Documents/Snes9x Corruptions` library. A hand-written config needs `[%General]` (not
  `[General]`) or `PauseEmulationWhenUnfocused=false` is ignored and the game pauses.
- **Launch the app with `timeout -s KILL`**: a SIGTERM can land on the emulation thread.
- **At most 3 `corrupt-smoke` runs at once, with `nice`** (more once crashed the machine).
  Write run scripts and run them with `bash` (zsh doesn't word-split; macOS bash 3.2 has no
  `wait -n`).
- **Qt widgets are tested offscreen**; offscreen can't paint the macOS style, so look at
  the Fusion pictures for layout and use `TOUR_PLATFORM=cocoa` for the real look.
- **lldb and sample can't attach here**; for a hang, a watchdog thread printing
  `Registers.PBPC` / `CPU.V_Counter` finds where it spins.
- `pint` in the core is signed: use `uintptr_t` for pointer arithmetic on ROM offsets.
- `QHttpServer::bind()` owns the `QTcpServer` it's given; don't delete it yourself.
- After a landing (#51's watch) the tracer teaches the map nothing for the rest of that
  frame; since #57 it still sets *Touched* on changed bytes that run, so `RomBlame` sees
  them. Changed bytes *read as data* after a landing are still unseen (the core's marks are off).
- A run ahead from a frame-start checkpoint must restore the engine's snapshot too: the
  frame hook fires at line 1, after the checkpoint.
