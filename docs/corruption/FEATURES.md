# Corruption features: where everything is

This fork of Snes9x adds live RAM and ROM corruption for fun, with tooling that keeps the game alive. Three layers:

- `corrupt/` is the Qt-free engine and logic. It is unit-tested against fake memory (`tests/corrupt/fake_memory.h`).
- `qt/src/` is the app glue. `CorruptionSession` is the hub and lives on the emulation thread. `CorruptionDock` is the panel.
- The core has a few tiny hooks, so upstream merges stay easy.

Each feature has a plan doc in `docs/corruption/`, tests, and a numbered section in `QA.md` (manual checks). `GUIDE.md` is the player's guide (shown in the app by "? Guide"). Paths below are relative to the repo root; plan docs are `docs/corruption/<date>-<name>.md`, shortened to the name.

## Where things happen each frame

Inside the frame (core): `cpuexec.cpp` calls `S9xCorruptFrameHook` at V=1 (frame start, not VBlank start, because games copy OAM/CGRAM shadows in VBlank). That is `CorruptionSession::frameHook` -> `onFrame` -> `Engine::on_frame` (rules fire, writes go to memory, backups tick). During a run ahead (`running_ahead`) it only calls `engine.on_frame`.

Between frames, `CorruptionSession::betweenFrames()` (`qt/src/CorruptionSession.cpp`), under the session mutex, after `S9xMainLoop()` returns:

