# Cockpit and Workshop: a panel for playing, one window for the rest (plan, roadmap #63)

**Status (2026-11-02):** done. Asked for by Mikael after a Pocky & Rocky session: the
corruption UI works but is "VERY messy". **Decided with Mikael:** no corruption hotkeys
except one (start and stop corrupting); the side panel becomes a *cockpit* with only what
you touch while playing, and everything else moves into **one Workshop window** with a
sidebar, replacing the separate windows. Rare actions also go in the Corruption menu.

## What's wrong now

1. **One long list where every section looks equally important.** Recipe, Corrupt, Rules, Crash Guard, ROM,
   Remote Control and Save Backups all sit at one level and need scrolling. What you touch
   every few seconds (Corrupt, Chaos, a rule's strength) sits next to what you touch once a
   month (the library folder, the remote key, Export ROM).
2. **A swarm of windows.** Memory Viewer, Functions, Change It…, Survivors, the rule editor
   and the guide each open on their own, and some open each other (Functions → Show in ROM
   → Memory Viewer).
3. **Sixteen hotkeys.** Corrupt, Back to Normal, Re-roll, Save, Fire 1-9, It Crashed,
   More, Less. Nobody remembers them, and keys 1-9 are numbers with no names.

## Everything, by how often it's touched

| How often | What |
|---|---|
| While playing (seconds) | Corrupt/Stop, Back to Normal, Chaos, Re-roll, a rule on/off and its strength, firing a manual rule, flipping a kept switch or change, It Crashed |
| Building a recipe (minutes) | pick/new/save, starters, rules, the saved moment, functions (name, Protect, off, timer, Change It), survivors and pins, paint, named areas |
| Looking | memory viewer (7 memories, 3 views, ~10 overlays, the map), the function list, what's learned (code, jump tables) |
| Set once | catch crashes, try ahead, after a crash, instant revert (recipe only today), save backups |
| Rare | library folder, Share/Import, open folder, look for new files, sync conflicts, remote control, Export ROM, Restore Clean ROM, Clear Paint |

## The design

### Hotkeys become Buttons

- **One hotkey stays: Corrupt / Stop (F7).** All other corruption shortcuts are removed
  from the app's shortcut list (`EmuConfig`: Reroll, SaveRecipe, Fire1-9, MarkCrash, More,
  Less, BackToNormal). An old config naming them is ignored.
- What keys 1-9 did (fire a manual rule, flip a kept switch or change) becomes **Buttons**:
  numbered slots 1-9 the recipe already has (the JSON keeps `"hotkey"`, so every recipe
  works unchanged). The cockpit shows one button per slot in use, named after what it
  does ("gravity off", "jump +3", "burst"; several things on one slot: "burst + 2 more").
  A slot holding only switches and changes is a toggle that shows whether it's on; a slot
  with a rule is a plain press.
