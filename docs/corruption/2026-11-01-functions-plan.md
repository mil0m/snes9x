# Functions: the call graph and kill switches (plan, roadmap #26 and #34)

**Status (2026-11-01):** planned, being built. #26 is in phase 8 (the tracer learns the
game's structure), #34 in phase 9 (wrong-but-valid operations). From
[the horizons doc](./2026-10-31-horizons.md).

**Goal.** Turn one of the game's own functions off while it plays, the way you'd flip a
switch: gravity, collision, the enemy AI, the scroll, the music driver's tick. The game
keeps running and does the wrong thing with its own parts. A function is skipped by
writing a return (`RTS` or `RTL`) on its first byte, so the stack stays balanced; turning
it back on puts the byte back.

**Decided with Mikael:** both ways in. A **Functions** window lists what the game is
running, busiest first: tick one off live, or put it on a hotkey 1-9 that toggles it, and
keep it in the recipe. And a rule type, **Turn functions off**, that picks random functions
for chaos. The window comes first.

## What has to be learned, and how long it takes (Mikael's question)

The tracer already sees every instruction run from ROM. #26 makes it note every call:
`JSR $abs` (near), `JSL $long` (long) and `JSR ($abs,X)` (near, through a jump table).
The target of a call is a function entry; the call's type says how it returns.

