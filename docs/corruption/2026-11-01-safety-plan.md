# Safety first: plan (roadmap phase 7, #50-#52, #55)

**Status (2026-11-01):** #50 done (QA.md section 27), #51 done (QA.md section 28), #52 done (QA.md section 29). #55 test at first use (the follow-up to #52): planned, being built. From Erik's list (sorted in
[the horizons doc](./2026-10-31-horizons.md#eriks-list-2026-11-01)): see what the game uses
live, catch a bad change sooner, and test a change before it reaches the game.

## #50 ROM live heat

**Goal.** In the memory viewer, on the ROM: what the game runs and reads *right now*
lights up and fades over a second and a half, so you can watch which code and tables a
scene uses, and see a corrupted area come into play. Today's Code, Data and DMA overlays
show everything ever seen; WRAM already has fading Reads and Writes (#23).

**How.** The code map's tracer and the core's read hooks already set a *touched* bit on
every ROM byte the game uses (the crash blame reads it for changed bytes, then clears it).
While the viewer shows the ROM:

- Every frame, after the crash blame has collected, `RomHeat::observe` scans the map for
  touched bits (8 bytes at a time), stamps each touched byte with the frame and clears the
  bit. The blame only reads the bits of changed bytes, and it has already done so for the
  frame, so nothing is lost to it.
- The first scan after the viewer starts watching the ROM only clears: bits set while
  nobody watched would otherwise light up everything at once.
- When the viewer pulls a snapshot (10 times a second), the stamps become one level per
  byte (0-255), fading linearly over `kFadeFrames` (90, 1.5 s). Only then: no per-frame
  work beyond the scan.
- **Live** overlay (ROM only), a bright yellow; hover says *in use right now* or *used in
  the last second or two*.

Cost: only while the viewer shows the ROM. The scan is one pass over the map a frame
(6 MB for FF6, about 0.3 ms with 8-byte words); stamps are 4 bytes per ROM byte.

**Not in it:** live heat for VRAM, OAM, CGRAM and ARAM. The PPU reads VRAM all the time to
draw, so "what's read" there means little; what the CPU writes there comes from WRAM and
ROM, which already show it.

**Tests.** `RomHeat`: stamps and clears touched bytes, fades to nothing after the fade,
the first scan after a pause lights nothing, the blame's bits on changed bytes still work
when it collects first. Real core: on the test ROM, the NMI handler and its table glow
after a frame and the padding doesn't. Viewer: the Live overlay is offered for the ROM only,
paints from the snapshot's levels, and the hover says so.

**How it went.** As planned, plus one catch: the crash blame clears the touched bits of
*changed* bytes before the heat scan runs, so corrupted bytes in use wouldn't have glowed;
`RomBlame::used_this_frame()` hands them over and the heat stamps them too. On Goof
Troop's title and demo the running code and tables glow, and graphics fade after each DMA
(tour shots `07b-viewer-rom-live`, `07c-viewer-rom-jumps`).

## #51 Instant revert

**Goal.** When a changed ROM byte sends the CPU somewhere that can't be code, put that byte
back at once and replay the frame, instead of letting the game run off and waiting for the
crash guard to notice a second or more later and roll back 1-6 s. The player should at most
see one frame repeat; the changes that don't derail anything stay.

**What "landed on non-code" means.** The code map tracer already sees every instruction
before it runs. An instruction *lands badly* when it starts at:

| Landing | Why it can't be the game's own code |
|---|---|
| open bus or I/O registers (the slow table finds no plain memory there) | nothing runs from there on purpose |
| a ROM byte the map knows only as an **operand** (never as an opcode) | the middle of an instruction: the stream is misaligned |
| a ROM byte the map knows only as **DMA source** (never as code) | graphics or sound data (*dropped after measuring, see below*) |
| a `BRK` (`$00`), `STP` (`$DB`) or `WDM` (`$42`) the map hasn't seen run there (or that has changed since), or anywhere in RAM | zeroed padding, the classic runaway; STP halts the CPU; WDM is never used |

Not counted, because the game does them itself: code never seen before (first visits are
normal), bytes known only as *data* (games checksum their ROM and copy routines to RAM, so
real code is often read as data first), code in WRAM, `COP` (some games use it as a
system call). The map is the player's own (the tracer's), not others' merged in.

**How soon after a changed byte ran.** Only within `kWindow` instructions (start: 64) after
a changed byte was *used*: run as part of an instruction (opcode or operand), or read as an
indirect jump's pointer (`JMP (a,X)`, `JSR (a,X)`, `JMP (a)`, `JML [a]`, which the tracer
already watches for #25). A misaligned stream or a bad jump lands within an instruction or
two; a window keeps innocent changes that ran long before out of it. Other data reads of
changed bytes (a table value that later becomes an address) aren't seen per instruction
without a core change, so those crashes stay the guard's. The **culprits** are the changed
bytes used in the window before the landing, newest first (a short ring of 8).

Changed bytes get a new code map bit, `Changed` (`0x40`, never saved, like `Touched`), set
and cleared by the ROM blame as the keeper reports bytes changing; pinned bytes never get it,
so a pin is never reverted. The per-instruction cost is one more test of bits the tracer
already loads.

**Mid-frame vs between frames.** The core can't be rolled back mid-frame, so the tracer only
*notes* the landing (the first one in the frame, with its culprits) and stops marking the
code map for the rest of the frame, so the garbage that runs afterwards doesn't teach the map
that data is code. Between frames, before anything else:

- A **frame-start checkpoint** is kept: the core's state saved at the end of every
  `betweenFrames`, but only while the ROM has changed bytes and the tracer is on (cost
  measured below).
- On a landing: load that checkpoint (the CPU, WRAM, VRAM, sound: the frame never
  happened), and fix the culprits the way the recipe's ROM policy says (revert puts the clean
  byte back for good; re-roll draws another value), through the keeper's `settle` so no
  rollback brings them back. The engine is *not* rewound: its frame counter goes on, its ROM
  writes from that frame stay (minus the culprits), and its WRAM writes from that frame are
  lost with the frame. So the guard sees time move forward as always and needs no change.
