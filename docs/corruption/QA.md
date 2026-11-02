# Corruption tooling: manual QA

What headless tests can't check: the panel, hotkeys, and what corruption looks and sounds
like. The engine, recipe format, library and backup logic have unit tests, and
`corrupt-smoke` checks recipes inside the real core.

Use **a copy** of a ROM with battery save RAM (Final Fantasy VI, Fire Emblem, Zelda) in a
scratch folder. By default Snes9x writes the `.srm` next to the ROM.

## 0. The first ten minutes (start here)
Use a game you have never set up (a fresh library folder, so no recipes exist for it).
1. Follow **Start here** in the guide: open the game (**File > Open**), then **Corruption > Show Panel**.
   - [ ] While no recipe is loaded, the panel shows the **Start here** card.
2. In the **Recipe** list, pick **Starter: Gentle WRAM noise**.
   - [ ] It loads (no Load button), and the **Start here** card goes away.
3. Press **Corrupt (F7)** and play.
   - [ ] The button reads *Stop (F7)*, and things start going slightly wrong.
4. Press **Re-roll (F8)**.
   - [ ] The seed changes, and the glitches are different.
5. Press **Back to Normal (Shift+F7)**.
   - [ ] The game goes back to the moment you pressed Corrupt, with the ROM clean.
   - [ ] The status says *Off. The game is back to normal.*
6. Click **Guide** at the top of the panel.
   - [ ] The guide opens.
7. Hover each button in the panel.
   - [ ] Every button shows a tooltip, and the ones with a hotkey name it (Corrupt: F7, Back to Normal: Shift+F7, Re-roll: F8).
8. **More > Share…**, and save the `.s9xrecipe` file.
   - [ ] One `.s9xrecipe` file is written.
9. **More > Choose the Library Folder…** and pick a different, empty folder. Open the same game, then **More > Import a Shared Recipe…** and pick the file.
   - [ ] The recipe lands in the Recipe list and loads, named like the original, or with a ` 2` after it if that name is taken.
10. Open the **Recipe** list and look at the starters.
   - [ ] All 15 are there: *Starter: Gentle WRAM noise*, *Low WRAM roulette*, *Music mangler*, *Palette drift*, *Sprite jitter*, *Stat swap on hotkey*, *Tile rot*, *Classic ROM sprinkle*, *Live ROM drip*, *ROM region shift*, *Branch flips*, *Operand nudge*, *Opcode swap*, *Pointer swap*, *Pointer roulette*.

## 1. Panel and library
1. `build/qt/snes9x-qt <copy>.sfc`, then **Corruption > Show Panel**.
2. **More > Choose the Library Folder…** (an empty folder, or your clone of the shared library repo).
   - [ ] The panel shows `<TITLE> [xxxxxxxx]`, and *Library: <path>* under the recipe row.
   - [ ] The Recipe list holds only the starters (*Starter: …*), no recipes of your own.
3. Pick **Starter: Palette drift** in the Recipe list, then **Save** and give it a name.
   - [ ] The game's own copy shows up in the list and is selected; `recipes/<name>.json` exists under the game folder.
   - [ ] The game folder has a `.gitignore` with `save-backups/`.

## 2. Corruption runs
1. Choose the recipe in the **Recipe** list (it loads), then press **Corrupt (F7)**.
   - [ ] The button reads *Stop (F7)*. The status reads *On for N s · N changes made*; the rule table shows `drift` and *cgram 0x0000..0x01FF: add -2..2 x4, every 30 frames* (columns *Rule*, *What it does*, *Strength*), with no sideways scroll bar.
   - [ ] Colours visibly change. A game that refreshes its palette every frame shows short flashes.
2. Repeat with **Starter: Tile rot**.
   - [ ] Graphics decay within a few seconds.
3. Untick the rule's **On** box.
   - [ ] The changes made stop counting up.

## 3. Hotkeys
1. **F7**:
   - [ ] the on-screen message says `Corruption OFF`; press again: `ON … seed N`. The Corrupt button follows.
2. **F8**:
   - [ ] `Corruption seed <new>`, and the seed field updates.
3. **Shift+F7**:
   - [ ] Back to Normal runs (see section 0).
4. Pick **Starter: Stat swap on hotkey**, press **Corrupt (F7)**, and press **F9** a few times.
   - [ ] The changes made go up by one per press.
5. Check *Options > Shortcuts…*.
   - [ ] It lists the *Corruption:* rows (Corrupt, Back to Normal, Re-roll, the hotkeys, More / Less Chaos, It Crashed, Save Recipe).

## 4. Save and reload a recipe
1. **More > Save As…** with the name `my test`.
   - [ ] `my-test.json` appears, with the current seed and the rule.
2. Edit `my-test.json` in a text editor (for example, `count` to 50), then **More > Look for New Recipe Files** and choose the recipe in the list again.
   - [ ] The change takes effect.
3. Break the JSON on purpose, then **More > Look for New Recipe Files**.
   - [ ] It's listed as *(broken)*, and choosing it shows the error with its path.
4. **More > Open This Game's Recipe Folder**.
   - [ ] The game's `recipes` folder opens.
5. **New**, add a rule, then **Save** (the recipe has no file yet).
   - [ ] It asks for a name. Before saving, the list shows the recipe as *(not saved yet)*.

