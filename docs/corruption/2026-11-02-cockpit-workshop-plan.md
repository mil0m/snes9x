# Cockpit and Workshop: a panel for playing, one window for the rest (plan, roadmap #63)

**Status (2026-11-02):** planned. Asked for by Mikael after a Pocky & Rocky session: the
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
the viewer's *New Rule from Selection* lands on the Recipe page. The Workshop remembers
its size and last page.

### The Corruption menu

Show Panel · Open Workshop · New Recipe… · Save Recipe As… · Share… · Import… · Export
ROM… · Restore Clean ROM · Choose the Library Folder… · Guide.

## How it's built (Qt only; the engine doesn't change)

- `FunctionsDialog` and `SurvivorsDialog` become plain widgets (`QWidget`) that can be
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

(Filled in when it's built.)
