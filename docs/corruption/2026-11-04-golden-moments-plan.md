# Keep a golden moment (plan, roadmap #47)

Mikael (4 Nov) picked this as the package after #78. You're playing a recipe and something
wonderful happens: the level turns inside out, a sprite starts eating the HUD. Today it's
gone the moment it's over. **Keep** saves it: the game from a little before, and everything
that happened since, so it plays back exactly, here or on Erik's machine.

## What a kept moment is

Not "recipe + seed". A run replays from those only if nothing else touched it, and live play
touches everything: crash-guard rollbacks, the instant revert, test-ahead re-rolls, Paint,
switches, Change It, live rule edits, buttons pressed. Re-running the engine would need all
of that recorded too.

So a moment records **outcomes, not causes**:

- a **keyframe**: the core's saved state at a frame boundary, plus the ROM's changed bytes
  then (save states don't hold the ROM);
- for each frame after it: **the pads** (8 x 16 bits), **the writes before the frame**
  (everything corruption wrote between frames: flips landing at a guard's fix, Paint, the
  remote) and **the writes in the frame hook** (the engine's rules at line 1), in order;
- what it came from, for the player: the recipe (JSON), the seed, who kept it, when, how
  long.

Playing it back loads the keyframe, puts the ROM bytes in, and plays each frame with its
pads, writing the recorded bytes at the same two points. The engine stays off. The emulator
is deterministic from a saved state with the same input (movies and netplay rely on it), so
the game should do exactly what it did. The engine, its random numbers, the guard: none of
them has to replay, because their writes are in the log.

**Where writes are caught.** Every corruption write goes through `RomKeeper` into
`CoreMemory` (the session's reads use `memory` directly, its writes never do). A
`RecordedMemory` decorator between the two hands each write to the recorder. Nothing has to
remember to record. While a run ahead plays (#52, #78), the recorder holds: those frames are
unseen and put back.

**Where input is caught.** In the Qt app, button presses reach the core on the emulation
thread between frames, so the pads hold still for a whole frame. A new
`CorruptionSession::beforeFrame()` right before `S9xMainLoop()` reads them
(`MovieGetJoypad`, through `CoreAccess::pads()`). Playback sets them in the same place
(`MovieSetJoypad`). Plain pads only. A mouse, Super Scope or Justifier isn't recorded, and
the moment says so if one is plugged in.

**When the game jumps elsewhere.** A crash-guard rollback, a paint watch's rollback, a run
start, Back to Normal and a loaded save state replace the core's state. The history before
that isn't the history of what's on screen now. So the recorder **cuts**: it drops what it
had and takes a new keyframe at the next frame. It notices this by itself.
`SnesCoreAccess` counts state loads and resets. The recorder sees a new count at
`beforeFrame()`, unless the session has said that load was a run ahead putting things back.

The instant revert (#51) is the one load that isn't a cut. It goes back to the start of the
frame that just ran, fixes a byte and plays that frame again. The recorder drops that
frame's hook writes, keeps its writes before, adds the fix, and records the frame again
(`redo_frame()`). The guard rollback could get the same treatment later: its checkpoint is a
known frame back. That waits until step 1 shows how often cuts shorten moments.

**How far back.** A keyframe every 10 s. The last 3 are kept, so 20-30 s of history are in
memory. Keep saves from the newest keyframe at least 10 s ago, so a moment is 10-20 s long.
After a cut, it is shorter. The memory cost is about 3 saved states (about 0.8 MB each) plus
the log. One keyframe costs about 1 ms every 10 s.

**Checked when kept.** Keep plays the moment once, unseen, from its keyframe to now, and
compares the memories (WRAM, VRAM, OAM, CGRAM, SRAM, ARAM, ROM) with the live game. Then it
puts everything back, as a run ahead does. It says *Kept the last 14.2 s; it plays back
exactly* or *Kept, but it doesn't play back the same (the replay drifts): …*. That catches a
path the recorder missed, and the numbers in step 1 come from the same check. The game
pauses for it, about as long as a Try.

## The file and the library

`<library>/<game folder>/moments/<recipe> <date> <time>.s9xmoment`. It is one binary file:
a small JSON header (game, recipe, seed, who, when, frames, notes), then the keyframe, the
ROM bytes and the frames. The library is shared, so Erik's moments show up in the list like
his recipes do. The keyframe is a save state, so the file is mostly that. Whether it gets
compressed (zlib, which the core already links) is decided by step 1's sizes.

## In the app

- **Keep** is a new shortcut (*Keep This Moment*, F8 by default, changed in the bindings like
  F7) and a button in the cockpit beside Corrupt/Stop. It works whenever a game runs,
  corrupting or not.
- **Moments** is a new Workshop page. It lists this game's kept moments (name, recipe,
  length, who, when) with **Play**, **Rename**, **Delete** and **Show in folder** (the file
  is the thing you share).
- **Play** turns corruption off and plays the moment from its keyframe. The status line
  counts *Playing a kept moment: 6.1 s of 14.2 s; press any button to take over*. A button
  pressed ends the playback where it is, and the game is yours. At the end you play on from
  the golden moment, corrupted ROM and all. Back to Normal puts the clean ROM back as usual,
  and F7 starts a new run.

## Steps

1. **Measure first: does it replay?** `corrupt-smoke --keep` plays Goof Troop, Pocky &
   Rocky and FE4 guarded (guard, instant revert and test ahead on, as in the app), with
   pseudo-random button presses so the input path is exercised. It runs three recipes: a
   WRAM noise recipe, a ROM recipe, and the function-change aim. Every 7 s it keeps a
   moment and checks it. The report covers:
   - how many replay exactly, with the check drawing frames and without (#78 found unseen
     frames aren't drawn, and some games read the sprite overflow flags drawing sets);
   - how long the moments are, and how often a cut shortened one, and by what;
   - the file size, raw and compressed;
   - how long a check takes.

   **Go** if nearly all moments replay exactly (19 in 20 or better). If they don't, the
   misses say which path isn't recorded, and that gets fixed before any UI.
2. **The recorder, the player and the file** in `corrupt/moment.{h,cpp}`, tests first with a
   fake game. Then a real-core case with a test ROM. The ROM reads the pads into WRAM and
   draws from them, and a rule writes noise. A kept moment replays to the same memory, and
   a cut or a redo is handled.
3. **The app**: `beforeFrame()`, Keep (the shortcut and the button), the Moments page and
   Play with take-over. Then the UI tour.

## How it went

### Step 1: does it replay?

`corrupt-smoke --keep 3` on Goof Troop, Pocky & Rocky and FE4, four recipes each
(*Gentle WRAM noise*, *Sprite jitter*, *Live ROM drip*, *ROM code drip*), three seeds of 60 s,
played as the app plays (guard, instant revert, test ahead on), with a pseudo-player holding
buttons for 5-45 frames at a time. Every 7 s a moment was kept and checked: 252 moments.

**First run: 246 of 252 played back exactly.** All 6 misses were *ROM code drip* (Goof 4,
FE4 2), the recipe that crashes most. The controls held: the same moment with its buttons
taken out drifted in 246 of 252, with its writes taken out in 243 of 244 that had any (a
still screen doesn't care about buttons). So the check proves something.

**What was missing wasn't the recorder's: it was in the core.** `CORRUPT_KEEP_DIAG` replays a
drifting moment frame by frame against the live game's memory at each frame start. The
drifts began on the very first frame after a keyframe taken right after a crash-guard
rollback, with no writes and no buttons in it. A replay was consistent with itself, the live
game wasn't. Diffing the core's globals at that frame start (CPU, PPU, DMA, timings, APU,
`Memory`, `GFX`) found nothing that mattered. A static in `cpuexec.cpp` did: `VBlankStarted`,
which this fork added to stop the overscan freeze. It isn't in a save state and a load
doesn't set it. A corrupted game that deadlocked left the core mid-frame with it unset, the
guard rolled back with a load, and that next frame got a second VBlank at line 240 and
ended 15 lines early; a playback of the same moment didn't. Fixed in the core: a load sets it
from where the frame is (`S9xVBlankStartedFromState`, from `S9xUnfreezeGameMem`). It's a fix
for the guard, the instant revert and every run ahead too: before it, a rollback after a
deadlock started its timeline slightly wrong.

**Second run: 252 of 252 exact**, drawing the check's frames or not. Another gap was found on
the way and measured: where the last drawn update of the screen ended (`GFX.EndY`, which
feeds the sprite overflow flags at `$213E`) isn't in a save state either. Kept in a trailer
after the state, it made no difference (63 of 63 exact on the three hardest runs without it),
so it was taken out again.

| | Goof | Pocky | FE4 |
|---|---|---|---|
| exact (of 84) | 84 | 84 | 84 |
| average length | 11.5 s | 11.0 s | 11.2 s |
| shorter than 10 s (a rollback cut it) | 26 | 30 | 28 |
| file, packed | 110-152 KB | 66-142 KB | 114-164 KB |
| check (not drawing) | 0.6 s | 0.4-0.7 s | 0.6-0.8 s |

**Go.** The file is packed with zlib (a saved state is 804 KB unpacked, mostly zeros). The
check doesn't draw: no difference, and it's faster.

**Cuts.** A crash-guard rollback starts the history again, so 84 of 252 moments were shorter
than 10 s (rollbacks every 10-20 s in these crash-heavy recipes). The rollback goes back to a
checkpoint whose frame is known, so the history up to it could be kept instead; that's a
follow-up if short moments turn out to matter in play.

### Steps 2 and 3: the recorder, the player, the app

`corrupt/moment.{h,cpp}`: `MomentRecorder` (keyframes every 10 s, the last 3; cut on a load
it wasn't told about; `redo_frame` for the instant revert), `RecordedMemory` (between the ROM
keeper and the core memory), `MomentPlayer` (start: the keyframe and the ROM as it was;
before each frame its buttons and writes; in the hook its hook writes; a button pressed takes
over), `check_moment`, the file (`encode_moment`, `decode_moment`, `read_moment_info`), and
`Library::save_moment/list_moments/rename_moment/delete_moment`. `CoreAccess` gains
`pads/set_pads` and `timeline` (loads and resets counted).

In the app: `CorruptionSession::beforeFrame()` right before `S9xMainLoop()`; Keep (F8, and
*Keep This Moment* in the cockpit) between frames or at once while paused; the Workshop's
Moments page; Play with take-over. The UI tour keeps a moment 4.7 s after Back to Normal
(*Kept the last 4.7 s (it starts where you went back to normal) … it plays back exactly*), then
plays it, and the cockpit counts *Playing a kept moment · 1.5 of 4.7 s*.

**Not done:** keeping the history across a rollback (above); the remote page has no Keep
button yet; a mouse or Super Scope isn't recorded.

## Follow-up (5 Nov, roadmap #79): history across a crash-guard rollback

Mikael said *start the next*, and this was the follow-up offered. A third of the moments in
crash-heavy recipes were shorter than 10 s, because a guard rollback starts the history again.
It doesn't have to. The guard goes back to one of its own checkpoints, and the recorder was
there when that checkpoint was taken. Everything recorded before it is still the history of
what's on screen after the rollback.

**Marks.** Between frames, right after the guard has had its turn, the session calls
`recorder.mark(engine frame)`. The recorder remembers which of its frames comes next and how
many writes are waiting for it (the writes before the checkpoint are in the checkpoint; the
ones after it aren't). One mark a frame, about 1800 small entries for 30 s. A mark for a
frame number the engine already had (it stands still while the core is stuck) replaces the
older one, as the guard's newest checkpoint of that frame is the one it goes back to.

**Back to a mark.** On a rollback the session calls `recorder.back_to(restored frame)` instead
of `cut`. The recorder:
- drops the frames from the mark on and any keyframe taken at or after it (it holds the old
  timeline);
- keeps the writes waiting at the mark that came before the checkpoint (a replay needs them;
  the live game has them in the checkpoint);
- keeps every **ROM** write since the mark, in order, and the rollback's own ROM undo writes after
  them. Save states don't hold the ROM, and `RomKeeper::revert_after` only undoes edits stamped
  after the checkpoint's frame, so the ROM ends as it is live. This is the same rule as
  `redo_frame`.

It falls back to a cut when the mark is gone (older than the history) or no keyframe is left
before it. A keyframe no longer folds the writes waiting for its frame into itself. They stay
in the frame's list and are written again on playback, which changes nothing, because the
keyframe already holds those values. A rollback can need them.

A moment that spans a rollback shows the timeline that survived, not the crash and the jump
back. That's what was on screen afterwards, and the crash never happened in it.

**The risk: a load isn't the same as playing on.** Until now a moment always started at a
keyframe, and the live game had also just loaded a state there, so both sides started from a
load. Across a rollback, the replay plays straight through the checkpoint's frame, while the
live game loaded the checkpoint. Anything the core doesn't save (`GFX.EndY` was one, and
`VBlankStarted` was another until #47 fixed it) could now differ. `corrupt-smoke --keep`
measures this on the same 3 games × 4 recipes × 3 seeds, against #47's 252 of 252. The
numbers to report:
- how many moments are exact;
- how many are shorter than 10 s;
- how many span a rollback.

`CORRUPT_KEEP_DIAG` shows the first frame that drifts.

**Go** if they still play back exactly (at least 19 of 20 that span a rollback, and none of
the rest worse). Otherwise the gap is fixed in the core, or this stays a cut.

Not in scope: Paint's crash watch rollback (it goes back to a state taken at Apply; rare)
and Back to Normal (the history before the run start is another timeline).

### How it went (#79)

Built as planned, with one change. The recorder marks only when the guard actually took a
checkpoint (`CrashGuard::took_checkpoint()`), not every frame. Marking every frame works
too, but right after a guard reset, with the core stuck mid-frame, the newest mark of a
frame number could come after the guard's only checkpoint of it. Taking the mark from the
guard is exact, and there are about 30 marks instead of 1800.

Tests first: three unit cases with the fake game: back across a rollback, with the
checkpoint's own frame's ROM edit standing; past a keyframe; to a mark that's gone; a frame
number marked again. When the writes waiting at the mark were dropped instead of kept, the
cases failed. One real-core case loads a checkpoint, plays on, and checks the moment through
it, drawn and not.

`corrupt-smoke --keep 3`, the same 12 runs as #47 (3 games × 4 recipes, 3 seeds of 60 s, guarded,
with the pseudo-player), against #47's numbers:

| | #47 (a rollback cuts) | #79 (keeps the history) |
|---|---|---|
| exact | 252 of 252 | 252 of 252 |
| moments that span a rollback | none | 102, all exact |
| rollbacks whose history was kept | none | 65 of 65 |
| shorter than 10 s | 84 | 14, all of them near the run's start (a rollback took away frames there) |
| average length | 11.2 s | 14.2 s |
| check (not drawing) | 0.4-0.8 s | 0.6-0.9 s (longer moments) |

The risk didn't happen. A replay that plays straight through a checkpoint matches a live game
that loaded it, in all 102 moments, including *ROM code drip* with deadlocks and instant
reverts. Since #47's `VBlankStarted` fix, the core's save state holds what these games run on.

**Go.** In the app, a moment that spans a rollback shows the timeline that survived: the
crash and the jump back aren't in it. Paint's crash watch and Back to Normal still start
the history again.

## Follow-up (7 Nov, roadmap #86): moments that read what drawing sets

#84's explorer runs left one gap open. In Link's house, 3 of 24 candidates' moments didn't play
back exactly, with and without #84's placebos, so the gap was older. A candidate that doesn't
replay can't be rated with confidence.

**Finding it.** `snes9x-explore` gained two switches for this. `SNES9X_EXPLORE_ONLY=567,399,407`
plays only those tries, keeping them whatever their verdict. `SNES9X_EXPLORE_DIAG=1` also
checks every moment without drawing, and with `ONLY` it remembers each frame of the runs and
says where a replay first leaves its run. The diagnostics corrupt-smoke's `CORRUPT_KEEP_DIAG`
used moved to `tests/headless/state_diag.cpp` so both share them. The three drifted on their
own too, so nothing earlier in the exploration caused it. Every frame of the replay started
exactly as the run's had; only the end differed.

**Cause 1: the check didn't draw, the runs did.** `check_moment` plays unseen without drawing
(#47 measured no difference in 252 moments and chose the faster way). The explorer's runs
draw every frame. Snes9x sets the sprite overflow flags (`$213E`) line by line when a frame
isn't drawn, but at each screen update when it is, so a game reading them in the middle of a
frame sees other values; other state only drawing touches may matter the same way. Two code
changes and a kill switch made Zelda's frames depend on drawing (which register they read
wasn't traced). Drawn, all three replayed exactly; not drawn, all three drifted.

**Cause 2: a save state doesn't hold the flags.** A test ROM that reads `$213E` in VBlank
(`testrom::build_overflow`: all 128 sprites on the first lines) drifted even with a drawn
check. The live game had the flags at `$40` at the keyframe, and a load left them at 0:
`PPU.RangeTimeOver` isn't in a Snes9x save state, and neither is `GFX.EndY`, the line the next
drawn update takes them from (#47 found that one and saw no effect then). The explorer never
met this, as its runs and their replays both start from a load; a moment kept with F8 in live
play does.

**What changed.**
- `corrupt::freeze_state` / `thaw_state`: every state the engine saves (keyframes, the guard's
  checkpoints, runs ahead, the explorer's starts) ends in a 7-byte trailer holding the flags
  and `GFX.EndY`. A state without it (a moment kept before) loads as before. The core itself
  is untouched; `headless::save_state` uses the same two functions.
- `check_moment` plays without drawing first, as before, and plays again drawn only when that
  drifts (`CheckHooks::redraw`, on by default; `MomentCheck::drawn` says which answered).
  The game drew its frames, so a drawn replay is the fair test, and the usual case costs
  nothing more. The app's F8 check gets this too.

Tests first: a unit case with the fake game (a game that reads drawing: one playback not drawn
drifts, the redraw is exact; a game that doesn't: one playback, as before) and a real-core case
with `build_overflow` (exact drawn and not; the same moment without its trailer still loads
but drifts).

### How it went (#86)

The same three scenes as #84, 577 tries each, seed 2982870414:

| | Pocky's temple | Zelda's field | Link's house |
|---|---|---|---|
| exact before | 24 of 24 | 24 of 24 | 21 of 24 |
| exact now | 24 of 24 | 24 of 24 | 24 of 24 |
| needed the drawn replay | 0 | 0 | 3 |
| the check, all 24 | 5.9 s | 6.2 s | 8.3 s (6.1 s not drawn) |

`corrupt-smoke --keep 3`, the same 12 runs as #47 and #79 (Goof, Pocky, FE4 × gentle WRAM noise,
sprite jitter, live ROM drip, ROM code drip; 60 s seeds, guarded, the pseudo-player), with the
trailer in every state: **252 of 252 exact drawing the frames and 252 of 252 not drawing**, as
before; the 102 moments that span a rollback (64 rollbacks kept) all exact; the controls still
drift (without the buttons 250 of 252, without the writes 249 of 252). A check costs what it
did: 0.64-0.81 s not drawn, 0.94-1.38 s drawn. These recipes never made a game depend on
drawing; the explorer's code changes did, 3 times in 72 candidates.

**Go.** The redraw costs nothing in the usual case and answers fairly in the rare one. Moments
kept before #86 have no trailer; they load with the flags clear, as they always did.
