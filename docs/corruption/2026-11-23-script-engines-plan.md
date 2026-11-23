# Script engines: find them, then corrupt scripts in their own words (roadmap #43, #44)

Mikael, 23 Nov: start on #43 and #44. RPGs run their events, battle AI and text as bytecode:
an interpreter reads a command byte from a script in ROM and jumps to that command's
handler. #43 finds those interpreters, their commands and each command's length while you
play; #44 corrupts the scripts in the game's own vocabulary: a command becomes another
command of the same length, or a command's arguments are nudged. Opcode swap, one level up.

## What a first look found (prototype, 23 Nov)

A ring of the last 8 ROM data reads in the core's existing marking path (only while the
tracer records), and at every indirect jump the tracer notes the reads before it. A
dispatch is explained when the jump's index is `stride x command + bias` for one of those
reads: X for `JMP/JSR (a,X)`; for a jump through a RAM pointer (`JMP (a)`, `JML [a]`) the ROM
offset of the table entry the game copied the pointer from (the newest read that holds the
target's address). `corrupt-smoke --scripts`, 12,000 frames of pseudo-play each:

| game | indirect-jump sites | engines | |
|---|---|---|---|
| FF6 | 15 | 2 | `$C0:9A8E` through RAM: 100% of 4,439 dispatches, 52 commands from 226 places in `$CA0000-$CDFCC2` (the event scripts). `$C0:77FE`: 98%, commands `$CC-$FF` (object actions) |
| FE4 | 89 | 3 | an event engine through RAM (100% of 642, 27 commands), two indexed ones (100%) |
| Goof Troop | 55 | 0 | |
| Pocky & Rocky | 29 | 0 | |

The command lengths it could tell for FF6 (from where the next command started) match the
event command reference as far as checked: `$35`, `$3D`, `$41`, `$B0`, `$D2` 2 bytes, `$37`,
`$3F`, `$40`, `$48` 3, `$6B` (load map) 6, `$49` and `$91-$95` 1. 25 of 52 commands had one
after 12,000 frames; the rest ran too rarely or always jump.

Dispatches the rule doesn't see: a pushed address and `RTS`/`RTL`, and pointers built in
code. Neither FF6's nor FE4's engines needed them.

## Design choices

Revised after a review of the first draft (Fable, 23 Nov); what changed is noted.

1. **The core change stays tiny**: 16 ROM offsets and a counter, written only inside the
   existing ROM marking, which runs only while the tracer records. DMA reads don't count.
   The counter is reset when the tracer starts. (Review: 8 deep loses a command followed by
   more than 7 reads.)
2. **The tracer notes a dispatch only at indirect jumps it already intercepts.** No new
   per-instruction cost; the cost is measured in the app's normal recording.
3. **Only real frames and the clean ROM teach it.** `note_jumps(false)` (test-ahead, Try 5 s
   Ahead, F8's check) pauses the script log too, and command values and table entries are
   read from the clean ROM, not the live one: a byte a rule changed must not become a
   command, and a swapped table entry must not look clean. (Review: the prototype did both.)
4. **The rule is (stride, bias), voted over every recent read**, strides 1 to 4: per
   dispatch the newest read that fits is the command. (Review: a fixed read slot loses
   engines that read a varying number of bytes before dispatching; stride 4 tables are
   common.)
5. **Candidates, not a frozen rule.** Per site a bounded table of candidate rules (the 64
   with the most dispatches), each with its own counts: dispatches, per command its handler
   and runs, and the places (ROM offsets) it ran with each place's steps. Nothing is
   decided while recording; `find_script_engines` decides when asked, so early ambiguity
   (a title loop running two commands, where several strides fit) can't lock in a wrong
   rule. Merging two logs adds candidates rule by rule, so both players' logs mean the same.
   (Review: freezing after 256 dispatches locks wrong rules and makes merges meaningless.)
6. **An engine is a candidate, not a site.** For an indexed jump the table base is fixed, so
   the candidate must explain 90% of the site's dispatches. A jump through a RAM pointer is
   often a shared trampoline (FE4's `JML [$0024]` serves a dozen dispatchers), so there each
   candidate stands on its own counts. The bias is then the table's base, which keys the
   engine. (Review.) Every engine needs at least 20 dispatches, 3 commands (5 through RAM),
   8 places, twice as many places as commands (a script uses its commands again; a walk over
   records doesn't), and 60% of its steps forward by 1-32 bytes (a script is read forwards).
   Those last three were set from measurements, see *How it went*.
7. **Lengths per place.** Interleaved scripts (FF6 runs several event threads and many object
   scripts a frame) make the step from one dispatch to the next noisy, so each place keeps
   its own steps (1-32 bytes). A command's length is the mode over its places when three in
   four agree, from at least 3 runs. A command whose places also show other steps is
   **branchy** (a conditional branch: steady when not taken, elsewhere when taken).
8. **What #44 may touch:** a place whose own step equals its command's length, of a command
   with a known length that isn't branchy. Branchy commands are never picked and never
   become the replacement. (Review: a 4-in-5 length, or a branch's argument, moves the next
   command or sends the interpreter into data.)
9. **No walk (yet).** Walking on from a seen command to find more of a script was built and
   measured first: on 36,000 frames of FF6 and FE4 it found nothing, because after a seen
   command the next one was either seen too or of a command whose length isn't known. Taken
   out; #44 works on places seen run.
10. **Saved in the library**, one file per person and game (`scripts.<user>.json`) with a hash
   of the clean ROM (a revision moves every offset), next to the other learned files; Who's
   Here lists it.
11. **#44's ops**, in the rule editor's *Code: the ROM only* group:
   - **Swap script commands**: picks allowed places and gives each another allowed command of
     the same engine and length. The script's shape stays.
   - **Nudge script arguments**: adds a small amount to the argument bytes of allowed seen
     places: another dialogue, map, character, event bit or position.
   - **Aim at one engine** when there are several (FF6: events or object actions).
   - Both greyed while the game has no engine, saying why ("Play a while: no script engine
     found in this game yet").
12. **Safety as for any ROM change**: changed script bytes are ROM data, so the crash guard
   blames them by use and test-ahead tries live changes first.
13. **Seen in the app**: the ROM page says what was found (*2 script engines: 62 commands, 25
   with a length*); the dossier (#42) lists engines and commands for naming; the Memory
   page's ROM colours get *Scripts* (places).

## Steps

1. **#43 in the engine**: `ScriptLog` (aggregated, merge, file format), `find_script_engines`,
   the walk. Unit tests with fake dispatches: a clean engine, a fixed table lookup (one
   place: not an engine), a stride-3 table, a through-RAM engine, a jumping command with no
   steady length, merge. A real-core test ROM with a tiny interpreter (indexed and through
   RAM) that the tracer must find, with its commands' lengths.
2. **Measure**: `corrupt-smoke --scripts` on FF6, FE4, Goof, Pocky (longer play), the walk's
   precision on FF6, and the tracer's cost with scripts noted against without.
3. **#43 in the app**: the tracer feeds the log while you play, the library file, the ROM
   page line, the dossier, the Memory page's colours. UI tour.
4. **#44**: the two ops, recipe format (`script_swap`, `script_nudge`, `engine`), the rule
   editor, a starter (*Script swap*). Measured on FF6 and FE4 with four arms on the same
   places: swaps, a random *valid* command of any length (does the length rule buy
   anything?), random bytes, and clean, plus the existing byte rules on the same script
   range. The denominator is changed commands the engine then ran (a changed event may not
   run in a minute); then still running and moving, picture differs from clean, new code
   run. Scenes with scripts running: events (FF6's intro), dialogue and a battle (FF6's
   battle AI in bank `$CF` hasn't shown up yet: find out whether pseudo-play never reached a
   battle or it dispatches another way). Report what it says, including if swaps don't beat
   the controls.
5. Docs: GUIDE (*Corrupting the game's scripts*), FEATURES, QA, the docs page gets a card,
   *How it went* here.

## Not now

- Pushed-address dispatch (`PEA`/`PHA` + `RTS`) and pointers built in code: neither showed up
  in FF6 or FE4. Measured again if a game's engine is missed.
- Engines that mask the command (`AND #$7F`, a flag in the high bit) or run from RAM: the
  rule doesn't see them.
- Text engines that print most bytes directly and dispatch only control codes: the control
  codes are found, the plain characters aren't commands. Corrupting text characters is the
  existing byte rules on the script bytes.
- Naming commands (*load map*, *show dialogue*): the map-game skill can do that from the
  dossier; the app doesn't need it.

## How it went

### Step 1 and 2: finding engines (#43)

Built: the core's ring of the last 16 ROM data reads (inside the existing marking, reset when
the tracer starts), `ScriptLog` (candidate rules per site, places with their steps, merge, the
file), `find_script_engines`, and the tracer noting every indirect jump with clean-ROM values
(paused with `note_jumps(false)`). Ten unit tests with a fake interpreter (indexed, through
RAM, a stride-3 table after an ambiguous start, two engines on one trampoline, a branch,
waits, a busy site full of coincidences, merge, the file) and two real-core tests on a test
ROM with two interpreters (`build_script`): both found with every length right; paused frames
note nothing; a script byte a rule changed in the live ROM is still logged as the clean one.

**Telling engines from coincidences took three rules, each from a measurement.** With every
candidate on a trampoline standing on its own counts, Pocky showed 16 "engines" and FE4 a
dozen at one shared jump: 3-11 commands, almost no lengths. What a script has and they don't:

| measure | real engines (FF6 2, FE4 5) | coincidences |
|---|---|---|
| steps forward by 1-32 bytes | 64-98% | 0-50% |
| commands through RAM | 10-52 | 3-4 (once the above filtered) |
| places per command | 2.3-17 | 1.0-1.7 |

A first version counted a command run again at the same place (a wait, dispatched every frame)
as going elsewhere: FF6's event engine read 6% forwards; ignoring repeats it reads 82%.

**36,000 frames of pseudo-play** (`corrupt-smoke --scripts`):

| game | indirect-jump sites | engines | commands with a length | cost of the log |
|---|---|---|---|---|
| FF6 | 30 | 2: events (52 commands, 25 usable, 240 places), object actions (10, 7, 171) | 32 of 62 | +2% |
| FE4 | 108 | 5 (26, 4, 28, 13 and 6 commands) | 36 of 77 | +4% |
| Pocky & Rocky | 31 | 0 | | +6% |
| Goof Troop | 76 | 0 | | +10% |

The cost is over the tracer and jump noting alone (about 2,500 frames a second either way, so
under 0.05 ms a frame at 60). The log kept 7,700-35,000 candidate rules per game, almost all
coincidences with a dispatch or two each; only those with 20+ go in the file.

Pseudo-play never reached a battle in FF6 (bank `$CF`'s AI scripts aren't in these runs), and
reaches little new script after the first minutes: FF6's event engine went from 226 places at
12,000 frames to 240 at 36,000. Real play will teach far more.

### Step 3: in the app (#43)

The session's tracer feeds the script log while you play (clean ROM, paused with jumps and
calls), keeps it like the jump log (`scripts.<user>.json`, saved once a minute when it learned
new places, with the clean ROM's CRC; Who's Here lists it), and finds engines every five
seconds. The ROM page says *2 script engines found: 63 commands, 28 with a known length*; the
Memory page's *Scripts* colours mark usable commands strong, their arguments lighter and
other commands in between, and hovering says which command, its length, how often it ran
there and whose engine; the dossier has a *Script engines* section with every command's
handler, length and runs. `corrupt-smoke --scripts` writes the library file with
`CORRUPT_SCRIPTS_OUT`, which is how the UI tour shows FF6's engines (`15a`).

The first file was 129 KB after 12,000 frames: most of it echo rules (other strides and
biases fitting part of a real engine's dispatches). It now keeps per site the 32 busiest rules
with at least 20 dispatches and 4 places: 50 KB.

### Step 4: script ops (#44)

Built: `script_swap` and `script_nudge` (`corrupt/script_swap.*`), fired like the pointer swap:
the engine asks for the engines found when a run starts and keeps each rule's slots for the
run. The rule editor has both, with an *Engine* choice; a starter, *Script swap*. A starter's
ROM range is written for any game, so aimed ROM rules (pointer swaps, function kills, script
ops) past a small ROM's end are trimmed when a recipe loads; that also fixes *Function
flicker*, whose `0x7FFFFF` range didn't load on a 512 KB game.

**Measured** with `corrupt-smoke --script-swaps`: the pseudo-player plays the lead while the
tracer learns, then a window is played clean, learning on; changes go on the usable places the
window read, and every arm replays the same frames with the same buttons from the same moment,
unguarded. 30 seeds per row; every changed byte was read by the game in every run but a few
nudges (a 1-byte command has no argument).

| scene | arm | moving at the end | on another picture | moving and on another picture | moving and ran new code |
|---|---|---|---|---|---|
| FF6, 30 s, 1 change | swap | 29 | 20 | 19 | 23 |
| | nudge | 26 of 26 | 7 | 7 | 0 |
| | any command | 25 | 24 | 19 | 25 |
| | random byte | 25 | 27 | 22 | 24 |
| FF6, 60 s, 2 changes | swap | 25 | 15 | 10 | **20** |
| | nudge | 29 of 29 | 7 | 7 | 0 |
| | any command | 12 | 29 | 11 | 12 |
| | random byte | 10 | 28 | 8 | 10 |
| FE4, 60 s, 1 change | swap | 26 | 12 | 8 | 10 |
| | nudge | 29 | 18 | **17** | 10 |
| | any command | 26 | 16 | 12 | 11 |
| | random byte | 12 | 22 | 4 | 8 |

What it says:

1. **The length rule keeps the game going** when changes stack up: on FF6 with two changes,
   swaps left it moving in 25 of 30, any other command in 12, random bytes in 10. Nudges
   almost never stop a game (55 of 55 on FF6).
2. **Swaps change behaviour while the game survives** where it counts: FF6 with two changes,
   20 of 30 still moving and running code the clean run never did, against 12 and 10. With
   one change, or on FE4, they tie with the cruder changes on this measure.
3. **Swaps show less at the end** (15-20 of 30 on another picture against 24-29): a swapped
   command often changes what happens and the scene then settles, or the change is in what
   the game decides, not what it draws. Nudges on FF6 rarely show (7), on FE4 they show
   most (17 moving and visible).
4. The samples are small: the windows read 7-18 usable places, so seeds pick the same ones
   again. Pseudo-play is a poor player for scripts; kept moments of real play (#85) will
   say more.

So both ops ship: safer than random bytes in the same places, a real gain on FF6 when
changes stack, not a clear gain everywhere. A classic swap (20 commands changed before the
game boots) is the most visible: on FF6 the opening march jumped ahead to the narration, lost
two of its three soldiers, or let another character wander alone (the docs page's clip).
