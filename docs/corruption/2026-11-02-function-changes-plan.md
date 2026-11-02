# Functions, deeper: bodies, guards, names, changes and timers (plan, roadmap #58-#61 and #35)

**Status (2026-11-02):** planned. Follows [the functions plan](./2026-11-01-functions-plan.md)
(#26 call graph, #34 kill switches). Asked for by Mikael: "Set what type of changes should
happen every x second", "corrupt the actual functions as well? To make them behave
differently?", "guard the related ROM parts so we don't touch them with other corrupt
features, and visualise that", and names shared between him and Erik. Everything here is
plain code fed by the tracer and by what players type: no feature needs an LLM (the rule in
CLAUDE.md).

Five pieces, built in this order, because the later ones need the earlier:

| # | Piece | In short |
|---|---|---|
| 58 | Function bodies and guards | Where each function's code ends; functions in play are guarded from every other rule and from paint; a Functions overlay in the ROM views |
| 59 | Shared names | `names.<you>.json` per game in the library: name a function once, Erik sees it after a pull |
| 60 | Change a function | Its numbers, its branches, its math, skip one of its calls; live, on hotkeys, kept in the recipe; code rules can aim "inside busy functions" |
| 35 | Function redirection | One of its calls goes to another function of the same kind (part of *Change a function*) |
| 61 | Timed switches | "Off for 2 s every 10 s", flicker; a kill rule that puts functions back after a while; the kill rule keeps learning |

## #58 Function bodies

**What a body is.** The bytes of a function's own code: from its entry, every instruction
it can reach before it returns, without going into the functions it calls. Found by walking
the code map (everyone's), never by running anything:

- start at the entry; an instruction is only followed if the code map knows it as an opcode
  (so the walk never wanders into data), with its length as it ran (M/X widths from the map);
- a conditional branch: both ways; `BRA`/`BRL`: its target; `JMP $abs`: its target in the
  same bank (worked out from the CPU address the map knows, and checked to land on the same
  ROM chunk);
- a call (`JSR`, `JSL`): past it, if the instruction after it ran (a function that reads
  data after its call skips it, so that byte isn't code and the walk doesn't count it);
- stops at returns (`RTS`, `RTL`, `RTI`), indirect jumps (`JMP (a)`, `JMP (a,X)`, `JML`),
  `JML $long`, `BRK`, `STP`;
- at most 4 KB per function (a walk that reaches it is marked *truncated*).

Bodies are worked out between frames every few seconds when the code map or the call log
grew (with the jump tables, which are found the same way), only for functions the call log
knows. A function whose walk meets another's entry keeps going: shared tails belong to both.

**Design choices.**
- *Static, from the map.* No tracer cost. A path the game never took isn't in the map, so it
  isn't in the body, which is right: a guard keeps exactly the code that's known to run.
- *Bodies, not callees.* A function's helpers are shared with others; guarding them with it
  would quickly guard much of the ROM. A *deep* guard (helpers too) is an option per function.
- *Data tables aren't in the body.* A gravity curve read with `LDA $table,X` is data
  elsewhere in ROM. Tying data reads to functions is a later step (#27, table shapes).

## #58 Guards

**What's guarded.** A function is *in play*, and its body guarded, while:
- the recipe has a switch for it (#34), or a change in it (#60), or it's ticked *Protect*;
- or it was switched off from the Functions window (live, not kept yet).

Functions turned off by a kill *rule* are not guarded: that's chaos, not a choice.

**What the guard does.**
- **Rules skip guarded bytes**, as they skip the header and vectors today: every op, area
  ops, code ops, pointer swaps. Skipped writes aren't logged.
- **Paint skips them** too, live and when a recipe's patches apply at a run's start; the
  status line says what was kept and why: "kept 214 bytes: guarded by $C0:9000 (gravity)".
- **Your own changes go through**: switches, changes in the function and pins are what the
  guard is for, so the guard never blocks them.
- **Going up, it cleans:** bytes in the body that other corruption already changed are put
  back to clean (not pins, not the function's own switch and changes), so the function is
  the game's code plus your change, nothing else.
- **Safety nets still act:** the crash guard and the instant revert can still put back your
  own change if it breaks the game.

The engine gets a second mask beside `locked`: `guarded`, which rules and patches skip and
switches, changes and pins don't. The session rebuilds it when a switch, change or protect
is added or removed, when a function is flipped from the window, and when bodies are
recomputed.

**Protect** is a new recipe list, `"protect": [{"entry": "0x009000", "deep": false}]`,
written by the window's *Protect* tick.

## #58 The Functions overlay

A new overlay in the memory viewer, **Functions (ROM)**, in the hex, pixel and tile views:
- known function bodies light; guarded bodies strong; an entry that's off strongest;
- hover: "in $C0:9000 gravity (Erik) · 46 bytes · guarded · off", on top of what hover
  already says (the instruction, changes, crash map);
- *Show in ROM* in the Functions window opens the viewer on the ROM with this overlay and
  the body selected.

The panel's ROM line: "331 functions known (2 off, 3 guarded)".

## #59 Shared names

Names are what makes the Functions window a map of the game. Without an LLM they come from
people, so they're shared like everything else the tracer learns:
- `names.<you>.json` in the game's folder, `{"format": "snes9x-names", "version": 1,
  "functions": [{"entry": "0x009000", "name": "gravity"}]}`, in entry order; each person
  writes only theirs;
- loaded with everyone's: the window shows "gravity" for your own name, "jump (Erik)" for
  someone else's, both when both named it;
- written when you rename (it's small), and reloaded with the rest when the library changes;
- the window's *Note* field becomes **Name**, saved to the names file; a switch kept in the
  recipe takes the name as its note.

This is the first part of game profiles (#41), made by players.

## #60 Change a function

**What can change.** From the body, every instruction that can be changed into something
of the same length that still runs. Each one is a *change*:

| Kind | From | To | Example |
|---|---|---|---|
| **Number** | the value of `LDA/ADC/SBC/CMP/AND/ORA/EOR/BIT/LDX/LDY/CPX/CPY #` (8- or 16-bit as it ran) | any value | gravity 4 → 1 |
| **Branch** | `BNE BEQ BCC BCS BPL BMI BVC BVS` | its opposite, or *always* (`BRA`) | "if on ground" ⇄ "if in air" |
| **Math** | `ADC⇄SBC`, `INC⇄DEC`, `INX⇄DEX`, `INY⇄DEY`, `ASL⇄LSR`, `ROL⇄ROR`, `CLC⇄SEC`, `AND⇄ORA` (same addressing mode only) | the other one | speed goes the other way |
| **Skip a call** | `JSR $abs`, `JSL $long` inside it | `NOP`s | its helper stops being called from here |
| **Redirect a call (#35)** | the same | another learned function: near calls to near functions in the same bank, long to long | the enemy runs the player's movement |

Instructions run at both widths are left out (as with code ops). Mixed functions can be
changed but not switched off.

**In the recipe**, `"changes": [{"function": "0x009000", "at": "0x009012", "kind":
"number", "value": "01", "was": "04", "hotkey": 2, "start_on": true, "note": "moon
jump"}]`; `value` and `was` are the bytes (one to four). Like a switch: its state is the ROM
bytes (changed when they hold `value`), `was` skips it on another ROM version, a hotkey
toggles it, `start_on` writes it as a run starts.

**In the engine**, a switch and a change are the same thing underneath: bytes that toggle
between two values. `Engine::Flip` becomes `{at, on bytes, off bytes}`; a kill switch is a
one-byte flip. Flips still happen at the start of the next frame, through the ROM keeper (a
rollback undoes them), never logged as a rule's write.

**In the app**, *Change It…* in the Functions window opens one function: its body as a
list (address, instruction, what it can become), and below it an editor for the selected
line: a number field, *Flip* / *Always*, *Swap to SBC*, *Skip the call*, or a list of
functions to call instead (busiest first, with names). *Apply* changes it at the next
frame, *Put Back* undoes it, *Keep in Recipe* keeps it with a hotkey and whether it starts
on.

**For chaos:** code rules (opcode swap, branch flip, NOP out) and aimed byte rules get a
new aim, **inside busy functions**: only bytes in the bodies of the 64 busiest functions.
Guarded bodies are skipped as everywhere.

## #61 Timed switches

- **A timer on a switch or a change**: "off for *A* s every *B* s" (a change: "on for *A* s
  every *B* s"). It's counted from the run's start, so a run replays the same; the window's
  switch row gets the two fields. Short values give a flicker.
- **A kill rule that puts back**: *Put back after* N seconds; each function it turns off
  comes back on its own (through the ROM keeper, not logged as a write). With a trigger every
  10 s and put back after 5, a different function is missing half the time.
- **The kill rule keeps learning**: it took its list at its first firing; while that list is
  shorter than the 64 it picks from, it takes it again on each firing, so a first play of a
  game isn't stuck with the first few functions seen.

## Tests

- **Unit (fakes):** bodies: a straight function, both sides of a branch, `BRA`, `JMP $abs`
  in the bank and out of it, stops at returns and indirect jumps, a call's fall-through only
  when it ran, unknown bytes, the size cap, a shared tail; guards: rules, area ops, code ops
  and pointer swaps skip guarded bytes, paint and patches skip them, switches and changes
  don't, going up cleans the body but not pins; names: file round trip, bad entries dropped,
  merge with who; changes: listing for each kind (widths, both-width left out, math pairs
  only in the same mode, calls by bank and type), multi-byte flips, start-on, mismatch
  skipped, recipe JSON round trip and errors; timers: off and on at the right frames, through
  a rollback; kill put-back; the kill list re-taken while short; the *inside busy functions*
  aim.
- **Real core (test ROM):** a function that adds 1 to a counter and calls a helper: its
  body is found exactly; guarded, a rule over all its code leaves it alone and it still
  counts; changes: the number (adds 3), `ADC`→`SBC` (counts down), skipping the helper
  (its count stops), redirecting the helper call to another function (that one counts
  instead), a branch flip; a timed switch stops and starts the counter on schedule.
- **Qt offscreen:** the window's new columns, Protect, Name, timer fields, Change It… and
  its editor, Show in ROM; the overlay; the UI tour gets pictures.
- **corrupt-smoke:**
  - `--bodies`: body sizes, how many functions have a complete body (no truncation), and
    the time it takes, on Goof Troop, Pocky & Rocky, FE4 and FF6.
  - `--change [seeds]`: unguarded, a random number change, branch flip or math swap in one
    of the busiest functions every ~8 s for a minute, against the same kind of change
    anywhere in code: still running, still moving, picture differs.
  - The guard: a code-drip recipe with a few functions guarded: writes landing in guarded
    bodies (must be 0) and whether those functions still run.

## How it went

(Filled in when it's built.)
