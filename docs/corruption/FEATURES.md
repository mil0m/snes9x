# Corruption features: where everything is

This fork of Snes9x adds live RAM and ROM corruption for fun, with tooling that keeps the game alive. Three layers:

- `corrupt/` is the Qt-free engine and logic. It is unit-tested against fake memory (`tests/corrupt/fake_memory.h`).
- `qt/src/` is the app glue. `CorruptionSession` is the hub and lives on the emulation thread. `CorruptionDock` holds the panel (`Cockpit`) and opens the `Workshop`; both act through `CorruptionControl` (the app's `SessionControl`, a fake in tests).
- The core has a few tiny hooks, so upstream merges stay easy.

Ideas that were built and then removed are listed at the end, under *Tried and removed*: check there before building a new safety feature.

Each feature has a plan doc in `docs/corruption/`, tests, and a numbered section in `QA.md` (manual checks). `GUIDE.md` is the player's guide (shown in the app by "? Guide"). Paths below are relative to the repo root; plan docs are `docs/corruption/<date>-<name>.md`, shortened to the name.

## Where things happen each frame

Inside the frame (core): `cpuexec.cpp` calls `S9xCorruptFrameHook` at V=1 (frame start, not VBlank start, because games copy OAM/CGRAM shadows in VBlank). That is `CorruptionSession::frameHook` -> `onFrame` -> `Engine::on_frame` (kill switches flip first, then rules fire, writes go to memory, backups tick). During a run ahead (`running_ahead`) it only calls `engine.on_frame`.

Right before each frame, `CorruptionSession::beforeFrame()` (#47, from `Snes9xController::mainLoop`): a kept moment playing back sets its pads and writes (or ends, or is taken over), then `MomentRecorder::before_frame` notes the pads and takes a keyframe every 10 s. The frame hook's writes go to the recorder too (`hook_begin`/`hook_end`), and every corruption write reaches it through `RecordedMemory` under the ROM keeper.

Between frames, `CorruptionSession::betweenFrames()` (`qt/src/CorruptionSession.cpp`), under the session mutex, after `S9xMainLoop()` returns:

1. Back to normal, if requested: engine off, `RomKeeper::restore_clean`, load the run's anchor.
2. Instant revert (#51): `tracer.take_derail()` -> `InstantRevert::after_frame`; a caught bad jump fixes the ROM byte and replays the frame.
3. `RunControl::between_frames`: starts a pending run at a frame boundary (restore clean ROM, ROM at_load rules, power cycle, anchor, pins, other at_load rules).
4. `RomBlame::collect`: which changed ROM bytes the game used this frame.
5. `CrashMap::observe` the frame's writes (before a rollback can discard them).
6. `CrashGuard::between_frames`: detector, policy, rollback; then `RomKeeper::after_guard`.
7. Derail watch on/off, `InstantRevert::keep` (frame-start checkpoint), crash-map survivals and save.
8. `MemoryWatch::observe` (heat, hits), WRAM access counters, `RomHeat::observe` (Live overlay).
9. Code map, jump log and call log housekeeping: reload, tick, `call_log.end_frame()` (#26), save, copy for the viewer twice a second, jump-table search.
10. Guard notice to the status line.
11. `testAhead()` (#52): plays the next frame's ROM changes ahead from exactly here.
12. `tryPending()` (#78): a change asked for with Change It's *Try 5 s Ahead*, played ahead with and without it.
13. `keepPending()` and `startPending()` (#47), last: a Keep checked from here and saved; a moment to play back loads for the next frame.

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

**Panel, starters, library path (#4)** - the cockpit (since #63) with a Start here card, the recipe list, status lines.
- Code: `qt/src/Cockpit.cpp`, `RecipeActions.cpp` (load, new, save, share, import, the rule editor; shared by the cockpit, the Workshop and the menu), `CorruptionSession.cpp`, `Starters.cpp` (13+ bundled starters, shared by panel and remote), `qt/src/resources/corruption_examples.qrc` (+ `docs/corruption/examples/*.json`).
- Tests: `tests/corrupt/examples_test.cpp` (checks the qrc too), `tests/qt/cockpit_test.cpp`.
- Docs: `phase1-engine-recipes-plan`, `ux-pass-plan`; QA 0, 1; GUIDE "Start here".

**Strength, per rule and for all rules (#15, #73)** - a live strength slider per rule and one for the whole recipe (*Strength, all rules*, once *Chaos*); they multiply. Below 1 a rule picks fewer bytes, Add and Flip bits hit softer (`scaled_op`), and under one byte a firing it fires less often. Since #73 every rule slider's tooltip (and the editor's line under Strength, and the remote page under each rule) says what that means for the rule, in its units: `describe_strength` in `corrupt/rule_text.cpp`, from the numbers `tick_rule` uses. The file and the remote protocol keep `intensity` and `chaos`.
- Code: `Engine::set_intensity`, `set_rule_intensity` (`corrupt/engine.cpp`), `qt/src/IntensitySlider.cpp`, the cockpit's *Strength, all rules* slider, `describe_strength` (`corrupt/rule_text.cpp`).
- Tests: `tests/corrupt/engine_test.cpp`, `rule_text_test.cpp`, `rule_test.cpp`, `tests/qt/intensity_slider_test.cpp`, `cockpit_test.cpp`, `workshop_test.cpp`, `rule_editor_test.cpp`.
- Docs: `phase3-live-panel-plan` "#15", `cockpit-workshop-plan` "#73"; QA 9, 49; GUIDE "The words".

**One key and the Buttons (#4, #63)** - one corruption shortcut, Corrupt/Stop (F7). The recipe's slots 1-9 (`"hotkey"` in the JSON) are named **Buttons** in the cockpit and on the remote page: a rule on a slot fires each press; a slot with only switches and changes is a toggle that shows whether it's on.
- Code: `qt/src/EmuConfig.hpp` (`eCorruptionToggle` only, num_shortcuts 56), `EmuApplication::handleCorruptionBinding`; `corrupt/buttons.cpp` (`recipe_buttons`: labels, toggles, on; `recipe_kept`), `qt/src/ButtonsBar.cpp`, `Engine::press_hotkey`; `status_json`'s `buttons`.
- Tests: `tests/corrupt/{buttons,trigger,engine,remote_api}_test.cpp`, `tests/qt/buttons_bar_test.cpp`.
- Docs: `cockpit-workshop-plan`; QA 3; GUIDE "The F7 key and the buttons".

**Live rule editor (#14)** - add, change and remove rules while the game runs; only the edited rule restarts.
- Code: `qt/src/RuleEditor.cpp`, `Engine::replace_rule`, the cockpit's rules and the Workshop's Recipe page.
- Tests: `tests/qt/rule_editor_test.cpp` (offscreen, writes PNGs).
- Docs: `phase3-live-panel-plan`; QA 8; GUIDE "Recipes and rules".

**Saved moments / anchor (#17)** - a recipe carries `<stem>.anchor.frz`; runs start at a frame boundary, and "from the saved moment" replays exactly.
- Code: `corrupt/run_control.cpp` (RunControl: Fresh/FromAnchor/AnchorFailed), `Engine::hold_starts`, `CorruptionSession` (`moment_on_disk`).
- Tests: `tests/corrupt/run_control_test.cpp`; `guard_integration_test.cpp` ("starting from the saved moment replays the same run").
- Measure: `--replay`.
- Docs: `phase3-live-panel-plan` "#17"; QA 10.
- Gotchas: a moment is about 800 KB uncompressed.

**Save backups (#5)** - automatic continuous SRAM backups, so SRAM is allowed freely.
- Code: `corrupt/save_backup.cpp`, `CorruptionSession::backupDir/backups`, the Workshop's Safety page.
- Tests: `tests/corrupt/save_backup_test.cpp`.
- Docs: `phase1-engine-recipes-plan`; QA 5.

**Library and sharing, .s9xrecipe (#3)** - any folder as library (aimed at a shared repo); Share/Import one file with the saved moment inside.
- Code: `corrupt/library.cpp` (Library: recipes, regions, rename/delete region, crash maps, code maps, jump logs, journal paths), `corrupt/share.cpp` (SharedRecipe, base64 moment, regions flattened), the Workshop's Recipe page and the Corruption menu (Share, Import); its Sharing page picks the library.
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
- Code: `corrupt/survivors.cpp` (`find_survivors`: survived and used >= 100 frames), `Pin` in `corrupt/patches.h` (`Recipe.pins`), `Engine::apply_pins/add_pins/remove_pins`, `RomBlame::set_pinned`, `qt/src/SurvivorsPanel.cpp` (on the Workshop's ROM page), session `pin/unpin`.
- Tests: `tests/corrupt/pins_test.cpp`, `tests/qt/survivors_panel_test.cpp`; `guard_integration_test.cpp` ("survivors and pins").
- Docs: `rom-corruption-plan` Phase 6; QA 25; GUIDE "Survivors and pins".
- Gotchas: a pin made on another ROM version is skipped and counted.

**Export ROM** - writes the corrupted ROM with a fixed checksum.
- Code: `SnesCoreAccess::rom_image` in `corrupt/core_access.cpp`; the Workshop's ROM page.
- Tests: `guard_integration_test.cpp` ("Export ROM").
- Docs: `rom-corruption-plan` Phase 1; QA 20.

**Back to normal** - one button: engine off, clean ROM, back to the run's starting moment.
- Code: `CorruptionSession::backToNormal` and the first block of `betweenFrames`; the cockpit's button.
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

**Cockpit and Workshop (#63)** - the panel keeps what you touch while playing (recipe, Corrupt/Stop, Back to Normal, Chaos, seed, Buttons, rules on and strength, one line each about the guard and the ROM, linking to the Workshop); everything else is one Workshop window with pages: Recipe (rules in full, *Also in This Recipe*: kept switches, changes, protects, pins, paint, with Remove), Functions, Memory (paint tools only while painting), ROM (status, what's learned, Export/Restore/Clear Paint, survivors), Safety (guard switches, now with the instant revert, crash status, save backups), Sharing (library, *Who's Here*, conflicts, remote control). The Corruption menu has New/Save As/Share/Import and the ROM and Sharing pages.
- Code: `qt/src/CorruptionControl.hpp` (`PanelStatus`, which `CorruptionSession::Status` now is, and the interface), `SessionControl.cpp`, `Cockpit.cpp`, `Workshop.cpp`, `WorkshopPages.cpp` (RecipePage, RomPage, SafetyPage, SharingPage), `RecipeActions.cpp`, `FunctionsPage.cpp` and `SurvivorsPanel.cpp` (were the Functions and Survivors dialogs), `EmuMainWindow.cpp` (menu); session `setInstantEnabled`, `removeKept`, `playerName`; `Library::players`.
- Tests: `tests/qt/{cockpit,workshop,buttons_bar}_test.cpp` (with `fake_control.h`), `tests/corrupt/{buttons,live_library}_test.cpp`; the UI tour photographs the cockpit and every page.
- Docs: `cockpit-workshop-plan`; QA 38; GUIDE "The panel and the Workshop".
- Always a recipe (#74): a game loads with a blank **Untitled** recipe (`newRecipeLocked({})` at the end of `gameLoaded`); everything lands in it, the made-up names (*Painted*, *Switches*, …) are gone, and the name comes at Save. *Blank* (Untitled with nothing in it, `blankLocked`) counts as no recipe for the Start card, Corrupt, Save and the remote page; `confirmDiscard` asks about any Untitled content. The Workshop names the recipe above its pages and in its title (`recipe_state`, `setWindowModified`); Functions, Memory, ROM, Safety and Sharing each say what of them is the recipe's. Tests: `cockpit_test.cpp`, `workshop_test.cpp`, the tour's *untitled* lines and `02a`–`02c` pictures. Plan: `cockpit-workshop-plan` #74; QA 50.
- The Safety page explains its switches (#76): each switch keeps a Secondary line under it (and as its accessible description) saying what it watches for, what it does, what it costs, what off means, whether it has anything to do for this recipe and what it has done (`guard_note`, `ahead_note`, `instant_note`, `policy_note` in `WorkshopPages.cpp`; `PanelStatus::test_first_seconds`). Two switches renamed: *Test ROM changes before they reach the game*, *Undo a ROM change the moment it derails the game*. Replaces review item 15's Careful/Balanced/Wild preset, rejected: presets hide what a setting does. Tests: `workshop_test.cpp` (#76 cases). Plan: `cockpit-workshop-plan` #76; QA 51.

**Panel quick wins (#64)** - after a look at every window with a real library: a state bar at the top of the cockpit (*Corrupting · 0:13 · 6 changes* in the accent colour, *Stopped · …* tinted, idle and normal plain) instead of the run line; the message line shows a short headline that fades after 12 s (the full text on hover) and keeps its height so Corrupt never moves; the title is the ROM's file name; a frameless seed; the guard and ROM lines are whole links with an icon and a chevron; *(new)* for an unsaved recipe; no Refresh buttons (Functions re-sorts when shown, survivors refresh every 2 s keeping the selection); Sharing hides the remote fields while it's off; the Workshop opens on its last page and size; stub functions no longer count as switched off.
- Code: `Cockpit::showState`/`setLink`/fade timer, `PanelStatus::message_detail` and `rom_name`, `CorruptionSession::note(text, detail)`; `headline` next to `summary` in `GuardOutcome`, `InstantRevertResult` and `AheadResult`; `function_off()` in `corrupt/calls.h`; `Workshop::onClosed` and `CorruptionDock::openWorkshopAgain` with `WorkshopPage/Width/Height` in the `[Corruption]` settings; `SurvivorsPanel::refresh` updates in place when the bytes are the same; `tests/qt/ui_tour.sh` takes `TOUR_LIBRARY=<dir>` (tours with a copy of a library).
- Tests: `calls_test` (stubs), headline checks in `crash_guard_test`, `instant_revert_test`, `test_ahead_test`; `cockpit_test` (state bar words and colour, message fade without moving Corrupt, title, *(new)*, frameless seed, whole-line links), `workshop_test` (remote fields, no Refresh, onClosed, Functions re-sort), `functions_page_test` (growing counts fit).
- Plan: [2026-11-02-cockpit-workshop-plan.md](2026-11-02-cockpit-workshop-plan.md), *Quick wins*. QA: 39.

**Workshop width and sidebar notes (#65)** - Recipe, ROM, Safety and Sharing keep a readable width (about 100 characters, never less than the page needs); Functions and Memory fill the window. The page list is a summary: a note beside each name (*3 rules · unsaved*, *2 off*, *40 painted*, *14 changed*, *1 crash* / *guard off* in the warning colour, *2 here* / *remote on*), updated twice a second whichever page shows.
- Code: `sidebar_notes()` in `qt/src/WorkshopPages.cpp`; `Workshop::readableWidth`, `capped()`/`capPages()` (again on show, after the Mac's polish), `NoteDelegate`, `NoteRole`/`NoteWarningRole`, notes also in `Qt::AccessibleTextRole`; who's here read every 5 s.
- Tests: `tests/qt/workshop_test.cpp` (the notes, the sidebar following the session, widths in a 1,600 px window).
- Plan: same doc, *The Workshop reads like a page*. QA: 40.
- A small Workshop (#68): the Memory page's combo boxes may be shorter than their longest choice, Go to and Clear Hits start its second row, Export/Import PNG are one *Picture* menu, *What's where* goes down to 130 px, and the page scrolls with its picture at least 260 px tall; the Workshop's scroll areas take their page's minimum width (`fitWidth`). Smallest: 869 × 450 (Fusion), 894 × 324 points (Mac). Test: every page at the smallest size gets its minimum width. QA 44.
- What follows Where in the rule editor (#69): the op list is grouped (*Bytes*, *Areas, like a picture*, *Code: the ROM only*; headings are enabled but not selectable), code ops (`rom_only`: code, jump and function ops) are greyed unless the target is the ROM, and leaving the ROM with one picked selects Random value with a note (`op_note`); pages are found by `page_index`, not the combo row. QA 45.
- Normal, Stay Here (#70): `CorruptionSession::backToNormal(stay_here)` skips loading the run's start; `PanelStatus::normal_here`; `CorruptionControl::normalHere`; the cockpit's `normal_here` button under Back to Normal; remote action `normal_here` and its button. QA 46.

**Paint with a draft, a history and a crash watch (#66)** - from a play-test: ROM paint crashed and nothing rolled back (the guard only watches while corrupting, and paint was rollback-proof). ROM strokes are drafted (tinted in the pixel view) and go in together with **Apply Paint**, which takes a restore point and watches ten seconds for a crash, corrupting or not; a crash loads the restore point, takes the batch out and puts its strokes back in the draft. Every stroke undoes and redoes (Cmd+Z / Shift+Cmd+Z), with a Paint History list; other memories paint live with undo. **Corruption > Start Games Paused** opens a game before its first frame. Remote ROM paint is a stroke and an Apply in one.
- Code: `corrupt/paint_history.{h,cpp}` (`PaintHistory` over `PaintStore`: Draft/Live/Applied/Discarded entries, `dab`/`end_stroke`, `undo`/`redo`/`go_to`, `draft`, `apply`, `take_back`, `rows`), `corrupt/paint_store.h` (`RecipePaintStore`: ROM keeper `write_at_start`, engine patches, locked and guarded skipped), `corrupt/paint_watch.{h,cpp}` (`PaintWatch`, 600 frames, the guard's detector config), `patch_byte`, `Engine::remove_patches`; session `paint/endStroke/applyPaint/undoPaint/redoPaint/goToPaint/discardDraft/paintRows/paintNews/setBytesNow`, `watchPaint` first in `betweenFrames`, `is_paused` (Apply at once while paused), `lookNow` (snapshots without a frame), `MemorySnapshot::draft`; `MemoryViewer` paint bar and history, `PixelView::onStrokeEnd` and draft marks; `EmuConfig::corruption_start_paused`; `PanelStatus::paused`.
- Tests: `tests/corrupt/{paint_history,paint_watch}_test.cpp`; `guard_integration_test.cpp` (a stroke of STP over the main loop taken back with corruption off; a harmless one kept); `memory_viewer_test.cpp` (draft bar, Apply/Throw Away for the ROM only, undo/redo keys, history rows, draft marks); `cockpit_test.cpp` (paused). The UI tour paints 1 KB of STP on Pocky & Rocky and photographs it taken back.
- Plan: [2026-11-02-paint-draft-plan.md](2026-11-02-paint-draft-plan.md). QA: 42.

## Phases 8 and 9: game-aware (#25, #26, #32, #34, #35, #58-#61)

**Jump tables (#25)** - finds tables of jump targets from indirect jumps the game ran.
- Code: `corrupt/jump_tables.cpp` (JumpLog noted by `RomTracer` for opcodes 7C/FC/6C/DC; `find_jump_tables`; JumpLogKeeper; Library `jumps.<user>.json`), `SnesCoreAccess::rom_offsets`, viewer Jumps overlay and hover, the ROM page's line.
- Tests: `tests/corrupt/jump_tables_test.cpp`; `guard_integration_test.cpp` (tracer notes both dispatch kinds).
- Measure: `--jumps [file]`.
- Docs: `pointer-swap-plan`; QA 26.
- Gotchas: likely entries must point at code that RAN; a looser rule exploded on HiROM.

**Pointer swap (#32)** - swap a table entry for another real target of the same jump site, so the game runs a wrong but valid routine.
- Code: `corrupt/pointer_swap.cpp` (SwapSlot, donors), `OpType::PointerSwap`, `AimedBytes.swaps` (not `slots`: a Qt macro), `Engine::set_jump_tables`; starters `rom-pointer-swap`, `rom-pointer-roulette`.
- Tests: `tests/corrupt/pointer_swap_test.cpp`; `guard_integration_test.cpp` ("a pointer swap makes the dispatcher run another real routine").
- Measure: `--swaps [seeds] [count]` (swap vs random bytes vs clean).
- Docs: `pointer-swap-plan`; QA 26.

**Call graph (#26)** - the tracer notes every call from ROM into ROM (`JSR` 20, `JSL` 22, `JSR (a,X)` FC) and folds them into functions: entry, near/long/mixed, calls, callers, frames run in all, and *now* (frames of the last 300).
- Code: `corrupt/calls.cpp` (CallLog noted by `RomTracer` via `note_call` and `note_jump`; CallKeeper with `end_frame` each frame, `functions()`, `forget_now` on state load; `busiest`; Library `calls.<user>.json`, merged), session `call_log` (loaded with the code map, saved once a minute and on close), paused with `note_jumps(false)` during runs ahead.
- Tests: `tests/corrupt/calls_test.cpp`; `guard_integration_test.cpp` suite "call graph"; test ROM `build_calls` (near, long, mixed via PHK+JSR, inline data after a JSR).
- Measure: `--calls` (speed with and without, functions found after 10 s, 1 min, 5 min, the busiest).
- Docs: `functions-plan`.
- Gotchas: counts frames, not calls (a loading loop calls one function hundreds of times in a frame). Mixed functions (called both ways) can't be switched off; none turned up in Goof, Pocky, FE4 or FF6. FF6's title screen runs from WRAM, so nothing is learned there (#30).

**Kill switches (#34)** - turn a function off live: a return (RTS/RTL, as it's called) at its entry. The switch's state is the ROM byte, so rollbacks, the crash blame, the instant revert and Back to Normal keep it honest.
- Code: `Switch` in `corrupt/patches.h` and `Recipe::switches` (`"switches"`: entry, return, was, hotkey, start_off, note), `Engine::flip_function` / hotkey flips at the start of `on_frame` (through the ROM keeper, not logged as a rule's write; kept through runs ahead via `pending_flips`), `apply_switches` at run start after the pins, `OpType::Kill` (`"kill"`, `"pick": "any"`; `AimedBytes.functions`, busiest 64 per run, `Engine::set_functions`); `qt/src/FunctionsPage.cpp` + `FunctionSource.hpp`, session `functions/flipFunction/keepSwitch/forgetSwitch`, the Workshop's Functions page and the cockpit's ROM line; rule editor *Turn functions off (ROM)*; starter `rom-function-roulette`. Starter ROM ranges are fitted to the game on load.
- Tests: `tests/corrupt/kill_switch_test.cpp`; `guard_integration_test.cpp` suite "kill switches" (near, long via a recipe hotkey, rollback, inline data caught by the instant revert); `tests/qt/functions_page_test.cpp`.
- Measure: `--kill [seeds] [count]` (busiest functions off vs random opcodes vs clean, unguarded): 6 off in a minute left the game running and moving in 31 of 35 runs (Goof, Pocky, FE4, FF6), against 7 of 35 for random code bytes.
- Docs: `functions-plan`; GUIDE "Turning the game's functions off"; QA 33.
- Gotchas: switches only flip while corruption is on (the engine is the only writer during a run). The Functions page keeps its row order while shown and re-sorts when it's shown again, so nothing moves under the mouse. Off means `function_off()` (calls.h): the first byte is the return and the clean ROM's isn't, so a one-byte stub that only returns isn't counted as switched off.

**Function bodies and guards (#58)** - where each function's code ends, walked from its entry over the code map (no tracer cost), and guards: the bodies of functions in play (a switch, a protect, or switched off from the window) are skipped by every rule and by paint.
- Code: `corrupt/function_body.cpp` (`function_body`/`function_bodies`: opcodes the map knows only, branches both ways, `BRA`/`BRL`, `JMP $abs` in its bank via `RomAddresses`, past calls when what follows ran, stops at returns, indirect and long jumps, 4 KB cap); `corrupt/function_guard.cpp` (`functions_in_play`, `guard_mask` with deep = callees through the call log, `guard_cleanup`, `own_bytes`, overlay `function_marks`/`function_at`); `Protect` and `Recipe::protects` (`"protect"`: entry, deep); `Engine::set_guarded` (rules and patches skip it via `rules_locked`; switches, pins don't); `HotMask::add`/`count`. Session: `findBodies` (every 5 s when the call log or map grew, on the clean ROM), `rebuildGuard` (when dirty and every 5 s; cleans guarded bytes with `rom.settle`), paint's "Kept N bytes as they were: guarded by …", `window_off`; Functions window *Size* column, *Protect* / *And what it calls*, *Show in ROM*; viewer overlay *Functions* (`MemoryPane::Overlay::Functions`, `snap.functions`, `snap.function_list`) and hover "in function …"; `MemoryViewer::showRange`; the ROM line's "(N off, M guarded)".
- Tests: `tests/corrupt/function_body_test.cpp`, `tests/corrupt/function_guard_test.cpp`; `guard_integration_test.cpp` suite "function bodies and guards" (test ROM `build_bodies`: the body exact; guarded with what it calls, a random rule over all of it leaves it alone and it keeps counting); `tests/qt/functions_page_test.cpp`.
- Measure: `--bodies` (functions with a body, sizes, how much of the code they cover, time, guard sizes): median body 35-54 B, 58-85% of known code in a body (Goof/Pocky/FE4/FF6).
- Docs: `function-changes-plan`; GUIDE "Turning the game's functions off"; QA 34.
- Gotchas: bodies are walked on the clean ROM, so corruption doesn't move them. Kill-rule offs aren't guarded (chaos, not a choice). Data tables a function reads aren't in its body.

**Shared names (#59)** - name a function once and everyone sharing the library sees it: `names.<user>.json` per game, everyone's loaded, only yours written.
- Code: `corrupt/function_names.cpp` (`encode_names`/`decode_names`, `FunctionNames::label`: "gravity · jump (erik)"), Library `names_path/load_names/save_names`; session `setFunctionName`, `loadNames` (with the code map); the window's *Name* field and *Name* column (a kept switch takes the name as its note).
- Tests: `tests/corrupt/function_names_test.cpp`; `tests/qt/functions_page_test.cpp`.
- Docs: `function-changes-plan`; QA 34.

**Game dossier and map-game (#42, first try)** - `game-dossier` turns everyone's tracer files for a game (calls, jump tables, code map, names) into one Markdown file: the busiest functions with callers, callees, the jump tables they dispatch through or are reached through, the RAM, registers and ROM data their instructions name, and a listing; every jump table with its entries; a RAM index. The app's own, no model. On top, optional: the `map-game` skill has Claude read the dossier and write `names.claude.json` (shown in Functions as *name (claude)*, never instead of a person's) and `mapping.claude.md` (each name's level, evidence and how to check it by turning it off). Zelda, named blind: of 81 sure/likely names, 65 match the fan disassembly, 14 close, 2 wrong.
- Code: `corrupt/dossier.cpp` (`make_dossier`, `operand_access`, `register_name`), `tests/headless/game_dossier.cpp` (the command); `.claude/skills/map-game/SKILL.md`.
- Tests: `tests/corrupt/dossier_test.cpp`.
- Run: `build/tests/game-dossier <rom> <library>/<GAME [hash]> --out dossier.md`.
- Docs: `game-mapping-plan`; QA 52.
- Since #67 names are typed in the table: the Name cell is editable (double-click or Return; `NameDelegate` starts from your own name, `FunctionsPage::rename` is shared with the field), and what you did with a function has its own *In play* column. QA 43.
- Filters and a Guard column (#72): *Show* (All, Running now, Off, In play, Guarded, Named, Not named yet; `shows()` in `FunctionsPage.cpp`) with the search, and a *12 of 545* count while rows are hidden; the *Guard* column is a box for the recipe's protect (*+ calls* when deep, *auto* when guarded anyway, no box without a body), so *In play* no longer says protected. A tick on a paused game stays ticked: `Engine::queued_state` gives what the queued flips make an entry, and the session's `functions()` (`FunctionRow::pending`) and the ROM line's *N off* count use it. Tests: `functions_page_test.cpp`, `kill_switch_test.cpp`. Plan: `cockpit-workshop-plan` #72; QA 48.
- What the instructions mean (#71): `corrupt/instruction_help.cpp` (`explain_mnemonic` for all 92 mnemonics, `explain_instruction` adds the operand by addressing mode, `explain_change` per kind); Change It…'s Instruction and Can Become cells carry them as tooltips. Tests: `tests/corrupt/instruction_help_test.cpp`, `function_editor_test.cpp`. QA 47.

**Change a function (#60, with #35 redirection)** - change how a function works instead of turning it off: a number (`LDA/ADC/CMP… #`), a branch (opposite, always, never), a math pair (ADC/SBC, INC/DEC, INX/DEX, INY/DEY, ASL/LSR, ROL/ROR, CLC/SEC, AND/ORA), a call skipped or sent to another function of the same kind (near: same bank). Live, on hotkeys, kept in the recipe. Code rules can aim *inside busy functions*.
- Code: `corrupt/function_change.cpp` (`change_options` over a body on the clean ROM, `math_partner`, `number_bytes`/`branch_bytes`/`math_bytes`/`skip_bytes`/`redirect_bytes`, `make_change`, `change_on`); `Change`/`ChangeKind` in `patches.h`, `Recipe::changes` (`"changes"`: function, at, kind, value, was, hotkey, start_on, note); `Engine::Flip` is general (`at`, `on`/`off` bytes, Toggle/On/Off; a switch is a one-byte flip), `flip_bytes`, `add_change`/`remove_change`, `apply_changes` at run start after the switches (`RunControl`), changes on hotkeys; a change puts its function in play (guarded, its bytes your own, `own_bytes(recipe, window_off, window_changes)`). `Aim::in_busy_functions` (`"within": "busy_functions"`), `aim_rule(..., busy)`, `Engine::set_busy_code` (the 64 busiest bodies, once a run). App: `qt/src/FunctionEditor.cpp` (*Change It…* from the Functions window: lines, what each can become, Apply / Put Back / Keep in Recipe / Remove, hotkey, starts on), session `changeOptions/romBytes/redirectBytes/applyChange/keepChange/forgetChange`, rule editor *Only inside the busiest functions*.
- Tests: `tests/corrupt/function_change_test.cpp`; `guard_integration_test.cpp` suite "function changes" (test ROM `build_bodies`: a number adds 3, SBC counts down, a skipped call, a redirected call, a branch always/never, a kept change on a hotkey and a rollback); `tests/qt/function_editor_test.cpp`.
- Measure: `--change [seeds] [count]` (changes inside the 64 busiest functions vs the same kinds anywhere in code vs clean, unguarded; `CORRUPT_CHANGE_KIND=number|branch|math` for one kind): inside, the game keeps running (29 of 30 on Goof/Pocky/FE4) but stalls more often (17 of 30 still moving, against 24 anywhere); no kind is reliably gentle.
- Docs: `function-changes-plan`; GUIDE "Changing how a function works"; QA 35.
- Gotchas: changes, like switches, happen while corruption is on. An immediate the map saw at both widths isn't offered (its length isn't fixed). Put Back writes the whole instruction's clean bytes.

**Which changes don't crash (#77)** - each line of Change It… says how risky a change there is, from where the changed value goes next, and why, in words, with the measured share; rules can aim at low-risk changes only; a crash blamed on your own change goes back to before the game first ran it.
- Code: `corrupt/change_risk.cpp` (`feed_of`: variable, scratch, hardware, index, loop count/bound, condition, pointer, argument, overwritten; branches: loop, forward, unseen path, wait loop, width path, stack path; skipped calls: result used/unused; `risk_of`, `measured_failures`, `low_risk_mask`). `Aim::low_risk` (`"risk": "low"`), `aim_rule(..., busy, low_risk)`, `Engine::set_low_risk_code` (every known body's low-risk bytes, once a run). `ByteBlame::own_first_use`, `RomBlame::set_own` (the recipe's and the window's changes), `CrashGuard::handle` going back before it. App: `FunctionEditor` *Risk* column (sortable; `KeyedItem`, rows map to options through `Qt::UserRole`) and `risk_note`; session `changeFeeds`; rule editor *Only where a change is low risk* with its note.
- Tests: `tests/corrupt/change_risk_test.cpp`; `crash_guard_test.cpp` ("a crash blamed on your own change…"); `function_editor_test.cpp`, `rule_editor_test.cpp` (#77 cases).
- Measure: `--risk [count] [trial] [kind]` (single changes from the 64 busiest functions, 30 s each, unguarded, per feed; TSV with the function each is in), `--change` has a *low risk* arm; `--guard` takes `CORRUPT_PRESS=<hotkey>@<frame>` and `CORRUPT_OWN_OFF`.
- Docs: `function-changes-plan` (#77 sections); GUIDE "Changing how a function works"; QA 53.

**Try a change before it lands (#78)** - *Try 5 s Ahead* in Change It… plays the chosen change 5 s ahead, unseen, and once without it, and says under the risk whether the game kept running, froze, crashed, or didn't run that code.
- Code: `corrupt/try_change.cpp` (`froze_at`: the window's last second one picture while the run without the change moved 3+ times; `judge_try`; `try_change`: both runs as queued flips through `RunAhead::play`, pending flips put back; `kTryFrames` 300). `RunSpec::pictures` / `RunVerdict::pictures`; `CoreAccess::run_unseen_frame(bool draw)` (a drawn unseen frame; Qt's `S9xDeinitUpdate` shows nothing while `unseen()`). Session `tryChange` (queued), `tryPending` (last in `betweenFrames`, after `testAhead`), `lastTry` (`TriedChange` with a growing id); `FunctionSource::tryChange/lastTry`. App: `FunctionEditor` `try` button and `try_line` (shown only for the choice it answers; red froze/crashed, green running).
- Tests: `tests/corrupt/try_change_test.cpp`; `guard_integration_test.cpp` suite "try a change before it lands (#78)" (test ROM `build_freeze`: the backdrop colour counts frames unless a flag is set); `function_editor_test.cpp` (#78 case); the UI tour tries the first line (`try.txt`, `14a-function-editor-tried.png`).
- Measure: `--foresee [count] [trial] [kind]` (each single change recorded frame by frame beside a clean run; candidate checks scored at 3, 5, 10 s against the 30 s outcome), `--try [count] [trial] [kind]` (`try_change` itself at 5 s; `CORRUPT_TRY_VERBOSE=1`).
- Docs: `function-changes-plan` (#78 sections); GUIDE "Changing how a function works"; QA 54.
- Gotchas: needs corruption on (flips need the engine) and a running game (it runs between frames). Drawing the unseen frames costs time: about 0.75-1 s per try (both runs). The answer is for this moment; code that doesn't run in 5 s gets no answer.

**Timed switches (#61)** - a switch off for a while every so often, a change on for a while; a kill rule that puts its functions back; the kill rule keeps learning.
- Code: `Switch::every/for_frames`, `Change::every/for_frames` (`"every"`, `"for"`, in frames), timer flips at the start of `on_frame` from `(frame - 1) % every`; `Op::put_back` (`"put_back"`, frames) with `RuleRuntime::put_backs` (in the snapshot, so rollbacks keep them; not logged as writes); the kill list taken again every 300 frames while it has fewer than 64. App: the Functions window's *Timer* row, Change It…'s *Kept with a timer*, the rule editor's *Back on after*.
- Tests: `tests/corrupt/timed_switch_test.cpp`; `guard_integration_test.cpp` ("a switch on a timer").
- Docs: `function-changes-plan`; GUIDE; QA 36.

## Phase 7: safety (#50-#52, #57; #55 and #56 tried and removed)

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
- Code: `corrupt/test_ahead.cpp` (TestAhead::before_frame, RunAhead, RunSpec, vetoes, `quiet_after`), `Engine::preview_next` (OverlayMemory), `CoreAccess::begin_unseen/run_unseen_frame/end_unseen`, `RomTracer::note_jumps(false)` during runs, session `testAhead`, `running_ahead`. Safety switch "Test ROM changes before they reach the game" (was "Try ROM changes ahead first") (`setTestFirstEnabled`, recipe `test_first` seconds, default 3, 0 off).
- Tests: `tests/corrupt/test_ahead_test.cpp`.
- Measure: `--ahead [seeds]`.
- Docs: `safety-plan` #52; QA 29.
- Gotchas: a run tests ONE firing (rules re-fire inside the window). 80-97% of written bytes are unused within 3 s in attract mode, so totals move little. About 130-210 ms per firing.

**Test at first use (#55) and proven blame (#56): removed.** Both were built, measured and taken out on 2026-11-01; see *Tried and removed* below before building anything like them.

**Eyes open after a landing (#57)** - after the derail watch notes a landing, the tracer still marks the changed bytes the game runs (Touched only), so the crash blame sees them.
- Code: `corrupt/core_access.cpp` (`traced()`'s derailed branch, `see_changed`, `see_changed_slow`), `RomTracer::set_blind_after_landing` (measuring only) and `seen_after_landing()`.
- Tests: `guard_integration_test.cpp` ("after a landing, changed bytes run later in the frame still count as used", "blind after a landing"); `test_rom.cpp` `build_landing`.
- Measure: `--eyes [seeds]` (the guard, #51 and #52 in both arms, the first blind as before).
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

## Phase 11: golden moments (#47, #79) and the explorer (#45, #46)

**Keep a golden moment (#47)** - *Keep This Moment* (F8, and a button in the cockpit) keeps the last 10-20 s: a keyframe (a saved state and the ROM's changed bytes) and every frame since (the pads, the corruption writes before it and in its hook), so it plays back exactly, here or on a friend's machine. Outcomes, not causes: the engine, the guard and Paint don't have to replay. It's checked as it's kept. The Workshop's **Moments** page lists everyone's, and **Play** plays one with corruption off; any button takes over.
- Code: `corrupt/moment.cpp` (`MomentRecorder`: keyframes every 600 frames, 3 kept, `take()` from the newest 600+ back; a load it wasn't told about cuts, `hold`/`accept` around runs ahead, `redo_frame` for the instant revert keeps the frame's ROM writes; `RecordedMemory`; `MomentPlayer`: `start` loads the keyframe and puts the ROM as it was, `taken_over` when the pads differ from what it set; `check_moment`: unseen playback compared with every memory, everything put back; the `.s9xmoment` file, zlib-packed where the build has it, `read_moment_info` reads only the header). `Library::moments_dir` (`<game>/moments/`), `save_moment`, `list_moments`, `rename_moment`, `delete_moment`. `CoreAccess::pads/set_pads/timeline` (`SnesCoreAccess`: `MovieGetJoypad`, loads and resets counted). Session: `beforeFrame`, `timeJumped` (rewind, reset), `keepMoment` (between frames, at once while paused), `keepPending` (the `checker` player; the frame hook plays only its writes while `checking`), `playMoment`/`startPending`, `stopMoment`, cuts named at each load (rollback, run start, Back to Normal, Paint's watch, a save state). Core: `S9xVBlankStartedFromState` (a load sets `VBlankStarted` from the frame). App: EmuConfig `eCorruptionKeep` (`CorruptionKeep`, F8), the cockpit's `keep_moment` (Stop Playing while one plays; the state bar counts), `MomentsPage` (Workshop page 7), the Corruption menu's *Kept Moments…*.
- Across a rollback (#79): between frames, when the guard has taken a checkpoint (`CrashGuard::took_checkpoint()`), the session calls `MomentRecorder::mark(engine frame)`. On a rollback it calls `back_to(restored frame)` instead of cutting. The frames since the mark go, and so does any keyframe at or after it. The writes waiting at the mark stay, and every ROM write since stands. It falls back to a cut when the mark is gone. Measured with `corrupt-smoke --keep` (CORRUPT_KEEP_CUT=1 cuts, as before): 102 of 102 moments that span a rollback are exact, and moments under 10 s went from 84 to 14.
- Tests: `tests/corrupt/moment_test.cpp` (a fake game whose every frame depends on its pads, its ROM and writes before and inside its hook); `guard_integration_test.cpp` suite "keep a golden moment (#47)" (test ROM `build_pads`) and "a state loaded after the core stopped mid-frame plays its next frame whole (#47)"; `workshop_test.cpp` (Moments), `cockpit_test.cpp` (Keep This Moment); the UI tour keeps and plays one (`keep.txt`, `17a-workshop-moments.png`, `17b-cockpit-moment-playing.png`).
- Measure: `--keep [seeds]` (played guarded with a pseudo-player; every 7 s a moment kept and checked, drawn and not, plus the controls without its buttons and without its writes; `CORRUPT_KEEP_VERBOSE=1`; `CORRUPT_KEEP_DIAG=1` finds the first frame that drifts and diffs the core's state and globals).
- Docs: `golden-moments-plan`; GUIDE "Kept moments"; QA 55.
- Gotchas: since #79 a crash-guard rollback keeps the history before its checkpoint; Back to Normal, Paint's crash watch, a rewind or a loaded save still start it again. A save state doesn't hold everything the core runs on: `VBlankStarted` is now set on load; `GFX.EndY` (the sprite overflow flags' line) isn't, and made no difference in 252 moments. Plain pads only (no mouse or Super Scope).

**The headless explorer (#45) and the Explored page (#46)** - from a kept moment, `snes9x-explore` plays thousands of variants unseen (4,000-6,000 an hour), each a recipe from one family (reroll, nudge, ram, code, swap, kill, data), pressing the moment's own buttons, and scores each against the moment played without corruption: alive × visible × looks like a game × moving, with a bonus for code never run before. The best (score 0.3 or more) are kept with a picture strip, the recipe and their run as a kept moment that plays back exactly. **Explore** on the Moments page runs it in the background; the Workshop's **Explored** page plays a candidate, keeps it as a recipe, opens the gallery.
- Code: `corrupt/explore.cpp` (`make_variants`, `busy_wram`, `start_from`, `play_variant`: unseen, drawn, definite crashes only (`definite_crashes()`), thumbnails, new code against the map, everything put back; `score_run`), `corrupt/picture.cpp` (`differing_share`, `detail_of`), `corrupt/exploration.cpp` (`candidates.json`, `gallery_html`), `CoreAccess::picture` (`SnesCoreAccess`: `GFX.Screen`, a 64 × 56 thumbnail or the full picture), `WramAccessCounter::read_counts`. `Library::explored_dir` (`<game>/explored/<name>/`), `list_explorations`, `new_exploration_dir`, `delete_exploration`. `tests/headless/snes9x_explore.cpp` (the command; built optimized beside the app by `build-mac.sh qt`). App: `ExploreRunner` (QProcess, `nice -n 10`, reads `progress`/`folder`/`done` lines, Stop = SIGTERM, which keeps what was found), `ExploredPage` (Workshop page 8), Explore on `MomentsPage`, `CorruptionControl::explorations/keepExplored/deleteExploration`, `PanelStatus::rom_file`.
- Tests: `tests/corrupt/explore_test.cpp` (a fake game whose picture comes from its WRAM: pictures, the score's cases (the same, tinted, black, frozen, crashed, cut short, garbage, a little change, new code), the families, the WRAM picker, playing and putting back, the file, the gallery, the library); `workshop_test.cpp` (Explored, with a fake explorer script; Explore on Moments); the UI tour explores its kept moment for 15 s (`17c`, `17d`, `flows.txt`).
- Measure: `corrupt-smoke --explore [variants] [moments]` (`CORRUPT_EXPLORE_AT=frames,...`, `CORRUPT_EXPLORE_SEED`, `CORRUPT_EXPLORE_OUT=<dir>`: per scene the control strip, `scene.s9xmoment`, and for a blind test the 12 best and 12 random visible survivors under letters with `key.txt`; `CORRUPT_EXPLORE_VERBOSE=1`).
- Does the score mean anything (#81): `corrupt-smoke --placebo [variants] [moments]` plays per scene 15 natural runs (the same buttons with one press shifted, dropped or added: `perturb`) plus the control, 20 placebos that set each signal's bar and 30 held-out ones, then the variants, and counts who passes each bar; with `CORRUPT_EXPLORE_OUT`, 24 strips of 12 frames per scene (placebos, top by score, top by behaviour, random) and `key.txt`. `corrupt/envelope.cpp` (`StateTrace`, `Envelope`: apart, new colours, steady bytes broken, out of range, new functions, silenced, sprite jumps; `sprite_jump_rate`). Tests: `tests/corrupt/envelope_test.cpp`. Found: in real play 30% of placebos pass today's keep line against 11% of corruptions; the code signals (new and silenced functions, new code) separate cleanly, memory and sprite signals don't. In the explorer since #82; plan `2026-11-06-placebo-plan`.
- Against normal play (#82): before the variants, `snes9x-explore` plays the control again and 36 placebos (`perturb`): 16 natural runs build an `Envelope` (calls, pictures, code they run), 20 bar-setters give `NormalBest` (their most: score, new functions, silenced, new code). Every run has a calls tracer (`CallLog`). `judge` keeps a variant that's alive and scores over normal play's best (at least 0.3) or, visibly changed, has a code signal past its bar; candidates rank by find (score ÷ keep line + 1 per code signal). Prints `normal <done> <of>` and `normal best ...`; `candidates.json` gains `normal`, each candidate's `find`/`looks`/`acts`/signals, `tried`'s 4th number. App: `ExploreRunner` reads the normal lines (the running line says so), the Explored page's Find column, summary and why-kept text (`normal_text`, `why_kept`, `acts_text`), the charts' keep line at normal play's best and finds by strength. `corrupt-smoke --placebo` counts the rule beside the 0.3 line and picks top-find strips. Tests: `explore_test.cpp` (best, verdict, file, charts, gallery), `workshop_test.cpp` (the page, the running line). Plan `2026-11-06-normal-play-plan`.
- What it did (#80): `candidates.json` keeps every try (`tried`: family, outcome, score); `stats_of` works out three charts, drawn by `ExploreCharts` on the Explored page (QPainter, palette colours, a tooltip per bar and dot) and as inline SVG in the gallery (theme tokens, a `<title>` per mark, the first chart also as a table): what each kind did (stopped / nothing showed / visibly changed), how the visible changes' scores spread around the keep line, when the good finds turned up. Older files get the first chart from their family totals. Tests: `explore_test.cpp` (stats, the file, the SVG), `workshop_test.cpp` (the charts and their tooltips).
- Docs: `2026-11-05-explorer-plan`; GUIDE "Exploring a moment"; QA 56.
- Gotchas: the detector's guesses misread unseen frames (every control "crashed" at 480 frames); a pseudo-player's Start pauses the game for the whole window. The score was tuned on blind ratings and confirmed on held-out ones (top 12 rated 2.57 against 1.62 for random visible changes; scores of 1 and up 3.4 of 5); in a quiet scene few variants change anything, and the floor keeps a short list. Keep as Recipe doesn't carry ROM changes the starting moment already had. Unseen frames don't count WRAM reads (by design), so `snes9x-explore` plays the moment once in plain view to aim the ram family (a first version counted during the unseen control and aimed nowhere).

## Phase 12: remote control (#53)

**Remote control** - a phone or browser page that drives the live run. Only the owner and one friend connect; no game picture (they stream). The public hostname lives in the owner's tunnel setup, never in this repo.
- Code: `corrupt/remote_api.cpp` (Control interface, Gate lock-out 5/min, Connection hello+token, actions active/back_to_normal/reroll/mark_crash/chaos/rule_enabled/rule_strength/hotkey/watch/paint/starter, `status_json`, binary `S9XM` memory messages), `qt/src/RemoteServer.cpp` (QHttpServer + QWebSocket, 127.0.0.1 only), `qt/src/RemoteControl.cpp` (SessionRemote), page `qt/src/resources/remote/{index.html,remote.css,remote.js}` (`remote.qrc`), the Workshop's Sharing page (EmuConfig `RemoteEnabled`, `RemotePort` 8095, `RemoteAddress`, `RemoteToken`). The page remembers the key in localStorage; a wrong key forgets it. Starter picker shares `Starters.cpp`.
- Tests: `tests/corrupt/remote_api_test.cpp`, `tests/qt/remote_server_test.cpp`, `tests/remote/remote_e2e.sh` + `remote_e2e.mjs`.
- Docs: `remote-plan`; QA 31; GUIDE "Remote control from a browser".
- Gotchas: `http->bind(tcp)` owns the QTcpServer; do not delete it. Never put the hostname, a token or an email in the repo.

**Privacy check** - a pre-commit check that rejects private words and secret-shaped strings.
- Code: `tools/privacy_check.sh [--staged]`, `tools/install-hooks.sh`; word list is the git-ignored `.private-words` (or `SNES9X_PRIVATE_WORDS`).
- Tests: `tests/remote/privacy_check_test.sh`.
- Docs: `remote-plan`.

**Live shared library (#62)** - the library syncs while you play (Syncthing, through an always-on computer that holds the git clone); git gets it when someone runs `push`.
- Code: `is_sync_leftover` (conflict copies, `.syncthing.*` / `~syncthing~` temp files, `.stfolder`) skipped by every per-person scan and `list_recipes`; `Library::conflict_copies`, `others_stamp` (everyone else's learned files: name, size, time), `recipes_stamp`; session `checkLibrary` every 30 s between frames (others changed: save ours, `loadCrashMap`, `code_map_reload`; recipes changed: `Status::library_generation` and the cockpit's `refreshRecipes`; conflicts: a note); `SNES9X_PLAYER` overrides the user name; `tools/library/stignore`, `tools/library/push.sh` (copied into the library repo as `tools/push.sh`).
- Tests: `tests/corrupt/live_library_test.cpp`; `tests/library/push_test.sh` (in `./build-mac.sh test`).
- Docs: `live-library-plan`; README "Playing together live"; QA 37.
- Gotchas: a reload reads everyone's code maps again (a few MB): at most once per 30 s, when someone else saved. Device ids and where the server keeps the folder belong in the private homelab repo.

## Tools and scripts

- `build-mac.sh qt` builds `build/qt/snes9x-qt` (prints compiler errors) and `build/qt/snes9x-explore` (optimized, from `build/explore`). `app` builds the Cocoa Snes9x.app, `libretro` the core, `all` all three.
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
- `--calls`: call noting's cost; functions found over time; the busiest (#26).
- `--kill [seeds=10] [count=6]`: turning busy functions off vs random opcodes vs clean, unguarded (#34).
- `--derail-fp [learn]`: false alarms of the derail watch on a clean game.
- `--instant [seeds=5]`: guard alone vs with instant revert.
- `--ahead [seeds=5]`: instant revert in both arms; the second tests each live ROM firing first.
- `--nmi`: clean NMI-off stretches per game.
- `--eyes [seeds]`: the guard, #51 and #52 in both arms; the first blind after a landing, as before #57.
- `--learn [held=3] [from=0]`: crash-map learning, then held-out seeds plain vs avoid vs random control.
- `--dump <dir>`: write raw `.bin` per target (needs a recipe and frames; used for viewer pictures).
- Env: `CORRUPT_CODEMAP`, `CORRUPT_LEARN_VERBOSE`, `CORRUPT_INSTANT_VERBOSE`, `CORRUPT_KILL_VERBOSE`, `CORRUPT_WATCH_BYTE` (one ROM byte's flags, value and PC each frame, the first 400), `CORRUPT_DUMP_DIR`, `CORRUPT_SHOTS_DIR`.

## Tried and removed (read before building a new safety feature)

Ideas that were built, measured on real games and taken out because they didn't pay. The
code is in git history at the commits named; the plan doc's section has the full numbers.
Don't build these again without a new reason that answers why they failed.

**Test at first use (#55)**, removed 2026-11-01 (built in `b3d3b997`). When the game first
ran a changed ROM byte, it played a second ahead from the frame's start with and without the
byte and put it back if only the run with it crashed.
- Result: it put back 0-2 bytes per game in 10 minutes, and rollbacks didn't go down (FE4
  Code roulette 36 -> 42, Goof 3 -> 4, Pocky unchanged; 5 seeds each).
- Why it fails: a corrupted byte rarely crashes the game within a second of its first use.
  The crashes come seconds later (NMI off, a fuse lit earlier), or from several bytes
  together, or from damage already in RAM. A short run from the first use can't see them.

**Proven blame (#56)**, removed 2026-11-01 (built in `fd33b13b`, `2b256adb`; narrowing fixed
in `1f8b1bd6`). A crash that came back after a rollback was replayed unseen from the guard's
checkpoints, with and without the changed bytes, narrowed by halves to the few bytes that
cause it, and only those were fixed.
- Result: no gain over guessing that holds up. After the narrowing fix, FE4 Code roulette 42
  -> 39 rollbacks and Pocky Code drip 9 -> 8, but FE4 Code drip 15 -> 20 (worse). Gaps that
  small are inside seed-to-seed noise, which was never measured. Only 7 of 17 replayed
  crashes could be proven at all. Each proof stalls the game 1-2 s.
- Why it fails: it assumes a crash comes from a few ROM bytes a replay can find. Many come
  from state already corrupted in RAM, from bytes read as data (unseen after a landing), or
  from combinations; and a corrupted game is chaotic, so a replay without some bytes goes
  somewhere else entirely. The cheap guess (the changed bytes the game ran last) already
  gets most of what there is.
- Trap worth remembering: its first version narrowed by "the half that crashes on its own"
  and reported 42 of 43 crashes proven. That proved bytes the game *could* crash on if it
  got further, not the ones that did. A proof has to test that cleaning a byte *prevents*
  the crash. Self-reported success rates are not evidence; rollbacks and frames lost are.

**Lessons for the next safety idea**
- The guard's real gains came from #51 (instant revert: rollbacks roughly halved on all
  three games) and, for opcode changes, #52 (test before applying). Both act on something
  seen *right away* (a landing, a crash in the next 3 s of one change). Ideas that try to
  explain a crash after the fact did not pay.
- Measure the noise first: run the baseline arm over enough seeds (say 20) to see its
  spread before trusting a gap of a few rollbacks.
- Count what the player feels (rollbacks, frames lost, corruption kept, stalls), not what
  the feature reports about itself.