- Words everywhere: "hotkey" / "key N" become "button N" (rule editor: *When I press
  button…*, Functions: *On a button*, Change It…: *Button*). The engine's `press_hotkey`
  stays (it's the mechanism); the remote page gets the same named buttons
  (`status_json` gains `buttons`: key, label, toggle, on).

### The cockpit (the side panel)

Top to bottom, no scrolling at a normal window height:

1. The game's name, *Guide* (icon) and *Workshop* buttons.
2. Recipe: the list and *Save*. (New, Save As, Share, Import: Workshop and menu.)
3. The message line.
4. **Corrupt / Stop (F7)** and **Back to Normal**, the run line, Chaos, Seed + Re-roll.
5. **Buttons** (only when the recipe has some).
6. **Rules**, compact: on, name, strength. Double-click (or the pencil) edits in the
   Workshop. No description or crash columns (tooltips have them).
7. One line about safety: "Crash guard on · 2 crashes caught" with *It Crashed*.
8. One line about the ROM: "ROM: 120 bytes changed · 3 functions off".

### The Workshop (one window, sidebar + pages)

| Page | What's on it (from where today) |
|---|---|
| **Recipe** | name and notes; the full rules table (what it does, strength, crashes) with Add/Edit/Duplicate/Remove; what else the recipe keeps (switches, changes, protects, pins, painted bytes) as one list with Remove; *Remember where each run starts*; New / Save / Save As / Share / Import |
| **Functions** | the Functions window as it is (Change It… stays a dialog from here) |
| **Memory** | the Memory Viewer as it is; paint tools only shown in Paint mode |
| **ROM** | what's changed and what's learned (code, data, graphics, jump tables, functions); Survivors and pins; Export ROM, Restore Clean ROM, Clear Paint |
| **Safety** | catch crashes, try ahead, instant revert (now a switch), after a crash; crash count, last crash, dangerous spots; save backups |
| **Sharing** | library folder (with a sync note: whose files are here, last change, conflict copies), open folder, look for new recipes; remote control |

Cross-links become page switches: Functions' *Show in ROM* opens the Memory page;
the viewer's *New Rule from Selection* lands on the Recipe page.

### The Corruption menu

Show Panel · Open Workshop · New Recipe… · Save Recipe As… · Share… · Import… · Export
ROM… · Restore Clean ROM · Choose the Library Folder… · Guide.

## How it's built (Qt only; the engine doesn't change)

- `FunctionsDialog` and `SurvivorsDialog` become plain widgets (`FunctionsPage`, `SurvivorsPanel`) (`QWidget`) that can be
  a page; `MemoryViewer` already is one. Their tests keep working (they construct them).
- `Workshop` (new): `QListWidget` sidebar + `QStackedWidget`; `showPage(Page)`; owns the
  pages and the sources (`SessionFunctions`, `SessionSurvivors`, `SessionMemorySource`
  move out of the dock). The Recipe, ROM, Safety and Sharing pages are new widgets built
  from the dock's code, each against a small interface so they can be tested with a fake.
- The session gains `buttons()` (slot → label, toggle, on), used by the cockpit and the
  remote page.
- The dock shrinks to the cockpit; the tour (`SNES9X_UI_TOUR`) photographs the cockpit
  and every Workshop page.

## Tests

- **Unit:** button labels and states from a recipe (a rule, a switch, a change, a mix,
  an empty slot) and `status_json`'s `buttons`; remote page e2e presses a named button.
- **Qt (offscreen):** the Workshop opens each page, `showPage` from a cross-link, its
  `ui::audit` is empty; each new page's widget test with a fake; the cockpit has no
  scroll at 800 px tall with three rules and four buttons; the shortcut list has one
  corruption entry.
- **UI tour** in light and dark, and the real Mac look (`TOUR_PLATFORM=cocoa`), read by a
  fresh-eyes review against the qt-widgets-ui checklist.
- **No smoke numbers**: nothing here changes how a game is corrupted.

## How it went

Built the same day, in two commits: the buttons first (one hotkey, named slots in the panel
and on the remote page), then the cockpit and the Workshop.

- **What came out of it.** The panel went from seven sections and a scroll to one screen: in
  the tour on Pocky & Rocky, a running recipe with a rule, the run line, Chaos, the seed and
  the guard and ROM lines fit in about 640 px. The Workshop's six pages replaced four
  windows (Memory Viewer, Functions, Survivors, and the panel's lower half). The instant
  revert (#51) got a switch on the Safety page; until now only a recipe file could turn it off.
  *Also in This Recipe* is the first place that lists a recipe's switches, changes, protects,
  pins and paint together, and can take one out.
- **The interface paid off at once.** `CorruptionControl` with a fake made the cockpit and
  every page testable without a game (15 new widget tests); the dock used to have none.
- **Found by the pictures, not the tests:** the Workshop opened narrower than its Memory
  page needs (a page cut off at the right), the empty *Also in This Recipe* still showed its
  table header (visibility only updated when the list changed), the save-backups list filled
  the Safety page when empty, and the panel greyed Chaos and Seed instead of hiding them.
- **Words.** "Hotkey" is gone from what you see (rule editor *When I press a button*,
  Functions and Change It… *Button N*, rule summaries *button 1*); starters' notes too, and
  *Stat swap on hotkey* is now *Stat swap on a button*. Recipe files keep `"hotkey"`.
- **Not done:** the Workshop doesn't remember its size or last page between launches (done
  in #64, below); the viewer's New Rule from Selection doesn't jump to
  the Recipe page (the rule shows up in the panel at once, which was enough in the tour).

## Quick wins after a look at every window (#64)

A pass over the tour's pictures (real Mac look, and a copy of the real library on Pocky &
Rocky) found the panel saying the same thing twice and talking like an engineer. These
are the cheap fixes; the bigger ones (rules in plain words, a rule editor beside the list,
Functions filters, one Careful/Balanced/Wild safety choice, fewer nouns) wait for their own
plans.

- **One state bar** at the top of the panel, in the accent colour while corrupting:
  *Corrupting · 0:13 · 6 changes*, *Stopped · its changes stay until Back to Normal*,
  *Not corrupting · the game is as it was*, *Pick a recipe to start*. The run line below it
  goes; what's left of it (a new start moment to save, starting from a saved one) becomes a
  quiet note.
- **Messages say it once, short, and fade.** Loading a game no longer says "Loaded <game>"
  (the title says it) and loading a recipe no longer says "Recipe: <name>" unless there's
  more to say (its saved moment). Test-ahead gets a headline ("Tested ahead: drip would
  crash here, so it picked again") with the full story in the tooltip. A message fades after
  12 seconds; the line keeps its height, so Corrupt doesn't jump.
- **The title is the game's name** as its file calls it (*Pocky & Rocky (USA)*), with the
  library folder name in the tooltip. The status grows a `rom_name`.
- **The seed is one quiet line**: *Seed 5101 · Re-roll*; clicking the number edits it.
- **Guard and ROM lines are whole links** with an icon and a chevron, no repeated word:
  *Crash guard on · no crashes ›*, *ROM: 12 bytes changed · 1 function off ›*.
- **"(not saved yet)" becomes "(new)"** in the recipe list, so it fits; the Save button
  being enabled already says there's something to save.
- **No Refresh buttons** on Functions and the ROM page's survivors: both refresh themselves.
  Functions keeps its busiest-first order until you leave the page (re-sorting under the
  mouse would be worse), so it re-sorts when the page is shown.
- **Sharing**: the library path short (`~/…`), and the remote address, Copy Link and New Key
  only shown while remote control is on.
- **The Workshop remembers** its size and last page (app settings).
- **A bug the panel showed**: "ROM clean · 2 functions off" with nothing changed. A function
  whose first byte already is a return (a stub) counted as switched off. Off now means
  *the byte is a return and the clean ROM's isn't* (`function_off()` in calls.h, tested).

Tests: `function_off` unit cases; cockpit tests for the state bar's words and tone, the
message fading, the seed line, the links; workshop tests for the hidden remote controls and
the remembered page; the tour read again in both looks.

### How it went (#64)

Built the same day, test first (unit cases for stubs and headlines, eight new widget tests),
then read in the tour with a copy of the real library on Pocky & Rocky, in the Mac look and
Fusion dark. The tour script now takes `TOUR_LIBRARY=<dir>` for exactly that.

- **The bug was real.** On Pocky & Rocky two functions are a lone `RTS` in the clean ROM
  ($82:E010 is one of them); the panel said *2 functions off* on a clean ROM, and the
  Functions page ticked them. `function_off()` fixed both.
- **Found in the pictures:** the Frames column, sized once per refresh, was outgrown within
  seconds (*28,9…*); it now leaves room for seven digits. The idle bar had no background on
  the Mac (its alternate base is as white as the window), so the bars are a tint of the text
  or the warning colour over the window. *Back to normal: the clean game…* repeated the bar
  and is gone; so is the note about starting from a saved moment (the message says it).
- **What it looks like now:** the panel reads top-down as name, state, recipe, buttons,
  dice, rules, guard and ROM, with no word said twice; with no recipe the only text besides
  the Start card is the two status lines.
- **Left for later** (their own plans): rules in plain words and an editor beside the list,
  Functions filters and a status column, one Careful/Balanced/Wild safety choice, a cap on
  page width, sidebar counts, fewer nouns.
