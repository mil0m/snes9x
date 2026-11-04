# Functions, deeper: bodies, guards, names, changes and timers (plan, roadmap #58-#61 and #35)

**Status (2026-11-02):** done (QA.md 34-36); see *How it went*. Follows [the functions plan](./2026-11-01-functions-plan.md)
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

### #58 bodies and guards, #59 names

Built as planned. The Functions window got a details area for the selected function (its
name, *Protect* and *And what it calls*, the switch, *Show in ROM*), a *Size* column, and a
*Name* column that also says what you do with it (key, protected, guarded).

**Bodies (`--bodies`, 5 minutes of each demo, debug build):**

| Game | Functions | Median body | 90% | Largest | Code the map knows that's in a body | Time | 10 busiest guarded: shallow / deep |
|---|---|---|---|---|---|---|---|
| Goof Troop | 331 | 38 B | 121 B | 489 B | 78% | 9 ms | 560 / 590 B |
| Pocky & Rocky | 517 | 35 B | 121 B | 495 B | 58% | 13 ms | 1212 / 1212 B |
| FE4 | 1012 | 38 B | 133 B | 539 B | 73% | 28 ms | 452 / 635 B |
| FF6 | 204 | 54 B | 228 B | 1055 B | 85% | 9 ms | 1058 / 1227 B |

- Every function the call graph knows has a body; none hit the 4 KB cap; 1-12% leave through
  a jump table (open-ended).
- The rest of the code the map knows is interrupt handlers, code reached only by jumps
  (dispatch targets), and FF6/FE4 code reached with `JML`.
- A deep guard costs little: helpers are small and shared.
- **A bug found by the numbers:** the first run gave FE4 a median body of 3 bytes. An
  instruction whose width depends on M but whose length doesn't (`PHA`, `LDA $1801,X`), run
  only at 16 bits, wasn't followed, so most bodies ended at the first one after a
  `REP #$20`. A unit test now covers it; Goof's coverage went from 35% to 78%.
- Bodies are found again (on the emulation thread) at most every 5 s and only while the call
  log or the map grows: a few ms in the release build.
- **The guard works on the real core:** with the test ROM's function guarded with what it
  calls, a rule writing 32 random bytes a frame over all of its code for 30 frames changed
  none of its bytes, logged no write in it, and it kept counting every frame.

### #60 change a function, #35 redirection, #61 timers

