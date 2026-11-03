# The game-mapping pipeline, a first try (#42)

Mikael (3 Nov): "For the game mapping pipeline I was never against it. Let's try it!"

The idea (horizons, 31 Oct): the tracer collects facts while people play; one command
turns them into a dossier; Claude reads it with the disassembly and names things ("the
battle command dispatcher"); names are checked; the result goes into the library as a
game profile (#41). Since 2 Nov core features must work without an LLM, so the pipeline
is split along that line: everything up to the dossier is the app's own and useful on its
own; Claude's step is an optional extra on top, and what it makes is marked as its own.

## What we have to work with (3 Nov)

- **Facts already collected**, per person, in the library: the call graph (`calls.*.json`,
  #26), jump tables (`jumps.*.json`, #25), the code map (`codemap.*.bin`), crash maps,
  and players' names (`names.*.json`, #59). Zelda: A Link to the Past (Mikael), Pocky &
  Rocky (Mikael), Chrono Trigger and Super Mario World (Erik, who named 20-odd SMW
  functions by hand).
- **Code to read them**: `decode_call_log`, `find_jump_tables`, `decode_code_map`,
  `function_body` (#58), `disassemble` (#71), `FunctionNames`.
- **Not collected yet**: table shapes (#27), state variables (#28), scene tags (#48).
  The dossier works from what code *says* instead: the addresses in each function's
  instructions (which RAM it reads and writes, which hardware registers, which ROM
  tables). That's static, so it's marked as such.

## The try, in three pieces

**1. The dossier (the app's own, no LLM).** `corrupt/dossier.{h,cpp}` and a command,
`build/tests/game-dossier <rom> <game folder> [--functions N]`, that writes one Markdown
file a person can read too: what the tracer has learned about this game.

- A header: game, ROM size and mapping, how much code is known, whose files went in.
- **Functions**, the N busiest by frames run (default 150), plus every named one and every
  jump-table target: address, call type, frames, callers; *called from* and *calls*, by
  name where there is one; the jump tables in it; **RAM it reads and writes** (absolute
  and long operands into WRAM, with read/write); **hardware registers** by name
  (`$2100 INIDISP`, `$420B MDMAEN`…); **ROM data it reads** (operands into ROM that the
  code map knows as data); then its disassembly, cut at about 60 instructions.
- **Jump tables**: where each dispatches from and the function behind every entry.
- **RAM index**: each WRAM address and the functions that read and write it, busiest first.
  This is where a "game mode" variable shows up: one byte read by a dispatcher, written by
  many.
- **What it can't know**, said once at the top: the direct page and data bank are assumed
  (direct page 0, data bank = the code's bank, `$0000-$1FFF` is low WRAM); indexed and
  indirect addresses are bases, not every byte; only code that ran is there.

**2. Annotate (Claude, optional).** A project skill, `.claude/skills/map-game/`, with a
fixed procedure: run the dossier, read it in parts, name functions, RAM variables and
jump tables, and write two files into the game's library folder:

- `names.claude.json`, in the existing names format, so the Functions page shows them at
  once as *name (claude)*, beside people's own names and never instead of them; a name
  of yours always comes first. `function_names.h`'s "never from a model" becomes "a
  model's names are its own file, marked as such".
- `mapping.claude.md`: every claim with how sure it is (*sure / likely / guess*), the
  addresses behind it, and how to check it in the app (*turn $02:8A4C off: Link should
  stop taking damage*). RAM variables and tables go here too, since there's no file for
  them yet (#41).

For a well-known game Claude may know fan documentation already; the report says when a
name comes from that rather than from the dossier.

**3. Check it.** Two ways, both reported in *How it went*:

- **Against a fan disassembly** (Zelda): the public decompilation names its functions by
  address. Count the names that match, that are close, and that are wrong. Done blind
  where possible (the annotation first, the comparison after, by a separate agent).
- **By play**: Mikael loads the game, opens Functions, turns named ones off and says
  whether they did what the name says. That's the verify step the horizons plan wanted,
  done by hand before anything automatic.

## Not in this try

- The automatic verify by experiment (set a state variable, see if the scene changes),
  table shapes, scene tags, a "Map this game" button, a profile file format (#41). Each
  waits for this try to show whether names from a dossier are good enough to be worth it.
- Pushing anything to the shared library: the files are written locally; whether they're
  shared is Mikael's call.

## Tests

- `tests/corrupt/dossier_test.cpp` with a fake ROM, code map and call log: functions
  ranked and named, callers and callees, RAM reads and writes from absolute and long
  operands, hardware register names, ROM data reads, jump-table entries named, the
  disassembly cut, the RAM index.
- The command on the real libraries (Zelda, Pocky & Rocky): size of the file, time taken.

## How it went (3 Nov)

Built the dossier and the skill the same day, then tried the skill on two games, each by
a fresh agent that only had the skill and the dossier, writing to a scratch folder (not
the library, which syncs to Erik).

**The dossier.** `game-dossier` on Zelda: A Link to the Past (Mikael's data: 799
functions, 59 tables) takes 0.3 s and writes 195 KB (about 50k tokens), 191 functions
listed; Pocky & Rocky 150 KB, 145 functions. The tracer's function entries are real:
**179 of Zelda's 191 sit exactly on a routine label** of the community disassembly
(spannerisms/usdasm); the other 12 are inside one (second entry points).

**Zelda, named blind and graded against the disassembly.** The annotator was told not to
use what it knows of the game and to mark anything it recognised anyway (7 rows); a
separate agent graded every name against the disassembly's label and code:

| Claude's level | match | close | wrong | vague |
|---|---|---|---|---|
| sure (20) | 18 | 2 | 0 | 0 |
| likely (61) | 47 | 12 | 2 | 0 |
| guess (110) | 16 | 49 | 8 | 37 |

The levels are honest: no *sure* name is wrong, *likely* is 77% exact and 97% right or
close, and guesses are mostly close or vague. Only sure and likely go into the names file
(81 names), so what a player would see is 65 exact, 14 close, 2 wrong. Without the 7
recognised rows the numbers barely move (sure 18/2/0/0, likely 44/11/2/0). From tracer
facts alone it found, among others, *game mode dispatch* (RunModule), *joypad read*
(ReadJoypad), *player draw* (DrawLink), *OAM slot allocator* (AllocateOAMInRegion), *tile
type dispatch* (ExecuteTileDetection), and the hit tests: *hitbox overlap test*
(CheckIfHitboxesOverlap), *object hit by player attack* (CheckDamageFromLink), *object
touches player* (CheckDamageToLink). The wrong ones are near misses in the player-movement
code (*clear collision results* is ResetAllAcceleration) and four screen-edge scrolls
taken for door handlers.

**Pocky & Rocky** (little fan documentation): 47 sure, 85 likely, 13 guess, every one with
what should happen when it's turned off (*$84:EB74 player 1 takes a hit: off, player 1
can't be hurt*). Checking those is play: the names file is ready to go into the library.

**What the annotators found missing**, both independently; the first four are fixed:
- Code reached by a jump was listed by address with the rest, sometimes before the entry:
  now the entry's code comes first and a gap is marked (*before the entry, reached by a
  jump*).
- Overlapping bodies blamed calls on the wrong function: an instruction now belongs to the
  nearest entry at or before it, and shared code is marked *shared with*.
- Functions reached through jump tables had no callers and looked idle: now *Reached
  through jump tables: entry 3 of the table at …; jumped to 38,045 times*.
- Listings were cut where the interesting calls are: past 60 instructions only calls,
  jumps, returns and register accesses are kept.
- Not yet: dispatchers with split tables (low, high and bank bytes in three tables, like
  Zelda's RunModule) and push-then-RTS dispatch aren't found as jump tables; the table that
  follows a `JSL` to a jump-table helper doesn't say which RAM byte selects it; many
  one-entry "tables" through a RAM vector swamp the list; no values seen in state bytes
  (#28) and no scene per function (#48). Those are the next tracer facts worth having.

**Cost**: each annotation took one agent about 8 minutes and 220-250k tokens.
