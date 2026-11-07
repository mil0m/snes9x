# Corruption tooling: manual QA

What headless tests can't check: the panel, the Workshop, the F7 key, and what corruption looks and sounds
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
4. Press **Re-roll** (next to **Seed**).
   - [ ] The seed changes, and the glitches are different.
5. Press **Back to Normal**.
   - [ ] The game goes back to the moment you pressed Corrupt, with the ROM clean.
   - [ ] The state bar says *Not corrupting · back to normal*.
6. Click the **?** button at the top of the panel.
   - [ ] The guide opens.
7. Hover each button in the panel.
   - [ ] Every button shows a tooltip, and Corrupt names its key (F7). Back to Normal and Re-roll have no key.
8. **Corruption > Share Recipe…** (or the Workshop's **Recipe** page, **Share…**), and save the `.s9xrecipe` file.
   - [ ] One `.s9xrecipe` file is written.
9. In the Workshop's **Sharing** page, **Choose…** a different, empty library folder. Open the same game, then **Corruption > Import a Shared Recipe…** and pick the file.
   - [ ] The recipe lands in the Recipe list and loads, named like the original, or with a ` 2` after it if that name is taken.
10. Open the **Recipe** list and look at the starters.
   - [ ] All 15 are there: *Starter: Gentle WRAM noise*, *Low WRAM roulette*, *Music mangler*, *Palette drift*, *Sprite jitter*, *Stat swap on a button*, *Tile rot*, *Classic ROM sprinkle*, *Live ROM drip*, *ROM region shift*, *Branch flips*, *Operand nudge*, *Opcode swap*, *Pointer swap*, *Pointer roulette*.

## 1. Panel and library
1. `build/qt/snes9x-qt <copy>.sfc`, then **Corruption > Show Panel**.
2. Workshop > **Sharing** > **Library** > **Choose…** (an empty folder, or your clone of the shared library repo).
   - [ ] The panel shows `<TITLE> [xxxxxxxx]`, and the Sharing page shows *Library: <path>*.
   - [ ] The Recipe list holds only the starters (*Starter: …*), no recipes of your own.
3. Pick **Starter: Palette drift** in the Recipe list, then **Save** and give it a name.
   - [ ] The game's own copy shows up in the list and is selected; `recipes/<name>.json` exists under the game folder.
   - [ ] The game folder has a `.gitignore` with `save-backups/`.

## 2. Corruption runs
1. Choose the recipe in the **Recipe** list (it loads), then press **Corrupt (F7)**.
   - [ ] The button reads *Stop (F7)*. The state bar reads *Corrupting · 0:NN · N changes*, in blue; the rule table shows `drift` and *cgram 0x0000..0x01FF: add -2..2 x4, every 30 frames* (columns *Rule*, *What it does*, *Strength*), with no sideways scroll bar.
   - [ ] Colours visibly change. A game that refreshes its palette every frame shows short flashes.
2. Repeat with **Starter: Tile rot**.
   - [ ] Graphics decay within a few seconds.
3. Untick the rule's **On** box.
   - [ ] The changes made stop counting up.

## 3. The F7 key and the buttons
1. Open *Options > Shortcuts…* and look at the *Corruption:* rows.
   - [ ] Only one: *Corruption: Corrupt / Stop*, set to F7. No Back to Normal, Re-roll, Chaos, Mark Crash, Save Recipe or hotkey rows. Close the dialog.
2. Press **F7**.
   - [ ] The on-screen message says `Corruption ON … seed N`, and the panel's button reads *Stop (F7)*. Press again: `Corruption OFF`, and the button reads *Corrupt (F7)*. Shift+F7, F8 and F9 do nothing.
3. Load **Starter: Stat swap on a button** (its rule fires on a button press). In the Workshop's **Functions** page, keep a switch (a busy function, **Button 2**, **Keep in Recipe**) so the recipe has a rule button and a kept switch. **Save**.
   - [ ] The panel's **Buttons** section shows one named button for the rule (named after the rule) and one for the switch (named after what it does, e.g. *gravity off* if you named the function, otherwise its address).
   - [ ] A recipe with no buttons shows no **Buttons** section.
4. Press **F7** to corrupt, then press the rule's button a few times.
   - [ ] The changes made go up by one per press. The message line says what was pressed.
5. Press the switch's button.
   - [ ] The button stays lit and the function goes off. Press again: it goes dark and the function is back on.
6. Open the remote page (section 31, steps 1-2).
   - [ ] It shows the same named buttons; pressing one does the same as in the panel, and the toggle's lit state follows.
7. Check that the *When* box in the rule editor and the Functions page's combo say *When I press a button* and *No button / Button 1…9*.
   - [ ] No *hotkey* or *key N* wording anywhere in the app.

## 4. Save and reload a recipe
1. Workshop > **Recipe** > **Save As…** (or **Corruption > Save Recipe As…**) with the name `my test`.
   - [ ] `my-test.json` appears, with the current seed and the rule.
2. Edit `my-test.json` in a text editor (for example, `count` to 50), then Workshop > **Sharing** > **Look for New Recipes** and choose the recipe in the list again.
   - [ ] The change takes effect.
3. Break the JSON on purpose, then **Look for New Recipes** again.
   - [ ] It's listed as *(broken)*, and choosing it shows the error with its path.
4. Workshop > **Sharing** > **Open This Game's Folder**.
   - [ ] The game's library folder opens, with its `recipes` folder.
5. **New Recipe**, add a rule, then **Save** (the recipe has no file yet).
   - [ ] It asks for a name. Before saving, the list shows the recipe as *Untitled (not saved)*.

## 5. Save backups
1. Load the game.
   - [ ] The Workshop's **Safety** page, **Game Save Backups**, lists a row like *<date time> · when the game loaded*.
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
1. Pick **Starter: Low WRAM roulette** in a game you know, press **Corrupt (F7)**, and play. Open the Workshop's **Safety** page: make sure **Catch crashes and go back a few seconds** is ticked and **After a crash** says *Re-roll the guilty rule's dice*.
   - [ ] Within a minute or two, a crash is caught: the message *Crash caught: went back N s, re-rolled roulette* appears (hover it for the crash's cause), and the game continues from slightly earlier.
   - [ ] The panel's guard line says *Crash guard on · N crashes caught*, the Safety page says *N crashes caught. Last: …*, and the rule's *Crashes* column goes up too. With no crashes it says *Crash guard on · no crashes yet*.
   - [ ] After a few crashes it says *It has learned N dangerous spots (rules can avoid them).*
   - [ ] `journal.<you>.jsonl` in the game's library folder has a `crash` line, and a `recovered` line after about 10 calm seconds.
2. Set **After a crash** to *Turn the guilty rule off* and wait for a crash.
   - [ ] The rule's **On** box unticks itself.
3. Try the other choices of **After a crash**: *Whatever the recipe says*, *Skip the guilty rule's last changes*, *Halve the guilty rule's strength*, *Re-roll twice, then halve, then turn off*.
   - [ ] Each one does what its name says on the next crash.
4. Open a text box or menu and leave it alone for 10+ s with a rule active.
   - [ ] No rollback.
5. Press **It Crashed** in the panel (and **It Crashed: Go Back** on the Safety page).
   - [ ] It rolls back about 2 s.
6. Untick **Catch crashes and go back a few seconds**.
   - [ ] Crashes are no longer caught.
7. Fast-forward (Tab) with a rule active.
   - [ ] No false "frozen picture" rollbacks.

## 8. Rule editor
1. Choose any recipe, press **Corrupt (F7)**. In the panel, double-click a rule.
   - [ ] The rule editor opens. In the Workshop's **Recipe** page, selecting a rule makes the pencil, copy and bin buttons (edit, duplicate, remove) clickable.
2. Workshop > **Recipe** > **Add Rule…**, then **OK** with the defaults.
   - [ ] A `new rule` row appears and is selected; **Save** becomes enabled, and the state bar's time keeps counting (no restart).
3. Edit that rule: set **Memory** to `cgram (palette)`, **From** `0` **to** `0x1FF`, *What* `Add / subtract` −8..8, *When* every 10 frames.
   - [ ] The summary line under the form updates as you type.
   - [ ] Colours start changing within a second; any other rule carries on as before.
4. In the editor, set **to** to `0x300`.
   - [ ] **OK** greys out and a red line says cgram is only `0x200` bytes. Set it back.
5. Duplicate the rule (copy button), then remove the copy (bin button).
   - [ ] Remove asks first; the table is back to how it was.
6. **Save**.
   - [ ] **Save** is disabled again, and the recipe's JSON file has the new rule.
7. Edit something, then choose another recipe in the **Recipe** list.
   - [ ] It asks before discarding the changes. **No** keeps them.
8. **New Recipe**, **Add Rule…**, then **Save As…** `scratch` (Recipe page).
   - [ ] `scratch.json` appears in the recipe list, and **Save** works on it afterwards.
9. If the game has a `regions.json` (add one by hand; see README), open the editor.
   - [ ] **Region** lists the names; picking one fills the range and greys it out, and the saved JSON uses the name.

## 9. Strength, per rule and for all rules
1. Pick **Starter: Tile rot** (or any visible recipe), press **Corrupt (F7)**, and drag **Strength, all rules** down to 20 %.
   - [ ] The effect slows down right away, and **Save** becomes enabled.
2. Drag it to 0 %.
   - [ ] The changes made stop counting up. Back to 100 %: they resume, and the state bar's time never resets.
3. With two rules, drag one rule's **Strength** slider to 0 %.
   - [ ] Only that rule goes quiet.
4. Drag **Strength, all rules** in steps, and hover a rule's slider after each.
   - [ ] Its tooltip follows, e.g. *At 40% (100% × 40% for all rules): …*.
5. Pick **Starter: Music mangler** (one byte each time), set its rule to 20 %, and listen for a minute.
   - [ ] Glitches come noticeably less often than at 100 %.
6. Set **After a crash** to *Halve the guilty rule's strength* and play **Starter: Low WRAM roulette** until a crash is caught.
   - [ ] The guilty rule's slider drops to half.
7. **Save**, then choose the recipe in the **Recipe** list again.
   - [ ] **Strength, all rules** and the rule sliders come back where they were.

## 10. Where each run starts
1. Make sure **Remember where each run starts** is ticked. Choose a recipe, play to a memorable spot, and press **Corrupt (F7)**.
   - [ ] The note under the buttons says *This run starts from a new moment: Save keeps it, so the run can be replayed.*, and **Save** is enabled.
2. Play for 20 seconds with a visible recipe (Tile rot, Palette drift), then **Save**.
   - [ ] `recipes/<name>.anchor.frz` appears next to the JSON, and the JSON has `"anchor_state"`.
3. Walk somewhere else, then choose the same recipe in the **Recipe** list again.
   - [ ] The game jumps back to the spot from step 1 and the run starts; the message line says *Starting from its saved moment*.
   - [ ] Without touching the controller, the corruption looks the same as the first time (same tiles or colours at the same times).
4. **Re-roll**.
   - [ ] The start-moment note shows again.
5. Pause the game (P) and press **Corrupt (F7)** off and on.
   - [ ] The state bar says *Starting…* until you unpause.
6. Untick **Remember where each run starts** and choose the recipe again.
   - [ ] The game does not jump; the recipe starts where you are.
7. **Save As…** (Recipe page) a recipe that started from its moment, under a new name.
   - [ ] The new name gets its own `.anchor.frz`, and loading it jumps to the same spot.

## 11. Reboots
1. Press **Reset** (Ctrl+R) with a recipe on.
   - [ ] No rollback: a reset from Snes9x is not a crash.
2. Play **Starter: Low WRAM roulette** (**After a crash**: *Re-roll the guilty rule's dice*) in Pocky & Rocky or another action game for a few minutes.
   - [ ] If the game drops back to its title screen or boot logos, the message says `Crash caught (the game rebooted itself)` and play continues from just before.
3. In a game with a soft-reset combo (often L+R+Start+Select), press it with corruption off.
   - [ ] The game resets normally; nothing is rolled back.

## 12. Memory viewer
1. In the middle of gameplay, press **Workshop** in the panel and choose the **Memory** page.
   - [ ] The page shows WRAM in hex, with the dropdowns **Memory**, **Show as** and **Colour by**; some areas glow orange and keep changing, others stay plain.
   - [ ] The strip on the right shows where the busy areas are; clicking it jumps there.
2. Pause the game (P), wait, then unpause and stand still.
   - [ ] While paused the picture holds still; after unpausing, areas that stopped changing fade within a couple of seconds.
3. Pick **Starter: Gentle WRAM noise**, press **Corrupt (F7)**, and set **Colour by** to **Hits**.
   - [ ] A legend line under the view explains the colours.
   - [ ] Purple bytes appear scattered above `0x2000` as the recipe fires; **Clear Hits** (shown only with Hits) clears them.
4. Press **Find Copies in WRAM**.
   - [ ] The list shows at least one `oam` or `cgram` copy in WRAM (most games have both). Clicking one selects it.
5. With a palette copy selected, **New Rule from Selection…**, choose *Add / subtract* −2..2, every 30 frames, count 4, **OK**.
   - [ ] The rule appears in the panel and the Recipe page, and colours drift and stay changed, unlike the **Palette drift** starter.
6. Switch **Memory** to **vram**, type `0x4000` in **Go to** and press Enter.
   - [ ] The view jumps to row `0x04000`.
7. Close the Workshop (or switch to another page).
   - [ ] The game runs as before (it stops watching when the Memory page isn't shown).
8. Look at the **Colour by** list for wram, then for rom.
   - [ ] Reads and Writes are offered only for wram; Code, Data, DMA and Changes only for rom.

## 13. Named areas
1. In the Workshop's **Memory** page, select a range (for example a palette copy from **Find Copies in WRAM**; shown as *Sprite and palette copies in WRAM* when the memory is wram, oam or cgram), **Name Selection…**, and call it `palette copy`.
   - [ ] It appears under **Named areas**, and the game's `regions.json` has it. Before any exist, it says *None yet. Select bytes, then Name Selection…*
2. Workshop > **Recipe** > **Add Rule…** and open **Region** in the editor.
   - [ ] `palette copy (wram)` is listed; picking it fills and locks the range. Save the recipe.
   - [ ] The recipe's JSON says `"range": "palette copy"`.
3. Switch **Memory** to another one, then click `palette copy` in the list.
   - [ ] It switches back to WRAM and selects the range.
4. **Rename…** it to `palette`.
   - [ ] The message says recipes follow; the recipe's JSON now says `"palette"`, it still loads, and **Save** stays disabled.
5. **Delete…** it.
   - [ ] The question names the recipe that uses it; afterwards the recipe still loads, with the range written out.

## 14. Memory map
1. In the Workshop's **Memory** page, look at the **What's where** list on the left with **wram** chosen.
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
1. In the Workshop's **Memory** page during gameplay, set **Memory** to **wram**, **Show as** to **Pixels**, and **Colour by** to **Plain**.
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
1. During gameplay, open the Workshop's **Memory** page, set **Memory** to **vram** and **Show as** to **Tiles**, and click **BG1 tiles** in What's where.
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
1. Load a game, pick **Starter: Low WRAM roulette** (**After a crash**: *Re-roll the guilty rule's dice*), and press **Corrupt (F7)**. Play for 5 minutes.
   - [ ] Crashes get caught as before. The crash status adds *It has learned N dangerous spots (rules can avoid them).*
2. Open the library folder for the game.
   - [ ] There's a `crashmap.<you>.json` with lines like `"0x0320": [3, 1]`, sorted by address.
3. Open the Workshop's **Memory** page on **wram** with **Colour by: Danger**, in Hex, Pixels and Tiles.
   - [ ] Red marks where crashes happened, brighter where they happened more than once. Everything else is plain.
4. Edit the roulette rule and tick **Avoid dangerous bytes**. Its summary ends with "avoiding danger".
   - [ ] Over the next few minutes it crashes less than before (this is noisy; it's fine if it's only a little).
5. Close and reopen the game.
   - [ ] The Danger colouring and the panel's dangerous-spots count come back (the file was saved on close).
6. Copy the file to `crashmap.erik.json` in the same folder and reload the game.
   - [ ] The red gets stronger: both files count.

## 18. Crash counts in the viewer
1. Play a few minutes of **Starter: Low WRAM roulette** with the guard on, then open the Workshop's **Memory** page on **wram** in **Pixels**.
2. Set **Colour by** to **Crashes**.
   - [ ] Gold where crashes were blamed, brighter where there were more. Blocks that were only ever calm stay grey.
3. Hover a gold dot.
   - [ ] The line under the view adds `block 0x…: N crashes, M calm, danger 0.xx`, and `, hot` on hot blocks. Bytes the map knows nothing about show only the address and value.
4. If the panel says it has learned dangerous spots, switch **Colour by** to **Danger** and then **Crashes**.
   - [ ] A red tick sits in the address gutter beside each row with a hot block, at 1×. From 4× zoom up, hot blocks also get a white outline.
   - [ ] Activity, Hits and Plain show no ticks or outlines.

## 19. The game's reads and writes (WRAM)
1. Load a game and play to somewhere with sprites moving. Open the Workshop's **Memory** page on **wram**, **Pixels**.
2. Set **Colour by** to **Reads**.
   - [ ] Blue runs appear and change as you play. The sprite table copy (find it with **Find Copies in WRAM**) is solid blue: the game sends it to the PPU every frame.
3. Hover a blue dot.
   - [ ] `read N, written M a frame` after the value. A grey byte shows `read 0, written 0 a frame`.
4. Set **Colour by** to **Writes**.
   - [ ] Green where the game writes; the stack and the direct page are among the brightest.
5. Switch **Memory** to **vram**.
   - [ ] Reads and Writes are no longer offered in **Colour by**, and the hover has no read/written part.
6. Close the Workshop and play on.
   - [ ] No slowdown with the Memory page closed (the counting stops) or open.

## 20. ROM corruption
1. Load FF6 (a copy). Pick **Starter: Classic ROM sprinkle** and press **Corrupt (F7)**.
   - [ ] The game restarts from its boot. The panel's ROM line says about *ROM: 40 bytes changed*, and the Workshop's **ROM** page says *40 bytes changed, N of them used by the game so far.*
   - [ ] Edit the rule to **Bytes each time** 3000 and press Corrupt off and on: the game restarts again, visibly broken (garbled text, graphics or music).
2. **Re-roll** a few times.
   - [ ] Each re-roll (**Re-roll**) restarts the game with a different corruption. The same seed (type it back into the seed field and press Enter) gives the same one.
3. Press **Corrupt (F7)** to turn it off.
   - [ ] The game keeps running corrupted; the ROM page still counts the changes. **Restore Clean ROM** (ROM page, or **Corruption > The ROM…**) makes it say *Clean: nothing has changed the game itself.*
4. Pick **Starter: Live ROM drip**, press **Corrupt (F7)** and play for two minutes.
   - [ ] No restart. The ROM page's byte count grows by about 2 bytes every 10 s.
5. Open the Workshop's **Memory** page on **rom**, in Pixels with **Colour by: Hits**.
   - [ ] The changed bytes light up. Scrolling through 6 MB stays smooth.
6. With a classic recipe on, **Export ROM…** (ROM page, or **Corruption > The ROM…**) and open the file in another emulator.
   - [ ] It boots with the same corruption, without a checksum warning.
7. Make a live rule that's sure to crash (a small range in the game's code, *Random value*, every 60 frames, **Bytes each time** 50) with the guard on.
   - [ ] Crashes are caught and rolled back, and the game keeps running: the ROM bytes from after the checkpoint are put back.
8. Press **Back to Normal**.
   - [ ] The ROM page says *Clean: nothing has changed the game itself.*

## 21. Editing memory like a picture
1. Load FF6, open the Workshop's **Memory** page on **rom**, **Show as: Pixels**, and set the row width until something lines up (graphics show as tiles at 16, 32 or 64).
2. Alt-drag a rectangle.
   - [ ] It's outlined and stays put when you scroll. The line below says its size, corner and row width.
3. **New Rule from Selection…**, choose **Shift the whole area**, right by 1, and **Once, when the run starts**. OK, then press **Corrupt (F7)**.
   - [ ] The game restarts, and on the Memory page the rectangle's contents have moved one dot to the right.
4. Try **Flip**, **Levels → Posterize**, **Filter → Blur** and **Stamp** the same way.
   - [ ] Each changes only the rectangle; the rule's summary says what it does.
5. Switch on **Paint Mode**, **Value** 0x00, brush 4, and scribble over part of the ROM. Alt-click a dot.
   - [ ] The dots turn black as you drag; Alt-click puts that dot's value in the value box. The ROM page says *N bytes painted* (in *N survivors · N pinned · N bytes painted*).
6. **Save** the recipe, press **Corrupt (F7)** off and on.
   - [ ] The paint is still there after the restart (it's in the recipe as patches). **Clear Paint** (ROM page) empties the count; the next start is clean.
7. Select a rectangle, **Picture ▾ > Export PNG…**, open it in an image editor, draw on it, save it (same size, greyscale), and **Picture ▾ > Import PNG…**.
   - [ ] The drawing appears on the Memory page and counts as painted bytes.

## 22. The code map
1. Load FF6 and let the intro play for a minute (or play a bit). Open the Workshop's **Memory** page on **rom** in **Pixels**, row width 256.
2. Set **Colour by** to **Code**.
   - [ ] Within a second, teal shows where the game's code is (FF6: the first 48 KB, bank `$C0`), opcodes brighter than operands. Everything else is dark grey.
3. Hover a teal dot.
   - [ ] The line below shows its CPU address, bytes, instruction and kind, e.g. `$C0:0053  22 06 00 C3 JSL $C30006 (jump)`. A dark dot shows only address and value.
4. Set **Colour by** to **Data**, then **DMA**.
   - [ ] Pink lines where the game reads tables; yellow-green where it copies graphics. Hover says `read as data` / `copied by DMA`.
5. Play on for a while; watch the ROM page.
   - [ ] *Known so far: N bytes of code, N of data, N of graphics (it learns as you play).* grows as you reach new places.
6. Close the game (or quit), then load it again.
   - [ ] The library's game folder has `codemap.<your name>.bin`, and the Code, Data and DMA colouring show what you saw last time straight away.
7. Copy someone else's `codemap.*.bin` into the folder (or rename yours to `codemap.erik.bin` and play a new area), and load the game.
   - [ ] Both maps show together.
8. Does the game run noticeably slower than before? (It should cost about 5%.)

## 23. Blame by execution
1. Load FF6 with a library set. Make a recipe with one rule: **Memory** rom, range `0x0`-`0xFFFF`, *Random value* 0-255, **When**: once, when the run starts, **Bytes each time** 300. Press **Corrupt (F7)** (the console restarts from the corrupted ROM).
2. Open the Workshop's **Memory** page on **rom**, **Pixels**, **Colour by: Changes**.
   - [ ] 300 orange dots in the first 64 KB; dim ones the game hasn't used, bright ones it has. Hovering says `changed from 0x…, used in N frames` or `not used yet`.
   - [ ] The ROM page says *300 bytes changed, N of them used by the game so far.*, and N grows as you play.
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
1. Load FF6 and play (or let the intro run) for a few minutes, so the code map knows some code. Open the Workshop's **Memory** page on **rom** with **Colour by: Code** to see it.
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
   - [ ] The ROM page grows *N survivors* (in *N survivors · N pinned · N bytes painted*).
2. On the ROM page, look at **Survivors and pins**.
   - [ ] A list of changes with where (`$C0:5AD7`), what (`D0 → F0`), the instruction before and after (`BNE … → BEQ …`) and how many frames it was used. Data changes say `data`.
   - [ ] With nothing yet, it says so instead of showing an empty table.
3. Select one or two, type a note, **Pin Selected**.
   - [ ] They move to **Pinned in this recipe** with your note; the ROM page says *2 pinned*; **Save** becomes enabled.
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
   - [ ] The ROM page says *N jump tables found, N with entries to swap*, and the numbers grow as new scenes show up.
2. Open the Workshop's **Memory** page on **rom**, **Colour by: Jumps**.
   - [ ] Blue marks: short runs of strong blue (entries the game used), lighter blue next to some (likely ones), and single marks at the instructions that jump.
   - [ ] Hovering a strong one says *entry N of M in a jump table for the `JMP ($xxxx,X)` at `$xx:xxxx`: goes to `$xx:xxxx`, jumped through N times*. A light one says *likely*. Hovering the instruction says it jumps through a table.
3. Pick **Starter: Pointer swap** and press **Corrupt (F7)** (the console restarts).
   - [ ] **Colour by: Changes** shows a few orange pairs of bytes, all on blue entries.
   - [ ] The game keeps running, and sooner or later something does the wrong thing (another behaviour, another screen, a step out of order) rather than crashing.
4. Press **Corrupt (F7)** a few more times with **Re-roll** between.
   - [ ] Most runs keep playing. Note the games and seeds that crash, and the ones that do something good.
5. Pick **Starter: Pointer roulette**, press **Corrupt (F7)**, then press **button 1** in the panel's **Buttons** a few times while the game runs.
   - [ ] Each press adds orange marks (two entries each, both ways) and the game changes live. **Back to Normal** puts them all back.
6. Open a pointer swap rule in the editor.
   - [ ] **What** says *Swap pointers in jump tables (ROM)* with its two checkboxes; there's no **Aim** section; **How Much** says *Entries each time* and *Which entries*.
7. Load a game you've never played (no jump tables yet) and press **Corrupt (F7)** with **Pointer swap**.
   - [ ] It changes nothing: nothing is known to swap yet.
8. Look in the game's library folder.
   - [ ] There's a `jumps.<you>.json` next to `codemap.<you>.bin`, one jump a line.

## 27. Live heat
1. Load a game, play, and open the Workshop's **Memory** page on **rom**, **Show as: Pixels**, **Colour by: Live**.
   - [ ] Yellow streaks flicker where the game is running code and reading tables, and fade over a second or two when they stop.
   - [ ] Changing scene (a menu, a battle, a new area) moves the yellow somewhere else.
   - [ ] Hovering a yellow byte says *in use right now* (or *used in the last second or two*).
2. Switch the Memory page to **wram** and back to **rom** after a while.
   - [ ] It doesn't flash everything that ran meanwhile: only what runs from now on lights up.
3. With a ROM recipe running (say **Starter: Branch flips**), compare **Changes** and **Live**.
   - [ ] Changed bytes the game is using right now glow in **Live** too.
4. The game runs as smoothly with the Memory page on **Live** as on **Code**.

## 28. Instant revert
1. Load Goof Troop (or any game) and play for a couple of minutes, so the code map knows its code.
2. Pick **Starter: Code drip** and press **Corrupt (F7)**. Play on.
   - [ ] Now and then the message line says *Caught a bad jump: reverted 1 ROM byte*; hovering it shows *Caught at $xx:xxxx (jumped into the middle of an instruction): reverted 1 ROM byte and played the frame again*, and the on-screen message says *caught a bad jump*.
   - [ ] When that happens the game doesn't visibly stop or jump back: at most a one-frame hitch.
   - [ ] The ROM page counts *N bad jumps put back at once*.
   - [ ] Crashes the guard catches (*Crash caught … back 1.5 s*) are rarer than before; some still happen (slow damage).
3. Open the Workshop's **Memory** page on **rom**, **Colour by: Changes**.
   - [ ] Orange bytes stay where the game keeps running them; the ones caught are gone.
4. On the Safety page, untick **Undo a ROM change the moment it derails the game**, and play.
   - [ ] No more *Caught at*; the same kind of changes now end in crashes the guard catches and rolls back.
5. Load a save state (File > Load State) while **Code drip** is running.
   - [ ] The game continues from the loaded state; nothing jumps back to before it.
6. Look at the crash journal (`journal.<you>.jsonl` in the library).
   - [ ] Instant reverts are lines with `"event":"instant_revert"`, the kind of landing in `signal` and the bytes in `rom_bytes`.

## 29. Test before applying
1. Load Goof Troop (or any game) and play for a couple of minutes, so the code map knows its code.
2. Pick **Starter: Code drip** and press **Corrupt (F7)**. Play on for a few minutes.
   - [ ] Every two seconds there's a very short pause (the change being tried ahead). The picture and sound don't jump ahead or stutter backwards: the future is never shown or heard.
   - [ ] Now and then the message line says *Tested ahead: drip would crash, so it picked again*; rarely *Tested ahead: drip would crash every time, so it's skipped this time* (hover for the full story).
   - [ ] The ROM page counts *N changes tested first (N re-rolled, N skipped)*.
   - [ ] Fewer *Caught at* and *Crash caught* lines than in section 28, though not none (changes the game uses only later aren't tried).
3. Open the Workshop's **Memory** page on **rom**, **Colour by: Live**, during the pauses.
   - [ ] Nothing lights up from the test runs (only what the game really runs).
4. While **Code drip** runs, press a button-driven recipe's button (say **Starter: Pointer roulette**, **button 1**).
   - [ ] Each press still does its swap (after a short pause): the press isn't lost to the test.
5. Save the recipe, add `"test_first": 0` to its file, load it again and play.
   - [ ] No pauses and no *Tested ahead* lines.
6. Load a save state, rewind if you use it, and fast-forward while **Code drip** runs.
   - [ ] Nothing odd: no frozen picture, no sound glitches beyond the short pauses.

## 30. Test at first use (removed)
Removed on 2026-11-01: it didn't pay off (see FEATURES.md, *Tried and removed*). Nothing to check.
   - [ ] **Test ROM changes before they reach the game** (Safety page) has no *Tested at first use* lines and the ROM page no *at first use* count.

## 31. Remote control from a browser
1. Load a game, pick a recipe with a button rule (say **Starter: Pointer roulette**). In the Workshop's **Sharing** page, **Remote Control**, tick **Allow remote control from a browser**.
   - [ ] The line under it says *Listening on this computer, port 8095. Nobody connected.*
2. **Copy Link** (Public address empty) and open it in a browser on this computer.
   - [ ] The page shows the game and recipe; the address bar no longer shows the key.
   - [ ] The Sharing page says *Connected: this computer*, and the message line *Remote: this computer connected*.
3. On the page: **Corrupt**, move **Chaos**, press button **1**, turn a rule off and on, **Re-roll**.
   - [ ] Each one happens in the app at once, and the panel's message line says what the remote did.
   - [ ] Changes made in the panel show up on the page within a second.
3b. On the page, pick **Code drip** under *Starter recipe* and **Load**.
   - [ ] It asks first if a recipe is already loaded; then the app's panel shows Code drip and its rule, and the page lists the rule.
4. Memory: WRAM, tick **Paint**, value `FF`, tap the picture.
   - [ ] The byte changes (in the app's Memory page too); the message line says *painted 1 byte of wram with 0xFF*.
5. **New Key** on the Sharing page.
   - [ ] The page goes offline, then says the key is old. Opening a freshly copied link works again.
6. Through the tunnel, from a phone on mobile data, with **Public address** set:
   - [ ] The access gate asks for an email login first; after it, the page works as on this computer, and the Sharing page names the email that connected.
   - [ ] Opening the bare address without logging in shows only the gate, never the page.
7. Untick **Allow remote control**.
   - [ ] The page goes offline; nothing listens on port 8095 (`nc -z 127.0.0.1 8095` fails).

## 32. Proven blame (removed)
Removed on 2026-11-01: it didn't pay off (see FEATURES.md, *Tried and removed*).
   - [ ] The Safety page has no *Replay a crash that comes back to find its cause* switch, and no catch says *found by replaying it*.

## 33. Functions and kill switches
1. Load Goof Troop (or any game) and play for a few seconds. Look at the panel's ROM line and the Workshop's **ROM** page.
   - [ ] They say *N functions known* (a few dozen within seconds, more as you go to new places).
2. Open the Workshop's **Functions** page without pressing Corrupt.
   - [ ] The list shows addresses like `$80:99EE`, busiest now first, with *Now* (percent of the last 5 s), *Frames* and *Callers*.
   - [ ] A warning line says switches work while corruption is on.
   - [ ] Ticking **Off** does nothing and stays unticked.
3. Pick **Starter: Gentle WRAM noise** (or any recipe), press **Corrupt (F7)**, then in Functions tick **Off** on one of the top rows.
   - [ ] Something in the game stops or changes right away (a movement, a sound, the scroll, an enemy), and the game keeps running.
   - [ ] The line under the table says it was turned off; the ROM line says *1 function off*.
   - [ ] Unticking puts it back; the game behaves as before.
4. Try a few more of the busy ones.
   - [ ] One that breaks the game is caught: *caught a bad jump* or *Crash caught*, and its box unticks by itself.
5. Select a row, pick **Button 2**, write a note, **Keep in Recipe**.
   - [ ] *In Recipe* shows *button 2 · <note>*. The panel gets a named button for it; pressing it (or the same button on the remote page) turns the function off; again turns it on.
6. **Save** the recipe, **Back to Normal**, then load it again and press Corrupt.
   - [ ] The switch is still there (button 2, the note), on until you press the button. With **Starts off** ticked and kept, it's off from the start.
7. **Starter: Function roulette**, Corrupt, press button 1 a few times.
   - [ ] Each press turns one more busy function off; **Back to Normal** turns them all back on.
8. Close the game and open it again.
   - [ ] The list is there at once (from `calls.<you>.json` in the library's game folder).

## 34. Function bodies, guards and names
1. Load Goof Troop, play for a few seconds, open the Workshop's **Functions** page.
   - [ ] *Size* shows bytes for most rows (a dash for ones whose code the map hasn't seen run).
2. Select a busy row, type a **Name** (*scroll?*), press Return.
   - [ ] The *Name* column shows it; `names.<you>.json` appears in the library's game folder.
   - [ ] With a copy named `names.erik.json` (edit the name inside), the column shows *yours · theirs (erik)* after reopening the game.
3. Tick **Protect it from other corruption**.
   - [ ] The *Name* column says *protected*; the ROM line says *1 function guarded*.
4. **Show in ROM**.
   - [ ] The Workshop switches to the **Memory** page on the ROM, in Pixels, coloured by **Functions**, with the function's code selected; hovering a byte of it says *in function $80:…*, its name, its size and *guarded*.
5. Load **Starter: Code drip**, Protect a function again, press Corrupt and let it drip for a minute.
   - [ ] In the Memory page with **Changes**, no changed byte lands inside the protected function's code.
6. In the Memory page's Pixels view, **Paint Mode** (the value and brush show only now), paint across the protected function's code.
   - [ ] The message line says *Kept N bytes as they were: guarded by $80:… (scroll?)*; those bytes don't change.
7. Tick **Off** on another function (corruption on).
   - [ ] Its code is guarded too while it's off (*guarded* in the column, strongest colour at its first byte in the Memory page); untick and the guard goes.
8. Untick **Protect**, **Save** with Protect on, reload the recipe.
   - [ ] Protect comes back with the recipe (`"protect"` in the file).

## 35. Changing how a function works
1. Goof Troop, a recipe on, Corrupt. Workshop > **Functions**, select a busy function, **Change It…**.
   - [ ] Its lines show (address, instruction, *Can Become*); a function with nothing to change says so.
2. Select a number line (`LDA #$..`), set another value, **Apply**.
   - [ ] *Now* says *now LDA #$..*; something in the game behaves differently (or nothing visible: try another).
   - [ ] **Put Back** undoes it; *Now* is empty again.
3. A branch line: **The opposite**, then **Always**, then **Never**, each with Apply.
   - [ ] The game keeps running or the crash guard catches it and puts it back.
4. A math line (`INC`, `ADC`…): Apply.
5. A call line: **Skip the call**, Apply; then **Call … instead**, Apply.
   - [ ] Near calls only list functions in the same bank.
6. Pick **Button 3**, untick **Starts on**, **Keep in Recipe**; press the panel's button 3 twice.
   - [ ] *Now* says *kept, button 3*; the button toggles it; the Functions page's *Name* column says *1 change*.
7. **Save**, **Back to Normal**, reload, Corrupt.
   - [ ] The change is back (`"changes"` in the file), off until you press button 3 (on from the start with Starts on).
8. A code rule (Starter: Branch flips), edit it, tick **Only inside the busiest functions**.
   - [ ] Its changes land only in function code (Memory page: **Functions** and **Changes**).

## 36. Timed switches
1. Workshop > Functions, select a busy function, tick **Timer**: off for 1 s every 3 s, **Keep in Recipe**, Corrupt.
   - [ ] It goes off and on by itself, every 3 s; the *Name* column says *off 1 s every 3 s*.
2. Change It…: a change **Kept with a timer** on for 0.5 s every 1 s.
   - [ ] It flickers.
3. **Starter: Function flicker** (or Function roulette with a trigger every 10 s and **Back on after** 5 s), Corrupt.
   - [ ] A function goes off every 10 s and comes back 5 s later (the ROM line's *off* count goes 1, 0, 1…).

## 37. Playing together live (shared library)
1. Two copies of the app on one computer: `SNES9X_PLAYER=a` and `SNES9X_PLAYER=b`, each with its own scratch config, both pointing at the same library folder; Goof Troop in both.
2. Play a minute in **a**, open the Workshop's **Functions** page, name a busy function *gravity* (press Return).
   - [ ] Within 30 s **b**'s message line says it picked up what the others learned; on **b**'s Functions page that function shows *gravity (a)*.
3. In **b**, save a recipe.
   - [ ] Within 30 s it shows in **a**'s recipe list, and **a**'s chosen recipe stays chosen.
4. Copy a recipe file to `<name>.sync-conflict-20261102-201512-ABCDEFG.json` in the game's `recipes/` folder.
   - [ ] It doesn't show in the list; within 30 s the message line says the sync kept two versions of it.
5. With Syncthing for real (README "Playing together live"): two computers, the always-on one holding the git clone.
   - [ ] A name set on one shows on the other within a minute; `tools/push.sh` on the always-on computer pushes a commit named *Session <date>: <games>*.


## 38. Cockpit and Workshop (#63)
1. Open a game with no recipe loaded, then **Corruption > Show Panel**.
   - [ ] The panel shows the game's name with a **Workshop** button and a **?** button, the **Start here** card, the recipe list, and the Corrupt and Back to Normal buttons. **Chaos**, **Seed** and **Re-roll** are hidden.
2. Pick a recipe with a few rules and a few buttons (for example the one from section 3, plus two more rules), in a normal-height window.
   - [ ] The **Start here** card goes away; **Chaos**, **Seed** and **Re-roll** appear; **Buttons** and **Rules** show. The whole panel fits without a vertical scroll bar.
   - [ ] Top to bottom: name and Workshop/?, state bar, recipe list and **Save**, message line, Corrupt (F7) and Back to Normal, the start-moment note (only when there is one), Chaos and Seed, Buttons, Rules, guard line with **It Crashed**, ROM line.
   - [ ] **Rules** shows only on/off, name and strength. Double-clicking a rule opens the editor.
3. Click the links: **Add or change rules**, then the guard line (*Crash guard on · …*), then the ROM line (*ROM: N bytes changed · …*).
   - [ ] Each opens the Workshop on the matching page (Recipe, Safety, ROM). Clicking a link while the Workshop is open switches its page instead of opening a second window.
4. Click through every Workshop page in the sidebar: Recipe, Functions, Memory, ROM, Safety, Sharing.
   - [ ] **Recipe**: name and notes; New Recipe, Save, Save As…, Share…, Import…; *Remember where each run starts*; the rules in full with Add Rule… and edit, duplicate and remove; **Also in This Recipe**.
   - [ ] **Functions**: same controls as before; **Change It…** opens its own dialog.
   - [ ] **Memory**: the memory viewer. The paint value and brush show only while **Paint Mode** is on.
   - [ ] **ROM**: what changed, what's learned, Export ROM…, Restore Clean ROM, Clear Paint, Survivors and pins.
   - [ ] **Safety**: the three crash guard switches, **After a crash**, crash count and last crash, **It Crashed: Go Back**, **Game Save Backups** (Restore This Save…, Open Folder).
   - [ ] **Sharing**: Library (Choose…, Open This Game's Folder, Look for New Recipes), Who's Here, Remote Control.
5. On **Functions**, select a function and press **Show in ROM**.
   - [ ] The Workshop switches to the **Memory** page, on the ROM, with the function's bytes selected.
6. Keep a switch (Functions: pick a busy function, **Button 1**, **Keep in Recipe**). Open **Recipe** > **Also in This Recipe**.
   - [ ] The switch is listed on one line. Select it and press **Remove from Recipe**: the line goes, the panel's button for it goes, and **Save** becomes enabled.
7. On **Safety**, untick **Undo a ROM change the moment it derails the game**. Load **Starter: Code drip**, press **Corrupt (F7)**, and play two minutes.
   - [ ] The ROM page's *bad jumps put back* count does not grow, and no *Caught at* lines appear (crashes are caught by the guard instead). Tick it again: the count grows again.
   - [ ] The switch is for the app, not the recipe: loading another recipe keeps it off (a recipe's own `"instant_revert": false` still turns it off when the switch is on).
8. Two copies of the app, `SNES9X_PLAYER=erik` and your own name, each with its own scratch config, both on the same library folder, same game. Play a minute in each (or learn something: name a function).
   - [ ] On **Sharing** > **Who's Here**, each copy lists both players, with when they last changed something, and *(you)* beside its own name.
   - [ ] With a `*.sync-conflict-*` file in the game's folder, a warning shows.
9. Open the **Corruption** menu.
   - [ ] It lists: Show Panel, Open Workshop, New Recipe, Save Recipe As…, Share Recipe…, Import a Shared Recipe…, The ROM (Export, Restore Clean)…, Library and Remote Control…, Guide…. Each opens what its name says (the Workshop page for the ROM and Library items, the guide for Guide…).
   - [ ] **Options > Shortcuts…** has only *Corruption: Corrupt / Stop*.
10. Switch the system between light and dark (or run with `TOUR_PLATFORM=cocoa`), and look at the panel and every Workshop page.
   - [ ] Text is readable, icons show in both, nothing is cut off, and the sidebar and pages use the same look as the rest of the app.

## 39. Panel quick wins (#64)
1. Open a game with no recipe, then pick a recipe in the list.
   - [ ] With no game the state bar reads *Load a game to start*; with a game and no recipe *Pick a recipe to start*; with a recipe and no run *Ready · press Corrupt to start*. Idle bars are light grey.
2. Press **Corrupt (F7)**, wait a few seconds.
   - [ ] The bar reads *Starting…*, then *Corrupting · 0:13 · 6 changes* (minutes:seconds, *1 change* for one), in the accent colour (blue). No *unsaved changes* text anywhere.
3. Press **Corrupt (F7)** again to stop.
   - [ ] The bar reads *Stopped · its changes stay until Back to Normal*, tinted as a warning.
4. Press **Back to Normal**.
   - [ ] The bar reads *Not corrupting · back to normal*, and the message line says nothing new.
5. Look at the bars in light and dark (system setting, or `TOUR_PLATFORM=cocoa`).
   - [ ] Corrupting (accent) and Stopped (tinted) are clearly different from each other and from the idle grey; the text is readable in both.
6. With a saved recipe loaded, move a rule's strength and press **Save** so a message shows (*Saved …*), and watch the panel.
   - [ ] The message fades after about 12 seconds. When it appears and fades, the Corrupt and Back to Normal buttons don't move.
7. Load Pocky & Rocky, make sure **Test ROM changes before they reach the game** (Safety page) is ticked, pick **Starter: Code drip**, press **Corrupt (F7)**, and play a few minutes.
   - [ ] Now and then the message line shows *Tested ahead: drip would crash, so it picked again*. Hover it: the full story shows.
8. Look at the panel's title.
   - [ ] It is the game's file name (for example *Pocky & Rocky (USA)*). Hover it: the library folder name (for example *POCKY ROCKY [72b2b3be]*) shows.
9. Press **Corrupt (F7)**. Click the seed number next to **Re-roll**, type `42`, press Enter.
   - [ ] The seed is a plain number with no box. The run restarts with seed 42 (the number reads 42).
10. Look at the guard line and the ROM line.
   - [ ] Each is one whole link with an icon and a chevron: *Crash guard on · no crashes yet ›*, *ROM: 12 bytes changed · 1 function off ›* or *ROM clean ›*. No separate *Safety* or *ROM* words.
   - [ ] Clicking the guard line opens the Workshop on **Safety**; clicking the ROM line opens **ROM**.
11. Pick **Starter: Tile rot** in the list.
   - [ ] A starter that has no file in the game's library yet shows as *<name> (new)* once loaded; after **Save** the *(new)* is gone.
12. Look at the **Functions** and **ROM** pages.
   - [ ] Neither has a **Refresh** button.
13. Corrupt for a minute. On **Functions**, select a function that isn't at the top, then switch to **Memory** and back.
   - [ ] The list is sorted busiest-first again, and the selected function is still selected. While you stay on the page, rows change in place and don't jump under the mouse.
14. On the **ROM** page, use a recipe with live ROM rules (**Starter: Code drip**), select a survivor row, and wait.
   - [ ] Survivors appear and update by themselves about every two seconds, and your selection stays.
15. On **Sharing**, untick **Allow remote control from a browser**.
   - [ ] **Public address**, **Copy Link** and **New Key** are hidden. Tick it again: they show.
16. Open the Workshop on **Safety**, make the window bigger, close it. Quit the app and start it again, load a game, press **Workshop**.
   - [ ] It opens on **Safety**, at the bigger size. **Corruption > Open Workshop** does the same. The panel's links still open their own page.
17. Load Pocky & Rocky with no ROM changes made (**Restore Clean ROM** if unsure) and look at the ROM line.
   - [ ] It reads *ROM clean ›*; it doesn't claim any function is off (one-byte stubs that are only a return don't count).

## 40. The Workshop's width and sidebar notes (#65)
1. Open the Workshop and make the window as wide as the screen.
   - [ ] Recipe, ROM, Safety and Sharing keep their text and tables about 100 characters wide, next to the list of pages; the rest of the window stays empty. Nothing is cut off at the right.
   - [ ] Functions and Memory still use the whole width.
2. With nothing loaded beyond a game, look at the list of pages.
   - [ ] No words beside any name.
3. Pick **Starter: Code drip**, press **Corrupt (F7)**, and stay on the **Memory** page.
   - [ ] Beside *Recipe* it says *1 rule*; beside *ROM* the number of changed bytes appears and goes up, without opening the ROM page.
4. On **Functions**, tick **Off** for one function; then paint a byte on **Memory**.
   - [ ] *Functions* says *1 off*; *Memory* says *1 painted*.
5. Move a rule's strength (Recipe page).
   - [ ] *Recipe* says *1 rule · unsaved*; after **Save** it says *1 rule* again.
6. Pick **Starter: Low WRAM roulette** and play until a crash is caught.
   - [ ] *Safety* says *1 crash* in the warning colour (white on the selected row). Untick **Catch crashes…**: it says *guard off*.
7. With someone else's files in the shared library for this game (or a copy of a library that has them), look at *Sharing*.
   - [ ] It says *1 here* (or more); with remote control ticked, *remote on*.
8. Turn on VoiceOver and move through the list of pages.
   - [ ] Each page is read with its words (*Functions, 1 off*).

## 41. Unsaved changes: Save, Don't Save, Cancel
1. Load a saved recipe, move a rule's strength (it now has unsaved changes), then press **New Recipe** (Recipe page).
   - [ ] It asks *Save the changes to "<name>" first?* with **Save**, **Don't Save** and **Cancel**.
   - [ ] **Cancel** (or Escape): nothing happens, no name is asked, the recipe and its change are still there.
2. Press **New Recipe** again and choose **Don't Save**.
   - [ ] An Untitled recipe starts (no name asked); the old recipe's change is gone.
3. Change the Untitled recipe (a rule, or just a function turned off), then pick another recipe in the panel's list and choose **Save**.
   - [ ] It asks for its name first (cancelling that cancels the switch); then it's saved and the other recipe loads. The same question comes for **Import…**.

## 42. Painting the ROM: draft, Apply, crash watch, history (#66)
1. Load Pocky & Rocky, don't press Corrupt. Workshop > Memory, **rom**, **Pixels**, **Paint Mode** on.
   - [ ] Under the toolbar: undo and redo arrows, *Paint here is a draft…*, **Apply Paint** (disabled) and **Throw Away Draft**. **Paint History** shows under *What's where*.
2. Zoom in at 0x000000, set **Value** DB, brush 8, and draw two strokes over the top rows.
   - [ ] The pixels change and are tinted, with a mark beside their rows; the game plays on unchanged. The history shows two *Stroke · N bytes at 0x… → DB* rows; the bar says *N bytes drafted*.
3. Press **Cmd+Z**, then **Shift+Cmd+Z**.
   - [ ] The second stroke disappears from the picture and greys out in the history, then comes back.
4. Press **Apply Paint**.
   - [ ] Within a second the game freezes or stops and comes back by itself (just before Apply); the bar says in orange *Your paint crashed the game: CPU stopped… It's back in the draft*; the panel's message says it too. The ROM page says the ROM is clean. The strokes are drafted again.
5. Undo both strokes; draw one over an area the **Code** colouring shows as unused (dark), and Apply.
   - [ ] *Applied N bytes: watching ten seconds for a crash*, then *Your paint held up for ten seconds: it stays*. The ROM page counts the changed bytes; the history shows *Applied · 1 stroke, N bytes*.
6. Press **Cmd+Z**.
   - [ ] The applied bytes come out of the game (ROM clean again) and the stroke is back in the draft.
7. **Throw Away Draft**, then **Cmd+Z**.
   - [ ] The draft empties (*Thrown away · 1 stroke*), and comes back.
8. Switch to **wram**, paint a stroke.
   - [ ] It changes in the game at once (no Apply button); **Cmd+Z** writes the old values back.
9. Safety page: untick **Catch crashes…**; Apply a crashing stroke on the ROM.
   - [ ] *Applied N bytes (the crash guard is off: no watch)*; the game stays crashed until you undo or go back to normal.
10. **Corruption > Start Games Paused**, then open the game again.
   - [ ] The game doesn't start; the panel says *Paused · Emulation > Run starts it*. The Memory page shows the ROM at once. Paint and Apply, then **Emulation > Run**: the game boots with the paint (or is taken back if it crashes within ten seconds).
11. On the remote page, paint the ROM.
   - [ ] It goes in at once and is taken back if it crashes within ten seconds.

## 43. Naming functions in the table (#67)
1. Workshop > Functions on a game with functions. Select a row with no name.
   - [ ] Its Name cell says *Double-click to name it*, readable whether the window is in front or not.
2. Double-click the Name cell, type `gravity`, press Return.
   - [ ] The cell shows *gravity*; the message says everyone sharing the library sees it; the **Name** field under the table shows *gravity* too.
3. Double-click it again: the editor starts with *gravity*. Type something else and press Esc.
   - [ ] Nothing changes.
4. Select another row and press Return (the Mac's edit key).
   - [ ] The Name cell opens for typing. Double-clicking Function, Frames or In play opens nothing.
5. Protect a function, or keep it on a button.
   - [ ] *protected* / *button N* shows in **In play**, not in Name, and the column is wide enough for it.
6. Empty a name and press Return.
   - [ ] *Name removed.*; with someone else's name for it in the library, theirs still shows (*jump (erik)*).

## 44. A small Workshop (#68)
1. Open the Workshop on **Memory** (rom, Pixels) and drag the window as small as it goes.
   - [ ] It gets to about 900 × 330 points. Nothing is cut off at the right; *Colour by* may be shortened (its list shows the whole choices). The picture stays tall enough to paint in, and the page scrolls to *Named areas*.
2. Open **Picture ▾**.
   - [ ] **Export PNG…** and **Import PNG…** are in it and work as before.
3. Visit every page at that size.
   - [ ] Each fits its width (Recipe, ROM, Safety and Sharing scroll vertically if they need to).

## 45. What follows Where in the rule editor (#69)
1. Workshop > Recipe > **Add Rule…**, with **Memory** wram. Open **What**.
   - [ ] The list has three bold headings, *Bytes*, *Areas, like a picture* and *Code: the ROM only*, that can't be picked. The five code options are greyed; hovering one says to choose rom under Where. No option says "(ROM)".
2. Set **Memory** to rom and open **What** again.
   - [ ] The code options can be picked. Pick *NOP out instructions*.
3. Set **Memory** back to wram.
   - [ ] **What** becomes *Random value*, and a line under it says *NOP out instructions works on the game's code, which only the rom has: switched to Random value.* OK stays enabled. Picking another option hides the line.
4. Edit an existing ROM rule that uses *Flip branches*.
   - [ ] It opens with *Flip branches* picked, and its settings page shows.

## 46. Normal, Stay Here (#70)
1. Pick **Starter: Code drip** on Pocky & Rocky, press **Corrupt (F7)**, play until the ROM line counts changed bytes, and walk somewhere new.
2. Press **Normal, Stay Here** (under Back to Normal).
   - [ ] Corruption stops, the ROM line says *ROM clean*, and the game carries on from exactly where you were (no jump back). The state bar says *Not corrupting · ROM clean, playing on from here*. **Normal, Stay Here** greys out; **Back to Normal** still works (back to the run's start).
3. Corrupt again for a while, then **Back to Normal**.
   - [ ] As before: back to the moment you pressed Corrupt.
4. On the remote page, press **Normal, stay here**.
   - [ ] The same happens in the app, and the page says *went back to normal, staying here*.

## 47. What the instructions mean, on hover (#71)
1. Workshop > Functions, select a busy function, **Change It…**.
2. Hover a few instructions: a number (`LDA #$..`), a branch (`BNE …`), a call (`JSR`/`JSL`), one with `,X`.
   - [ ] Each tooltip says what the instruction does in plain words, then what its operand is: *#$04 is the number itself: 4*, *…where it jumps to*, *…the function it calls*, *…X picks the entry*.
3. Hover the **Can Become** cells.
   - [ ] Each says what that kind of change does to the game (a number, a decision, a sum, a call).

## 48. Functions: filters, the Guard column, ticks on a paused game (#72)
1. Workshop > Functions on a game you've played a while (with the default *Pause when unfocused* on). Press **Corrupt (F7)** in the panel, then click in the Workshop: the game pauses.
2. Tick **Off** on a busy function and wait a few seconds without going back to the game.
   - [ ] The box stays ticked; its tooltip says *Turns off when the game runs its next frame*. The panel's ROM line already counts it as off.
3. Click the game window so it runs.
   - [ ] The function goes off (watch what stops); the box is still ticked and the tooltip no longer mentions the next frame.
4. Pick **Show: Off**.
   - [ ] Only that function is left, and *1 of N* shows beside the combo. **Show: All functions** brings everything back and the count goes away.
5. Pick **Running now**, then type part of an address in the search field.
   - [ ] Both apply: only functions that ran in the last 5 seconds and match the text.
6. Tick the **Guard** box on another function.
   - [ ] The message says it's protected; select the row: **Protect it from other corruption** is ticked under the table. Tick **And what it calls**: the Guard cell says *+ calls*.
7. Look at the function you turned off in step 2.
   - [ ] Its Guard cell says *auto*; hovering it says it's guarded while it's off. **Show: Guarded** lists it and the one you protected.
8. A function whose Size is *–* (its code isn't known yet).
   - [ ] No box in Guard; hovering says there's nothing to protect yet.

## 49. What Strength means, for each rule (#73)
1. Pick **Starter: Low WRAM roulette** (or any recipe with a rule that changes several bytes), press **Corrupt (F7)**.
   - [ ] The panel says **Strength, all rules**; the word *Chaos* is gone (panel, Workshop, remote page).
2. Hover the rule's **Strength** slider.
   - [ ] It says what the rule does at 100%, in its words: *At 100%: changes 3 bytes each time it fires.*
3. Drag the rule to 50% and **Strength, all rules** to 50%, then hover again.
   - [ ] *At 25% (50% × 50% for all rules): …*, with fewer bytes; an *Add* rule also says *adding -1..1 instead of -2..2*.
4. Drag a one-byte rule to 10%.
   - [ ] *it fires on about 1 in 10 of its chances*; glitches come that much less often.
5. Workshop > Recipe: hover the same slider; then double-click the rule.
   - [ ] The same words on the Recipe page; in the editor, a line under **Strength** says them and follows the spin box and **Bytes each time** as you change them.
6. A code rule (Starter: Code drip, or an NOP-out or pointer swap rule).
   - [ ] It counts in its own things: *instructions*, *jump table entries*, *functions*.
7. On the remote page (QA 31), drag **Strength, all rules**.
   - [ ] Under each rule's slider, a line says what it does, and it follows within a second.

## 50. Always a recipe: Untitled until it's saved (#74)
1. Load a game and open the Workshop before choosing anything.
   - [ ] The top of the page list says **Untitled** / *Empty · not saved*; the window's title is *Untitled — Corruption Workshop*. The Recipe page says nothing is in it yet; **Add Rule…** works, **Save** is greyed. The panel still shows the *Start here* card and *Choose a recipe…*, and **Corrupt** waits.
2. Without picking a recipe, turn a function **Off** (Functions page; press Corrupt first so it takes).
   - [ ] The header now says *Not saved*, the title has the edited mark, the panel's list says *Untitled (not saved)* and **Corrupt** works. The Recipe page's *Also in This Recipe* lists the switch. No recipe called *Switches* appears (nor *Painted*, *Protected*, *Changes* or *Pinned* for the others).
3. Press **New Recipe**.
   - [ ] It asks Save / Don't Save / Cancel (the switch is worth keeping). **Save** asks a name, offering *My recipe*.
4. With a blank Untitled, press **Re-roll** or move **Strength, all rules**, then **New Recipe** or pick a starter.
   - [ ] Nothing is asked: an empty recipe has nothing to lose.
5. Look at the top of each page: Functions, ROM, Safety, Sharing; and paint on WRAM in Memory.
   - [ ] Each says what is the recipe's: Functions (Off, Guard, buttons, changes; names are everyone's), ROM (pins; what's learned is the game's), Safety (the app's), Sharing (nothing), Memory (the paint line says it's kept in the recipe).
6. Pick a starter, then a saved recipe and change it.
   - [ ] The header says *Starter · not yours yet*, then *Saved*, then *Changed since saved*.

## 51. The Safety page explains its switches (#76)
1. Load a game with a blank Untitled recipe and open **Safety**.
   - [ ] Under each switch a grey line says what it does. *Test ROM changes…* ends *This recipe has no ROM rules, so there's nothing to test now*; *Undo a ROM change…* ends *The ROM isn't changed, so there's nothing to watch now*.
2. Pick **Starter: Code drip**, press **Corrupt (F7)** and play a minute with Safety open.
   - [ ] The lines count up: *So far: N tested* (with *picked again* / *skipped* when it happened) and *So far: N put back* once a bad jump was caught.
3. Change **After a crash** through every choice.
   - [ ] The line under it says what each one does, and every one ends *Changed ROM bytes the game used just before the crash are put back either way*.
4. With VoiceOver, move to each switch.
   - [ ] It reads the switch's line as its description.

## 52. Claude's names in Functions (#42)
1. Put a `names.claude.json` (from the map-game skill) in a game's library folder and load the game.
   - [ ] Functions shows those names as *name (claude)*; a function you named yourself shows yours first, then *· name (claude)*.
2. Pick five names from `mapping.claude.md` and do what its *how to check* column says (turn the function off).
   - [ ] Note for each whether the game did what the name says; add the count to the plan's *How it went*.

## 53. Which changes don't crash (#77)
1. Load Goof Troop (or any game you've played with the app), open **Functions**, select a busy function and **Change It…**.
   - [ ] A *Risk* column says *Low*, *Medium*, *High* or *Unclear* and a few words (*a value in memory*, *an index*); Medium is orange, High red.
   - [ ] Selecting a line shows a line under the choices: the risk, why (*Stored into $7E:0D40*) and *about N in 100 changes like this stopped the game*.
   - [ ] Clicking the Risk header sorts lowest first; clicking Address puts code order back; the controls act on the line you picked.
2. Press **Corrupt (F7)**, apply a few *Low* lines one by one, playing a minute after each.
   - [ ] Note how many kept the game going; then the same with *High* lines.
3. Apply a change that breaks the game (a High line in the busiest function usually does), and wait.
   - [ ] The crash guard goes back to before you applied it (the message says how far), the line's *Now* is empty again, and the game plays on normally.
4. Make a **Flip branches** rule on the ROM and tick **Only where a change is low risk**.
   - [ ] The note under it says what that picks; the rule's line in the recipe ends *where a change is low risk*; saved and loaded, it stays ticked.

## 54. Try a change before it lands (#78)
1. Load Goof Troop (or any game you've played with the app), press **Corrupt (F7)**, open **Functions**, select the busiest function and **Change It…**.
   - [ ] **Try 5 s Ahead** sits left of Apply; hovering says it plays the change unseen and once without it.
2. Select the first line (in Goof Troop a `BEQ`), leave *The opposite*, press **Try 5 s Ahead**.
   - [ ] The game stops for about a second (no sound glitch beyond that, nothing odd on screen), then runs on as before.
   - [ ] A line under the risk says *Tried 5 s ahead:* and what happened (*froze after…*, *crashed…*, *Still running…*); froze and crashed are red, still running green.
   - [ ] The game itself didn't change: the line's *Now* is empty.
3. Pick another choice (*Always*): the answer goes away; try again and it answers for that one.
4. Apply a change the try called *froze* or *crashed*.
   - [ ] The game does go wrong within a few seconds (the crash guard then puts it back).
5. Apply a few changes the try called *still running* and play a minute after each.
   - [ ] Note how many kept the game going.
6. Try a line in a function that runs only somewhere else (a menu's, from the Functions list with few frames).
   - [ ] It says the game didn't run this code in 5 s.
7. Pause the game (or turn corruption off) and press Try.
   - [ ] Paused: it says *Trying…* and answers once the game runs. Corruption off: the button is greyed out.

## 55. Keep a golden moment (#47)
1. Load Goof Troop (or any game), pick *Gentle WRAM noise*, press **Corrupt (F7)** and play for half a minute.
   - [ ] **Keep This Moment (F8)** sits under Corrupt; hovering says how far back it reaches (*Keep the last 14 s…*).
2. Press **F8** while something is moving on screen.
   - [ ] The game stops for about half a second, then runs on.
   - [ ] The message line says *Kept the last N s: it plays back exactly*; hovering it gives the moment's name.
3. Open the Workshop's **Moments** page (or Corruption > Kept Moments…).
   - [ ] The moment is first, with its length and *your name · date time*; the line under the table names the recipe.
4. Select it and press **Play**.
   - [ ] Corruption turns off; the game jumps back and plays what you saw, your own moves included.
   - [ ] The panel's bar counts *Playing a kept moment · 3.2 of 14.2 s*, and Keep has become **Stop Playing**.
   - [ ] At the end, the message says the game is yours; you play on from there.
5. Play it again and press a pad button halfway.
   - [ ] The playback stops at once and you're in control (*You took over*).
6. Press **Back to Normal**, play 3 seconds, press F8.
   - [ ] It keeps about 3 s and says it starts where you went back to normal.
7. Rename the moment (pencil, type, Enter), then delete it (bin, confirm).
   - [ ] The file in the game's `moments` folder (folder button) follows both.
8. With a crash-prone recipe (*ROM code drip*), keep a few moments after the crash guard has rolled back.
   - [ ] Each says *plays back exactly* and is still 10-20 s long: a rollback doesn't shorten it (#79).
   - [ ] Play shows the game going on from before the crash, without the crash and the jump back.
9. Pause the game (Emulation > Pause) and press F8.
   - [ ] It keeps at once, from the frame on screen.
10. Copy a moment's file to Erik's library (or open the shared library on another machine) and Play it there.
   - [ ] It plays the same. On another version of the ROM, Play says it was kept on another version.

## 56. Explore a moment (#45, #46)
1. Keep a moment while playing (F8), then open the Workshop's **Moments** page and pick it.
   - [ ] **Explore** sits beside Play; hovering explains it (about 4,000 tries an hour).
2. Press **Explore** and choose **For 10 Minutes**.
   - [ ] The message says it's exploring in the background; the game keeps running smoothly.
   - [ ] The sidebar shows *exploring* beside **Explored**.
3. Open **Explored**.
   - [ ] The line at the top counts what it tried, what kept running and what visibly changed, with **Stop Exploring**.
   - [ ] Within a minute or two the first candidates appear, best first, with six pictures of the picked one.
4. Pick a candidate and press **Play**.
   - [ ] Corruption turns off and the game plays the candidate's run; a pad button takes over.
5. Press **Keep as Recipe**.
   - [ ] The message names the new recipe; it's in the recipe list. Load it and press Corrupt: it starts from the moment's start and does much the same.
6. Press **Stop Exploring** (or wait the 10 minutes).
   - [ ] The line goes away; the candidates stay. The summary says how many it tried.
7. Press the arrow button.
   - [ ] The gallery opens in the browser: every candidate's pictures and recipe, in light and dark.
8. Delete the exploration (bin, confirm).
   - [ ] It's gone from the list and from the game's `explored` folder; the recipe you kept stays.
9. Below the candidates, look at **What it did**.
   - [ ] Three charts: a bar per kind (blue visibly changed, grey nothing showed, orange stopped), a histogram with the keep line, and dots over time with a best-so-far line.
   - [ ] Hovering a bar or a dot shows its numbers. The gallery has the same charts, in light and dark.
10. On a quiet moment (nothing moving), explore for 10 minutes.
   - [ ] Few or no candidates, rather than many that look like the moment.
11. Normal play (#82): start an exploration and open **Explored** right away.
   - [ ] For about half a minute the line says it's first playing the moment without corruption (12 of 37...), then counts tries as before.
   - [ ] The summary has a *Normal play: 37 runs...* line with normal play's best score.
   - [ ] The last column is **Find**; hovering one says why it was kept and its score. The text under the pictures says *Looks different beyond normal play* and/or *Acts different: ...*.
   - [ ] **How the scores spread** draws the keep line at normal play's best; **When the good finds turned up** plots find numbers with the line at 1.
   - [ ] Play a few *Acts different* candidates: does the game behave oddly (a mode it doesn't enter, enemies or physics wrong), not only look different?
   - [ ] An exploration from before still shows its score column and the 0.3 line.
12. Ratings (#83): pick a candidate, press **Play** and watch it, then answer **Is it a find?**
   - [ ] **Good Find** turns into *Rated Good Find* with a check; the **Rated** column shows a thumb up; the text under the pictures ends *You: good find.*
   - [ ] **Not a Find** switches it; pressing the chosen one again takes the rating back (the column empties).
   - [ ] `ratings.<you>.json` is in the exploration's folder; with Erik's file beside it (or a copy renamed `ratings.erbl.json`), hovering the column lists both, yours first.
   - [ ] Rate while it's still exploring: the rating stays when the list updates.
   - [ ] `build/tests/explore-ratings "<library>"` counts your ratings and prints how well each measure picks the good ones.
13. Normal play along the way (#84): explore for 10 minutes and watch the running line.
   - [ ] After the first tries it adds *Normal play, checked along the way: 0 of 20 runs without corruption would have been kept*, the count rising by one every 20 tries.
   - [ ] When it's done, the summary's normal play line counts those runs too and says how many would have been kept.
   - [ ] The candidates' find numbers and the charts agree with the final normal play (no find under 1 in the list).
14. Real moments (#85): keep 3-5 moments of real play with F8 (20 s+ in an action scene: Pocky, Zelda, Goof Troop out of the pause menu), then run `tools/explore_moments.sh "<library>" <rom> 30` overnight.
   - [ ] It explores each moment of that ROM (three at a time) and skips moments of other games.
   - [ ] Each shows up on the Explored page; rate the candidates after playing them, then run `build/tests/explore-ratings "<library>"`.
