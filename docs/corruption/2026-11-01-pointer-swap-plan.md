# Jump tables and pointer swap: plan (roadmap #25 → #32)

**Status (2026-11-01):** phase 1 done; Mikael said go. Builds on the code map tracer from
[the ROM plan](./2026-10-31-rom-corruption-plan.md) phase 3. Ideas behind it:
[the horizons](./2026-10-31-horizons.md).

## Goal

A **pointer swap** replaces one entry of a game's jump table with another entry of the
same table. The CPU still lands on the first instruction of a real routine, with the
stack as the dispatcher expects, so the game keeps running and does the wrong thing with
its own working parts: an enemy runs another enemy's behaviour, a menu opens the wrong
screen, a cutscene step plays another step. Mikael's favourite, and the purest
wrong-but-valid change there is.

To do it we need the tables: where they are, how wide an entry is, which entries the game
has actually used and where each goes. That is #25. The index's origin (which WRAM byte
picks the entry) is left to #28.

## What the games do (measured 2026-11-01)

Code maps recorded headless with no input (title screens and attract modes only), counting
the indirect jumps that ran:

| Game | `JMP`/`JSR (table,X)`, table in ROM | `JMP (ptr)` / `JML [ptr]`, pointer in RAM |
|---|---|---|
| Goof Troop | 81 sites (tables right after the instruction) | 0 |
| Pocky & Rocky | 10 | 23 (`JMP ($0010)`, `JMP ($0018)`) |
| FE4 | 7 | 18 (`JMP ($00D3)`, `JML [$0024]`, ...) |
| FF6 | 1 | 1 (its title runs from WRAM; battle and field need play) |

So there are **two kinds**, and both matter (A Link to the Past dispatches the second
way, through a pointer it builds in RAM):

1. **Direct**: `JMP (a,X)` (`7C`) and `JSR (a,X)` (`FC`). The table is at `PB:a`, the entry
   at `PB:(a+X)`, two bytes. When that is ROM, the tracer knows the entry exactly.
2. **Through RAM**: `JMP (a)` (`6C`) and `JML [a]` (`DC`), and `7C`/`FC` whose table is in
   RAM. The game first copied the entry from a ROM table into RAM (`LDA table,X` /
   `STA $10`), then jumps through the copy. The tracer only sees the target.

## How the tables are found

**Recording (the tracer, while you play).** The tracer already wraps every opcode. The four
indirect-jump opcodes get one more step before they run: read the operand, the X register
and the pointer, and note `(site, entry offset or none, target)`, where *site* is the ROM
offset of the jump instruction and *target* the 24-bit address it goes to. Only those four
opcodes pay; nothing changes for the others. No core change.

**Finding (between frames, from the recording plus the code map).**

- **Direct entries** are known: the entry's ROM offset, 2 bytes, bank `PB`.
- **Entries through RAM are inferred**: the ROM bytes holding the target's value (2 bytes;
  3 for `JML`, bank included) that the code map has seen **read as data**. A unique match
  is the entry; no match or several matches, and the target stays a plain "pointer jump"
  (not swappable). Phase 1 measures how often this resolves in Pocky and FE4; if it's
  rare, the fallback is a short ring of recent ROM data reads (a core hook in `getset.h`).
- **A table** is the entries one site used, grouped by stride (2 or 3) when they line up
  (`(offset - first) % stride == 0`, within 1 KB). A table belongs to one site: two
  dispatchers reading the same entries are two tables (see *Risks*), and the viewer says
  the entries are shared.
- **Several spots with the value**: the one spot in line with the site's other entries
  (the same stride, within 1 KB) is the entry; otherwise it stays ambiguous.
- **Likely entries** fill the gaps between known entries and continue the table outward,
  one stride at a time, while the value points at a routine that **has run** (just not
  through this entry), in the same bank, and the entry's own bytes aren't code. The first
  miss stops the outward walk. They're kept apart from **seen** ones. (A looser rule, "lands
  in ROM, not on data", walked off through plain data: in HiROM nearly any 16-bit value
  lands in ROM; FF6 found 128 "likely" entries from one seen one.)

To turn targets into ROM offsets and back, the core gives a map of every 4 KB block of the
CPU's address space to its ROM offset (`SnesCoreAccess::rom_offsets()`), the reverse of
the existing `rom_addresses()`.

**Kept like the code map**: per person in the library (`jumps.<user>.json` beside
`codemap.<user>.bin`), saved once a minute while it grows, combined with everyone else's.
Erik's play teaches Mikael's copy. Counts are hits, summed.

## The op: `pointer_swap`

A ROM-only op. Each firing picks `count` entries among the **seen** entries of the tables
in the rule's range, and for each writes the target of another entry of the same table
(chosen from the seen ones, or seen + likely with `reach: likely`) that goes somewhere
else. The value written comes from the recording (taken from the clean ROM), not the
current bytes, so a chain of swaps still lands on real routines.

