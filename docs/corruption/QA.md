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

## 8. Rule editor
1. Load any recipe, tick **Active**, and select a rule.
   - [ ] **Edit**, **Duplicate** and **Remove** become clickable; double-clicking the rule opens the editor too.
2. **Add Rule**, then **OK** with the defaults.
   - [ ] A `new rule` row appears and is selected; the status line says *(edited)*, and the frame count keeps going (no restart).
3. Edit that rule: set **Memory** to `cgram (palette)`, **From** `0` **to** `0x1FF`, *What* `Add / subtract` −8..8, *When* every 10 frames.
   - [ ] The summary line under the form updates as you type.
   - [ ] Colours start changing within a second; any other rule carries on as before.
4. In the editor, set **to** to `0x300`.
   - [ ] **OK** greys out and a red line says cgram is only `0x200` bytes. Set it back.
5. **Duplicate** the rule, then **Remove** the copy.
   - [ ] Remove asks first; the table is back to how it was.
6. **Save**.
   - [ ] *(edited)* goes away, and the recipe's JSON file has the new rule.
7. Edit something, then pick another recipe and **Load**.
   - [ ] It asks before discarding the changes. **No** keeps them.
8. **New**, name it `scratch`, **Add Rule**, **Save As…** `scratch`.
   - [ ] `scratch.json` appears in the recipe list, and **Save** works on it afterwards.
9. If the game has a `regions.json` (add one by hand; see README), open the editor.
   - [ ] **Region** lists the names; picking one fills the range and greys it out, and the saved JSON uses the name.

## 9. Intensity
1. Load **tile-rot** (or any visible recipe), tick **Active**, and drag **Chaos** down to 20 %.
   - [ ] The effect slows down right away, and the status line says *(edited)*.
2. Drag it to 0 %.
   - [ ] Firings stop counting up. Back to 100 %: they resume, and the frame count never reset.
3. With two rules, drag one rule's **Intensity** slider to 0 %.
   - [ ] Only that rule goes quiet.
4. Bind **Corruption: More Chaos** and **Less Chaos** under *Settings > Shortcuts*, and press them.
   - [ ] The message says `Corruption chaos 40%` and so on, and the Chaos slider follows.
5. Load **music-mangler** (one byte per firing), set its rule to 20 %, and listen for a minute.
   - [ ] Glitches come noticeably less often than at 100 %.
6. Set the crash policy to **dampen** and play **low-wram-roulette** until a crash is caught.
   - [ ] The guilty rule's slider drops to half.
7. **Save**, then **Load** the recipe again.
   - [ ] Chaos and the rule sliders come back where they were.

## 10. Saved moments
1. Make sure **Saved moments** is ticked. Load a recipe, play to a memorable spot, and tick **Active**.
   - [ ] The status line says *new moment, not saved*, and **Save** is enabled.
2. Play for 20 seconds with a visible recipe (tile-rot, palette-drift), then **Save**.
   - [ ] `recipes/<name>.anchor.frz` appears next to the JSON, and the JSON has `"anchor_state"`.
3. Walk somewhere else, then **Load** the same recipe.
   - [ ] The game jumps back to the spot from step 1 and the run starts; the status says *from its saved moment*.
   - [ ] Without touching the controller, the corruption looks the same as the first time (same tiles or colours at the same times).
4. **Re-roll** (F8).
   - [ ] The status says *new moment, not saved* again.
5. Pause the game (P) and tick **Active** off and on.
   - [ ] The status says *starting with the next frame* until you unpause.
6. Untick **Saved moments** and **Load** the recipe.
   - [ ] The game does not jump; the recipe starts where you are.
7. **Save As…** a recipe that started from its moment, under a new name.
   - [ ] The new name gets its own `.anchor.frz`, and loading it jumps to the same spot.

## 11. Reboots
1. Press **Reset** (Ctrl+R) with a recipe active.
   - [ ] No rollback: a reset from Snes9x is not a crash.
2. Play **low-wram-roulette** (crash policy *reroll*) in Pocky & Rocky or another action game for a few minutes.
   - [ ] If the game drops back to its title screen or boot logos, the message says `Crash caught (the game rebooted itself)` and play continues from just before.
3. In a game with a soft-reset combo (often L+R+Start+Select), press it with no recipe active.
   - [ ] The game resets normally; nothing is rolled back.