- The frame then simply runs again. If it lands badly again, the next culprits go, and so
  on; after `kMaxInRow` (8) instant reverts with no clean frame between them it stops trying
  until a clean frame, and the guard handles whatever is left.
- No checkpoint for the frame before (a run just started, back to normal, a rollback): the
  note is dropped and the guard handles it as today.

The crash map learns the culprits as ROM crashes (precise ones: no window, no halves). The
journal gets a line (`"event": "instant_revert"`, where it landed, the bytes); the dock's
ROM line counts "N put back at once"; the status line says it (*Jumped into data at
$C0:1234: put back 1 ROM byte*). It's on by default whenever the ROM blame is, with a
recipe switch (`"instant_revert": false`) for anyone who wants to watch the crash happen.

**False positives.** A landing in the game's own code can still happen: code run at a new
M/X width for the first time starts at what the map only knew as an operand. Measured
directly: `corrupt-smoke --derail-fp` runs the clean game with the window always open and
counts landings by kind (every one is a false alarm), after recording the map for the same
stretch. A kind with more than a handful per hour is dropped from the table above.

**Measuring.** `corrupt-smoke <rom> <recipe> <frames> <warmup> --instant [seeds]` plays a
ROM recipe guarded for each seed twice: guard only, and guard plus instant revert. Per arm:
crashes the guard caught, instant reverts, frames lost to rollbacks, changed bytes still in
at the end, used ones, and whether the game is alive at the end. Instant revert should cut
guard rollbacks and frames lost and keep more changes. On Goof Troop, Pocky & Rocky and
FE4, with the opcode-swap and code roulette starters.

**Tests.** Unit: the landing table (`landing(...)` as a pure function), the window and culprit
ring, the `Changed` bit kept by the blame (set, cleared, pins, attach, forget, not saved),
`InstantRevert` with a fake core (no checkpoint: nothing; landing: loads the checkpoint,
settles the culprits under both policies, counts in a row, stops after `kMaxInRow`).
Real core, a new test ROM: a changed branch offset that jumps into the middle of an
instruction is put back within the frame, the frame counter in WRAM shows one frame
repeated, not a rollback; a changed byte that never derails stays; the same ROM with
instant revert off is caught by the guard as before.