| Field | | Default |
|---|---|---|
| `range` | the tables whose entries lie in it (the whole ROM by default) | all |
| `count` | entries changed per firing | 1 |
| `reach` | `seen`: replace with entries the game has used; `likely`: also ones nobody reached | `seen` |
| `mutual` | both ways: the donor gets the entry's target too (a true swap) | false |
| `when` | `at_load` (classic: boots changed, replays exactly) or any live trigger; a hotkey makes a "swap one more" button | |

It goes through the usual ROM path: the RomKeeper journals it, blame sees the entry bytes
read as data (both kinds: the copy loop reads them too), and survivors and pins work
unchanged. With no recording yet, the rule says so ("no jump tables known yet: play a
while with the game loaded") instead of failing quietly, like aimed rules do.

## Phases

1. **Record and find** (`corrupt/jump_tables`, tracer, `rom_offsets`). JumpLog (sites,
   entries, targets, hits; merge; JSON), `find_tables(log, rom, flags, offsets)` (direct,
   inferred, grouped, likely). A test ROM that dispatches both ways (`JSR (table,X)` with X
   cycling, and `LDA table,X; STA $10; JMP ($0010)`), each routine counting its runs in
   WRAM. `corrupt-smoke ... --jumps [file]`: record, print the tables (kind, sites,
   seen/likely entries) and the tracing cost, which must stay within the current 4-7%.
2. **Keep and show** (app). JumpTableKeeper beside CodeMapKeeper; Library
   load/save; the viewer's Jumps overlay on table entries (hover: "entry 3 of 12 of the
   `JSR ($8282,X)` at `$80:8278`, goes to `$80:9A10`, used 340 times; 5 more likely");
   the dock's ROM line adds "N jump tables".
3. **Pointer swap** (op, engine, editor). `OpType::PointerSwap`, `Engine::set_jump_tables`
   (fetched once per run, like the code flags), recipe JSON (`"op": "pointer_swap"`,
   `"reach"`, `"mutual"`), describe_rule text, the editor's page (Swap with: entries it
   has used / also ones nobody reached; Both ways; "N tables, M entries in this range"),
   starters `rom-pointer-swap` (at load, 3 entries) and `rom-pointer-roulette` (hotkey 1:
   one more), GUIDE, QA section 26.
4. **Prove it** (headless). On the test ROM: a swap makes entry 0 run routine 1 (exact
   counter check). On Goof, Pocky, FE4 (attract modes, warmup 1): pointer swap at load vs
   the same number of random ROM bytes in the same tables' banks, over 10 seeds each:
   alive (picture changes, NMI on, code runs) and how different the picture is. Expect
   swaps to stay alive far more often. FF6 waits for live play (QA 26).

## Phase 1: how it went (2026-11-01)

`corrupt/jump_tables` (JumpLog, JSON file, `find_jump_tables`), the tracer's `note_jump`
for the four opcodes, `SnesCoreAccess::rom_offsets()`, the test ROM `build_dispatch`
(both kinds, checked in the real core), and `corrupt-smoke <rom> <recipe> <frames> 1
--jumps [file]`. 4000 frames each, no input:

| Game | Cost on top of the code map | Tables direct / through RAM | Seen + likely entries | Swappable | Unexplained / ambiguous |
|---|---|---|---|---|---|
| Goof Troop | none measurable | 81 / 0 | 279 + 51 | 72 | 0 / 0 |
| Pocky & Rocky | none | 10 / 21 | 116 + 55 | 21 | 7 / 11 |
| FE4 | none | 7 / 44 | 73 + 16 | 18 | 11 / 50 |
| FF6 (title only) | none | 1 / 1 | 3 + 7 | 2 | 0 / 0 |

FE4's ambiguous values are mostly inline pointers (`JSL` to a trampoline that reads 3
bytes after the call and `JML [$0024]`s there), the same routine named in many places.
Swapping one of those is still wrong-but-valid (the same trampoline, another target it
used); they show up as through-RAM "tables" whose entries sit in the code stream.

**Pointer swap donors come from the site, not the table**: every target the same
dispatcher went to, from any of its tables, as long as a 2-byte entry can hold it (same
bank). That's the safety rule (same calling convention); tables only shape the viewer
and the likely walk.

## Out of scope

- **Pushed-address dispatch** (`PEA`/`PHA` then `RTS`/`RTL`): no clean signal at `RTS`.
  Later, if the inferred kind shows the value-match works.
- **Where the index comes from** (#28) and **state jumps** (#33).
- **Function redirection** (#35) is the same move for `JSR`/`JSL` operands, after the
  call graph (#26).

## Risks

- **Inference matches the wrong bytes**: a 16-bit value can appear twice in data. Only
  unique matches among data-read bytes count; inferred entries are marked as such.
- **Pointers built in code, not read from tables** (`LDA #$1234; STA $10`): no data match,
  so they're plain pointer jumps. Swapping those is an operand change (`rom-operand-nudge`
  already does it).
- **A table shared by two dispatchers with different stacks** (one `JMP`, one `JSR`): the
  routine ends with the wrong return. Tables are per site, so a swap only uses targets that
  same dispatcher went to.
- **Tracing cost**: four opcodes with an extra read each; checked in phase 1.
