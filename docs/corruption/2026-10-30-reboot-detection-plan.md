# Reboot detection: implementation plan (roadmap #20)

Spec: [`2026-10-30-ram-corruption-design.md`](./2026-10-30-ram-corruption-design.md),
*Crash detection*. Phase 2's findings
([plan](./2026-10-30-phase2-crash-rollback-plan.md)) found the gap: runaway code often
lands back in the game's own reset code, the game boots again, and from outside that
looks like a normal title screen, so nothing is caught.

**Status (2026-10-30):** being built.

## How it's detected

- **Watch the reset handler's first instruction.** On game load, read the reset vector
  ($00:FFFC) and the opcode at its target. The 65816 opcode handlers are global tables of
  function pointers (`S9xOpcodesE1`, `…M1X1`, `…M1X0`, `…M0X1`, `…M0X0`, `…Slow`), so that
  one opcode's entry in each table is wrapped with a check, and every other opcode runs
  untouched. No core source file changes, so the "hook pointer only" rule holds.
- **Which executions count.** The wrapper first compares the low 16 bits of the PC (cheap),
  then checks that the instruction is really the reset handler's first byte through the
  memory map, so a jump into a mirror bank ($80:8000 for $00:8000) counts too.
- **A reboot is the handler running in native mode.** A real power-on or a reset from the
  app enters the handler in emulation mode (the E1 table): those are never counted. A
  crashed game jumping to its own reset code is nearly always in native mode.
- **A heuristic signal.** Like NMI-off and the frozen picture, it is ignored when no
  corruption happened just before, so a game's own soft-reset button combo (some games
  jump to the reset code on L+R+Start+Select) doesn't roll back a clean game.
- **What it misses:** a reboot that jumps past the handler's first instruction (for
  example straight to the code after a `JML` to fast ROM), and a game that switches to
  emulation mode before jumping. Both are documented, not handled.

## Tasks

1. `FrameSignals::reboots` (a running count) and a `Reboot` signal in `CrashDetector`:
   caught at once when the count goes up, `heuristic`, switchable in `DetectorConfig`
   and the recipe's `crash_detection` (`"reboot"`). After a reset the detector takes the
   next count as its baseline. Unit tests.
2. The watcher in the core glue (`core_access.cpp`): install on game load, re-install for
   a new game, uninstall when unloaded; `SnesCoreAccess::sample()` reports the count.
3. A generated test ROM that reboots on cue (`JML` to its reset code in native mode) and
   real-core tests: a boot isn't a reboot; a console reset isn't a reboot; a reboot is
   caught, rolled back, and play continues; with no corruption before it, it's ignored.
4. `corrupt-smoke --guard` reports reboots on real games; docs and QA.
