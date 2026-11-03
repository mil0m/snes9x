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

## The Workshop reads like a page, and its sidebar tells you things (#65)

Items 9 and 10 of the same review. On a wide window the Recipe, ROM, Safety and Sharing
pages stretched their text and tables across the whole width (*What it does* became a
1,300 px column with one short line in it), and the sidebar was only a list of names: to
know whether a function was off or a crash had happened you had to open the page.

- **A readable width.** Recipe, ROM, Safety and Sharing keep their content at most about
  100 characters wide (never less than the page needs), left-aligned next to the sidebar;
  the window's spare width stays empty. Functions and Memory keep the full width: a table
  of 500 functions and a hex view use every pixel.
- **A sidebar that's a summary.** Each page's row carries a short note on its right, in the
  quiet colour (the warning colour for a crash): *Recipe · 3 rules* (with *unsaved* when
  there is something to save), *Functions · 2 off*, *Memory · 40 painted*, *ROM · 14
  changed*, *Safety · 1 crash* (or *guard off*), *Sharing · 2 here* (others in the library)
  or *remote on*. Nothing to say: no note. The notes follow the session twice a second while
  the Workshop is open, whichever page shows; who's here is read every few seconds.
- **How**: a small item delegate draws the note right-aligned in the row (selected rows in
  the selection's text colour), so the names stay where they are; the sidebar is as wide
  as the longest name plus the longest note it can have. The notes' words come from one
  function, `sidebar_notes(PanelStatus, others)`, tested on its own.

Tests: `sidebar_notes` cases (each note, the empty ones, the warning tone); the Workshop
test reads the notes off the sidebar for a fake status, and checks that the Recipe page is
no wider than its cap in a 1,600 px window while Memory fills it; the tour read again.

### How it went (#65)

Built the same day, test first (three new widget tests), read in the tour with the copy of
the real library in the Mac look and Fusion dark.

- **Width:** in a full-width window the Recipe page's rules table is now about 650 points
  wide instead of 1,300, and *What it does* reads as a sentence next to its rule. A cap
  set once at construction would have been below what the Mac's polished controls need,
  so it's set again when the window shows.
- **Sidebar:** with *Code drip* running, *ROM · 14 changed* and *Recipe · 1 rule* show while
  Memory is open. The sidebar got wider by the longest note (*12 rules · unsaved*), which
  raised the Workshop's minimum width; a test that resized to 1,100 px had to grow.
- **Not done:** the notes don't count anything the session doesn't already count (no
  *named areas* on Memory, no per-person detail on Sharing beyond the number).

## Play-test findings (Mikael and Erik)

What came back from playing, one line each, with what was done about it.

- **"I want to be able to cancel New after tapping it; the button should say New Recipe."**
  The unsaved-changes question was *Discard them?* with Yes and No, so backing out wasn't
  obvious. It is now the Mac's **Save / Don't Save / Cancel** (Escape cancels, and Save on a
  new recipe asks for its name first; cancelling that cancels everything); the same
  question guards loading another recipe and importing. The button reads **New Recipe…**.
- **"The drawing crashed the game and the rollback didn't come; we want to finish a drawing
  before it goes in, and undo every stroke."** Built as #66
  ([plan](2026-11-02-paint-draft-plan.md)): ROM paint is a draft until **Apply Paint**, which
  is watched for ten seconds (corrupting or not) and taken back into the draft on a crash;
  every stroke undoes and redoes, with a history; *Start Games Paused* to paint before the
  first frame.
- **"Can the Workshop window be made smaller? It has a very large minimum size."** (#68) It
  couldn't go under 1,107 px (Fusion; about 1,250 points on the Mac): the Memory page's
  *Colour by* box was as wide as its longest choice (400 px), its toolbar held Go to and
  Clear Hits, *What's where* insisted on 190 px, and Export and Import PNG were two buttons.
  Now the boxes may be shorter than their longest choice (the list shows them whole), Go to
  and Clear Hits start the second row, Export/Import are one **Picture** menu, the list
  goes down to 130 px, and the Memory page scrolls in a short window with its picture at
  least 260 px tall. Smallest Workshop: 869 × 450 px in Fusion, 894 × 324 points on the Mac.
  The scroll areas take their page's minimum width, so a narrow window never cuts a page off
  (a test checks every page at the smallest size).
- **"Inside Add Rule, is all the What really applicable to all the Where?"** (#69) No: five
  (swap opcodes, flip branches, NOP out, swap pointers, turn functions off) work on code,
  which only the ROM has, but were offered for every memory and only refused with a red line
  after you picked one. Now **What follows Where**: the list is grouped under headings
  (*Bytes*, *Areas, like a picture*, *Code: the ROM only*), the code ones are greyed for
  other memories with a tooltip saying to choose rom, and switching memory away from the ROM
  with one of them picked moves to *Random value* and says why under the list. The "(ROM)"
  tags in their names went (the heading says it).
- **"Back to Normal, but stay at the current place and not reset the game: one more version
  of it next to it."** (#70) **Normal, Stay Here**, under Back to Normal in the panel and
  on the remote page: corruption off and the ROM back to clean between two frames, without
  loading the run's start. What rules did to RAM stays until the game overwrites it (it
  can't be undone without going back), which its tooltip says. The state bar reads *ROM
  clean, playing on from here*; the button greys out once there's nothing to clean.

## Functions: filters, a Guard column, and Off that stays ticked (#72)

From the review's leftovers (*Functions filters and a status column*) and a play-test
finding. Mikael: "Function filters next! Also a column if they've guarded. Also, when
tapping a checkbox right now they LOOK like they directly toggle off again, but that's just
a visual glitch until the game runs more."

**The glitch, found.** Clicking in the Workshop deactivates the game's window, and with
*Pause when unfocused* (on by default) the game pauses. A tick asks the engine for a flip at
the next frame; the page re-reads the ROM twice a second, the ROM still holds the old byte,
and the box unticks itself until the game runs again. The fix is in the session, not the
page: `functions()` counts the flips still queued for an entry (`Engine::pending_flips`) and
reports the state they lead to, with `pending` set so the tooltip can say *at the next
frame*. The page stays a plain view of what the session says.

**Filters.** A *Show* combo box beside the search field (a combo, not a row of chips: the
Workshop's minimum width is set by its widest page, #68):

| Show | Rows |
|---|---|
| All functions | every one (the default) |
| Running now | ran in the last 5 seconds (*Now* above 0%) |
| Off | switched off now (or at the next frame) |
| In play | a switch or button in the recipe, changes, or protected |
| Guarded | rules and paint leave its code alone, for whatever reason |
| Named | someone named it |
| Not named yet | nobody has |

The search field and the combo both apply. A quiet count says *12 of 545* when either hides
rows, so an empty table never looks like "no functions". The choice isn't remembered between
launches (the page has no settings of its own yet; cheap to add if missed).

**A Guard column**, after *Off*, the same kind of cell: a box to tick.
- Ticked = protected by the recipe (the same as *Protect it from other corruption* below,
  which follows it). The cell's text says *+ calls* when what it calls is protected too.
- Not ticked but guarded anyway (it's off, has changes in play, or a protected function calls
  it): the text *auto*, and the tooltip says why.
- No box when its code isn't known yet (nothing to protect), as *Off* does for a function
  called both ways.
- *protected* and *guarded* leave the *In play* column, which keeps switches, buttons,
  timers and changes: nothing is said twice.

**Tests (offscreen Qt, the fake):** each filter's rows, filter and search together, the
count; the Guard box protects and unprotects, *+ calls* and *auto* with their tooltips, no
box without a body; In play no longer says protected. **Session:** a unit test that a queued
toggle flips the reported state (two queued toggles cancel). No smoke numbers: nothing here
changes how a game is corrupted.

### How it went (#72)

- **The glitch was the pause, as suspected.** `EmuMainWindow` pauses on `WindowDeactivate`,
  and the Workshop is its own window, so every click in it paused the game with the flip
  still queued. `Engine::queued_state` replays the queued flips for an entry (a toggle twice
  cancels, On/Off win); the session's `functions()` uses it for the rows and the status's
  *functions off* count, so the panel's ROM line and the table agree on a paused game. The
  page didn't change for it beyond a tooltip.
- **Filters.** The combo and the search apply together, and only when one of them changes or
  the page is shown: a row you untick under *Off* stays until you filter again, so nothing
  jumps away under the mouse (the page's old rule). On the tour's Pocky & Rocky
  *Running now* leaves the 14 that ran in the last 5 seconds.
- **Guard column.** Ticking it is the same call as *Protect it…* below (`source.protect`,
  not deep); the box below follows when the row is selected. *protected* left the *In play*
  column.
- **Width:** the Functions page needs 460 px at most (its details), so the Workshop's
  smallest width is still set elsewhere (887 px in today's tour, the same with and without
  this change's page).
- **Not done:** the Show choice isn't remembered between launches; the clear button in the
  search field was tried and dropped (its icon button has no accessible name, which the
  audit catches).
- Tests 545/57/180 (unit/real-core/Qt), ASan clean, tour read (Functions, selected).

## Strength, for one rule and for all rules (#73)

From the review's leftovers (*Chaos vs Strength*). Mikael: "Chaos vs strength is very
unclear to me." Then, after hearing what they do: keep both (with ten rules on, one slider
outside is quicker than editing each), "let's name these the same if we keep both. But say
that the outer one is for all rules", and keep them multiplied. The other half of the
complaint: "since a rule can be super complicated … and then you have like strength
outside. How do you know what that means?"

**What they are (unchanged).** A rule's *Strength* and the recipe's *Chaos* are the same
kind of knob at two levels; a rule hits at its own × the recipe's (`tick_rule`). Below 100 %
it picks fewer bytes (count × strength), smaller deltas for *Add* and fewer bits for *Flip
bits*; once that is under one byte a firing, it fires that much less often instead. Area ops
always do their whole area, so for them it only means how often.

**Names.** *Chaos* becomes **Strength, all rules** in the panel, on the remote page and in
tooltips. Each rule's slider stays **Strength**. The recipe file keeps `intensity`, the remote
protocol keeps its `chaos` action (renaming wire names breaks shared pages for nothing).

**What it means, said for each rule.** A new `describe_strength(rule, all_rules)` in
`corrupt/rule_text` says what this rule does at this setting, in its own units, from the
same numbers `tick_rule` uses (`scaled_op` moves to `rule.h` so the text can't drift from
the engine):
- *At 50%: 4 of its 8 bytes each time it fires, adding -1..1 instead of -2..2.*
- *At 25% (50% × 50% for all rules): …* when all rules is below 100 %.
- *At 10%: it fires about 1 time in 10, changing 1 byte.*
- In the rule's own units: instructions for code ops, jump table entries, functions; *the
  whole area* for area ops; *the pair* for a fixed swap. *Does nothing* at 0 %.

Where it shows: the tooltip (and accessible description) of every rule slider, in the panel
and on the Workshop's Recipe page, following both sliders as they move; a line under
*Strength* in the rule editor (which now gets the recipe's all-rules value); a second line
under each rule on the remote page (touch has no hover). The all-rules slider's tooltip says
it multiplies: *a rule at 50% with this at 50% hits at 25%*.

**Not done:** no "→ 25%" next to each slider (the panel's rule column has no room, and the
tooltip says it); no presets.

**Tests:** `describe_strength` for each kind (bytes with Add and bit flips, thinning, area,
fixed swap, code/jump/kill units, the all-rules product, 0 %), and that its numbers match
`tick_rule` on a fake memory; the panel and Recipe page sliders' tooltips follow both
sliders; the editor's line; the remote status carries `effect`. No smoke numbers: nothing
changes how a game is corrupted.

### How it went (#73)

- **Named alike.** The panel's slider is *Strength, all rules* (form label, accessible name,
  tooltip saying they multiply), and so is the remote page's; the column headers say
  *times Strength, all rules*. The object name (`chaos`), the recipe's `intensity` and the
  remote action `chaos` stayed: nothing anyone sees, and old pages and files keep working.
- **What it means, per rule.** `describe_strength` is built on the engine's own numbers:
  `scaled_op` moved out of `rule.cpp`'s anonymous namespace into `rule.h`, and a test checks
  that the bytes and bits it promises are what `tick_rule` changes (8 bytes, 4 bits, at
  50% × 50%: 2 bytes, 1 bit each). Percentages keep one decimal, so a guard's halving shows
  as *12.5%*, not a rounded 13%.
- **Where.** `IntensitySlider` got a `describe` callback: its tooltip and the slider's
  accessible description follow every move, and `redescribe()` runs when the other slider
  changes (at once in the panel, on the next refresh for the Recipe page and when the remote
  moves it). The editor's line sits under *Strength* and follows the whole form (count, op);
  it gets all rules from `RecipeActions`. On the remote page the line sits under each rule's
  slider (`effect` in the status), since a phone has no hover.
- **Fit:** the label is longer than *Chaos*; the tour's running panel shows it whole, with
  the slider beside it.
- Tests 548/57/183 (unit/real-core/Qt), ASan clean, tour and the remote end-to-end read.

## Always a recipe: Untitled until it's saved (#74)

Mikael: "we create a new recipe from the workshop. But you can open the workshop and then
… do all of the changes and add rules and open a ROM … without ever having created a new
recipe. It seems so strange to me. … It's difficult to understand if some things are tied
to a recipe existing or not."

**What the code did.** Three behaviours with no rule behind them: *Add Rule* and *Corrupt*
refused without a recipe; turning a function off, Guard, painting, keeping a change and
pinning each made an unsaved recipe with a name of its own (*Switches*, *Protected*,
*Painted*, *Changes*, *Pinned*); names, what's learned, Safety and Sharing never belonged to
one, and no page said so.

**Choice (Mikael agreed): there is always a recipe, like a document in an editor.** Asking
for a name whenever the Workshop opens was the other option; it was turned down because the
Workshop is opened just to look (memory, names, Safety) as often as to build, and that would
leave a trail of throwaway recipes.
- **A game loads with an Untitled recipe** (empty, a fresh seed). Everything lands in it:
  rules, switches, guards, paint, changes, pins. The five made-up names go.
- **The name comes at Save** (Save on Untitled is Save As, as for a starter). *New Recipe*
  starts another Untitled without asking for a name (it still asks about unsaved work).
- **Blank** = Untitled with nothing in it (no rules, paint, pins, switches, protects,
  changes). Blank behaves as "no recipe" did where that was useful: the panel's *Start here*
  card shows, *Corrupt* and the run controls wait, the remote page offers starters. A re-roll
  or a strength change alone doesn't make it worth saving.
- **Unsaved work is never lost silently:** choosing another recipe, *New*, or *Import* with
  a non-blank Untitled (or any edited recipe) asks Save / Don't Save / Cancel, as now. Today
  that asks only when it has rules; it will ask for any content.
- **Which recipe you're editing, on every page:** the Workshop's window title is the
  recipe's name with the edited mark (*Untitled — Corruption Workshop*, the platform's
  document convention), and the top of the sidebar names it with its state (*not saved*,
  *saved*, *changed*). The panel's recipe list shows *Untitled (not saved)*.
- **Each page says what of it is the recipe's:** one line under the page's intro. Functions:
  Off, Guard, buttons and changes are saved in the recipe; names are shared with everyone
  for this game. Memory: paint is saved in the recipe. ROM: pins are; what's learned about the
  code is the game's. Safety: these switches are for the app (After a crash can follow the
  recipe). Sharing: nothing in it is the recipe's.

**Later (Mikael):** maybe group more into the recipe; this change makes the line visible
first.

**Tests:** session (fake-free parts in `tests/qt` with the fake control; the session's own
behaviour where it is testable): a loaded game has an Untitled blank recipe; switch/paint/
protect/change/pin land in it without renaming it; blank → not worth keeping, content →
asks; New makes Untitled without a name; Save on Untitled asks for a name. Panel: Start card
and Corrupt follow *blank*; the list shows *Untitled (not saved)*. Workshop: title and
sidebar header follow the recipe and its state; each page's scope line. Remote: a blank
recipe reads as none. No smoke numbers: nothing changes how a game is corrupted.

### How it went (#74)

- **The session.** `gameLoaded` ends with `newRecipeLocked({})`: an empty name means
  *Untitled*, `untitled` set and `saved_json` taken from it, so it isn't *edited* until
  something is in it. The five places that made their own recipe now make the same Untitled
  one (they only run with no game recipe at all, which no longer happens after a load).
  `blankLocked()` (Untitled, no rules, paint, pins, switches, protects or changes) keeps
  `setActive`/`toggleActive` from running an empty recipe and goes out in the status as
  `blank`; loading or saving a recipe clears `untitled`. The tour logs it on the real
  session: *untitled at load: "Untitled" untitled=1 blank=1 edited=0 rules=0*, then
  *after Add Rule: … blank=0 edited=1 rules=1* (Add Rule straight away, no "make a recipe
  first").
- **The panel** treats blank as it treated no recipe (Start card, *Choose a recipe…*,
  Corrupt and the run controls waiting), so a fresh game looks exactly as before; with
  something in it the list says *Untitled (not saved)*. **The remote page** reads a blank
  recipe as none.
- **New Recipe** lost its dialog and its ellipsis (button and menu); **Save** on Untitled
  offers *My recipe*, not *Untitled*. Unsaved-work questions now cover an Untitled recipe
  with any content, not only rules; a re-roll or strength change alone on a blank one isn't
  asked about.
- **The Workshop** has the recipe's name and state above its page list (on the list's own
  surface, so it reads as one sidebar) and in the window title with the edited mark. The
  sidebar's Recipe note is empty for a blank recipe (not *0 rules*). Scope lines: a
  sentence at the end of Functions' intro, one help line under the ROM and Safety headings,
  the Library help on Sharing, and Memory's paint status line ("kept in the recipe") so the
  page, which has no intro, doesn't grow taller.
- **Found on the way:** a fresh recipe's seed (up to 20 digits) showed its *end* in the
  panel's narrow seed field, since `setText` puts the cursor last; the field now shows the
  start and is only re-set when the seed changes.
- Narrowest Workshop still 887 px; tests 548/57/187 (unit/real-core/Qt), ASan clean, tour
  (28 pictures, three new) and the remote end-to-end read.

## Rejected: one Careful/Balanced/Wild safety choice (review item 15)

The review after #63 suggested replacing the Safety page's three switches (catch crashes,
try ROM changes ahead, put back a bad jump at once) and *After a crash* with one
Careful / Balanced / Wild choice, the switches tucked under *Details*. **Rejected by Mikael
(3 Nov), not to be rebuilt.** A preset hides what each setting does, and that makes it
truly impossible to understand. The direction here is the opposite: explain the details
themselves, in plain words, where they are, as #73 did for Strength. If the switches are
hard to parse, fix their words and explanations, not their visibility.

## The Safety page explains its switches (#76)

What item 15 should have been: keep every switch visible and say what it does. Each switch
and *After a crash* get a line under them, in Secondary tone, like Strength's line in #73:
what it watches for, what it does then, what it costs, what *off* means, whether it has
anything to do for this recipe now, and what it has done so far.

- **Catch crashes and go back a few seconds**: watches for the game stopping, restarting
  itself, or freezing or going black while you press buttons; rewinds a few seconds and
  deals with the rule that caused it (*After a crash*). Off: a crash stays, and Back to
  Normal gets the game back. The crash count and learned dangerous spots stay under it.
- **Test ROM changes before they reach the game** (was *Try ROM changes ahead first*): only
  rules that change the ROM; plays N seconds ahead (the recipe's `test_first`, 3 unless it
  says otherwise) unseen, tries up to 3 picks, skips the change if all crash; a short pause
  each time. *This recipe has no ROM rules, so there's nothing to test*; *this recipe turns
  it off*; *so far: 12 tested, 2 picked again, 1 skipped*.
- **Undo a ROM change the moment it derails the game** (was *Put back a ROM change that
  sends the game astray, at once*): when a changed ROM byte (a rule's, paint, a kept
  change) makes the game jump into something that isn't code, puts that byte back and plays
  the frame again, before the crash; watches every instruction while the ROM is changed,
  which costs a little speed. *The ROM isn't changed, so there's nothing to watch*; *this
  recipe turns it off*; *so far: 4 put back*.
- **After a crash**: the chosen action in a sentence (*the rule that caused it gets new
  random values*, *…halved, not below 5%, in the recipe*, *…turned off in the recipe; turn
  it back on in Recipe*, the Escalate ladder), plus *changed ROM bytes the game used are
  put back either way*.
- The lines are the switches' accessible descriptions; the long tooltips go (they said the
  same thing, hidden). `PanelStatus` gains `test_first_seconds`.

Tests: the four notes from statuses (no ROM rules, recipe off, counts, each policy) and the
renamed switches in `workshop_test.cpp`; the tour's Safety picture read.

### How it went (#76)

Built the same day, test first (two widget tests: the four notes from statuses, and the
page showing them under renamed switches with matching accessible descriptions).

- **Read in the tour** on Goof Troop with *Code drip*: the lines count while it runs (*So
  far: 8 tested, 1 picked again*). The *After a crash* line first sat a line lower than
  its menu (the form row was sized for three lines and centred two); it's top-aligned now.
- **The old tooltips are gone**: they said the same as the lines, hidden.
- Tests 548/57/189 (unit/real-core/Qt), ASan clean.