## 12. Memory viewer
1. In the middle of gameplay, press **Memory Viewer…** in the panel.
   - [ ] A window shows WRAM in hex; some areas glow orange and keep changing, others stay plain.
   - [ ] The strip on the right shows where the busy areas are; clicking it jumps there.
2. Pause the game (P), wait, then unpause and stand still.
   - [ ] While paused the picture holds still; after unpausing, areas that stopped changing fade within a couple of seconds.
3. Load and activate **gentle-wram-noise**, and switch the overlay to **Hits**.
   - [ ] Purple bytes appear scattered above `0x2000` as the recipe fires; **Clear Hits** clears them.
4. Press **Find Copies**.
   - [ ] The list shows at least one `oam` or `cgram` copy in WRAM (most games have both). Clicking one selects it.
5. With a palette copy selected, **New Rule from Selection…**, choose *Add / subtract* −2..2, every 30 frames, count 4, **OK**.
   - [ ] The rule appears in the panel, and colours drift and stay changed, unlike the **palette-drift** starter recipe.
6. Switch the target to **vram**, type `0x4000` in **Go to** and press Enter.
   - [ ] The view jumps to row `0x04000`.
7. Close the viewer window.
   - [ ] The game runs as before (it stops watching when closed).

## 13. Named areas
1. In the memory viewer, select a range (for example a palette copy from **Find Copies in WRAM**), **Name Selection…**, and call it `palette copy`.
   - [ ] It appears under **Named areas**, and the game's `regions.json` has it.
2. **Add Rule** in the panel and open **Region** in the editor.
   - [ ] `palette copy (wram)` is listed; picking it fills and locks the range. Save the recipe.
   - [ ] The recipe's JSON says `"range": "palette copy"`.
3. Switch the viewer to another memory, then click `palette copy` in the list.
   - [ ] It switches back to WRAM and selects the range.
4. **Rename…** it to `palette`.
   - [ ] The message says recipes follow; the recipe's JSON now says `"palette"`, it still loads, and the panel doesn't call it *(edited)*.
5. **Delete…** it.
   - [ ] The question names the recipe that uses it; afterwards the recipe still loads, with the range written out.

## 14. Memory map
1. In the memory viewer, look at the **Map** on the left with **wram** chosen.
   - [ ] It lists the direct page, low RAM, bank $7E upper and bank $7F; hovering shows what each is.
2. Switch to **vram** during gameplay.
   - [ ] Italic lines appear: BG tiles and tile maps for the current mode, and two sprite tile tables. Clicking one selects it in the grid.
3. Make a rule from **BG1 tiles** (New Rule from Selection…, random, every 30 frames, count 4).
   - [ ] Background graphics get garbled, sprites don't.
4. Walk into another area or open a menu that changes the screen.
   - [ ] If the game moves its layers, the italic lines update.
5. Switch to **aram**.
   - [ ] **Echo buffer** and **Sample directory** appear. A rule on the echo buffer makes the sound smear or crackle, not crash.

## 15. Pixel view
1. In the memory viewer during gameplay, choose **Pixels** in the first menu, with **wram** and **Plain**.
   - [ ] All of WRAM fits in the window: black and white bands, coloured stripes and noisy areas, with addresses down the left.
   - [ ] The colours are easy to tell apart and nothing looks blurry or smeared.
2. Hover a few dots.
   - [ ] The bottom row shows the address and value, and the value matches the same byte in **Hex**.
3. Click **+** a few times, then Ctrl+wheel over one spot.
   - [ ] The dots get bigger, rows get shorter, and the spot under the mouse stays under the mouse. **−** stops at 1×, **+** at 16×.
4. Choose **Activity**.
   - [ ] The memory turns grey and the busy bytes glow orange; they flicker as the game runs.
5. Drag across a band, then switch to **Hex** and back.
   - [ ] The same range is selected in both, and each view shows it. **New Rule from Selection…** gets that range.
6. Click areas in the **Map**, then a found copy (Find Copies in WRAM).
   - [ ] Each is selected in the pixel view, with a blue bar beside its rows.
7. Switch to **vram**, **cgram** and **oam** at a few zooms.
   - [ ] VRAM shows tile patterns; cgram comes out 32 bytes a row (one palette each), oam too.
8. Run a recipe with **Hits** on.
   - [ ] The recipe's writes show as purple dots on grey.