Built as planned, in one window: *Change It…* (from the Functions window) lists the
function's lines that can change and what each can become; *Apply* / *Put Back* act at the
next frame, *Keep in Recipe* keeps a change on a hotkey, starting on or not, with an
optional timer. Redirection (#35) is one of a call's choices there. Flips became general
byte toggles (a switch is a one-byte one). A starter, **Function flicker**, shows #61's
*Back on after*.

**Changes inside the busiest functions vs anywhere (`--change`, unguarded, one change every
~8.5 s, 6 in a minute: a new number, the opposite branch, or the other half of a math pair):**

| Game | Inside the 64 busiest: running / moving / picture differs | Anywhere in code: running / moving / differs | Clean |
|---|---|---|---|
| Goof Troop (10 seeds) | 10 / 5 / 10 | 10 / 7 / 10 | 10 / 10 |
| Pocky & Rocky (10) | 9 / 7 / 10 | 10 / 9 / 10 | 10 / 10 |
| FE4 (10) | 10 / 5 / 10 | 8 / 8 / 9 | 10 / 10 |
| FF6 (5, 5 minutes) | 5 / 5 / 3 | 5 / 3 / 2 | 5 / 5 |

- Every change in both arms was run by the game ("ran a changed byte" in all runs), so the
  difference isn't dormant code.
- Inside the busy functions the game nearly always keeps *running* (29 of 30 on the first
  three games, against 28 of 30 anywhere) but more often stops *moving* (17 against 24): a
  change in code that runs every frame tends to stall the picture (a menu that no longer
  advances, a demo stuck on a frame) rather than crash. FF6 goes the other way, on 5 seeds.
- So the *inside busy functions* aim is the stronger option for chaos, not a gentler one.
  The point of #60 is the hand-picked change in Change It…, with the guard keeping the
  rest of the function clean, where a stall is put back with one click.
- **A measuring bug found on the way:** `headless::load_state` didn't clear the core's stop
  switch after a CPU deadlock (the app's `SnesCoreAccess::load_state` does), so in the first
  Pocky run every run after a deadlocked one looked dead, clean ones included. Fixed; the
  earlier `--kill` and `--swaps` numbers had no deadlock (their clean arms all ran), so they
  stand.
- **Two bugs found by tests:** a window flip's return byte was in its own guarded body, so
  the guard's cleanup would have turned the function straight back on (now what the window
  did counts as your own bytes); and a timer wrote its bytes even over a ROM version that
  holds something else there (timer flips now only touch bytes holding their own on or off).

**One kind at a time (`CORRUPT_CHANGE_KIND`, 10 seeds each; still moving after a minute,
inside the busiest functions / anywhere):**

| Game | Numbers | Branches (opposite) | Math pairs |
|---|---|---|---|
| Goof Troop | 5 / 6 | 7 / 1 | 1 / 7 |
| Pocky & Rocky | 2 / 5 | 7 / 9 | 10 / 10 |
| FE4 | 7 / 6 | 4 / 4 | 3 / 0 |
| All three | 14 / 17 of 30 | 18 / 14 | 14 / 17 |

- No kind is the gentle one: which survives depends on the game (Goof's branch flips stall
  it anywhere but rarely inside its busy functions; its math swaps the other way round).
  The guide doesn't promise any kind is safe; Put Back and the crash guard are the answer.

## #67 Names typed in the table

Mikael: *"Let's make it so we can add names to the functions in the function table!"* The
names (#59) could only be typed in the field under the table, and the table's Name column
mixed names with what you did to the function ("gravity — button 3 · protected"), so it
didn't read as the place to type.

- **The Name cell is where you type.** Double-click it (or select the row and press Return),
  type, Return. The cell shows everyone's names (yours first, then *scroll (erik)*); the
  editor starts from yours, since that's the only one you change. An empty name on the
  selected row shows a quiet *Double-click to name it*. Esc cancels; an empty name removes
  yours. The field under the table stays and the two agree.
- **What you did gets its own column**, *In play*: the button, *starts off*, the timer,
  changes, *protected* / *guarded*. Name is the wide column; In play fits its words.
- Nothing changes in how names are stored or shared.

Tests: the table shows names and In play apart; editing a cell starts from your name,
Return saves it through the source (and the field under the table follows), Esc doesn't,
an emptied cell removes it; only Name cells are editable.

### How it went (#67)

Built in an hour, test first. Two things the pictures and tests found: table cells are
editable by default, so with double-click editing on every column could be typed over (now
only Name cells carry the flag); and the *In play* column, sized once per re-sort, was too
narrow for a function protected after it ("prot…"), so it now grows when a cell outgrows
it. The hint on an empty, selected name uses the selection's own text colour for the window's
state (white on the Mac's grey inactive selection was hard to read).

## #71 What the instructions mean, on hover

Mikael: *"In the change in a function where one can see all operations, please add so one
can hover on operations to understand what they mean."* Change It… lists the function's
instructions as assembly (`ADC #$01`, `BNE $9012`, `JSL $C30006`): readable for someone who
knows the 65816, not for a player.

- **Hover an instruction** and a tooltip says what it does in plain words, then what its
  operand is: *Adds a number to A, the main register, plus the carry. Health, scores and
  positions often go through it.* / *#$01 is the number itself: 1.* Every one of the 92
  mnemonics has a line; every addressing mode explains its operand (a number, a fast
  variable, a table entry picked by X, a pointer, a jump table, where a branch goes).
- **Hover Can Become** and it says what that kind of change does to the game (a number, a
  decision, a sum, a call).
- The words live in `corrupt/instruction_help` (no Qt), so a test can check none is
  missing; the memory viewer's ROM hover can use them later.

Tests: every mnemonic in the opcode table has an explanation; each addressing mode's
operand is explained (immediates with their decimal value); each change kind has words;
the Change It… cells carry them as tooltips.

### How it went (#71)

Built the same evening, test first. The test that every one of the 92 mnemonics has words
(and that the opcode table really has 92) is what keeps it complete. Not done: the memory
viewer's ROM hover still shows bare assembly; it can take the same words.

## Changes that don't crash: what a change feeds into (#77)

Mikael (3 Nov): "We tried changing functions and most of the time they crashed. I'd like
suggestions somehow how to modify them without crash." #60's measurements already said no
kind of change is the safe one. The idea here: the risk isn't in the kind of change but in
**what the changed value feeds into**. On the 65816, `4 → 1` is harmless as a speed and
deadly as an index; flipping a forward branch takes another real path, flipping a loop's
exit loops forever; skipping a call is fine unless the caller reads its result.

**First a measurement, then UI.** `corrupt/change_risk.{h,cpp}`: `feed_of(option, body…)`
follows the changed value through the straight-line code after it and names where it goes:
*variable* (stored into RAM), *hardware*, *index* (`TAX`, `,X`, a jump table), *loop count*,
*loop bound* (compared, branch back), *condition* (compared, branch forward), *pointer*
(stored where the body reads a pointer, or into D/S), *argument* (to a call, pushed,
returned), *overwritten*; branches as *loop* or *forward*; a skipped call as *result used*
or *unused*; *unknown* when the trail goes cold. Unit tests for each.

`corrupt-smoke --risk [count] [trial]`: from one learned moment, single changes from the 64
busiest functions (a quarter each numbers, branches, math, skipped calls), one per 30 s
run, unguarded; each scored *moving*, *frozen* or *dead*, tallied per class. Numbers get
two arms: any new value, and nudged (±1, ×2, ÷2). Goof Troop, Pocky & Rocky, FE4.

**If the classes separate survivors from crashes:** Change It shows each line's class and
reason in words (*stored into $7E:0D40: low risk*, *becomes an index: high risk*), sorts low
risk first without hiding anything, offers nudges for numbers, and the chaos rules get an
aim of *low-risk changes only*. If they don't, the measurement says so and the next idea is
testing each option ahead (#52's run-ahead) instead.

### How it went (#77, the measurement)

`--risk 240 1800` on Goof Troop, Pocky & Rocky and FE4: 884 single changes from the 64
busiest functions, 767 of which the game ran (the rest never ran in 30 s and don't count).
Runs are deterministic: a second run with the same picks gave the same 767 outcomes.

**A single change rarely kills the game in 30 s of its demo**: 89% kept moving (numbers
83% for any value, 87% nudged; branches 93%; math 87%; skipped calls 94%). #60's runs
stacked six changes in a minute, which is where most stalls came from.

**What it feeds into separates them** (failed = frozen or dead after 30 s):

| Feeds into | Changes | Failed | Moving and the picture differs |
|---|---|---|---|
| variable (RAM, used as a value) | 105 | 4% | 31% |
| condition (compared, branch forward) | 122 | 5% | 20% |
| forward branch flipped | 136 | 7% | 35% |
| loop branch flipped | 25 | 8% | 48% |
| skipped call, result used / unused | 53 / 11 | 8% / 9% | 47% / 36% |
| unknown (trail went cold) | 102 | 9% | 17% |
| loop bound (compared, branch back) | 32 | 16% | 50% |
| scratch byte ($00-$1F) | 35 | 20% | 26% |
| index (`TAX`, `,X`, jump table) | 94 | 21% | 28% |
| argument (to a call, pushed, returned) | 34 | 38% | 9% |
| pointer (D, S, a pointer, DMA address) | 7 | 43% | 0% |

Grouped: **low risk 6% failed (27 of 461), risky 25% (50 of 204)**, four times as often,
on every game: Goof 6% vs 23%, Pocky 1% vs 33%, FE4 12% vs 21%. Low-risk changes aren't
dull: about a third of them leave a visibly different game. Two refinements came from the
first pass's failures: temporary bytes `$00-$1F` fail 16-20% where real variables fail
3-7%, so they're their own class; a value written to a DMA address or size is a pointer.
What doesn't separate: whether a skipped call's result is used (8% vs 9%), and nudging a
number instead of any value (87% vs 83%).

**What it can't see:** FE4 still fails 12% on low risk, mostly in one region of bank $1B
(its demo script code) where nearly everything fails: some functions are fragile as a whole.
That's what testing each option ahead would catch.

## What made flips feel like crashes, and the UI (#77, second round)

Mikael (3 Nov): "What we tried was to change the operations BEQ to BNE and so on." So the
question narrowed to branch flips in Change It, and three things were built and measured.

**Branch facts.** A flipped branch swaps which case goes where; both ways start from the
same registers and stack. `feed_of` now reads what the ways are: *unseen path* (one way
never ran while the tracer watched: flipped, it runs every time), *wait loop* (a short
loop back that only reads: flipped, it can wait forever), *width path* / *stack path* (the
skipped block or loop body leaves REP/SEP or the stack different where the ways join; a
REP…SEP pair, or a block that ends in a return or jump, doesn't count).

**Risk in Change It.** A *Risk* column (*Low · a value in memory*, *Medium · an index*,
*High · a call's input*, *Unclear · can't tell*), sortable, code order by default; the
selected line's reason in words and the measured share under the choices, not in a tooltip
(Mikael's rule: explain the detail, don't hide it). Levels come from the measurements:
under 10% failed is Low, 10-25% Medium, above High; fewer than 10 seen is said as *too few
measured*. The busiest functions get a line of their own (below).

**Low-risk aim.** `Aim::low_risk` (`"risk": "low"`): a rule picks only bytes where a
change is Low (a number's value bytes, a branch's or math instruction's opcode, a call's
whole instruction), from every known body, taken once a run like the busy code.

**Your own change, rolled back far enough.** When the guard blames a byte you changed
yourself (Change It, or the recipe's changes), it goes back to the newest checkpoint
before the game first ran it, as far as its 30 s of checkpoints reach, instead of just
before the crash.

### How it went (#77, second round)

**Branch flips alone rarely kill a game.** 533 flips from the 64 busiest functions
(`--risk 200 1800 branch`): 6% failed in 30 s. The new branch facts don't predict it:
unseen path 5% (235), wait loop 10% (74), width path 0% (12), stack path never seen. So
they're said in words but rated by what was measured (ways that never ran: Medium,
for what they do stacked; see below).

**The busiest functions are fragile as a whole.** 952 single changes of every kind with
the function each is in: in the 16 busiest functions 18% failed, further down 8%
(Goof 18% vs 6%, Pocky 8% vs 9%, FE4 26% vs 11%). Failures cluster in a few functions
(Goof's main loop around `$80:80B1`, FE4's `$1B:E08A` region). The tour showed it at once:
Goof's busiest function opens with `BEQ $83CE`, which kills the game when flipped. The
Functions window lists busiest first, which is where anyone starts; Change It now says so
for the 16 busiest.

**The real culprit behind "they crashed": the rollback.** With the guard on, a flip that
stops the CPU 12 s in was caught and put back, but the guard went back only 2 s, to a
moment that already carried the damage in RAM, and Goof stayed stuck: the picture changed
in 146 of the remaining 2,870 frames (clean: 2,271). Going back to before the game first
ran the change (13 s): 1,832, the game plays on. When the crash comes soon after the
change, the normal step back already reaches before it (pressed at 20 s, caught at 26 s:
both recover).

**Final measured levels** (single changes, 30 s, unguarded; 952 of every kind plus the 533
branch flips): variable 4%, condition 4%, unseen path 5%, result used 6%, unused 7%,
forward branch 8%, loop branch 10%, wait loop 10%, unknown 10%, scratch 16%, index 21%,
loop bound 28%, argument 37%, pointer 40% (10), loop count 6 of 7. Grouped: low 5% (567),
risky 26% (261).

**The low-risk aim, stacked** (`--change 20 6`: six changes a minute inside the 64 busiest
functions, unguarded, still moving at the end): any of them 33 of 60, low-risk only **45 of
60** (Goof 11 → 13, Pocky 12 → 17, FE4 10 → 15). A first cut that rated ways that never
ran as low gave 38 of 60: alone they're safe (5%), but several each send the game down
code it never ran, and that adds up, so they're Medium. The low-risk picks still change
the game: 59 of 60 ended on another picture than a clean run.

**Not done: testing each option ahead.** A *Test* button in Change It that plays the
change a few seconds ahead, unseen (#52's `RunAhead::play`), would catch the fragile
functions directly; its crash detector only takes definite crashes, and many failures
here are freezes, so it needs the freeze check first.


## Try it before it lands (#78)

Mikael (4 Nov) picked this as the next package. #77 gives each line in Change It a risk,
but that's an average over changes of its kind. This plays **the exact change** a few
seconds ahead, unseen, from this moment of this game, and says what happened.

**The warning in *Tried and removed*.** #55 tested a changed byte from its first use and
failed because crashes come seconds later, from damage already done. Two things differ
here. #55 judged against a definite crash, and many of #77's failures are freezes, which
it couldn't see. And here the answer goes to the player as information beside the risk;
nothing is kept or put back on its own. Still, if a short test can't see most failures,
it isn't worth a button. So the measurement comes first and decides.

**Step 1, the freeze check, measured offline.** A run ahead is deterministic, so a test of
*N* seconds is the first *N* seconds of #77's 30 s runs. `corrupt-smoke --foresee [count]
[trial]` records every frame of a clean run and of each single change from the same moment:
the picture, NMI, a halted CPU, a reboot, and the frame the changed bytes first ran. Then
it scores candidate checks at 3, 5 and 10 s against the 30 s outcome:

- **definite**: what #52 sees today (CPU stopped, deadlock, runaway, reboot);
- **+ still picture**: the last second of the window shows one picture while the clean run's
  picture changed in that same second. The clean run is the control: a menu nobody touches
  is still in both, so it's no verdict. Real play has a player who stops moving too, which
  is why it's compared with a control rather than with a threshold;
- **+ NMI off**: the frame interrupt stays off for the last second while the clean run's is on.

For each: how many of the 30 s failures it catches, how many survivors it wrongly calls
failed, and how many changes didn't run in the window (no answer). Compared with the Risk
column on the same changes. **Go** if a 5 s check catches most failures (half or more) with
few false alarms (under 1 in 20), and does better than the Risk column. Otherwise this
section ends with the numbers and nothing is built.

**Step 2, if it goes.** `FreezeCheck` in `corrupt/` (the check that won, with a control run),
and `RunAhead` gains it as an option (#52 keeps definite only until measured there).

**Step 3, the button.** *Test* beside the choices in Change It. It plays the selected
change *N* seconds ahead from now, with the same buttons held as now, and once without it
(the control). Then it shows, under the risk:
*Tried 5 s ahead: still running; the picture differs from without it* /
*froze after 1.2 s (the picture stopped; without the change it kept moving)* /
*stopped the CPU 0.4 s in* / *the game didn't run this code in 5 s, so it says nothing yet*.
The game pauses for the test (a fraction of a second). A result is for this moment; it
says so.

**Maybe step 4.** The low-risk aim could test each change ahead before it lands, like #52
does for ROM rules. Only if it beats the 45 of 60 the aim gets alone.

### How it went (#78, step 1: does a short test see the failures?)

`--foresee 240 1800` on Goof Troop, Pocky & Rocky and FE4: the same 767 single changes as
#77 that the game ran within 30 s, each recorded frame by frame with the clean run beside it.
The window counts from the moment of the change, as a test from now would.

| Check, at 5 s | Failures caught | Survivors called failed | No answer (code not run yet) |
|---|---|---|---|
| definite (what #52 sees today) | 10 of 79 | 0 of 610 | 78 of 767 |
| NMI off for the last second, clean on | 31 of 79 | 2 | |
| **still picture for the last second, clean moving** | **64 of 79 (81%)** | **4 (0.7%)** | |
| still picture for any second of the window | 64 of 79 | 14 | |
| the Risk column (Medium or High) | 53 of 86 (62%) | 222 of 681 (33%) | none |

Per game, the still check at 5 s: Goof 20 of 27 caught, 1 false; Pocky 17 of 19, 0; FE4
27 of 33, 3. At 3 s it catches 53 with 4 false and more changes haven't run yet (118); at
10 s 71 with 14 false. **5 s is the window.**

**Why this works where #55 didn't.** #55 judged against definite crashes, and those really
are rare and late: here they're 10 of 79. The failures from a function change are mostly
freezes, and they show fast: of 86 failures, the first sign (a still picture, NMI off, a
stopped CPU) came within 3 s of the change's first use in 69, 3-10 s in 8, later or never
in 9. The still picture has to be judged against a run without the change, or a menu nobody
touches would look frozen; with the control, survivors almost never trip it.

**Go.** The check is *definite, or a still picture for the window's last second while the
run without the change moved at least 3 times in it*. What it can't do: answer for code
that doesn't run in the 5 s (1 in 10 here), or for a failure that builds up later (1 in 10
of failures). All of it is attract mode; real play has a player pressing buttons, which the
test holds as they are now.

### How it went (#78, steps 2 and 3: the check and the button)

**The real thing matches the measurement.** `--try 240 1800`: `try_change` itself (two runs
ahead, drawn but unseen, through `RunAhead`) from the same moment as `--foresee`, against
the 30 s outcome. Of the 79 failures whose code ran within 5 s it called 64 froze or crashed
(Goof 20 of 27, Pocky 17 of 19, FE4 27 of 33); of 610 survivors it called 6 failed (Goof 1,
FE4 5); 78 changes got *didn't run this code* (7 of them failures). About 0.75-1 s per try
for both runs, three games at once under `nice`.

**One thing the plan didn't know: unseen frames aren't drawn.** #52's run ahead skips
rendering for speed, so there was no picture to judge. `run_unseen_frame(true)` draws into
the core's screen, and the Qt front end shows nothing while unseen, as it already did for
sound.

**In the app.** The tour opens Change It on Goof Troop's busiest function, where the first
line, `BEQ $83CE`, is *Low · a block it skips* (flips like it fail 8 in 100). Tried: *froze
after 2.1 s: the picture stopped, and without the change it kept moving*, which is the flip
#77 found kills the game. The average and the exact change disagree, and both are in view.

**Not done:** step 4 (the low-risk aim testing each change ahead) waits for play-testing of
the button; #52 keeps definite crashes only.