## 5. Save backups
1. Load the game.
   - [ ] **Game Save Backups** lists a row like *<date time> · when the game loaded*.
2. Play until the game saves (or use a recipe with an `sram` rule), and wait over a minute.
   - [ ] A new automatic backup row appears.
3. Select the *when the game loaded* row, then **Restore This Save…**.
   - [ ] It asks first. After you confirm, the game resets with the old save, and a row for the save as it was before the restore appears.
4. Quit.
   - [ ] A backup from closing exists on the next launch.

## 6. Sound
1. Pick **Starter: Music mangler**, press **Corrupt (F7)**, and wait a few changes (5 s each).
   - [ ] The music audibly glitches. It may also stop entirely; that's expected.

## 7. Crash guard
1. Pick **Starter: Low WRAM roulette** in a game you know, press **Corrupt (F7)**, and play. Make sure **Catch crashes and go back a few seconds** is ticked and **Then** says *Re-roll the guilty rule's dice*.
   - [ ] Within a minute or two, a crash is caught: the message `Crash caught (…): back N s, reroll roulette` appears, and the game continues from slightly earlier.
   - [ ] The crash status says *N crashes caught. Last: …*, and the rule's *Crashes* column goes up too. With no crashes it says *No crashes yet.*
   - [ ] After a few crashes it says *It has learned N dangerous spots (rules can avoid them).*
   - [ ] `journal.<you>.jsonl` in the game's library folder has a `crash` line, and a `recovered` line after about 10 calm seconds.
2. Set **Then** to *Turn the guilty rule off* and wait for a crash.
   - [ ] The rule's **On** box unticks itself.
3. Try the other choices of **Then**: *Whatever the recipe says*, *Skip the guilty rule's last changes*, *Halve the guilty rule's strength*, *Re-roll twice, then halve, then turn off*.
   - [ ] Each one does what its name says on the next crash.
4. Open a text box or menu and leave it alone for 10+ s with a rule active.
   - [ ] No rollback.
5. Press **It Crashed: Go Back**.
   - [ ] It rolls back about 2 s.
6. Untick **Catch crashes and go back a few seconds**.
   - [ ] Crashes are no longer caught.
7. Fast-forward (Tab) with a rule active.
   - [ ] No false "frozen picture" rollbacks.

## 8. Rule editor
1. Choose any recipe, press **Corrupt (F7)**, and select a rule.
   - [ ] The pencil, copy and bin buttons (edit, duplicate, remove) become clickable; double-clicking the rule opens the editor too.
2. **Add Rule**, then **OK** with the defaults.
   - [ ] A `new rule` row appears and is selected; the status line says *· the recipe has unsaved changes*, and *On for N s* keeps counting (no restart).
3. Edit that rule: set **Memory** to `cgram (palette)`, **From** `0` **to** `0x1FF`, *What* `Add / subtract` −8..8, *When* every 10 frames.
   - [ ] The summary line under the form updates as you type.
   - [ ] Colours start changing within a second; any other rule carries on as before.
4. In the editor, set **to** to `0x300`.
   - [ ] **OK** greys out and a red line says cgram is only `0x200` bytes. Set it back.
5. Duplicate the rule (copy button), then remove the copy (bin button).
   - [ ] Remove asks first; the table is back to how it was.
6. **Save**.
   - [ ] *· the recipe has unsaved changes* goes away, and the recipe's JSON file has the new rule.
7. Edit something, then choose another recipe in the **Recipe** list.
   - [ ] It asks before discarding the changes. **No** keeps them.
8. **New**, **Add Rule**, then **More > Save As…** `scratch`.
   - [ ] `scratch.json` appears in the recipe list, and **Save** works on it afterwards.
9. If the game has a `regions.json` (add one by hand; see README), open the editor.
   - [ ] **Region** lists the names; picking one fills the range and greys it out, and the saved JSON uses the name.

## 9. Chaos and Strength
1. Pick **Starter: Tile rot** (or any visible recipe), press **Corrupt (F7)**, and drag **Chaos** down to 20 %.
   - [ ] The effect slows down right away, and the status line says *· the recipe has unsaved changes*.
2. Drag it to 0 %.
   - [ ] The changes made stop counting up. Back to 100 %: they resume, and *On for N s* never reset.
3. With two rules, drag one rule's **Strength** slider to 0 %.
   - [ ] Only that rule goes quiet.
4. Bind **Corruption: More Chaos** and **Less Chaos** under *Options > Shortcuts…*, and press them.
   - [ ] The message says `Corruption chaos 40%` and so on, and the Chaos slider follows.
5. Pick **Starter: Music mangler** (one byte each time), set its rule to 20 %, and listen for a minute.
   - [ ] Glitches come noticeably less often than at 100 %.
6. Set **Then** to *Halve the guilty rule's strength* and play **Starter: Low WRAM roulette** until a crash is caught.
   - [ ] The guilty rule's slider drops to half.
7. **Save**, then choose the recipe in the **Recipe** list again.
   - [ ] Chaos and the rule sliders come back where they were.

## 10. Where each run starts
1. Make sure **Remember where each run starts** is ticked. Choose a recipe, play to a memorable spot, and press **Corrupt (F7)**.
   - [ ] The status line says *start moment not saved yet*, and **Save** is enabled.