9. Switch to **vram** and set **Row** to 64, then 66, then step back to 64 with the arrows.
   - [ ] At 64 the tile map areas (see the Map) look like upright blocky pictures; at 66 they lean. Each arrow click moves one byte.
   - [ ] Type 300 at 8×: rows scroll sideways, the addresses stay on the left, and clicking a dot still selects the right byte (check in **Hex**).
   - [ ] Switch to **wram**: it's still on Auto. Back to **vram**: 64 again.
10. Resize the window and scroll with the plain wheel.
   - [ ] On Auto, rows reflow to the new width, the view stays near the same place, and the plain wheel scrolls without zooming.

## 16. Tile view
1. During gameplay, open the memory viewer, choose **vram** and **Tiles**, and click **BG1 tiles** in the Map.
   - [ ] Real graphics appear: pieces of the background you see on screen, in the right colours. The format and palette boxes changed to match.
2. Click **Sprite tiles, first table**.
   - [ ] The characters' sprite pieces appear in their colours (palette 8 or nearby). Step the palette with the arrows: the colours change row by row; **Grey** shows plain greys.
3. Try **16** and **32 per row**, and zoom in and out (buttons and Ctrl+wheel).
   - [ ] Multi-tile sprites and font letters come out whole at some width; the zoom stays sharp, with faint lines between tiles from 3×.
4. Drag across a few sprite tiles.
   - [ ] They get a blue-and-white outline. Switch to **Hex**: the same bytes are selected, a whole number of tiles.
5. **New Rule from Selection…**: random, every 30 frames, count 4. Run it.
   - [ ] Just that character's graphics get garbled, live in the tile view too (pick **Hits** to see where).
6. Make a rule on **cgram** (palette drift) and watch the tile view.
   - [ ] The tiles change colour within a fraction of a second.
7. Hover a tile.
   - [ ] The bottom row shows its number and address.
8. Choose **wram** in Tiles mode, 4 bpp, and scroll through.
   - [ ] Mostly noise, but some games show unpacked graphics somewhere; nothing slows down.

## 17. Crash map
1. Load a game, pick **low-wram-roulette** (crash policy reroll), and turn it on. Play for 5 minutes.
   - [ ] Crashes get caught as before. The panel's crash line adds whether the crash map has found dangerous areas.
2. Open the library folder for the game.
   - [ ] There's a `crashmap.<you>.json` with lines like `"0x0320": [3, 1]`, sorted by address.
3. Open the memory viewer on **wram** with the **Danger** overlay, in Hex, Pixels and Tiles.
   - [ ] Red marks where crashes happened, brighter where they happened more than once. Everything else is plain.
4. Edit the roulette rule and tick **Avoid dangerous bytes**. Its summary ends with "avoiding danger".
   - [ ] Over the next few minutes it crashes less than before (this is noisy; it's fine if it's only a little).
5. Close and reopen the game.
   - [ ] The Danger overlay and the panel's count come back (the file was saved on close).
6. Copy the file to `crashmap.erik.json` in the same folder and reload the game.
   - [ ] The red gets stronger: both files count.

## 18. Crash counts in the viewer
1. Play a few minutes of **low-wram-roulette** with the guard on, then open the memory viewer on **wram** in **Pixels**.
2. Pick the **Crashes** overlay.
   - [ ] Gold where crashes were blamed, brighter where there were more. Blocks that were only ever calm stay grey.
3. Hover a gold dot.
   - [ ] The line under the view adds `block 0x…: N crashes, M calm, danger 0.xx`, and `, hot` on hot blocks. Bytes the map knows nothing about show only the address and value.
4. If the panel says the map knows dangerous areas, switch to **Danger** and then **Crashes**.
   - [ ] A red tick sits in the address gutter beside each row with a hot block, at 1×. From 4× zoom up, hot blocks also get a white outline.
   - [ ] Activity, Hits and Plain show no ticks or outlines.

## 19. The game's reads and writes (WRAM)
1. Load a game and play to somewhere with sprites moving. Open the memory viewer on **wram**, **Pixels**.
2. Pick **Reads**.
   - [ ] Blue runs appear and change as you play. The sprite table copy (find it with **Find Copies**) is solid blue: the game sends it to the PPU every frame.
3. Hover a blue dot.
   - [ ] `read N, written M a frame` after the value. A grey byte shows `read 0, written 0 a frame`.