- **Not long for what matters.** Kill switches rank by how often a function runs, and the
  functions that run every frame (the player's movement, enemy updates, collision,
  sprites, the frame's bookkeeping) are all called within the first second of a scene.
- **Breadth takes play.** A boss's AI, the menu or the ending are learned when the game
  gets there. The window only lists what has run, so it can't offer what it hasn't seen.
- **Ranked by now.** The window ranks by how many of the last 5 seconds' frames each
  function ran in, so its top rows are what is on screen *now*, not a menu routine while
  you're in a battle. Lifetime counts are kept too (for the rule, below).
- **A data file per game, no code per game.** Each person's calls are saved in the library
  next to their code map and jump log (`calls.<you>.json` in the game's folder) and merged
  with everyone else's on load, like those. It's the first real part of a game's profile
  (#41). What belongs to which scene is #48 (scene tags), later.

## #26 The call graph

**Recording.** In the tracer's opcode wrappers, for `JSR $abs` (`$20`), `JSL $long`
(`$22`) and `JSR ($abs,X)` (`$FC`, where `note_jump` already reads the target): the call
site and the target as ROM offsets, and the type. A call into RAM or I/O isn't noted (code
in WRAM is #30). Noted into a `CallLog`: one record per (site, target) with its hits and the
frame it was last seen in. A hash lookup per call; frames run a few hundred to a few
thousand calls, so it should cost well under 1% (measured, below). Paused during runs ahead
(#52) like the jump log, so a rejected change teaches nothing.

**Functions.** Between frames the keeper folds the log into functions, one per entry:
- the call type (near, long, or **mixed**: reached both ways, so which return it needs
  isn't known and it can't be switched off);
- calls and callers (distinct sites);
- **frames run**: in how many frames it was called at all, over its lifetime;
- **now**: in how many of the last 300 frames it was called (a ring of per-frame bits, kept
  only in memory).

A function called 500 times in one loading frame is not busy; one called once in every
frame is. So the ranking counts frames, not calls.

**File.** `calls.<you>.json`, JSON like the jump log: `{"format": "snes9x-calls",
"version": 1, "rom_size": N, "calls": [{"site": "0x0081A3", "target": "0x009000", "op":
"20", "hits": 1234}], "functions": [{"entry": "0x009000", "frames": 5120}]}`, in offset
order so files diff well in the shared library. Merged by adding up. Saved once a minute
while it grows, and when the game closes, like the jump log.

**Design choices.**
- *Function entries from calls only.* An entry the game jumps to (`JMP`, `BRA`) without
  calling isn't a function you can return from. Interrupt handlers end in `RTI` and aren't
  called, so they never show up. Good: those are the ones a return would break.
- *Mixed functions aren't offered.* A wrong return pops the wrong number of bytes and the
  game crashes for sure, which isn't the point.
- *ROM offsets, not CPU addresses.* Mirrors fold into one; the window shows the CPU
  address the game called (`$C0:9000`) from the bank it was called from.

## #34 Kill switches

**A switch** is a function entry, the return byte (`$60` RTS for near, `$6B` RTL for long)
and the clean byte it replaces. **Its state is the ROM byte itself**: off when the entry
holds the return byte. So a rollback that undoes the write, the crash blame putting it
back, Back to Normal, a new run: the switch follows, and nothing can disagree with the
ROM.

**The Functions window** (*Functions…* in the ROM section, next to *Survivors…*):
- a table: function (`$C0:9000`, near/long), **now** (a bar: frames of the last 5 s it ran
  in), frames run in all, callers, **Off** (a tick box), **Hotkey** (none, 1-9), note;
- busiest now first; a filter box (address); updates twice a second while open;
- ticking *Off* turns it off at the next frame; unticking puts it back;
- a hotkey makes the switch part of the recipe: pressing that key (the panel's, the remote
  page's or the keyboard's 1-9) flips it at the next frame. The same keys still fire hotkey
  rules;
- *Keep in recipe* adds the switch to the recipe as it is now (hotkey, note, and whether it
  starts off), like pinning a survivor.

**In the recipe:** `"switches": [{"entry": "0x009000", "return": "60", "was": "22",
"hotkey": 3, "start_off": false, "note": "gravity?"}]`. `was` is the clean byte, so a
switch made on another version of the game is skipped (like a pin). Start-off switches are
written as a run starts, after pins, before the rules.

**In the engine:** `Engine::flip_switch(i)` and hotkeys queue a flip for the next frame;
`on_frame` flips first (reads the ROM byte: the return byte → the clean byte, else → the
return byte), then runs the rules. Flips go through the `RomKeeper` (journalled, so a
rollback undoes them) but not into the rules' event log (they aren't a rule's doing and
the guard mustn't re-roll a rule for them).

**When a switch crashes the game.** Most don't (that's the point), but some will: a
function that reads parameters stored after its `JSR`, or one whose result the caller
needs. The return byte is a changed ROM byte like any other, so the existing safety nets
catch it with no new code: the instant revert (#51) puts it back if the CPU lands in
non-code right after it ran, and the crash guard's blame puts it back if the game ran it
before a crash. Either way the switch shows *on* again, and the status line says why.

**The rule type: Turn functions off** (`"op": "kill"`, ROM only, like pointer swap):
- each firing turns `count` functions off, picked from those learned, in the rule's range;
- **Pick from**: *busiest* (the 64 functions that ran in the most frames, lifetime) or
  *any* function learned;
- fixed when the run starts (the lifetime counts, not *now*), so a run replays exactly from
  its seed, as pointer swaps do;
- undone like any rule's ROM writes (rollback, Back to Normal); a hotkey trigger makes it a
  "turn something off" button.
- Starter: **Function roulette** (on hotkey 1: turn a busy function off).

## Tests

- **Unit (fakes):** `CallLog` notes, merges and round-trips its file (bad file, wrong ROM
  size, mixed types); the keeper's functions: frames-run counts frames not calls, *now*
  forgets after 300 frames, mixed is mixed, the busiest order; the engine's switches: a
  flip writes the return byte and a second flip the clean one, through the ROM keeper, not
  into the event log; queued hotkey flips; start-off switches at run start; a `was`
  mismatch skips; the kill op picks only near/long functions in range, busiest-first, the
  same picks from the same seed; recipe JSON round trip and validation.
- **Real core (test ROM):** a ROM whose main loop calls a near function that increments a
  counter and a long function that increments another, plus a function called both ways:
  the tracer finds the three with their types and frame counts; the near one switched off
  stops its counter and the game keeps running, switched back on it counts again; the long
  one with `RTL` the same; a rollback to before a flip undoes it; a function that reads data
  after its `JSR` switched off lands in non-code and the instant revert turns it back on.
- **Qt offscreen:** the Functions window lists, ranks, ticks a switch off (the ROM byte
  changes), sets a hotkey, keeps it in the recipe; the UI tour gets a picture of it.
- **corrupt-smoke:**
  - `--calls`: speed with and without call noting; functions found after 10 s, 1 min and
    5 min; how many run every frame. On Goof Troop, Pocky & Rocky, FE4 and FF6. This
    answers "how long must it run" with numbers.
  - `--kill [seeds]`: from a learned map, turn one busiest function off every 10 s for a
    minute, against the same number of random code bytes, unguarded: how many runs are still
    moving, and how different the picture is from a clean run (like `--swaps`).

## How it went

(To be written.)
