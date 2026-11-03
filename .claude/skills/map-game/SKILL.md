---
name: map-game
description: Use when Mikael or Erik want Claude to map a game's code ("map this game", "name the functions", "annotate the dossier", roadmap #42) — reads the game dossier the app makes from the library's tracer files, names functions, RAM variables and jump tables, and writes names.claude.json plus mapping.claude.md into the game's library folder. An optional extra: the app never needs it.
---

# Map a game (#42, optional)

The app's tracer learns a game while people play (calls, jump tables, code map). The
`game-dossier` command turns that into one Markdown file. This skill is the optional step
on top: Claude reads the dossier and proposes names. Plan:
`docs/corruption/2026-11-03-game-mapping-plan.md`.

**Ground rules**
- Core features never depend on this. Everything written here is marked as Claude's:
  `names.claude.json` shows in the app as *name (claude)*, beside people's names, never
  instead of them.
- Every name must rest on evidence **in the dossier** (registers, RAM, callers, tables,
  the listing). If a name comes from what fans documented about the game (memory of a
  disassembly or RAM map), say so in the report (`source: fan docs`); never present it as
  read from the code.
- Never push the library to git. Write the two files locally; Mikael decides about sharing.
- Never overwrite a person's names file. Only `names.claude.json` and `mapping.claude.md`.

## 1. Make the dossier

```sh
./build-mac.sh test   # builds build/tests/game-dossier if needed
build/tests/game-dossier "<rom>" "<library>/<GAME [hash]>" --out <scratchpad>/dossier.md
```

The library is `~/Documents/Snes9x Corruptions` (the game folder is `TITLE [first 8 of
sha256]`). ROMs are in `~/Emulation/Snes/` or the library's `roms/`. It prints how many
functions and tables went in. Too few functions (under ~100) means the game hasn't been
played enough with the app: say so and stop.

## 2. Read it in parts

Read the header first (what can't be told), then *Jump tables* and the *RAM index*
(the structure), then the functions top-down (busiest first). Use Read with offset/limit;
the file is ~200 KB.

## 3. Name things, with what gives them away

Look for, in this order:
1. **The frame skeleton**: the busiest functions, run every frame: NMI work (DMA to
   VRAM/OAM/CGRAM: `$420B`, `$2104`, `$2118`, `$2122`), joypad reading (`$4218`…), the
   main dispatcher (reads one RAM byte, indexes a table, jumps: the *game mode*).
2. **Dispatchers and their states**: each jump table's entries are states or object types;
   name the table by what selects it (the RAM byte) and the entries by what they touch.
3. **Shared workers**: functions with many callers (sprite drawing, math helpers: `$4202`/
   `$4204` multiply/divide; decompression: long ROM reads into RAM; sound: `$2140`).
4. **RAM variables** from the index: read by many and written by few is state (mode,
   timers, player position); a block written in a loop with stride is a table (OAM buffer).

Name style: short player words, like people name them in the app ("joypad read",
"sprite OAM upload", "game mode dispatch", "Link movement?"). Add `?` when it's a guess.
Three levels, used in the report: **sure** (the code says it outright: registers, a clear
pattern), **likely** (several hints agree), **guess** (one hint). Only *sure* and *likely*
go into `names.claude.json`, and *likely* names end with `?`.

## 4. Write the two files into the game's folder

- `names.claude.json`, the app's names format, entries as **ROM offsets** (the dossier's
  `· ROM 0x…` on each heading), in entry order:
  ```json
  {"format": "snes9x-names", "version": 1, "functions": [
    {"entry":"0x0000B5","name":"game mode dispatch"}
  ]}
  ```
- `mapping.claude.md`: a table of every name (address, ROM offset, name, sure/likely/guess,
  source dossier/fan docs, the evidence in a line, **how to check it in the app**: "turn it
  off in Functions: the screen should stop updating"), then RAM variables, then jump
  tables, then what's still unknown.

## Known blind spots (from the first try, 3 Nov)

- Split jump tables (low, high and bank bytes in three tables, then `JML [$xx]`) and
  push-then-`RTS` dispatch aren't found as jump tables: read the listing for them.
- A table placed right after a `JSL` to a jump-table helper doesn't say which RAM byte
  selects it: look at the instruction before the `JSL`.
- Many one-entry "tables" from one site are usually one RAM vector seen in several banks.
- The direct page is assumed 0: a direct write that doesn't fit the rest may be elsewhere.
- What a first try scored (Zelda, graded against a fan disassembly): *sure* 18 of 20 exact
  and none wrong, *likely* 47 of 61 exact and 2 wrong; guesses are mostly vague. Keep the
  levels honest; they were the most useful part.

## 5. Report

Tell Mikael how many names at each level, the five most interesting ones with how to check
them, and that they appear in Functions as *name (claude)* the next time the game loads.
