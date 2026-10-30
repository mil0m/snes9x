# Phase 2 — crash detection + automatic rollback: implementation plan

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md),
sections *Crash detection* and *Rollback*. Phase 1 is done ([plan](./2026-10-30-phase1-engine-recipes-plan.md)).

Goal: while a recipe runs, a crash is caught, the game goes back to a checkpoint from just
before the guilty corruption, the policy decides what to change (re-roll by default), play
continues, and every crash is written to the journal. Test-first throughout.

## Two decisions shaped by phase 1

- **Detection and rollback run between frames.** The frame hook fires inside the core's
  scanline processing, where loading a state is unsafe. Front ends call
  `CrashGuard::between_frames()` after each `S9xMainLoop()` return, which is the same place
  the Qt rewind code already restores states. It also catches the core's "CPU is
  deadlocked" stop, which ends a frame early and never reaches the hook.
- **Re-rolls are recipe data.** Each rule gets an optional `salt` (in JSON, omitted when 0)
  that is mixed into its seed. Re-rolling a suspect rule changes its salt, so *Save As*
  after a few caught crashes saves exactly what you saw.

## Pieces (all in `corrupt/`, Qt-free)

- **`CoreAccess`**, the seam for everything the guard needs from the emulator:
  `save_state()`, `load_state()`, `sample() → FrameSignals`. `FakeCore` in the tests;
  `corrupt/core_access.cpp` over the real core.
- **`FrameSignals`**: CPU halted (`STP` or deadlock), emulation stopped, PC location class
  (ok / I/O / open bus), NMI enabled (`$4200` bit 7), forced blank, brightness, and the
  picture hash.
- **`CrashDetector`**: feed it signals once per frame and it returns a verdict
  `{signal, frame, since_frame, text}`, or nothing. Each signal can be switched on or off
  and tuned via `DetectorConfig`, which a recipe can override:

  | signal | default | since_frame |
  |---|---|---|
  | CPU stopped / deadlocked | on, immediate | now |
  | runaway PC (I/O or open bus) | on, 30 frames | first bad frame |
  | NMI off | on, 300 frames (raised from 180: clean runs reached 2.7 s) | when it went off |
  | frozen picture | on, 300 frames | first frozen frame |
  | black screen | on, 480 frames | when it went black |

- **`Engine` additions**: `snapshot()` / `restore()` of runtime state (frame, per-rule
  runtimes, the event log trimmed to the restored frame); `reroll_rule(i)` (a new salt);
  `mute_rule_until(i, frame)`; `set_rule_intensity`. Per-rule crash counts.
- **`CheckpointRing`**: every K frames (default 60) it stores an emulator state plus an
  engine snapshot, keeping the last N (default 30).
- **Attribution**: the suspects are the rules with events in (checkpoint, since_frame]. If
  there are none, it widens to older checkpoints. The heuristic signals (NMI off, frozen,
  black) are ignored when no corruption happened in the window, so a calm title screen
  never triggers a rollback.
- **`Policy`** (pluggable): `decide(context) → action` for the suspects. `reroll` (the
  default), `skip` (mute the suspects until the crash frame), `dampen` (halve intensity),
  `disable`, and `escalate` (re-roll twice, then dampen twice, then disable). Recipes set
  `"crash_policy"`; the panel can override it for the session.
- **`CrashGuard`**: ties it all together in `between_frames()`: checkpoint, detect,
  attribute, restore the state and engine, apply the policy. It returns an outcome
  (nothing, rolled back, or gave up) for the front end to show. It gives up after 5 crashes
  without a clean 10 s in between: it restores the last checkpoint and turns corruption
  off.
- **`Journal`**: appends one JSON line per crash (plus `recovered` / `gave_up` lines) to
  `<library>/<game>/journal.<user>.jsonl`.

## Tasks

1. Rule `salt` + JSON round-trip; the engine uses `seed ^ mix(salt)` per rule.
2. Engine snapshot/restore, reroll_rule, mute_rule_until, intensity, crash counts.
3. `FrameSignals` + `CrashDetector` with every signal and its config.
4. `CheckpointRing` + attribution.
5. Policies.
6. `CrashGuard` over a `FakeCore` whose memory decides its signals (byte X = 0xEE means
   "halted"), so whole crash → rollback → retry cycles are unit-tested.
7. Recipe JSON: `crash_policy` and `crash_detection` overrides.
8. Journal.
9. `core_access.cpp` over the real core (PC map class, `$4200`, PPU state, picture hash,
   freeze/unfreeze, clearing the halt flags after a restore).
10. **Generated test ROMs** + headless integration tests: tiny hand-assembled 65816 programs
    that `STP`, switch NMI off, or jump into I/O space when a WRAM byte becomes `$42`. A
    recipe pokes `$42` there, and the test asserts the verdict, the rollback, and that play
    continues.
11. Qt: `between_frames` after `S9xMainLoop`; on-screen messages; panel additions (crash
    count, last crash, per-rule crashes, policy override, *Mark crash*, detection toggle).
12. Docs: README section, QA additions.

## Findings while building

- **STP stalls the frame hook.** After `STP` the core no longer reaches V=1, so the engine's
  frame count stands still. The guard treats only a count that goes *backwards* as a
  restart; a stalled count still gets checked, which is how hard crashes are caught.
- **STP also trips the core's deadlock watchdog**, since STP never adds cycles. The
  adapter reports the opcode at PC, so the verdict still says "CPU stopped (STP at …)".
- **Runaway-PC sampling rarely fires.** Every jump into I/O space or open bus we tried
  found its way back into ROM within a frame, so a once-per-frame PC sample never sees
  it. Those crashes show up as STP, NMI off, a frozen picture, or a self-reboot. The
  signal stays (it's cheap), but no test pretends it works.
- **Still screens are not crashes.** Frozen and black screens only count when the player
  pressed buttons at least 3 times and nothing reacted; otherwise every text box or menu
  would have rolled back while a rule was active.
- **Skipped frames keep the old picture.** The frozen check ignores frames that weren't
  drawn (frameskip, fast-forward).

- **Latent damage needs deeper rollbacks.** Running `low-wram-roulette` under the guard on
  real games (`corrupt-smoke … --guard`) caught real STP, NMI-off and even runaway-PC
  crashes, but the same NMI-off crash recurred every ~4.7 s until the guard gave up. The
  harmful byte had been written long before the crash, so every recent checkpoint already
  held it. Each crash in a row now goes 5 s further back (`kStepBackFrames`): in 5-minute
  runs Pocky & Rocky then survived 24 crashes and FE4 survived 18, with no give-ups.
- **NMI-off threshold raised to 5 s**: clean runs had natural stretches up to 2.7 s.

## Out of this phase
Bisecting a suspect set across repeated crashes (until the journal shows we need it), the
crash map (phase 4), sound CPU hang detection, and **detecting a self-reboot** (the CPU
re-entering the reset vector): a real crash mode the I/O-jump experiments turned up.
