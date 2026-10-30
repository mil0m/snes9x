# Crash map: implementation plan (roadmap #19)

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md),
"Crash map (learning safe ranges)".

**Status (2026-10-30):** done (tasks 1–7), pending QA.md section 17.

Goal: learn, per game, which memory crashes the game when corrupted, show it in the memory
viewer, and let rules stay out of it on their own, so corruption runs last longer and get
weirder instead of ending in a crash.

## Decisions

- **Scored per 16-byte block, not per byte.** Rules mostly pick fresh random addresses, so
  any one byte is hit about once a session: too rare to learn from. What crashes games
  comes in areas anyway (the stack, pointer tables, code copied to RAM, DMA tables). A
  block of 16 is small enough to avoid cheaply and makes the file small and mergeable.
- **Two counts per block.** *Crashes*: writes inside a crash's blame window (the same
  window the guard blames rules from: from the checkpoint it rolled back to until the
  trouble started). *Survivals*: writes followed by 15 s (900 frames) without a crash.
  Writes undone by a rollback count as neither.
- **Counts, not shares of the blame.** One crash with 300 writes in its window gives all
  300 blocks a crash. The innocent ones keep surviving afterwards; the guilty ones crash
  nearly every time. Splitting the blame (1/300 each) would never let a culprit stand out.
- **Danger** = crashes / (crashes + survivals + 2), a crash rate with a small prior.
  **Hot** = at least 2 crashes and danger of at least 0.4 (0.5 at first; see findings).
  One crash never makes a block hot. Starting values, to tune from play.
- **`avoid_hot` drops hot addresses** after a rule picks them (it doesn't draw new ones),
  so rules without it replay exactly as before, and avoiding rules do a little less in
  dangerous memory. With `avoid_hot`, replays only match while the map is the same. It
  steers the picked addresses; a copy or swap's second address isn't checked.
- **Files**: `crashmap.<user>.json` in the game's library folder, one per person like the
  journal, holding only that person's counts. Every `crashmap.*.json` is read and summed
  when the game loads. Hex block keys, sorted, so diffs stay readable. Saved after each
  crash, every minute while counts change, and when the game closes.
- **In the viewer**, a **Danger** overlay (red) next to Activity and Hits. In the rule
  editor, an **Avoid dangerous bytes** checkbox. In the panel, how many blocks are hot.

## Tasks

1. `corrupt/crash_map.{h,cpp}`: `CrashMap` (counts per target and block, add, merge,
   danger, hot, a `HotMask` for the engine) and JSON read/write. Tests.
2. `CrashLearner`: collects writes from the engine's log; `crash(after, through,
   restored)` turns the window's writes into crashes and forgets undone ones; writes
   older than 900 frames become survivals; a restart or rewind drops what was undone.
   Tests.
3. Guard: `GuardOutcome` reports its blame window. Test.
4. Rules: `avoid_hot` skips hot addresses (fresh and sticky picks); the engine holds the
   hot mask. Tests, including that a rule without it is unchanged.
5. Library paths, session wiring (learn between frames, save, hot mask to the engine,
   danger in the viewer's snapshot), `corrupt-smoke --guard` printing what it learned on
   a real game.
6. Qt: the Danger overlay, the rule editor checkbox, the panel line. Tests and PNGs.
7. Docs: README, QA.md section 17, findings here.

## Findings while building

- **Long blame windows smear the blame.** On repeat crashes the guard rolls back up to
  25 s to get past latent damage; counting every write in that stretch made bystanders
  hot. The learner now only blames the last 180 frames (3 s) before the trouble.
- **Crash loops teach nothing but blame.** A first test poked a fixed crash value: the
  re-roll couldn't get past it, the run rolled back and crashed at the same frame twenty
  times, and since no write ever got 15 s old, every bystander turned hot. With play moving
  on (the Skip policy in the test), sixteen busy harmless blocks stay cool while the
  culprit turns hot. Real re-rolls usually move on; the README warns about loops.
- **Counts per episode, not per write.** A crash counts once per block, and survivals at
  most once per block every 5 s, so a rule hammering one block doesn't drown its crashes.
- **Real games learn slowly, and only some help.** `corrupt-smoke --learn` (five 5-minute
  sessions of the low-WRAM roulette, then three held-out seeds with and without avoiding):
  - FE4: 147 of 509 blocks were in some crash window, a few stood out (0x0320: 8 crashes,
    1 survival). With hot at 0.5, held-out crashes went 22 → 20; at 0.4, 22 → 17 while
    avoiding just 6 blocks. Hot is now 0.4.
  - Pocky & Rocky: crashes spread over 82 blocks with no clear hot spot; 16 → 16.
  - Reusing the learning seed instead (23 → 3) only showed that avoiding the exact writes
    that crashed avoids those crashes. The held-out seeds are the honest number.
- **Ideas for later, from the data:** a relative score (a block's crashes against how
  often its neighbours were blamed alongside it) could pick culprits out of crash loops;
  the guard's re-rolls are natural experiments (a crash that goes away when one rule's
  writes change) that could feed the map more than co-occurrence; #21's bisecting blame
  would sharpen it most.

