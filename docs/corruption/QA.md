# Corruption tooling: manual QA

What headless tests can't check: the panel, hotkeys, and what corruption looks and sounds
like. The engine, recipe format, library and backup logic have unit tests, and
`corrupt-smoke` checks recipes inside the real core.

Use **a copy** of a ROM with battery save RAM (Final Fantasy VI, Fire Emblem, Zelda) in a
scratch folder. By default Snes9x writes the `.srm` next to the ROM.

## 1. Panel and library
1. `build/qt/snes9x-qt <copy>.sfc`, then **Corruption > Show Panel**.
2. **Choose…** a library folder (an empty folder, or your clone of the shared library repo).
   - [ ] The panel shows `<TITLE> [xxxxxxxx]`, and the recipe list says *No recipes yet*.
3. Pick **palette-drift**, then **Add Example**.
   - [ ] It shows up and is selected; `recipes/palette-drift.json` exists under the game folder.
   - [ ] The game folder has a `.gitignore` with `save-backups/`.

## 2. Corruption runs
1. **Load**, then tick **Active**.
   - [ ] Status counts frames and firings; the rule table shows `drift`, `cgram`, `add -2..2 x4`, `every 30 frames`.
   - [ ] Colours visibly change. A game that refreshes its palette every frame shows short flashes.
2. Repeat with **tile-rot**.
   - [ ] Graphics decay within a few seconds.
3. Untick the rule's **On** box.
   - [ ] Firings stop counting up.

## 3. Hotkeys
1. **F7**:
   - [ ] the on-screen message says `Corruption OFF`; press again: `ON … seed N`.
2. **F8**:
   - [ ] `Corruption seed <new>`, and the seed field updates.
3. Add **stat-swap-hotkey**, load it, activate, and press **F9** a few times.
   - [ ] Firings go up by one per press.
4. Check *Settings > Shortcuts*.
   - [ ] It lists the twelve *Corruption:* rows.

## 4. Save and reload a recipe
1. **Save As…** with the name `my test`.
   - [ ] `my-test.json` appears, with the current seed and the rule.
2. Edit `my-test.json` in a text editor (for example, `count` to 50), then **Rescan** and **Load**.
   - [ ] The change takes effect.
3. Break the JSON on purpose, then **Rescan**.
   - [ ] It's listed as *(broken)*, and **Load** shows the error with its path.

## 5. Save backups
1. Load the game.
   - [ ] **Save Backups** lists a `load` entry.
2. Play until the game saves (or use a recipe with an `sram` rule), and wait over a minute.
   - [ ] An `auto` entry appears.
3. Select the `load` entry, then **Restore**.
   - [ ] The game resets with the old save, and a `before-restore` entry appears.
4. Quit.
   - [ ] A `close` backup exists on the next launch.

## 6. Sound
1. Add, load and activate **music-mangler**, and wait a few firings (5 s each).
   - [ ] The music audibly glitches. It may also stop entirely; that's expected.

## 7. Crash guard
1. Add, load and activate **low-wram-roulette** in a game you know, and play.
   - [ ] Within a minute or two, a crash is caught: the message `Crash caught (…): back N s, reroll roulette` appears, and the game continues from slightly earlier.
   - [ ] The panel's crash count goes up, and the rule's *Crashes* column too.
   - [ ] `journal.<you>.jsonl` in the game's library folder has a `crash` line, and a `recovered` line after about 10 calm seconds.
2. Set the policy to **disable** and wait for a crash.
   - [ ] The rule unticks itself.
3. Open a text box or menu and leave it alone for 10+ s with a rule active.
   - [ ] No rollback.
4. Press **Mark Crash**.
   - [ ] It rolls back about 2 s.
5. Untick **Detect crashes**.
   - [ ] Crashes are no longer caught.
6. Fast-forward (Tab) with a rule active.
   - [ ] No false "frozen picture" rollbacks.
