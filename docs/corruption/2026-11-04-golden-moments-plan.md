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