4. Pick **Writes**.
   - [ ] Green where the game writes; the stack and the direct page are among the brightest.
5. Switch the target to **vram**.
   - [ ] Reads and Writes show nothing there, and the hover has no read/written part.
6. Close the viewer and play on.
   - [ ] No slowdown with the viewer closed (the counting stops) or open.

## 20. ROM corruption
1. Load FF6 (a copy). Add the starter **Classic ROM sprinkle** and turn it on.
   - [ ] The game restarts from its boot. The panel's ROM line says about 40 bytes changed.
   - [ ] Edit the rule to `count` 3000 and turn it off and on: the game restarts again, visibly broken (garbled text, graphics or music).
2. **Re-roll** a few times.
   - [ ] Each re-roll restarts the game with a different corruption. The same seed (type it back in and **Set**) gives the same one.
3. Turn the recipe off.
   - [ ] The game keeps running corrupted; the ROM line still counts the changes. **Restore Clean ROM** sets it to "clean".
4. Add **Live ROM drip**, turn it on and play for two minutes.
   - [ ] No restart. The ROM line grows by about 2 bytes every 10 s.
5. Open the memory viewer on **rom**, in Pixels with the **Hits** overlay.
   - [ ] The changed bytes light up. Scrolling through 6 MB stays smooth.
6. With a classic recipe on, **Export ROM…** and open the file in another emulator.
   - [ ] It boots with the same corruption, without a checksum warning.
7. Make a live rule that's sure to crash (a small range in the game's code, `random`, every 60 frames, `count` 50) with the guard on.
   - [ ] Crashes are caught and rolled back, and the game keeps running: the ROM bytes from after the checkpoint are put back.

## 21. Editing memory like a picture
1. Load FF6, open the memory viewer on **rom** in **Pixels**, and set the row width until something lines up (graphics show as tiles at 16, 32 or 64).
2. Alt-drag a rectangle.
   - [ ] It's outlined and stays put when you scroll. The line below says its size, corner and row width.
3. **New Rule from Selection**, choose **Shift the whole area**, right by 1, and **Once, when the run starts**. OK, then turn the recipe on.
   - [ ] The game restarts, and in the viewer the rectangle's contents have moved one dot to the right.
4. Try **Flip**, **Levels → Posterize**, **Filter → Blur** and **Stamp** the same way.
   - [ ] Each changes only the rectangle; the rule's summary says what it does.
5. Switch on **Paint**, value 0x00, brush 4, and scribble over part of the ROM. Alt-click a dot.
   - [ ] The dots turn black as you drag; Alt-click puts that dot's value in the value box. The panel's ROM line says how many bytes are painted.
6. **Save** the recipe, turn it off and on.
   - [ ] The paint is still there after the restart (it's in the recipe as patches). **Clear Paint** empties the count; the next start is clean.
7. Select a rectangle, **Export PNG…**, open it in an image editor, draw on it, save it (same size, greyscale), and **Import PNG…**.
   - [ ] The drawing appears in the viewer and counts as painted bytes.

## 22. The code map
1. Load FF6 and let the intro play for a minute (or play a bit). Open the memory viewer on **rom** in **Pixels**, row width 256.
2. Choose the **Code** overlay.
   - [ ] Within a second, teal shows where the game's code is (FF6: the first 48 KB, bank `$C0`), opcodes brighter than operands. Everything else is dark grey.
3. Hover a teal dot.
   - [ ] The line below shows its CPU address, bytes, instruction and kind, e.g. `$C0:0053  22 06 00 C3 JSL $C30006 (jump)`. A dark dot shows only address and value.
4. Choose **Data**, then **DMA**.
   - [ ] Pink lines where the game reads tables; yellow-green where it copies graphics. Hover says `read as data` / `copied by DMA`.
5. Play on for a while; watch the panel's ROM line.
   - [ ] `code map: N code, N data, N DMA bytes` grows as you reach new places.
6. Close the game (or quit), then load it again.
   - [ ] The library's game folder has `codemap.<your name>.bin`, and the overlays show what you saw last time straight away.
7. Copy someone else's `codemap.*.bin` into the folder (or rename yours to `codemap.erik.bin` and play a new area), and load the game.
   - [ ] Both maps show together.
8. Does the game run noticeably slower than before? (It should cost about 5%.)