**How it went.** Built as planned (`corrupt/derail`, `corrupt/instant_revert`, the tracer's
`watch_derails`/`take_derail`, `RomBlame::fix` and the `Changed` bit, `CrashMapKeeper::
rom_crashed`, the journal's `instant_revert` event, the recipe's `instant_revert`, starter
**Code drip**, the dock's *N bad jumps put back at once*), with three findings:

- **DMA-only landings dropped.** `--derail-fp` on the clean games (map learned from 10-60 s
  of boot, then 10 min with the window held open): Goof Troop, Pocky & Rocky, FF6 0, but
  FE4 ~7000 an hour, all at one spot it DMAs to RAM *and* runs from ROM. Without that kind:
  **0 false alarms** on all four.
- **STP stops the frame hook**, so the engine's frame count stands still in a frame that hit
  STP. The checkpoint counts as the frame's start when it was kept at this count or the one
  before.
- **Saving a state into a fresh vector costs 3.7 ms on macOS** (an 800 KB allocation that
  page-faults; most of it is the fixed 512 KB SRAM block), against 0.04 ms for the freeze
  itself. `CoreAccess::save_state_into` reuses the buffer: the checkpoint costs **0.035 ms a
  frame**.

- **The watch itself costs 5-10% on top of tracing** (Goof 9.6%, Pocky 5.6%, FE4 5.2%, from
  `--codemap`; still 600+ fps), so the session attaches it only while a run could change the
  ROM: corruption on, and the ROM already changed or a ROM rule enabled. A clean game pays
  nothing.

Also: a front-end state load now tells the session (`stateLoaded`), so neither the guard nor
the instant revert goes back to before it.

Measured with `corrupt-smoke --instant 20`, 20 seeds of 60 s each, guard alone vs guard +
instant revert, code map learned from a 60 s clean boot. **Code roulette** (30 random code
bytes at the start, classic):

| | guard rollbacks | frames lost | used changes kept | instant reverts |
|---|---|---|---|---|
| Goof Troop | 7 → 6 | 3308 → 2761 | 86 → 164 | 51 |
| Pocky & Rocky | 64 → 32 | 31146 → 17383 | 72 → 103 | 55 |
| FE4 | 136 → 65 | 65623 → 31076 | 39 → 91 | 201 |

**Code drip** (two code bytes every 2 s, live):

| | guard rollbacks | frames lost | used changes kept | instant reverts |
|---|---|---|---|---|
| Goof Troop | 7 → 4 | 3105 → 1423 | 119 → 194 | 58 |
| Pocky & Rocky | 45 → 21 | 18705 → 9006 | 116 → 182 | 103 |
| FE4 | 75 → 31 | 33016 → 13029 | 79 → 136 | 96 |

About half the rollbacks and half the frames lost, and 1.5-2x the changes in use stay in.
Landings: mostly mid-instruction, then BRK, open bus, a few STP and WDM. Whether the game
is still moving after a minute barely changes (Pocky roulette 12 → 10, FE4 6 → 8 of 20; drip
the same within one): the changes the game can run without derailing still break it
quietly, which is #52's job.

## #52 Test before applying

**Goal.** A live ROM change only reaches the game you're playing once it has been tried: the
app saves the moment, plays a few seconds ahead with the change, unseen and unheard, and
keeps the change only if the game is still standing. If it isn't, the rule rolls again
(another pick) and that is tried instead; after a few failures this firing is skipped. A
short pause per change (about 0.15 s for 3 s ahead) buys live ROM corruption that almost
never crashes. Erik's "how much do we get away with if performance matters less".

**Which changes.** The ROM writes of live rules (every N frames, a chance, a hotkey, once
at a frame): what the engine will write in the next frame. Not tested: WRAM and the other
memories (rules fire there every frame, and the guard handles them well), classic at-load
changes (hundreds at a power cycle: the guard and #51 handle those), painting.

**How.** Last thing between frames, when the recipe has an enabled live ROM rule:

1. **Preview.** `Engine::preview_next` runs the engine's next frame against a memory that
   drops writes, records the ROM writes it would make, and restores the engine (its
   snapshot plus the pressed hotkeys, which a snapshot doesn't hold). No ROM writes: done,
   nothing else costs anything.
2. **Run ahead.** Save the core's state, the engine, and the code map's flags; play
   `frames` frames (default 180, 3 s) with nothing drawn (`IPPU.RenderThisFrame` off, which
   also makes them faster), the sound thrown away (the Qt front end clears samples while a
   flag is up), the WRAM counters and the jump log paused, and buttons as they are. The
   engine fires as it would for real, the preview's writes in the first frame.
3. **Verdict**, per frame: a landing from #51's watch whose culprits include a previewed
   byte (guilty at once); otherwise only definite signals count, since the window is short
   and nobody presses buttons: CPU stopped, deadlock, a reboot, runaway PC. NMI off, frozen
   and black screens need seconds and presses: not in a run ahead. A landing that blames
   other, older changes stops the run as *inconclusive* (#51 will deal with it for real).
4. **Put everything back**: the core's state, the engine, the hotkeys, the ROM (the
   keeper's journal reverts the run's writes, and the blame's bookkeeping follows), the code
   map's flags (no touched bits from the future, no garbage learned), the reset watch's
   count (or the guard would see a reboot), the on-screen message's timer.
5. **Decide.** Survived, or inconclusive: keep; the real frame makes the same writes. A
   crash with the previewed bytes never used: not theirs, keep. A crash otherwise gets a
   **control run**: the same window with those writes vetoed. If that crashes too, the
   change isn't guilty (older damage): keep it and leave the crash to the guard. If only the
   changed run crashed, the rule is **re-rolled** (a new salt, as the guard's re-roll does)
   and the new picks are previewed and tested, up to `tries` (3); after that the firing is
   **vetoed**: the engine skips those writes in the real frame.

Engine additions: `preview_next(mem)`, pressed hotkeys readable and settable, and vetoes
by frame and offset (kept until the frame has passed for real, so a control run and the
restore don't lose them).

**Cost.** Only in frames where a live ROM rule fires: ~3 s of emulation unseen (Goof Troop
runs ~1000 fps drawn, more undrawn) plus a control run only after a crash. The game pauses
for that long; the status line says what was tried. Per recipe `"test_first": 3` (seconds;
`0` off), on by default for live ROM rules. *(Design choice: on by default, since this is
what phase 7 is for; a recipe or a panel switch can turn it off if the pauses bother.)*

**What it can't see.** Crashes later than the window, crashes that need the player's
input, damage that only shows as a wrong picture. Those stay the guard's and #51's.

**Tests.** Engine: preview returns the next frame's writes and changes nothing (state,
memory, hotkeys); vetoes skip exactly that frame's writes. `TestAhead` with a fake core:
nothing to test, a change that survives (kept, ROM and engine as before, hotkey still
pressed), a poisoned pick re-rolled into a good one, an always-poisoned rule vetoed after
the tries, a crash the control run shares (kept), a crash with the change unused (kept), a
landing blaming the change (no control run), an unrelated landing (kept), the map's flags
restored. Real core, the derail test ROM: a live rule that would branch onto the STP never
reaches the game (no instant revert, no guard rollback, the frame counter never repeats),
while a harmless live change goes through. `corrupt-smoke --ahead [seeds]`: guard + #51
with and without testing first, on Goof Troop, Pocky & Rocky and FE4: rollbacks, instant
reverts, frames lost, firings re-rolled or skipped, time spent testing, and whether the game
still moves after a minute.

**How it went.** Built as planned (`corrupt/test_ahead`, `Engine::preview_next`, pressed
hotkeys, vetoes, `CoreAccess::begin_unseen/run_unseen_frame/end_unseen`, the tracer's
`note_jumps`, the recipe's `test_first`, the session's re-entrant frame hook, the Qt front
end dropping samples while unseen, the dock's *N changes tested first*), with these changes
along the way:

- **A run ahead tests one firing.** The rule fires again inside the window (Code drip every
  2 s, the window 3 s), so the control run crashed too and nothing was ever blamed.
  `Engine::quiet_after` keeps ROM writes after the tested frame from landing during a run.
- **A landing names the rule.** With two rules writing in the same frame, only the one whose
  byte the landing blamed rolls again or is skipped; the other's write goes ahead.
- **Most changes aren't used within the window.** On FE4, 111 of 118 Code drip bytes that
  went ahead were never run in those 3 s: it picks from all the code ever seen, mostly cold.
  The test can only vouch for what's used, and a re-roll would drift towards cold code (a
  pick survives easily if nobody runs it). So a re-roll that survives untouched is only a
  reserve, kept while a pick the game ran is sought; and a run stops early once every
  written byte was used and held up for a second (`settle_frames`).
- **It found a way to freeze Snes9x itself.** A corrupted Goof Troop ran `INC $2133,X` in
  a loop, flipping overscan between lines 225 and 240, so VBlank never started and
  `S9xMainLoop` never returned. That would hang the app in normal play too: fixed in the
  core (f346a6b8, VBlank at the latest at line 240, once a frame), with a test ROM.
- **A panel switch.** *Try live ROM changes a few seconds ahead first* in the Crash Guard
  section, on by default, so the pauses can go without editing the recipe.

Measured with `corrupt-smoke --ahead 10`: 10 seeds of 60 s, both arms with the guard and #51,
the second also testing first; code map and jump tables learned from a 60 s clean boot.
Rollbacks are the guard's; *untried* is the share of written bytes the game didn't use in
the 3 s window, which the test can't vouch for.

| Recipe (live, every 2 s) | Game | guard rollbacks | frames lost | instant reverts | re-rolled / skipped | untried | ms per firing |
|---|---|---|---|---|---|---|---|
| Opcode drip (2 opcode swaps) | Goof Troop | 7 → 0 | 3659 → 2 | 3 → 2 | 1 / 0 | 79% | 214 |
| | Pocky & Rocky | 5 → 5 | 1784 → 1783 | 1 → 0 | 1 / 0 | 89% | 128 |
| | FE4 | 14 → 11 | 6319 → 4697 | 4 → 0 | 5 / 0 | 79% | 148 |
| Code drip (2 random code bytes) | Goof Troop | 0 → 2 | 35 → 1050 | 35 → 30 | 15 / 2 | 91% | 152 |
| | Pocky & Rocky | 7 → 9 | 2464 → 3524 | 49 → 55 | 17 / 2 | 94% | 126 |
| | FE4 | 17 → 15 | 7376 → 6047 | 48 → 11 | 29 / 1 | 87% | 164 |
| Swap drip (1 pointer swap) | Goof Troop | 0 → 0 | 0 → 0 | 0 → 0 | 0 / 0 | 94% | 157 |
| | Pocky & Rocky | 3 → 1 | 462 → 365 | 2 → 0 | 2 / 0 | 97% | 197 |
| | FE4 | 12 → 12 | 5456 → 5456 | 0 → 0 | 0 / 0 | 87% | 141 |

What it means: where the game uses a change within the window, the test does its job (Goof
Troop's opcode drip: no rollbacks left; FE4's instant reverts 48 → 11 and 4 → 0). But in
these attract-mode runs 80-97% of changes aren't used within 3 s, so most later crashes come
from changes nobody could try yet, and the totals move little (and, with re-rolls changing
the run, sometimes the wrong way). Costs ~130-210 ms per firing. **Next idea, not built:
test at first use** — when the crash blame sees a changed byte used for the first time, play
ahead from there with and without it. That reaches the dormant changes, at the moment they
matter, once per byte.

## #55 Test at first use

**Goal.** #52 tries a live change when it's written, but most changed bytes aren't used in
those 3 s (80-97% in the measurements above), and a classic run's hundreds of at-load
changes are never tried at all. So try each changed byte at the moment it matters: the
first frame the game runs or reads it. Then play a second ahead from the start of that
frame with the byte, and if the game falls over, again without it; if only the run with
the byte falls over, fix the byte the ROM policy's way (revert or re-roll) and play the
frame again, as the instant revert does. Each changed value is tested once.

**Trigger.** Between frames, right after `RomBlame::collect`, which already finds the
changed bytes the game used this frame: the ones used for the first time (`frames_used ==
1`) go into a queue. They're tested against the **frame-start checkpoint**: the state
the instant revert keeps of the start of the frame that just ran (#51), so the byte's
first use is replayed in the test, not skipped. The test runs after the guard and before
the checkpoint is replaced for the next frame. Nothing is tested in a frame where the
guard acted (its rollback left the checkpoint's timeline), where the instant revert
replayed the frame (the culprits are fixed already; the rest stay queued for the next
frame), or without a checkpoint of this frame. The checkpoint is kept whenever either the
instant revert or test at first use is on.

**"Without it".** The byte's **clean** value, not the journal's previous one: a byte's
uses are counted from when it first differs from the clean ROM, so an earlier changed
value was never used either and has nothing to vouch for it. The control run swaps the
clean values in directly (`RomKeeper::borrow`: no journal entry, no change notice, so the
blame's bookkeeping isn't disturbed) and puts the changed ones back after; the code map's
*changed* bit is cleared for the run so a landing doesn't name a byte that isn't changed
in it.

**A run.** The same run ahead as #52 (`TestAhead`'s runner, now shared): unseen and
unheard, from the checkpoint instead of from now, the engine snapshotted and the ROM
writes of every rule quieted for the whole run (only the bytes under test differ between
the two runs; WRAM rules fire as they would), the code map's flags put back after.
Verdict:

- a landing (#51) naming a byte under test: **guilty** at once, no control run;
- a landing naming none of them: *inconclusive*, left to the instant revert for real;
- a definite crash (STP, deadlock, reboot, runaway): a **control run** with the whole
  batch clean. If that crashes too, older damage: not theirs, keep. If only the changed
  run crashed: guilty.
- every byte under test used, then 60 frames held up (`settle_frames`): **survived**,
  early. Since the first use is replayed in frame 1, a run is usually 61 frames, ~1 s.

**Batches and the cost cap.** First uses come in bursts (a classic run's at-load changes
as the game reaches new code), so the queue is tested as a **batch** of up to 16 bytes
per run, oldest first. At most one test every 30 frames (2 a second), about 50 ms each
(Goof Troop runs ~1200 undrawn frames a second): under 10% of the time while a burst
lasts, nothing otherwise. A batch that crashes alone without a landing naming the
culprit is **narrowed down**: halves tried alone (the other half clean) while a half
crashes on its own, within 8 runs per test; a byte that crashes alone is guilty, and the
rest of the batch gets one more run without it. If the budget runs out, or neither half
crashes alone (two bytes needed together), the whole remaining batch is fixed, as the
guard's third attempt would. A queued byte that has already been running for the window's
length for real (180 frames) drops out of the queue: it has had its test, live.
*(Design choice: batches and a fixed rate keep a burst of hundreds of first uses from
freezing the picture for seconds; the price is that a byte may wait a few frames for its
test and crash for real first, which leaves it to #51 and the guard as before.)*

**With the rest of phase 7.**

- **#51 instant revert** catches a landing in the very frame of the first use; test at
  first use catches what comes later (a bad value written somewhere, a loop that never
  ends, a jump far from the change). They share the checkpoint and the replay.
- **#52 test before applying** vouches for the bytes its window used: those (at the value
  tested) are skipped at first use. The rest of its writes, the *untried*, are what this
  catches.
- **The guard** stays the net for crashes later than a second, ones needing input, and
  quiet breakage.
- **The ROM policy**: revert puts the clean byte back; re-roll writes a new value, which
  starts unproven and is tested again at its next use. After 3 failed values of one byte
  it's reverted, whatever the policy.

**Switch.** The same as #52: *Try live ROM changes a few seconds ahead first* becomes
*Try ROM changes ahead first* (it now covers classic changes too), and the recipe's
`"test_first"` seconds sets both windows (0 turns both off). *(Design choice: one switch.
They're the same idea at two moments; separate switches would mean explaining the
difference in the panel.)* Journal event `first_use` (bytes, what the run saw); the crash
map learns from it like an instant revert; dock: *N first uses tested (N put back)*.

**Tests.** `FirstUseTest` with a fake core: a byte first used and harmless is kept (one run,
everything as it was, tested only once); one whose crash comes frames later is fixed and
the frame replayed; a crash the control run shares is kept; a landing naming the byte needs
no control run; a landing naming another byte is left alone; a batch with one bad byte
narrows down to it and keeps the rest; the rate cap and the queue's expiry; bytes #52
vouched for are skipped; the re-roll policy retests the new value and reverts after 3.
Real core: a ROM with a fuse (a rarely run routine whose changed operand lights a fuse
that stops the CPU 20 frames later, far from any landing): with the test, the guard never
sees a crash and an innocent change made alongside stays; without it, the guard rolls
back. `corrupt-smoke --first-use [seeds]`: both arms with the guard, #51 and #52, the
second also testing at first use, on Goof Troop, Pocky & Rocky and FE4, with Code drip
(live) and Code roulette (30 code bytes at load, classic): rollbacks, instant reverts,
frames lost, bytes tested, put back, time spent, still moving after a minute.