2. Play for 20 seconds with a visible recipe (Tile rot, Palette drift), then **Save**.
   - [ ] `recipes/<name>.anchor.frz` appears next to the JSON, and the JSON has `"anchor_state"`.
3. Walk somewhere else, then choose the same recipe in the **Recipe** list again.
   - [ ] The game jumps back to the spot from step 1 and the run starts; the status says *started from its saved moment*.
   - [ ] Without touching the controller, the corruption looks the same as the first time (same tiles or colours at the same times).
4. **Re-roll** (F8).
   - [ ] The status says *start moment not saved yet* again.
5. Pause the game (P) and press **Corrupt (F7)** off and on.
   - [ ] The status says *Starting with the next frame…* until you unpause.
6. Untick **Remember where each run starts** and choose the recipe again.
   - [ ] The game does not jump; the recipe starts where you are.
7. **More > Save As…** a recipe that started from its moment, under a new name.
   - [ ] The new name gets its own `.anchor.frz`, and loading it jumps to the same spot.

## 11. Reboots
1. Press **Reset** (Ctrl+R) with a recipe on.
   - [ ] No rollback: a reset from Snes9x is not a crash.
2. Play **Starter: Low WRAM roulette** (**Then**: *Re-roll the guilty rule's dice*) in Pocky & Rocky or another action game for a few minutes.
   - [ ] If the game drops back to its title screen or boot logos, the message says `Crash caught (the game rebooted itself)` and play continues from just before.
3. In a game with a soft-reset combo (often L+R+Start+Select), press it with corruption off.
   - [ ] The game resets normally; nothing is rolled back.

## 12. Memory viewer
1. In the middle of gameplay, press **Memory Viewer…** (on the rules row of the panel).
   - [ ] A window shows WRAM in hex, with the dropdowns **Memory**, **Show as** and **Colour by**; some areas glow orange and keep changing, others stay plain.
   - [ ] The strip on the right shows where the busy areas are; clicking it jumps there.
2. Pause the game (P), wait, then unpause and stand still.
   - [ ] While paused the picture holds still; after unpausing, areas that stopped changing fade within a couple of seconds.
3. Pick **Starter: Gentle WRAM noise**, press **Corrupt (F7)**, and set **Colour by** to **Hits**.
   - [ ] A legend line under the view explains the colours.
   - [ ] Purple bytes appear scattered above `0x2000` as the recipe fires; **Clear Hits** (shown only with Hits) clears them.
4. Press **Find Copies in WRAM**.
   - [ ] The list shows at least one `oam` or `cgram` copy in WRAM (most games have both). Clicking one selects it.
5. With a palette copy selected, **New Rule from Selection…**, choose *Add / subtract* −2..2, every 30 frames, count 4, **OK**.
   - [ ] The rule appears in the panel, and colours drift and stay changed, unlike the **Palette drift** starter.
6. Switch **Memory** to **vram**, type `0x4000` in **Go to** and press Enter.
   - [ ] The view jumps to row `0x04000`.
7. Close the viewer window.
   - [ ] The game runs as before (it stops watching when closed).
8. Look at the **Colour by** list for wram, then for rom.
   - [ ] Reads and Writes are offered only for wram; Code, Data, DMA and Changes only for rom.

## 13. Named areas
1. In the memory viewer, select a range (for example a palette copy from **Find Copies in WRAM**; shown as *Sprite and palette copies in WRAM* when the memory is wram, oam or cgram), **Name Selection…**, and call it `palette copy`.
   - [ ] It appears under **Named areas**, and the game's `regions.json` has it. Before any exist, it says *None yet. Select bytes, then Name Selection…*
2. **Add Rule** in the panel and open **Region** in the editor.
   - [ ] `palette copy (wram)` is listed; picking it fills and locks the range. Save the recipe.
   - [ ] The recipe's JSON says `"range": "palette copy"`.
3. Switch **Memory** to another one, then click `palette copy` in the list.
   - [ ] It switches back to WRAM and selects the range.
4. **Rename…** it to `palette`.
   - [ ] The message says recipes follow; the recipe's JSON now says `"palette"`, it still loads, and the status doesn't say the recipe has unsaved changes.
5. **Delete…** it.
   - [ ] The question names the recipe that uses it; afterwards the recipe still loads, with the range written out.

## 14. Memory map
1. In the memory viewer, look at the **What's where** list on the left with **wram** chosen.
   - [ ] One line per area, like *Name · 0x0000..0x1FFF*: *Busiest variables (direct page)*, *Stack and system (crashes fast)*, *Game data (bank $7E)* and *Big buffers (bank $7F)*; hovering shows what each is.
2. Switch **Memory** to **vram** during gameplay.
   - [ ] Italic lines appear: BG tiles and tile maps for the current mode, and two sprite tile tables. Clicking one selects it in the grid.
3. Make a rule from **BG1 tiles** (New Rule from Selection…, random, every 30 frames, count 4).
   - [ ] Background graphics get garbled, sprites don't.
4. Walk into another area or open a menu that changes the screen.
   - [ ] If the game moves its layers, the italic lines update.
5. Switch **Memory** to **aram**.
   - [ ] **Echo buffer** and **Sample directory** appear. A rule on the echo buffer makes the sound smear or crackle, not crash.

## 15. Pixel view
1. In the memory viewer during gameplay, set **Memory** to **wram**, **Show as** to **Pixels**, and **Colour by** to **Plain**.
   - [ ] All of WRAM fits in the window: black and white bands, coloured stripes and noisy areas, with addresses down the left.
   - [ ] The colours are easy to tell apart and nothing looks blurry or smeared.
2. Hover a few dots.
   - [ ] The bottom row shows the address and value, and the value matches the same byte in **Hex**.
3. Click **+** a few times, then Ctrl+wheel over one spot.
   - [ ] The dots get bigger, rows get shorter, and the spot under the mouse stays under the mouse. **−** stops at 1×, **+** at 16×.
4. Set **Colour by** to **Activity**.
   - [ ] The memory turns grey and the busy bytes glow orange; they flicker as the game runs.
5. Drag across a band, then switch to **Hex** and back.
   - [ ] The same range is selected in both, and each view shows it. **New Rule from Selection…** gets that range.
6. Click areas in **What's where**, then a found copy (Find Copies in WRAM).
   - [ ] Each is selected in the pixel view, with a blue bar beside its rows.
7. Switch **Memory** to **vram**, **cgram** and **oam** at a few zooms.
   - [ ] VRAM shows tile patterns; cgram comes out 32 bytes a row (one palette each), oam too.
8. Run a recipe with **Colour by: Hits**.
   - [ ] The recipe's writes show as purple dots on grey.
9. Switch **Memory** to **vram** and set **Row** to 64, then 66, then step back to 64 with the arrows.
   - [ ] At 64 the tile map areas (see What's where) look like upright blocky pictures; at 66 they lean. Each arrow click moves one byte.
   - [ ] Type 300 at 8×: rows scroll sideways, the addresses stay on the left, and clicking a dot still selects the right byte (check in **Hex**).
   - [ ] Switch **Memory** to **wram**: it's still on Auto. Back to **vram**: 64 again.
10. Resize the window and scroll with the plain wheel.
   - [ ] On Auto, rows reflow to the new width, the view stays near the same place, and the plain wheel scrolls without zooming.

## 16. Tile view
1. During gameplay, open the memory viewer, set **Memory** to **vram** and **Show as** to **Tiles**, and click **BG1 tiles** in What's where.
   - [ ] Real graphics appear: pieces of the background you see on screen, in the right colours. The format and palette boxes changed to match.
2. Click **Sprite tiles, first table**.
   - [ ] The characters' sprite pieces appear in their colours (palette 8 or nearby). Step the palette with the arrows: the colours change row by row; **Grey** shows plain greys.
3. Try **16** and **32 per row**, and zoom in and out (buttons and Ctrl+wheel).
   - [ ] Multi-tile sprites and font letters come out whole at some width; the zoom stays sharp, with faint lines between tiles from 3×.
4. Drag across a few sprite tiles.
   - [ ] They get a blue-and-white outline. Switch to **Hex**: the same bytes are selected, a whole number of tiles.
5. **New Rule from Selection…**: random, every 30 frames, count 4. Run it.
   - [ ] Just that character's graphics get garbled, live in the tile view too (set **Colour by** to **Hits** to see where).
6. Make a rule on **cgram** (palette drift) and watch the tile view.
   - [ ] The tiles change colour within a fraction of a second.
7. Hover a tile.
   - [ ] The bottom row shows its number and address.
8. Set **Memory** to **wram** in Tiles mode, 4 bpp, and scroll through.
   - [ ] Mostly noise, but some games show unpacked graphics somewhere; nothing slows down.

## 17. Crash map
1. Load a game, pick **Starter: Low WRAM roulette** (**Then**: *Re-roll the guilty rule's dice*), and press **Corrupt (F7)**. Play for 5 minutes.
   - [ ] Crashes get caught as before. The crash status adds *It has learned N dangerous spots (rules can avoid them).*
2. Open the library folder for the game.
   - [ ] There's a `crashmap.<you>.json` with lines like `"0x0320": [3, 1]`, sorted by address.
3. Open the memory viewer on **wram** with **Colour by: Danger**, in Hex, Pixels and Tiles.
   - [ ] Red marks where crashes happened, brighter where they happened more than once. Everything else is plain.
4. Edit the roulette rule and tick **Avoid dangerous bytes**. Its summary ends with "avoiding danger".
   - [ ] Over the next few minutes it crashes less than before (this is noisy; it's fine if it's only a little).
5. Close and reopen the game.
   - [ ] The Danger colouring and the panel's dangerous-spots count come back (the file was saved on close).
6. Copy the file to `crashmap.erik.json` in the same folder and reload the game.
   - [ ] The red gets stronger: both files count.

## 18. Crash counts in the viewer
1. Play a few minutes of **Starter: Low WRAM roulette** with the guard on, then open the memory viewer on **wram** in **Pixels**.
2. Set **Colour by** to **Crashes**.
   - [ ] Gold where crashes were blamed, brighter where there were more. Blocks that were only ever calm stay grey.
3. Hover a gold dot.
   - [ ] The line under the view adds `block 0x…: N crashes, M calm, danger 0.xx`, and `, hot` on hot blocks. Bytes the map knows nothing about show only the address and value.
4. If the panel says it has learned dangerous spots, switch **Colour by** to **Danger** and then **Crashes**.
   - [ ] A red tick sits in the address gutter beside each row with a hot block, at 1×. From 4× zoom up, hot blocks also get a white outline.
   - [ ] Activity, Hits and Plain show no ticks or outlines.

## 19. The game's reads and writes (WRAM)
1. Load a game and play to somewhere with sprites moving. Open the memory viewer on **wram**, **Pixels**.
2. Set **Colour by** to **Reads**.
   - [ ] Blue runs appear and change as you play. The sprite table copy (find it with **Find Copies in WRAM**) is solid blue: the game sends it to the PPU every frame.
3. Hover a blue dot.
   - [ ] `read N, written M a frame` after the value. A grey byte shows `read 0, written 0 a frame`.
4. Set **Colour by** to **Writes**.
   - [ ] Green where the game writes; the stack and the direct page are among the brightest.
5. Switch **Memory** to **vram**.
   - [ ] Reads and Writes are no longer offered in **Colour by**, and the hover has no read/written part.
6. Close the viewer and play on.
   - [ ] No slowdown with the viewer closed (the counting stops) or open.

## 20. ROM corruption
1. Load FF6 (a copy). Pick **Starter: Classic ROM sprinkle** and press **Corrupt (F7)**.
   - [ ] The game restarts from its boot. The *ROM (the Game Itself)* section says about *40 bytes changed, N of them used by the game so far.*
   - [ ] Edit the rule to **Bytes each time** 3000 and press Corrupt off and on: the game restarts again, visibly broken (garbled text, graphics or music).
2. **Re-roll** a few times.
   - [ ] Each re-roll restarts the game with a different corruption. The same seed (type it back into the seed field and press Enter) gives the same one.
3. Press **Corrupt (F7)** to turn it off.
   - [ ] The game keeps running corrupted; the ROM section still counts the changes. **Restore Clean ROM** makes it say *Clean: nothing has changed the game itself.*
4. Pick **Starter: Live ROM drip**, press **Corrupt (F7)** and play for two minutes.
   - [ ] No restart. The ROM section's byte count grows by about 2 bytes every 10 s.
5. Open the memory viewer on **rom**, in Pixels with **Colour by: Hits**.
   - [ ] The changed bytes light up. Scrolling through 6 MB stays smooth.
6. With a classic recipe on, **Export ROM…** (in the ROM section) and open the file in another emulator.
   - [ ] It boots with the same corruption, without a checksum warning.
7. Make a live rule that's sure to crash (a small range in the game's code, *Random value*, every 60 frames, **Bytes each time** 50) with the guard on.
   - [ ] Crashes are caught and rolled back, and the game keeps running: the ROM bytes from after the checkpoint are put back.
8. Press **Back to Normal (Shift+F7)**.
   - [ ] The ROM section says *Clean: nothing has changed the game itself.*

## 21. Editing memory like a picture
1. Load FF6, open the memory viewer on **rom**, **Show as: Pixels**, and set the row width until something lines up (graphics show as tiles at 16, 32 or 64).
2. Alt-drag a rectangle.
   - [ ] It's outlined and stays put when you scroll. The line below says its size, corner and row width.
3. **New Rule from Selection…**, choose **Shift the whole area**, right by 1, and **Once, when the run starts**. OK, then press **Corrupt (F7)**.
   - [ ] The game restarts, and in the viewer the rectangle's contents have moved one dot to the right.
4. Try **Flip**, **Levels → Posterize**, **Filter → Blur** and **Stamp** the same way.
   - [ ] Each changes only the rectangle; the rule's summary says what it does.
5. Switch on **Paint Mode**, **Value** 0x00, brush 4, and scribble over part of the ROM. Alt-click a dot.
   - [ ] The dots turn black as you drag; Alt-click puts that dot's value in the value box. The ROM section says *N bytes painted* (in *N survivors · N pinned · N bytes painted*).
6. **Save** the recipe, press **Corrupt (F7)** off and on.
   - [ ] The paint is still there after the restart (it's in the recipe as patches). **Clear Paint** (in the panel) empties the count; the next start is clean.
7. Select a rectangle, **Export PNG…**, open it in an image editor, draw on it, save it (same size, greyscale), and **Import PNG…**.
   - [ ] The drawing appears in the viewer and counts as painted bytes.

## 22. The code map
1. Load FF6 and let the intro play for a minute (or play a bit). Open the memory viewer on **rom** in **Pixels**, row width 256.
2. Set **Colour by** to **Code**.
   - [ ] Within a second, teal shows where the game's code is (FF6: the first 48 KB, bank `$C0`), opcodes brighter than operands. Everything else is dark grey.
3. Hover a teal dot.
   - [ ] The line below shows its CPU address, bytes, instruction and kind, e.g. `$C0:0053  22 06 00 C3 JSL $C30006 (jump)`. A dark dot shows only address and value.
4. Set **Colour by** to **Data**, then **DMA**.
   - [ ] Pink lines where the game reads tables; yellow-green where it copies graphics. Hover says `read as data` / `copied by DMA`.
5. Play on for a while; watch the ROM section.
   - [ ] *Known so far: N bytes of code, N of data, N of graphics (it learns as you play).* grows as you reach new places.
6. Close the game (or quit), then load it again.
   - [ ] The library's game folder has `codemap.<your name>.bin`, and the Code, Data and DMA colouring show what you saw last time straight away.
7. Copy someone else's `codemap.*.bin` into the folder (or rename yours to `codemap.erik.bin` and play a new area), and load the game.
   - [ ] Both maps show together.
8. Does the game run noticeably slower than before? (It should cost about 5%.)

## 23. Blame by execution
1. Load FF6 with a library set. Make a recipe with one rule: **Memory** rom, range `0x0`-`0xFFFF`, *Random value* 0-255, **When**: once, when the run starts, **Bytes each time** 300. Press **Corrupt (F7)** (the console restarts from the corrupted ROM).
2. Open the memory viewer on **rom**, **Pixels**, **Colour by: Changes**.
   - [ ] 300 orange dots in the first 64 KB; dim ones the game hasn't used, bright ones it has. Hovering says `changed from 0x…, used in N frames` or `not used yet`.
   - [ ] The ROM section says *300 bytes changed, N of them used by the game so far.*, and N grows as you play.
3. Play until it crashes (or let the intro run).
   - [ ] The message says something like `back 1.0 s, reverted 3 ROM bytes`, not `re-roll sprinkle`, and the game plays on.
   - [ ] The changed count dropped by only that many; the other orange dots are still there.
4. Keep going for a few minutes.
   - [ ] It doesn't give up and switch corruption off the way classic runs used to after a few crashes in a row (it still can, on a stubborn one).
5. Open the journal (`journal.<you>.jsonl` in the game's library folder).
   - [ ] Crash lines have `rom_action` and `rom_bytes` with the offsets.
6. Set **Colour by** to **Crashes** on the ROM.
   - [ ] The blocks of the reverted bytes show up.
7. Add `"rom_policy": "reroll"` to the recipe file and load it again; crash it again.
   - [ ] The message says `re-rolled N ROM bytes`, and those dots stay orange (a new value) instead of disappearing.

## 24. Code-aware corruption
1. Load FF6 and play (or let the intro run) for a few minutes, so the code map knows some code. Open the memory viewer on **rom** with **Colour by: Code** to see it.
2. Pick **Starter: Branch flips** and press **Corrupt (F7)** (the console restarts).
   - [ ] **Colour by: Changes** shows about 20 orange dots, all on teal (code) bytes. Hovering one shows a `BEQ`/`BNE`-style branch.
   - [ ] The game plays on, and something behaves oddly sooner or later.
3. Pick **Starter: Opcode swap**. Open its rule in the editor.
   - [ ] **What** says *Swap opcodes*, **Aim** has *All but these instructions* with `stack` and `width` ticked, and the choice of bytes is greyed out (code ops always aim at opcodes).
4. Make a rule on **rom**, range `0x0`-`0xFFFF`, *Add / subtract* -2..2, every 600 frames, **Bytes each time** 2, **Bytes: Operands** (under Aim), **Only these instructions**: `arith`, `compare`.
   - [ ] The summary line ends `operands of arith, compare`. OK is greyed out if you tick nothing under *Only*.
   - [ ] While it runs, every new orange dot is on a lighter-teal (operand) byte, never on a bright opcode.
5. Switch the rule's **Memory** to **wram**.
   - [ ] The **Aim** section disappears (it's hidden, not greyed), and a hint *To pick bytes by what they are…* shows instead.
6. Load a game you've never played (no code map), choose a recipe with an aimed rule, and press **Corrupt (F7)**.
   - [ ] It changes nothing: nothing is known to aim at yet.

## 25. Survivors and pins
1. Load FF6 (with a code map, see section 24) and **Starter: Branch flips**. Press **Corrupt (F7)** and play for a couple of minutes.
   - [ ] The ROM section grows *N survivors* (in *N survivors · N pinned · N bytes painted*).
2. Click **Survivors…**.
   - [ ] A list of changes with where (`$C0:5AD7`), what (`D0 → F0`), the instruction before and after (`BNE … → BEQ …`) and how many frames it was used. Data changes say `data`.
   - [ ] With nothing yet, it says so instead of showing an empty table.
3. Select one or two, type a note, **Pin Selected**.
   - [ ] They move to **Pinned in this recipe** with your note; the ROM section says *2 pinned*; the status says *· the recipe has unsaved changes*.
4. **Save** the recipe, press **Corrupt (F7)** to turn it off, untick the rule's **On** box, and press Corrupt again.
   - [ ] The console restarts, and **Colour by: Changes** shows just the pinned bytes, orange.
5. Make the game crash some other way (a ROM rule or **It Crashed: Go Back**).
   - [ ] The pinned bytes are never what gets reverted.
6. Select a pin, **Unpin Selected**, save.
   - [ ] It's gone from the list and the recipe file.
7. Open the recipe file in a text editor.
   - [ ] `"pins"` lists offset, value, `was` and the note. Change a `was` to a wrong value and load it: the panel says a pin was left out (made on another version).

## 26. Jump tables and pointer swaps
1. Load Goof Troop (or any game) and let it play for a couple of minutes, ideally through a few different scenes.
   - [ ] The ROM section says *N jump tables found, N with entries to swap*, and the numbers grow as new scenes show up.
2. Open the memory viewer on **rom**, **Colour by: Jumps**.
   - [ ] Blue marks: short runs of strong blue (entries the game used), lighter blue next to some (likely ones), and single marks at the instructions that jump.
   - [ ] Hovering a strong one says *entry N of M in a jump table for the `JMP ($xxxx,X)` at `$xx:xxxx`: goes to `$xx:xxxx`, jumped through N times*. A light one says *likely*. Hovering the instruction says it jumps through a table.
3. Pick **Starter: Pointer swap** and press **Corrupt (F7)** (the console restarts).
   - [ ] **Colour by: Changes** shows a few orange pairs of bytes, all on blue entries.
   - [ ] The game keeps running, and sooner or later something does the wrong thing (another behaviour, another screen, a step out of order) rather than crashing.
4. Press **Corrupt (F7)** a few more times with **Re-roll (F8)** between.
   - [ ] Most runs keep playing. Note the games and seeds that crash, and the ones that do something good.
5. Pick **Starter: Pointer roulette**, press **Corrupt (F7)**, then press hotkey **1** a few times while the game runs.
   - [ ] Each press adds orange marks (two entries each, both ways) and the game changes live. **Back to Normal** puts them all back.
6. Open a pointer swap rule in the editor.
   - [ ] **What** says *Swap pointers in jump tables (ROM)* with its two checkboxes; there's no **Aim** section; **How Much** says *Entries each time* and *Which entries*.
7. Load a game you've never played (no jump tables yet) and press **Corrupt (F7)** with **Pointer swap**.
   - [ ] It changes nothing: nothing is known to swap yet.
8. Look in the game's library folder.
   - [ ] There's a `jumps.<you>.json` next to `codemap.<you>.bin`, one jump a line.

## 27. Live heat
1. Load a game, play, and open the memory viewer on **rom**, **Show as: Pixels**, **Colour by: Live**.
   - [ ] Yellow streaks flicker where the game is running code and reading tables, and fade over a second or two when they stop.
   - [ ] Changing scene (a menu, a battle, a new area) moves the yellow somewhere else.
   - [ ] Hovering a yellow byte says *in use right now* (or *used in the last second or two*).
2. Switch the viewer to **wram** and back to **rom** after a while.
   - [ ] It doesn't flash everything that ran meanwhile: only what runs from now on lights up.
3. With a ROM recipe running (say **Starter: Branch flips**), compare **Changes** and **Live**.
   - [ ] Changed bytes the game is using right now glow in **Live** too.
4. The game runs as smoothly with the viewer on **Live** as on **Code**.

## 28. Instant revert
1. Load Goof Troop (or any game) and play for a couple of minutes, so the code map knows its code.
2. Pick **Starter: Code drip** and press **Corrupt (F7)**. Play on.
   - [ ] Now and then the status line says *Caught at $xx:xxxx (jumped into the middle of an instruction): reverted 1 ROM byte and played the frame again*, and the on-screen message says *caught a bad jump*.
   - [ ] When that happens the game doesn't visibly stop or jump back: at most a one-frame hitch.
   - [ ] The ROM section counts *N bad jumps put back at once*.
   - [ ] Crashes the guard catches (*Crash caught … back 1.5 s*) are rarer than before; some still happen (slow damage).
3. Open the memory viewer on **rom**, **Colour by: Changes**.
   - [ ] Orange bytes stay where the game keeps running them; the ones caught are gone.
4. Save the recipe, add `"instant_revert": false` to its file, load it again and play.
   - [ ] No more *Caught at*; the same kind of changes now end in crashes the guard catches and rolls back.
5. Load a save state (File > Load State) while **Code drip** is running.
   - [ ] The game continues from the loaded state; nothing jumps back to before it.
6. Look at the crash journal (`journal.<you>.jsonl` in the library).
   - [ ] Instant reverts are lines with `"event":"instant_revert"`, the kind of landing in `signal` and the bytes in `rom_bytes`.

## 29. Test before applying
1. Load Goof Troop (or any game) and play for a couple of minutes, so the code map knows its code.
2. Pick **Starter: Code drip** and press **Corrupt (F7)**. Play on for a few minutes.
   - [ ] Every two seconds there's a very short pause (the change being tried ahead). The picture and sound don't jump ahead or stutter backwards: the future is never shown or heard.
   - [ ] Now and then the status line says *Tested ahead: drip crashed the game (…), so it rolled again*; rarely *…so it's skipped this time*.
   - [ ] The ROM section counts *N changes tested first (N re-rolled, N skipped)*.
   - [ ] Fewer *Caught at* and *Crash caught* lines than in section 28, though not none (changes the game uses only later aren't tried).
3. Open the memory viewer on **rom**, **Colour by: Live**, during the pauses.
   - [ ] Nothing lights up from the test runs (only what the game really runs).
4. While **Code drip** runs, press a hotkey-driven recipe's key (say **Starter: Pointer roulette**, hotkey **1**).
   - [ ] Each press still does its swap (after a short pause): the press isn't lost to the test.
5. Save the recipe, add `"test_first": 0` to its file, load it again and play.
   - [ ] No pauses and no *Tested ahead* lines.
6. Load a save state, rewind if you use it, and fast-forward while **Code drip** runs.
   - [ ] Nothing odd: no frozen picture, no sound glitches beyond the short pauses.

## 30. Test at first use (removed)
Removed on 2026-11-01: it didn't pay off (see FEATURES.md, *Tried and removed*). Nothing to check.
   - [ ] **Try ROM changes ahead first** has no *Tested at first use* lines and the ROM section no *at first use* count.

## 31. Remote control from a browser
1. Load a game, pick a recipe with a hotkey rule (say **Starter: Pointer roulette**). In **Remote Control**, tick **Allow remote control from a browser**.
   - [ ] The line under it says *Listening on this computer, port 8095. Nobody connected.*
2. **Copy Link** (Public address empty) and open it in a browser on this computer.
   - [ ] The page shows the game and recipe; the address bar no longer shows the key.
   - [ ] The panel says *Connected: this computer*, and the status line *Remote: this computer connected*.
3. On the page: **Corrupt**, move **Chaos**, press hotkey **1**, turn a rule off and on, **Re-roll**.
   - [ ] Each one happens in the app at once, and the panel's status line says what the remote did.
   - [ ] Changes made in the panel show up on the page within a second.
3b. On the page, pick **Code drip** under *Starter recipe* and **Load**.
   - [ ] It asks first if a recipe is already loaded; then the app's panel shows Code drip and its rule, and the page lists the rule.
4. Memory: WRAM, tick **Paint**, value `FF`, tap the picture.
   - [ ] The byte changes (in the app's memory viewer too); the status line says *painted 1 byte of wram with 0xFF*.
5. **New Key** in the panel.
   - [ ] The page goes offline, then says the key is old. Opening a freshly copied link works again.
6. Through the tunnel, from a phone on mobile data, with **Public address** set:
   - [ ] The access gate asks for an email login first; after it, the page works as on this computer, and the panel names the email that connected.
   - [ ] Opening the bare address without logging in shows only the gate, never the page.
7. Untick **Allow remote control**.
   - [ ] The page goes offline; nothing listens on port 8095 (`nc -z 127.0.0.1 8095` fails).

## 32. Proven blame (removed)
Removed on 2026-11-01: it didn't pay off (see FEATURES.md, *Tried and removed*).
   - [ ] Crash Guard has no *Replay a crash that comes back to find its cause* switch, and no catch says *found by replaying it*.

## 33. Functions and kill switches
1. Load Goof Troop (or any game) and play for a few seconds. Look at the ROM section.
   - [ ] It says *N functions known* (a few dozen within seconds, more as you go to new places).
2. Open **Functions…** without pressing Corrupt.
   - [ ] The list shows addresses like `$80:99EE`, busiest now first, with *Now* (percent of the last 5 s), *Frames* and *Callers*.
   - [ ] A warning line says switches work while corruption is on.
   - [ ] Ticking **Off** does nothing and stays unticked.
3. Pick **Starter: Gentle WRAM noise** (or any recipe), press **Corrupt (F7)**, then in Functions tick **Off** on one of the top rows.
   - [ ] Something in the game stops or changes right away (a movement, a sound, the scroll, an enemy), and the game keeps running.
   - [ ] The status line under the table says it was turned off; the ROM section says *(1 off)*.
   - [ ] Unticking puts it back; the game behaves as before.
4. Try a few more of the busy ones.
   - [ ] One that breaks the game is caught: *caught a bad jump* or *Crash caught*, and its box unticks by itself.
5. Select a row, pick **Key 2**, write a note, **Keep in Recipe**.
   - [ ] *In Recipe* shows *key 2 · <note>*. Pressing hotkey 2 (panel, keyboard shortcut or remote page) turns it off; again turns it on.
6. **Save** the recipe, **Back to Normal**, then load it again and press Corrupt.
   - [ ] The switch is still there (key 2, the note), on until you press the key. With **Starts off** ticked and kept, it's off from the start.
7. **Starter: Function roulette**, Corrupt, press hotkey 1 a few times.
   - [ ] Each press turns one more busy function off; **Back to Normal** turns them all back on.
8. Close the game and open it again.
   - [ ] The list is there at once (from `calls.<you>.json` in the library's game folder).

## 34. Function bodies, guards and names
1. Load Goof Troop, play for a few seconds, open **Functions…**.
   - [ ] *Size* shows bytes for most rows (a dash for ones whose code the map hasn't seen run).
2. Select a busy row, type a **Name** (*scroll?*), press Return.
   - [ ] The *Name* column shows it; `names.<you>.json` appears in the library's game folder.
   - [ ] With a copy named `names.erik.json` (edit the name inside), the column shows *yours · theirs (erik)* after reopening the game.
3. Tick **Protect it from other corruption**.
   - [ ] The *Name* column says *protected*; the ROM section says *(1 guarded)*.
4. **Show in ROM**.
   - [ ] The memory viewer opens on the ROM, in Pixels, coloured by **Functions**, with the function's code selected; hovering a byte of it says *in function $80:…*, its name, its size and *guarded*.
5. Load **Starter: Code drip**, Protect a function again, press Corrupt and let it drip for a minute.
   - [ ] In the viewer with **Changes**, no changed byte lands inside the protected function's code.
6. In the viewer's Pixels view, **Paint Mode**, paint across the protected function's code.
   - [ ] The status line says *Kept N bytes as they were: guarded by $80:… (scroll?)*; those bytes don't change.
7. Tick **Off** on another function (corruption on).
   - [ ] Its code is guarded too while it's off (*guarded* in the column, strongest colour at its first byte in the viewer); untick and the guard goes.
8. Untick **Protect**, **Save** with Protect on, reload the recipe.
   - [ ] Protect comes back with the recipe (`"protect"` in the file).

