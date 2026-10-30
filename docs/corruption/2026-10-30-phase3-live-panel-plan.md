# Phase 3: full live panel, implementation plan

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md), the
*Qt UI* section and phase 3 of *Phases*. This phase is phase 4 on the roadmap, "Shape
corruption while playing": #14 rule editor, #15 intensity sliders, #16 named regions,
#17 `anchor_state`. Phases 1 and 2 are done
([plan 1](./2026-10-30-phase1-engine-recipes-plan.md),
[plan 2](./2026-10-30-phase2-crash-rollback-plan.md)).

**Status (2026-10-30):** #14 is done (tasks 1–9 below; see *Findings while building*), and
it still needs a manual QA pass (QA.md section 8). #15 is done too (its own section
below; QA.md section 9). #17 is done too (QA.md section 10). #16 is being built (its own section below);
each gets its own task list once the one before it has been play-tested. #14 was meant to
wait for the first play-test (#12), so whatever that turns up feeds back in as fixes.

Goal of #14: while the game runs, add, change and remove rules in the panel, see the
effect right away, and save the result over the recipe file or as a new one. Editing a
JSON file and reloading becomes optional.

**Ground rules (unchanged):** test-first for everything outside Qt (red, green, commit);
one commit per task; `corrupt/` never includes Qt; no new core changes.

## Decisions

- **An edit changes only the rule that was edited.** The other rules keep their frame
  count, sticky addresses, freezes and firings, so the run doesn't restart. Restarting
  everything on each edit would make it impossible to tune one rule against a running
  game.
- **Rules get a run-time id.** Every rule in the engine gets an id that is never saved.
  Replacing a rule gives it a new id. Engine snapshots store runtimes by id, so restoring
  a checkpoint taken before an edit gives the edited or added rule a fresh runtime
  instead of the old rule's sticky addresses, which may lie outside its new range.
  `restore()` no longer needs the rule count to match.
- **Crash counts are kept by rule id** in the guard, so removing a rule no longer shifts
  every count after it, and a replaced rule starts at zero (escalate starts over for it).
- **Removing a rule shifts the random streams of the rules after it**, because the stream
  is seeded from the rule's position. That's like a re-roll for those rules. Since the
  saved recipe replays the same way, it's documented, not fixed.
- **The editor is a dialog, not a form in the dock.** The dock is narrow, and the op and
  trigger fields change with the type. `RuleEditor` takes a rule, the game's regions and
  the memory sizes, and gives back a rule. It doesn't know the session, so a small Qt
  test target can drive it without the emulator.
- **Save and Save As.** The session remembers which file the recipe came from and whether
  the panel changed it. *Save* writes over that file. *Save As* stays. Changes the crash
  guard makes (salts, dampened intensity, disabled rules) also count as edits, since
  they're part of what you see.
- **New recipe.** An empty recipe for the loaded game, so a recipe can be built entirely
  in the panel.

## Tasks

### 1. Number parsing is public
`parse_number(std::string_view) → optional<int64_t>` in `recipe_json.h`: `0x…`, `$…` and
decimal, the same rules as the JSON reader. The editor's address and value fields use it.
Tests: hex, `$` hex, decimal, negative, whitespace trimmed, junk and overflow rejected.

### 2. Rule ids in the engine
`Engine` gives every rule a run-time id, and `EngineSnapshot` stores `{id, runtime}`
pairs. `restore()` matches runtimes by id; rules it doesn't find start fresh, and
snapshot entries whose rule is gone are dropped.
Tests: the existing snapshot tests still pass; a snapshot taken before a rule was replaced
restores that rule fresh and the others exactly; a snapshot with a different rule count
restores what matches.

### 3. Live rule edits in the engine
`replace_rule(i, Rule)`, `add_rule(Rule) → index`, `remove_rule(i)`, and `rule_id(i)`.
- Replace: new id, fresh runtime, mute cleared.
- Add: appended, fresh runtime.
- Remove: drops the rule, its runtime, its mute and its log entries, and renumbers the
  log entries after it.
- All of them work while active, with no restart: the frame count goes on.

Tests: after replacing rule 1 of 3, rules 0 and 2 write exactly what they would have
written without the edit (compared against an unedited run); the replaced rule's sticky
addresses are picked again; add and remove keep the frame count; remove renumbers the log
and drops the removed rule's events; out-of-range indices do nothing.

### 4. Crash counts by rule id
`CrashGuard` keys its per-rule crash counts by id. `rule_crashes(const Engine&)` returns
them in the engine's current rule order.
Tests: the existing count test; counts follow a rule when an earlier one is removed; a
replaced rule starts at 0; a rollback across an edit still restores and blames correctly.

### 5. Rule text and defaults: `corrupt/rule_text.{h,cpp}`
- `describe_op(rule)` and `describe_trigger(trigger)` move from the dock into `corrupt/`,
  since the editor shows them too.
- `default_rule()`: what *Add Rule* starts with. A gentle WRAM rule: `random` 0..255,
  one byte every 120 frames, in `wram 0x2000..0x1FFFF`, the range `gentle-wram-noise`
  uses to stay clear of the stack and direct page.
- `SizedMemory`: a `MemoryAccess` that only knows sizes, so a rule can be validated
  against the loaded game's sizes outside the emulation thread.

Tests: each op and trigger type has a description; `default_rule()` passes
`validate_rule` against nominal sizes; `SizedMemory` reports the sizes it was given.

### 6. Session: edits, Save, New
`CorruptionSession` gains `replaceRule`, `addRule`, `removeRule` (each validated against
the game's sizes; returns an error or empty), `newRecipe(name)`, `save()`, and in
`Status`: `recipe_path`, `edited`, `sizes`, `regions`. Edits bump a separate
`rules_generation` so the dock rebuilds its table without treating it as a new recipe.
No unit tests (emulator glue); covered by tasks 7–8 and the manual QA.

### 7. `RuleEditor` dialog: `qt/src/RuleEditor.{hpp,cpp}`
- Name, target, range (from/to, or a named region from `regions.json`), op type with a
  page of fields per op, trigger type with its fields and an optional window, count,
  pick (fresh/sticky), enabled, intensity.
- Every change rebuilds the rule and runs `validate_rule` against `SizedMemory`. The
  error shows under the form, and OK stays disabled until the rule is valid.
- A one-line summary at the bottom (`random 0..255 x1, every 120 frames`) from task 5.

Tested by a small Qt target, `corrupt-qt-tests` (doctest plus `QT_QPA_PLATFORM=offscreen`,
so no screen is needed), built when Qt is found:
- a rule goes in and comes out unchanged for every op and trigger type;
- changing fields gives the expected rule;
- an out-of-range address disables OK and shows the target size;
- picking a named region fills the range and keeps the region name for saving.
It also renders the dialog to a PNG (`QWidget::grab`) in `build/tests/shots/`, so the
layout can be looked at without a screen.

### 8. The dock
- Rule table: double-click a row to edit it; *Add Rule*, *Edit*, *Remove* (asks first),
  and *Duplicate* under the table; the table keeps its selection across refreshes.
- Recipe row: *New*, *Save* (enabled when the recipe came from a file and was edited),
  *Save As…*. The recipe label shows `(edited)`.
- Loading another recipe while there are edits asks first.

Same Qt test target: the dock's table and buttons are driven through a fake session
interface if that's cheap; otherwise this task is covered by manual QA only, and the plan
says so.

### 9. Docs and QA
README: the editor and Save/New. QA.md: a section *8. Rule editor*: add a rule while
running, edit one and watch only that one change, remove one, Save, Reload, and New.

## Findings while building

- **Tasks 2 and 3 landed as one commit.** Snapshots by id can only be tested with an
  edit to cross, so the two were written test-first together.
- **A bug fixed on the way.** The guard reset every per-rule crash count whenever the
  rule count changed. Nothing could change the count before this phase, but adding a
  rule would have wiped them.
- **The dock (task 8) has no automated test.** It needs `EmuApplication` and the
  emulator globals behind `CorruptionSession`, and a fake for them wasn't cheap. The
  editor it opens is fully tested, the app is launched offscreen
  (`QT_QPA_PLATFORM=offscreen`, muted scratch config) to check that it starts with the
  panel, and QA.md section 8 covers the rest.
- **Offscreen renders work.** `QWidget::grab()` on the offscreen platform gives real
  PNGs of the dialog, so the layout was checked and polished without a screen: target
  names gained plain labels (`cgram (palette)`), and the decimal fields drop trailing
  zeros.

## #15: intensity sliders

Goal: turn the chaos up and down while playing, per rule and for the whole recipe, with
the mouse or a hotkey, without restarting the run.

### Decision: below one address, intensity thins firings
Today intensity scales `count` with a floor of one address, so for a count-1 rule (most
`random` and `swap` rules, the default new rule) a slider would do nothing, and so does
the crash guard's *dampen*. From now on, when `count × intensity` is below 1, the rule
fires with that probability instead (count 1 at 30 %: about 3 firings in 10), hitting one
address. The draw comes from its own random stream, and a skipped firing doesn't count as
a firing, so the values a rule writes stay replayable. At 1 or more nothing changes.

### Tasks
1. **Thinning** in `tick_rule`. Tests: count 1 at 0.3 fires about 30 % of 10 000 frames;
   the same seed thins the same frames; the values written when it does fire match an
   unthinned run's firings in order; count 10 at 0.5 still hits 5.
2. **Recipe intensity**: `Recipe::intensity` (0–1, default 1, written only when not 1),
   multiplied into every rule's intensity at run time; `Engine::set_intensity`, live.
   Tests: JSON round-trip and range errors; 0 stops every rule; 0.5 equals every rule at
   half; changing it keeps the frame count.
3. **Session and panel**: an *Intensity* column of sliders in the rule table, a *Chaos*
   slider for the recipe in the run box, both live and both counted as edits for Save.
   Two rebindable hotkeys, *Corruption: more* and *less* (10 % steps, unbound by default),
   with an on-screen message. A crash-guard *dampen* moves the rule's slider.
4. **Docs and QA**: README, QA.md section 9.

### Findings
- **The slider became its own widget** (`IntensitySlider`) so the Qt test target could
  cover it: silent when the engine moves it, ignoring refreshes mid-drag, and keeping a
  dampened 12.5 % exact until someone touches it.
- **Two tests changed meaning.** "A floor of one address" became the thinning test,
  and the delta-scaling test moved from count 1 to count 2 at half strength, the
  smallest setting that still hits exactly one address.
- **Hotkeys are unbound by default.** F10–F12 are taken or risky (F11 is fullscreen), so
  More/Less Chaos wait for a binding instead of taking a key someone already uses.

## #17: start a recipe from a saved moment

Goal: a recipe can carry the moment its run started, so loading it puts the game back at
that moment and the same seed replays the same chaos (until the player's own inputs
differ). That's how Erik gets to see exactly the run Mikael saved.

### Decisions
- **The moment is the start of the run, not the time of saving.** Replays count from
  activation, so the state worth keeping is the one from just before frame 1.
- **Runs start at a frame boundary.** Activating, re-rolling or loading a recipe used to
  restart the engine from the UI thread, so the first corrupted frame could fall in the
  middle of an emulated frame. Now the engine holds a restarted run at frame 0 until
  `RunControl` (Qt-free, called between frames, before the crash guard) takes a snapshot
  of the state, or loads the recipe's saved one, and releases it. While the game is
  paused, a start waits for it to run.
- **Files:** `<recipe>.anchor.frz` next to `<recipe>.json`, written in Snes9x's own
  save-state format; `anchor_state` holds the file name. It's only loaded when the
  recipe's ROM SHA-256 matches the loaded game. A state from another version would
  load the wrong code.
- **One switch in the panel**, *Saved moments*, on by default: Save and Save As write the
  moment the current run started (when it isn't the one already on disk), and Load
  jumps to the recipe's moment. Off, recipes load and save as before.

### Tasks
1. `Engine::hold_starts` / `release` / `waiting`. Tests: a held run writes nothing and
   stays at frame 0 until released; every restart holds again; off by default.
2. `RunControl`: `request_start_from(state)`, `between_frames(engine, core)` that
   captures or loads, releases, and reports what it did, plus `anchor()` and
   `anchor_is_from_file()`. Tests over a fake core whose memory changes every frame:
   the anchor is the state before the first corrupted frame; starting from it replays
   the original run byte for byte; a broken state starts from the current moment and
   says so.
3. Library: `anchor_path(recipe_path)`, `save_anchor`, `load_anchor`. Tests: next to
   the recipe, binary round-trip, a missing file is an error.
4. Real core: a headless test that runs a recipe from a captured moment on the test
   ROM, loads the moment, runs again, and gets identical WRAM.
5. Session and panel: `RunControl` in `betweenFrames`; a guard reset after a loaded
   moment; the *Saved moments* switch; Save/Save As/Load use it; status shows a
   waiting start.
6. Docs and QA.

### Findings
- **Replays are exact in real games.** `corrupt-smoke --replay` (added for this) restores
  the moment and replays 900 frames with every memory target and the picture identical:
  Pocky & Rocky, FE4 Project Naga and FF6 T-Edition with gentle-wram-noise, and Pocky with
  tile-rot, sprite-jitter and music-mangler.
- **A stale moment bug, caught in review.** Loading a recipe kept the last run's moment
  until the new run started, so Save in that window (or while paused) would have written
  another recipe's moment. Loading or creating a recipe now forgets it, and nothing counts
  as a new moment while a start is waiting.
- **Load starts the run.** A recipe with a moment is about replaying it, so Load jumps and
  activates; the *Saved moments* switch is the way out.
- **Moments are about 800 KB, uncompressed.** Fine for a few, heavy for a shared repo full
  of them; compressing them (the core already links zlib) is a cheap follow-up.

## Later in this phase (outlined, not yet planned in tasks)

## #16: named memory areas

Goal: name a range once ("player stats", "enemy table") and use the name in any rule for
that game, managed from the memory viewer.

### Decisions
- **Names live in the game's `regions.json`**, which recipes and the rule editor already
  read. A recipe stores the name, so redefining a region moves every rule that uses it
  the next time the recipe loads.
- **Names:** 1–40 characters, starting with a letter; letters, digits, spaces, `_` and `-`.
- **Rename updates the recipes** in the game's folder that use the name, and the running
  recipe, so nothing breaks.
- **Delete writes the region's range into the recipes that use it** (the rule keeps its
  bytes, just without the name), instead of leaving them unloadable.
- **Notes in `regions.json` survive** every edit (the reader used to drop them).

### Tasks
1. `validate_region_name`; `parse_regions` returns notes and `write_regions` writes them;
   `Library::set_region`, `rename_region`, `delete_region`, `recipes_using_region`. Tests
   in a temp library: add and redefine; rename rewrites the recipes that use it and leaves
   the rest byte-identical; delete inlines the range and the recipe still loads; notes
   survive; bad and duplicate names are errors.
2. `Engine::rename_region(old, new)` (empty new = inline), no restart. Test.
3. Session: region calls through to the library, the running recipe follows, and a
   recipe file rewritten on disk doesn't count as an unsaved edit.
4. Memory viewer: *Name Selection…*, and a Regions list (click to jump and select;
   Rename, Delete, which says which recipes use it). Offscreen tests with the fake source.
5. Docs and QA.

## Out of this phase
The memory viewer and crash map (phase 4 in the design doc), undo for edits (Reload
discards them), reordering rules.