1. Back to normal, if requested: engine off, `RomKeeper::restore_clean`, load the run's anchor.
2. Instant revert (#51): `tracer.take_derail()` -> `InstantRevert::after_frame`; a caught bad jump fixes the ROM byte and replays the frame.
3. `RunControl::between_frames`: starts a pending run at a frame boundary (restore clean ROM, ROM at_load rules, power cycle, anchor, pins, other at_load rules). Forgets first-use state.
4. `RomBlame::collect`: which changed ROM bytes the game used this frame.
5. `testFirstUse()` (#55): plays newly used bytes ahead from the frame's start checkpoint, before the guard looks.
6. `CrashMap::observe` the frame's writes (before a rollback can discard them).
7. `CrashGuard::between_frames`: detector, policy, rollback, proven blame (#56); then `RomKeeper::after_guard`.
8. Derail watch on/off, `InstantRevert::keep` (frame-start checkpoint), crash-map survivals and save.
9. `MemoryWatch::observe` (heat, hits), WRAM access counters, `RomHeat::observe` (Live overlay).
10. Code map and jump log housekeeping: reload, tick, save, copy for the viewer twice a second, jump-table search.
11. Guard notice to the status line.
12. `testAhead()` (#52), last: plays the next frame's ROM changes ahead from exactly here.

## Phases 1 and 4: recipes, panel, shaping while playing (#2-#5, #14-#17)

**Recipes, rules, triggers, ops (#2)** - JSON recipes replace Lua. A recipe holds rules; a rule picks addresses in a memory target, a trigger says when, an op says what to write.
- Code: `corrupt/recipe.h` (Recipe, GameId), `recipe_json.cpp` (parse/emit, never throws), `rule.cpp` (Rule, pick addresses, AimedBytes), `trigger.cpp` (every_n, hotkey, at_load, windows), `ops.cpp` (Op, OpState), `memory.h` (Target, AddressRange, MemoryAccess), `rng.cpp` (seeded, replayable), `rule_text.cpp` (rule summaries in words), `engine.cpp` (Engine, EngineSnapshot, event log).
- Tests: `tests/corrupt/{recipe_json,rule,rule_text,trigger,ops,engine,rng,memory,examples}_test.cpp`; `examples_test.cpp` loads every file in `docs/corruption/examples/`.
- Measure: `corrupt-smoke <rom> <recipe>` (default mode: clean run vs recipe run, checks writes land).
- Docs: `ram-corruption-design`, `phase1-engine-recipes-plan`; QA 1-4; GUIDE "Recipes and rules".
- Gotchas: rules get run-time ids; snapshots and guard crash counts key on them. The frame hook is at V=1.

**Core memory access** - the real emulator behind `MemoryAccess`.
- Code: `corrupt/core_memory.cpp` (CoreMemory), `corrupt/core_access.cpp` (SnesCoreAccess: save/load state, reset, `rom_offsets`, `locked_rom`, unseen frames), `corrupt/hook.h`. Qt: `qt/src/Snes9xController.cpp` clears samples while `SnesCoreAccess::unseen()`.
- Tests: `tests/headless/guard_integration_test.cpp`, `tests/headless/headless_core.cpp` (core without a window).
- Docs: `phase1-engine-recipes-plan`.

**Panel, starters, library path (#4)** - dock with Start here card, recipe list, status lines.
- Code: `qt/src/CorruptionDock.cpp`, `CorruptionSession.cpp`, `Starters.cpp` (13+ bundled starters, shared by panel and remote), `qt/src/resources/corruption_examples.qrc` (+ `docs/corruption/examples/*.json`).
- Tests: `tests/corrupt/examples_test.cpp` (checks the qrc too). The dock itself has no automated test; use the UI tour.
- Docs: `phase1-engine-recipes-plan`, `ux-pass-plan`; QA 0, 1; GUIDE "Start here".

**Intensity and Chaos (#15)** - a live strength slider per rule and one for the whole recipe. Below 1 it thins firings rather than shrinking values.
- Code: `Engine::set_intensity`, `set_rule_intensity` (`corrupt/engine.cpp`), `qt/src/IntensitySlider.cpp`, dock Chaos slider.
- Tests: `tests/corrupt/engine_test.cpp`, `tests/qt/intensity_slider_test.cpp`.
- Docs: `phase3-live-panel-plan` "#15"; QA 9; GUIDE "The words".

**Hotkeys (#4)** - Corrupt (F7), Back to Normal (Shift+F7), re-roll, save recipe, fire hotkey rules 1-9, mark crash, more/less.
- Code: `qt/src/EmuConfig.hpp` (`eCorruptionToggle` ... `eCorruptionBackToNormal`, num_shortcuts 71), `ShortcutsPanel`, `EmuApplication::handleCorruptionBinding`, `Engine::press_hotkey`.
- Tests: `tests/corrupt/trigger_test.cpp`, `engine_test.cpp`.
- Docs: QA 3; GUIDE "Hotkeys".

**Live rule editor (#14)** - add, change and remove rules while the game runs; only the edited rule restarts.
- Code: `qt/src/RuleEditor.cpp`, `Engine::replace_rule`, dock rule list.
- Tests: `tests/qt/rule_editor_test.cpp` (offscreen, writes PNGs).
- Docs: `phase3-live-panel-plan`; QA 8; GUIDE "Recipes and rules".

**Saved moments / anchor (#17)** - a recipe carries `<stem>.anchor.frz`; runs start at a frame boundary, and "from the saved moment" replays exactly.
- Code: `corrupt/run_control.cpp` (RunControl: Fresh/FromAnchor/AnchorFailed), `Engine::hold_starts`, `CorruptionSession` (`moment_on_disk`).
- Tests: `tests/corrupt/run_control_test.cpp`; `guard_integration_test.cpp` ("starting from the saved moment replays the same run").
- Measure: `--replay`.
- Docs: `phase3-live-panel-plan` "#17"; QA 10.
- Gotchas: a moment is about 800 KB uncompressed.

**Save backups (#5)** - automatic continuous SRAM backups, so SRAM is allowed freely.
- Code: `corrupt/save_backup.cpp`, `CorruptionSession::backupDir/backups`, dock "Game Save Backups".
- Tests: `tests/corrupt/save_backup_test.cpp`.
- Docs: `phase1-engine-recipes-plan`; QA 5.

**Library and sharing, .s9xrecipe (#3)** - any folder as library (aimed at a shared repo); Share/Import one file with the saved moment inside.
- Code: `corrupt/library.cpp` (Library: recipes, regions, rename/delete region, crash maps, code maps, jump logs, journal paths), `corrupt/share.cpp` (SharedRecipe, base64 moment, regions flattened), dock "More" menu.
- Tests: `tests/corrupt/library_test.cpp`, `share_test.cpp`.
- Docs: `ux-pass-plan`; QA 1, 4; GUIDE "Sharing with a friend".

## Phases 2 and 5: crash guard (#6-#9, #19-#20)

**Crash guard, detector, policies, journal (#6-#9)** - detects a dead game, rolls back to a checkpoint, steps further back on repeats, gives up (corruption off) after 5 fruitless crashes in a row or 40 total.
- Code: `corrupt/crash_detector.cpp` (CrashDetector, FrameSignals, DetectorConfig), `corrupt/crash_guard.cpp` (CrashGuard, CoreAccess, ByteBlame, GuardOutcome), `corrupt/policy.cpp` (re-roll suspect rule, disable, revert...), `corrupt/journal.cpp` (JSONL event log), session `configureGuard`.
- Tests: `tests/corrupt/{crash_detector,crash_guard,journal}_test.cpp` (policies are in crash_guard_test.cpp), `tests/headless/guard_integration_test.cpp` (STP, NMI off, crash loop).
- Measure: `--guard [noblame]` (prints crashes, rollbacks, liveness).
- Docs: `phase2-crash-rollback-plan` ("Findings while building"); QA 7; GUIDE "When the game crashes".
- Gotchas: NMI-off natural stretches reach 2.7 s so the threshold is 5 s; STP stalls the frame hook; frozen screens need button presses.

**Reboot detection (#20)** - a game jumping back to its reset code counts as a crash. Heuristic signal.
- Code: `corrupt/core_access.cpp` (wraps the reset handler's first opcode in the `S9xOpcodes*` tables), `CrashDetector` (`reboots`, `Reboot` kind).
- Tests: `guard_integration_test.cpp` (boot/reset are not reboots; a rebooting game is caught).
- Docs: `reboot-detection-plan`; QA 11.

**Crash map, learner, keeper, avoid_hot (#19)** - 16-byte blocks with crash and survival counts; hot = at least 2 crashes, danger >= 0.4 and raw crash rate >= 3x the memory's other blocks. `avoid_hot` skips hot writes; opt-in, not default.
- Code: `corrupt/crash_map.cpp`, `crash_learner.cpp` (blames last 180 frames of the guard window; survival after 900 frames), `crash_map_keeper.cpp` (mine vs others; saves after a crash, every minute, on close), Library `crashmap.<user>.json`, `Rule.avoid_hot`, `Engine::set_hot_mask`.
- Tests: `tests/corrupt/{crash_map,crash_learner,crash_map_keeper}_test.cpp`.
- Measure: `--learn [held] [from]` (learning seeds, then held-out seeds: plain vs avoid vs random-blocks control; `tests/headless/learn_summary.py` sums split runs).
- Docs: `crash-map-plan`; QA 17; GUIDE "When the game crashes".
- Gotchas: evaluate on held-out seeds only; crash loops give no survivals. Gains showed on FE4 only.

## Phases 4 and 5: memory viewer (#16, #18, #23, #24)

**Memory watch and viewer (#18)** - live view of any target with heat and hit counts.
- Code: `corrupt/memory_watch.cpp` (MemoryWatch, MemorySnapshot, `find_copies`), `qt/src/MemoryViewer.cpp` (toolbar, overlays, sidebar), `MemoryView.cpp` (hex), `MemoryPane.hpp` (interface hex/pixels/tiles share), `MemorySource` interface (fake in tests).
- Tests: `tests/corrupt/memory_watch_test.cpp`, `tests/qt/memory_view_test.cpp`, `memory_viewer_test.cpp`.
- Docs: `memory-viewer-plan`; QA 12; GUIDE "The memory viewer".

**Find copies** - lists where WRAM holds copies of OAM and CGRAM.
- Code: `find_copies` in `corrupt/memory_watch.cpp`.
- Measure: `--copies`.
- Docs: `memory-viewer-plan`; QA 12.

**Named areas / regions (#16)** - name a selection; rename rewrites recipes, delete inlines the range.
- Code: `corrupt/library.cpp` (`set_region`, `rename_region`, `delete_region`, `regions.json`), viewer "Name Selection" and "Named areas" list.
- Tests: `tests/corrupt/library_test.cpp`.
- Docs: `phase3-live-panel-plan` "#16"; QA 13.

**Memory Map sidebar** - fixed hardware layout, plus live VRAM and ARAM layout from PPU/DSP registers.
- Code: `corrupt/memory_map.cpp` (MapArea, VideoLayout, SoundLayout; `video_map`, `sound_map`), `SnesCoreAccess::video_layout/sound_layout`.
- Tests: `tests/corrupt/memory_map_test.cpp`.
- Measure: `--map`.
- Docs: `memory-viewer-plan`; QA 14.

**Pixel view and row width** - memory as colours (cubehelix), rows a power of two >= sqrt(size) by default; row width 1-4096 (0 = auto); zoom 1-16; hover label.
- Code: `corrupt/byte_colour.cpp`, `qt/src/PixelView.cpp` (`setRowWidth`), viewer Hex/Pixels combo.
- Tests: `tests/corrupt/byte_colour_test.cpp`, `tests/qt/pixel_view_test.cpp`.
- Measure: `--dump <dir>` writes raw `.bin` per target; env `CORRUPT_DUMP_DIR`, `CORRUPT_SHOTS_DIR` make the Qt tests render real memory PNGs.
- Docs: `memory-viewer-plan`; QA 15.

**Tile view** - decodes 2/4/8bpp and mode 7 tiles with the live palette.
- Code: `corrupt/tile_decode.cpp` (`snes_colour`), `MapArea.tile_format`, `MemorySource::palette()`, `qt/src/TileView.cpp`.
- Tests: `tests/corrupt/tile_decode_test.cpp`, `tests/qt/tile_view_test.cpp`.
- Docs: `memory-viewer-plan`; QA 16.

**Crash frequency overlay (#24)** - Danger (rate) and Crashes (count, gold) overlays; hot blocks get a gutter tick and outline; hover shows counts.
- Code: viewer overlays in `qt/src/MemoryViewer.cpp`, `CrashMap` counts.
- Tests: `tests/qt/memory_viewer_test.cpp`.
- Docs: `crash-frequency-plan`; QA 18.

**WRAM access map (#23)** - Reads (blue) and Writes (green) overlays of the game's own accesses (Activity only sees value changes).
- Code: `corrupt/core_access.cpp` (`WramAccessCounter`), `MemoryWatch::observe_accesses` (EMA per frame). Core: `S9xWramReads/S9xWramWrites` null-guarded counters in `getset.h` fast paths, `dma.cpp` (CPU->PPU source), `ppu.cpp` (register $2180).
- Tests: `guard_integration_test.cpp` ("the core counts the game's WRAM reads and writes", "the access counter feeds the memory watch").
- Measure: `--access [dir]` (dir writes `wram.reads.f32` etc.).
- Docs: `access-map-plan`; QA 19.
- Gotchas: off = free, on = about 0.9 ms/frame. Not counted: code fetched from WRAM, HDMA, SA-1.

## Phase 6: ROM corruption (#22)

Plan: `rom-corruption-plan` (six phases, each with "Phase N in detail"). QA 20-25; GUIDE "Corrupting the ROM". Model: a run = clean ROM + the run's edits; off keeps the damage; giving up restores clean.

**ROM target (phase 1)** - `Target::ROM` (up to 8 MB), trigger `at_load`, header and vectors locked.
- Code: `corrupt/rom_keeper.cpp` (RomKeeper: MemoryAccess decorator, clean copy, journal, revert/settle/restore_clean/borrow), `SnesCoreAccess::locked_rom`, `Engine::set_locked`, `RunControl` start order, `CoreAccess::reset_console`. Export ROM (`SnesCoreAccess::rom_image`) fixes the checksum on a copy.
- Tests: `tests/corrupt/rom_keeper_test.cpp`; `guard_integration_test.cpp` (locked header, classic at_load patch, Export ROM).
- Docs: `rom-corruption-plan` Phase 1; QA 20.

**Area ops, Paint, PNG (phase 2, the Photoshop way)** - shift, flip, levels, filter, stamp, fill on a rectangle; paint bytes with a brush; export/import PNG.
- Code: `corrupt/area_ops.cpp` (Area, Rect), `Rule.rect`, `corrupt/patches.cpp` (Patch: runs of hex, `Recipe.patches`, applied at run start), `PixelView` Alt-drag selection and Paint mode, `qt/src/AreaImage.cpp` (PNG text chunk "snes9x-area"), session `setBytes`.
- Tests: `tests/corrupt/{area_ops,patches}_test.cpp`, `tests/qt/area_image_test.cpp`.
- Docs: `rom-corruption-plan` Phase 2; QA 21; starter `rom-region-shift`.

**Code map and tracer (phase 3)** - learns which ROM bytes are opcode, operand, data or DMA by watching the game run.
- Code: `corrupt/opcodes.cpp` (256-entry table, disassembler), `corrupt/code_map.cpp` (flags, RLE file `S9XCMAP1`, CodeMapKeeper, Library `codemap.<user>.bin`), `RomTracer` in `core_access.cpp` (wraps all 6x256 opcode entries). Core: `S9xRomFlags`, `S9xRomFetchFrom/To` (`cpuexec.cpp`), `S9xMarkRom` (`getset.h`), `dma.cpp` loop.
- Tests: `tests/corrupt/{opcodes,code_map}_test.cpp`; `guard_integration_test.cpp` (lengths match core tables, marks byte for byte, chains with reset watch).
- Measure: `--codemap [file]` (cost: 4-7% of speed).
- Docs: `rom-corruption-plan` Phase 3; QA 22.
- Gotchas: `S9xMarkRom` uses `uintptr_t` (`pint` is signed and caused an out-of-bounds write). FF6's title runs from WRAM, so use warmup 1 and 18000 frames.

**Blame by execution (phase 4)** - a crash reverts the changed bytes the game actually ran, not guesses.
- Code: `corrupt/rom_blame.cpp` (RomBlame, ByteUse; `codeflag::Touched`), `ByteBlame` in `crash_guard.h`, `RomPolicy` in `policy.cpp` (recipe `rom_policy` revert|reroll), `RomKeeper::settle`, `CrashMapKeeper::learn_rom_by_use`.
- Tests: `tests/corrupt/rom_blame_test.cpp`; `guard_integration_test.cpp` ("blame by execution").
- Measure: `--guard`.
- Docs: `rom-corruption-plan` Phase 4; QA 23.
- Gotchas: viewer Changes overlay (orange) and hover show use counts. macOS bash 3.2 has no `wait -n`.

**Code-aware ops and aim (phase 5)** - opcode swap, branch flip, NOP out, and aim rules at a role (code, operand, data, DMA, used).
- Code: `corrupt/code_aim.cpp` (Aim, Role, swap families, `apply_code_op`), `OpType::OpcodeSwap/BranchFlip/NopOut`, `Engine::set_code_flags`, rule editor Aim group; starters `rom-branch-flips`, `rom-operand-nudge`, `rom-opcode-swap`.
- Tests: `tests/corrupt/{code_aim,code_rules}_test.cpp`; `guard_integration_test.cpp` (NOP-out, branch flip).
- Measure: `CORRUPT_CODEMAP=<file>` loads a saved map for guard runs.
- Docs: `rom-corruption-plan` Phase 5; QA 24.

**Survivors and pins (phase 6)** - changes that proved themselves can be pinned and re-applied at every run.
- Code: `corrupt/survivors.cpp` (`find_survivors`: survived and used >= 100 frames), `Pin` in `corrupt/patches.h` (`Recipe.pins`), `Engine::apply_pins/add_pins/remove_pins`, `RomBlame::set_pinned`, `qt/src/SurvivorsDialog.cpp` (dock "Survivors..."), session `pin/unpin`.
- Tests: `tests/corrupt/pins_test.cpp`, `tests/qt/survivors_dialog_test.cpp`; `guard_integration_test.cpp` ("survivors and pins").
- Docs: `rom-corruption-plan` Phase 6; QA 25; GUIDE "Survivors and pins".
- Gotchas: a pin made on another ROM version is skipped and counted.

**Export ROM** - writes the corrupted ROM with a fixed checksum.
- Code: `SnesCoreAccess::rom_image` in `corrupt/core_access.cpp`; dock ROM section.
- Tests: `guard_integration_test.cpp` ("Export ROM").
- Docs: `rom-corruption-plan` Phase 1; QA 20.

**Back to normal** - one button or Shift+F7: engine off, clean ROM, back to the run's starting moment.
- Code: `CorruptionSession::backToNormal` and the first block of `betweenFrames`; dock button.
- Docs: `ux-pass-plan`; QA 0; GUIDE "Getting back to normal".

## Phase 3: UX and UI pass (#49)

**UX pass** - rebuilt panel, plain words, in-app guide, starters, Share/Import.
- Code: `qt/src/CorruptionDock.cpp`, `GuideWindow.cpp` (QTextBrowser rendering `GUIDE.md`; `showSection`), `Starters.cpp`.
- Docs: `ux-pass-plan`; QA 0; GUIDE (whole file).

**Visual UI pass** - native style, no app stylesheet, Tabler icons recoloured at paint time, light and dark first-class.
- Code: `qt/src/UiKit.cpp` (tones, `ui::icon`, heading/form helpers, ElidedLabel, StatusLine, `ui::audit`), `corrupt::hex_address` (5 digits wram/sram, 6 rom, else 4), icons in `qt/src/resources/corruption-icons`.
- Tests: `tests/qt/ui_kit_test.cpp`; UI tour (below).
- Docs: `ui-pass-plan`; project skill `.claude/skills/qt-widgets-ui`.
- Gotchas: offscreen cannot paint the macOS style (blank controls). A hand-written scratch config needs `[%General]` or `PauseEmulationWhenUnfocused` is ignored and the game pauses when another window takes focus.

## Phases 8 and 9: game-aware (#25, #32)

**Jump tables (#25)** - finds tables of jump targets from indirect jumps the game ran.
- Code: `corrupt/jump_tables.cpp` (JumpLog noted by `RomTracer` for opcodes 7C/FC/6C/DC; `find_jump_tables`; JumpLogKeeper; Library `jumps.<user>.json`), `SnesCoreAccess::rom_offsets`, viewer Jumps overlay and hover, dock line.
- Tests: `tests/corrupt/jump_tables_test.cpp`; `guard_integration_test.cpp` (tracer notes both dispatch kinds).
- Measure: `--jumps [file]`.
- Docs: `pointer-swap-plan`; QA 26.
- Gotchas: likely entries must point at code that RAN; a looser rule exploded on HiROM.

**Pointer swap (#32)** - swap a table entry for another real target of the same jump site, so the game runs a wrong but valid routine.
- Code: `corrupt/pointer_swap.cpp` (SwapSlot, donors), `OpType::PointerSwap`, `AimedBytes.swaps` (not `slots`: a Qt macro), `Engine::set_jump_tables`; starters `rom-pointer-swap`, `rom-pointer-roulette`.
- Tests: `tests/corrupt/pointer_swap_test.cpp`; `guard_integration_test.cpp` ("a pointer swap makes the dispatcher run another real routine").
- Measure: `--swaps [seeds] [count]` (swap vs random bytes vs clean).
- Docs: `pointer-swap-plan`; QA 26.

## Phase 7: safety (#50-#52, #55-#57)

Plan for all: `safety-plan` (one section per item, each with "How it went").

**ROM live heat (#50)** - a fading overlay of ROM bytes the game ran or read, per frame.
- Code: `corrupt/rom_heat.cpp` (RomHeat, 90-frame fade), viewer "Live" overlay; scans `Touched` bits after `rom_blame.collect`; paused unless the viewer shows ROM.
- Tests: `tests/corrupt/rom_heat_test.cpp`; `guard_integration_test.cpp` ("live heat").
- Docs: `safety-plan` #50; QA 27.

**Instant revert + derail watch (#51)** - when the CPU lands in non-code right after a changed byte ran, put the byte back at once and replay the frame.
- Code: `corrupt/derail.cpp` (DerailWatch: landing table, 64-instruction window, ring of 8 culprits), `corrupt/instant_revert.cpp` (frame-start checkpoint via `CoreAccess::save_state_into`), `codeflag::Changed 0x40`, `RomBlame::fix`, `RomTracer::watch_derails/take_derail`, `CrashMapKeeper::rom_crashed`, journal event `instant_revert`, recipe `instant_revert` (default on), `CorruptionSession::stateLoaded` (from `EmuApplication::loadState`); starter `rom-code-drip`.
- Tests: `tests/corrupt/{derail,instant_revert}_test.cpp`.
- Measure: `--derail-fp [learn]` (false alarms on a clean game; target 0), `--instant [seeds]` (guard alone vs with instant revert; `CORRUPT_INSTANT_VERBOSE=1`).
- Docs: `safety-plan` #51; QA 28.
- Gotchas: the watch costs 5-10% on top of tracing, so it is on only while a ROM run is active. DMA-only landings were dropped (FE4 runs code it DMAs). Use a reused state buffer: a fresh 800 KB vector costs 3.7 ms.

**Test before applying (#52)** - plays a live ROM change a few seconds ahead, unseen, and keeps it only if it survives.
- Code: `corrupt/test_ahead.cpp` (TestAhead::before_frame, RunAhead, RunSpec, vetoes, `quiet_after`), `Engine::preview_next` (OverlayMemory), `CoreAccess::begin_unseen/run_unseen_frame/end_unseen`, `RomTracer::note_jumps(false)` during runs, session `testAhead`, `running_ahead`. Panel switch "Try ROM changes ahead first" (`setTestFirstEnabled`, recipe `test_first` seconds, default 3, 0 off).
- Tests: `tests/corrupt/test_ahead_test.cpp`.
- Measure: `--ahead [seeds]`.
- Docs: `safety-plan` #52; QA 29.
- Gotchas: a run tests ONE firing (rules re-fire inside the window). 80-97% of written bytes are unused within 3 s in attract mode, so totals move little. About 130-210 ms per firing.

**Test at first use (#55)** - when a changed byte is used for the first time, run ahead with and without it; fix it if the control run is clean.
- Code: `corrupt/first_use.cpp` (FirstUseTest: queue batch 16, gap 30 frames, narrowing by halves within 8 runs, `vouch`), `RomKeeper::borrow`, `RomBlame::fix(bytes, policy)`, `Engine::put_back_log`, `InstantRevert::checkpoint_for(frame)`, session `testFirstUse`; journal `first_use`. Same panel switch and `test_first` as #52.
- Tests: `tests/corrupt/first_use_test.cpp`.
- Measure: `--first-use [seeds]` (+ `CORRUPT_FU_FRAMES/_SETTLE/_NMI/_GAP`).
- Docs: `safety-plan` #55; QA 30.
- Gotchas: the frame hook fires after the frame-start checkpoint, so a run from it must restore the engine snapshot or the frame's own writes vanish. Cheap (<1% time) but catches 1-2 bytes per 10 min in attract mode.

**Proven blame (#56)** - a crash that comes back after a rollback is replayed from the guard's checkpoints to find the bytes that cause it, then only those are fixed.
- Code: `corrupt/proven_blame.cpp` (ProvenBlame; `BlameProver` in `crash_guard.h`; budget 10 runs/4000 frames; `reduce` narrows to a small set whose clean values *prevent* the crash, since #57; at most 8 bytes else fall back), `RomBlame::used_between`, `RunSpec.detect`, session `configureGuard` (`proven`, `setProven...`). Panel switch "Replay a crash that comes back to find its cause" (on).
- Tests: `tests/corrupt/proven_blame_test.cpp`.
- Measure: `--prove [seeds]` (+ `CORRUPT_PROOF_DIAG`: each proven byte's flags and use record; `CORRUPT_WATCH_BYTE=<hex>`: one byte each frame; `CORRUPT_PROVE_FIRST`), `--nmi` (clean NMI-off stretches per game).
- Docs: `safety-plan` #56; QA 32.
- Gotchas: only for a repeat crash (same signal within 120 frames). Finding: used bytes seen by the tracer never explained FE4's repeat NMI-off crashes, and the real reason (#57) was the narrowing: it kept a half that crashed *on its own*, which finds bytes that could crash the game if it got further, not the ones that did. Now it narrows by prevention. Since the fix, on FE4 Code roulette it beats guessing (39 rollbacks vs 42); FE4 Code drip is still worse (20 vs 15).

**Eyes open after a landing (#57)** - after the derail watch notes a landing, the tracer still marks the changed bytes the game runs (Touched only), so the crash blame, #55 and #56 see them.
- Code: `corrupt/core_access.cpp` (`traced()`'s derailed branch, `see_changed`, `see_changed_slow`), `RomTracer::set_blind_after_landing` (measuring only) and `seen_after_landing()`.
- Tests: `guard_integration_test.cpp` ("after a landing, changed bytes run later in the frame still count as used", "blind after a landing"); `test_rom.cpp` `build_landing`.
- Measure: `--eyes [seeds]` (everything in both arms, the first blind as before).
- Docs: `safety-plan` #57.
- Gotchas: the map still learns nothing after a landing (no Opcode/Operand/width, no jumps), and the core's data/DMA marks stay off, so a changed byte *read as data* after a landing is still unseen. The slow opcode table's half isn't reached by a test ROM (the core runs it only for an instruction crossing a bank's end).

**Core VBlank freeze fix** - corrupted `INC $2133,X` flipped overscan so V never equalled the VBlank line and `S9xMainLoop` never returned.
- Code: `cpuexec.cpp` static `VBlankStarted` (VBlank at 225/240, or at the latest 240, once a frame).
- Tests: `guard_integration_test.cpp` ("a game flipping overscan around VBlank can't freeze the emulator"), `tests/headless/test_rom.cpp` (`build_overscan_flip`, watchdog).
- Docs: `safety-plan` #52 "How it went".
- Gotchas: lldb and `sample` cannot attach here; use a watchdog thread printing `Registers.PBPC` and `CPU.V_Counter`.

**SIGTERM quit handler** - upstream `quit_handler` can deadlock on SIGTERM if it lands on the emulation thread.
- Code: `qt/src/QuitSignals.cpp` (self-pipe), wired in `qt/src/main.cpp`.
- Tests: `tests/qt/quit_signals_test.cpp`.
- Gotchas: use `timeout -s KILL` for launch checks.

## Phase 12: remote control (#53)

**Remote control** - a phone or browser page that drives the live run. Only the owner and one friend connect; no game picture (they stream). The public hostname lives in the owner's tunnel setup, never in this repo.
- Code: `corrupt/remote_api.cpp` (Control interface, Gate lock-out 5/min, Connection hello+token, actions active/back_to_normal/reroll/mark_crash/chaos/rule_enabled/rule_strength/hotkey/watch/paint/starter, `status_json`, binary `S9XM` memory messages), `qt/src/RemoteServer.cpp` (QHttpServer + QWebSocket, 127.0.0.1 only), `qt/src/RemoteControl.cpp` (SessionRemote), page `qt/src/resources/remote/{index.html,remote.css,remote.js}` (`remote.qrc`), panel section Remote Control (EmuConfig `RemoteEnabled`, `RemotePort` 8095, `RemoteAddress`, `RemoteToken`). The page remembers the key in localStorage; a wrong key forgets it. Starter picker shares `Starters.cpp`.
- Tests: `tests/corrupt/remote_api_test.cpp`, `tests/qt/remote_server_test.cpp`, `tests/remote/remote_e2e.sh` + `remote_e2e.mjs`.
- Docs: `remote-plan`; QA 31; GUIDE "Remote control from a browser".
- Gotchas: `http->bind(tcp)` owns the QTcpServer; do not delete it. Never put the hostname, a token or an email in the repo.

**Privacy check** - a pre-commit check that rejects private words and secret-shaped strings.
- Code: `tools/privacy_check.sh [--staged]`, `tools/install-hooks.sh`; word list is the git-ignored `.private-words` (or `SNES9X_PRIVATE_WORDS`).
- Tests: `tests/remote/privacy_check_test.sh`.
- Docs: `remote-plan`.

## Tools and scripts

- `build-mac.sh qt` builds `build/qt/snes9x-qt` (prints compiler errors). `app` builds the Cocoa Snes9x.app, `libretro` the core, `all` all three.
- `build-mac.sh test [args]` builds and runs `corrupt-tests` (args pass to doctest), `corrupt-headless-tests` and `corrupt-qt-tests` (offscreen; PNGs in `build/tests/shots`). It reports crashing test binaries.
- `build-mac.sh asan` runs the same suites under AddressSanitizer (`build/tests-asan`).
- `tests/qt/ui_tour.sh <out> [rom] [--dark] [--scale N]` screenshots every window using the app's `SNES9X_UI_TOUR=<dir>`, with a scratch config and a ROM copy. `TOUR_PLATFORM=cocoa SNES9X_COLOR_SCHEME=light|dark` gives the real Mac look (windows show for about 30 s).
- `tests/qt/ui_tour_matrix.sh <out> [--mac]` runs the tour in Fusion light/dark at scale 1 and 2 (and Mac light/dark); `audit.txt` lists anything a screen reader would miss.
- `tests/remote/remote_e2e.sh <out> <rom>` runs the real app offscreen with remote control on and a headless Chromium (via `~/scripts/kh-browser.mjs` or `BROWSER_HELPER`) at phone width, light and dark.
- `tools/privacy_check.sh [--staged]`, `tools/install-hooks.sh` as above.
- Run GUI tests on a ROM copy with a scratch `XDG_CONFIG_HOME` (`snes9x/snes9x-qt.conf` with `Mute=true`); the `.srm` is written next to the ROM.
- Run at most 3 `corrupt-smoke` processes at once, with `nice`.

`build/tests/corrupt-smoke <rom> <recipe.json> [frames=600] [warmup=600] [mode]`. Modes (from the usage comment at the top of `tests/headless/corrupt_smoke.cpp`):

- (none): run `frames` clean and with the recipe from a saved state; report changes; check every logged write reads back.
- `--guard [noblame]`: one guarded run like the app; reports crashes, rollbacks, liveness. `noblame` blames rules only.
- `--replay`: start at a frame boundary via RunControl, play on, restart from the saved moment, check the same memory and picture.
- `--copies`: list WRAM copies of the sprite table and palette.
- `--map`: print the VRAM and ARAM map with nonzero counts.
- `--access [dir]`: count WRAM reads and writes; busiest blocks; optional raw dumps.
- `--codemap [file]`: record the code map; speed with and without tracing; coverage; optional map file.
- `--jumps [file]`: record jumps; cost; jump tables found; optional jump log file.
- `--swaps [seeds=10] [count=3]`: pointer swap vs random bytes vs clean, unguarded.
- `--derail-fp [learn]`: false alarms of the derail watch on a clean game.
- `--instant [seeds=5]`: guard alone vs with instant revert.
- `--ahead [seeds=5]`: instant revert in both arms; the second tests each live ROM firing first.
- `--first-use [seeds=5]`: both of those in both arms; the second also tests at first use.
- `--prove [seeds]`: proven blame on repeat crashes (see #56). `--nmi`: clean NMI-off stretches per game.
- `--eyes [seeds]`: everything in both arms; the first blind after a landing, as before #57.
- `--learn [held=3] [from=0]`: crash-map learning, then held-out seeds plain vs avoid vs random control.
- `--dump <dir>`: write raw `.bin` per target (needs a recipe and frames; used for viewer pictures).
- Env: `CORRUPT_CODEMAP`, `CORRUPT_LEARN_VERBOSE`, `CORRUPT_INSTANT_VERBOSE`, `CORRUPT_FU_*`, `CORRUPT_PROVE_FIRST`, `CORRUPT_PROOF_DIAG`, `CORRUPT_WATCH_BYTE`, `CORRUPT_DUMP_DIR`, `CORRUPT_SHOTS_DIR`.
