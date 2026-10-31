# UX pass: plan (roadmap #49)

**Status (2026-10-31):** decisions agreed by Mikael ("Sounds right!"); in progress. After it: a visual **UI pass** with the best design guidance for Qt Widgets (to be researched). Mikael: "Everything
needs to be clear how to use even if one hasn't implemented it. I've already forgotten many
things."

## Goal

Someone who didn't build it, and someone who built it two weeks ago, can sit down and:

1. make a game glitch within a minute, and get the normal game back;
2. understand what every control does without leaving the app;
3. find the deeper tools (memory viewer, ROM corruption, code map, survivors) when they want
   them, and not be buried by them before;
4. read one short page to start, and find the rest when needed.

## How it was audited

- **The UI tour** (`SNES9X_UI_TOUR`, see the README's developer section) screenshots every
  corruption window of the real app with a game running. Every step below is checked
  against a fresh tour.
- **A fresh-eyes review**: a reviewer with no knowledge of the code tried six newcomer tasks
  on the screenshots alone, then with the README. Its findings are folded into the audit.
- Counting: the panel has about 31 controls and 7 tooltips; the rule editor about 58 and 2.

## The audit

Six newcomer tasks, and where they break today:

| Task | Where it breaks |
|---|---|
| Glitch a little, now | Starters hide under "Saved moments" as file names (`gentle-wram-noise`), need Add Example, then Load, then Active; Active is greyed with no reason; Chaos starts at 100% |
| Get back to normal | Nothing says Undo, Stop or Reset. Unticking Active keeps the damage. Restore Clean ROM is ROM only. "Save Backups → Restore" looks like undo but restores the *cartridge save* and resets the game |
| Glitch only the code, keep a good one | Aim is greyed until Memory is `rom`, and nothing says so. "Keep it" could be Save, Pin, Paint or Export ROM |
| Find where sprites or palettes live | The viewer's three dropdowns have no labels and no colour legend; "Sprite and palette copies" shows on every memory |
| Recover after a crash | Automatic (good), but "Mark Crash", "Policy: reroll" and "Recipe's choice" are unexplained |
| Share with a friend | No Share; only "Open Folder" and git in the README |

Also, per screen:

- **Panel**: the recipe dropdown can disagree with the loaded recipe; six equal buttons
  (Load, Rescan, Open Folder, New, Save, Save As); the status line says "new moment, not
  saved - frame 90 - 1 firings"; the ROM line is raw counts with four buttons; the crash guard
  says both "Recipe's choice" and "Policy: reroll"; the rule table has no empty state and an
  "Op" column; plurals show as "byte(s)"; the hotkeys (F7 on/off, F8 re-roll, F9 hotkey 1)
  appear nowhere; 31 controls, 7 tooltips.
- **Rule editor**: greyed sections instead of hidden ones; "Addresses per firing", "Pick";
  Intensity is 0-1 here but a percentage in the table; 58 controls, 2 tooltips.
- **Memory viewer**: unlabelled dropdowns; overlays offered where they mean nothing (Code on
  VRAM); no legend; map entries on two lines with jargon ("Direct page (usually)"); Paint looks
  like a button, its value is an unlabelled `0x0`; empty Named areas with no hint; the tile
  view leaves a white half.
- **Survivors**: three address formats; unsaved pins not called out.
- **Words**: recipe/example/starter; Active/run/firing/seed/salt; Chaos vs Intensity; moment
  vs save state vs save backup; Target vs Memory vs Where; Op vs What; Paint vs pin vs
  survivor; Hits vs Activity; `wram` vs WRAM.
- **README**: opens with phase history and build commands; "Using it" is five lines; research
  numbers sit in the middle of the user guide.

## Decisions

- **One vocabulary**, kept in a glossary that the app and the README share: *recipe* (a saved
  set of rules; *starters* come with the app), *rule*, *run* (corruption on, from its start
  moment), *seed*, *chaos* (the whole recipe's strength) and *strength* (one rule's), *memory*
  (WRAM, VRAM, ROM… with a one-line gloss each), *start moment* (the save state a run starts
  from), *game save* (the cartridge's battery save), *crash guard*, *code map*, *survivor*,
  *pin*, *paint*.
- **"Back to normal"** is its own button and hotkey: turn corruption off, put the ROM back
  to clean, and jump back to the run's start moment (the save state taken when it began), so
  RAM damage is gone too. Turning off alone still keeps the damage, and now says so.
- **Starters** are listed in the recipe dropdown itself, under their real names; choosing one
  loads it. Saving makes it the game's own copy.
- **Hide what doesn't apply** instead of greying it: aim for non-ROM rules, ROM-only
  overlays on other memories, the copies finder outside WRAM/OAM/CGRAM.
- **Every control gets a tooltip**, and every hotkey is named in the tooltip of what it does.
- **In-app Guide**: a **?** on the panel opens the user guide inside the app (the same
  Markdown file as the README's "Start here", bundled), at the section for where you are.
- **Share** writes one `.s9xrecipe` file (the recipe, its start moment and pins, zipped) and
  **Import** reads it, so sharing doesn't need git.
- **Addresses**: offsets within a memory as `0x1A3F` everywhere; ROM code also shows the
  CPU's address, always labelled (`CPU $C2:1A3F`).

## The plan

Each step is test-first where there's logic, checked with a fresh UI tour, and pushed.

1. **Words**: the glossary (`docs/corruption/GUIDE.md`), plain status lines ("Running 12 s ·
   34 bytes changed · no crashes"), plurals, the crash policy names as sentences ("Re-roll
   the guilty rule's dice"), one address format, tooltips on every control with hotkeys.
2. **The panel**: sections in the order you use them (Recipe, Corrupt, Rules, Crash guard,
   ROM, Game saves); the loaded recipe always shown; starters in the dropdown; a big
   **Corrupt (F7)** toggle and **Back to normal**; Re-roll (F8) with the seed secondary; a
   "Start here" card while no recipe is loaded; the rule table's empty state; the ROM section
   in words; "Game save backups (your in-game saves)"; the library under a small "Library…"
   line.
3. **The rule editor**: hidden instead of greyed sections, Strength as a percentage, "Bytes
   each time", a hint where Aim would be ("Choose memory rom to aim at code"), tooltips.
4. **The memory viewer**: labelled controls (Show, Memory, Colour by), overlays filtered per
   memory, a colour legend line for the current overlay, one-line map entries with glosses,
   Paint as a mode with a labelled value, the copies finder only where it applies, empty
   states, the tile view's background.
5. **Share and Import** (`.s9xrecipe`), from the panel's recipe menu.
6. **The guide**: GUIDE.md ("Start here": first glitch in a minute, back to normal, keep and
   share, hotkeys, glossary; then one page per tool), shown in the app by **?**. The README
   becomes a short pointer to it plus the developer notes; research numbers move to the
   plan docs. QA.md gets a short "first ten minutes" section at the top.
7. **Check with fresh eyes again**: a new tour and the same six tasks by a reviewer who hasn't
   seen the first round.
