# Corrupting games: the guide

Snes9x can corrupt the game you're playing: change bits of its memory, or of the game
itself, while it runs, for glitches you'd never see otherwise. This guide is also in the
app: **Guide** at the top of the Corruption panel.

**Contents:** Start here · Getting back to normal · Keeping a good one · The words · Hotkeys ·
Recipes and rules · When the game crashes · Sharing with a friend · The memory viewer ·
Corrupting the ROM · Survivors and pins · Editing memory like a picture · Tips · When nothing
seems to happen

## Start here

1. Open a game (**File > Open**). Show the panel: **Corruption > Show Panel**.
2. In the **Recipe** list, pick a starter, say **Starter: Gentle WRAM noise**. Starters come
   with the app.
3. Press **Corrupt** (**F7**) and play. Things start going slightly wrong.
4. **Re-roll** (**F8**) for different dice: the same recipe, different glitches.
5. **Back to Normal** (**Shift+F7**) puts everything back as it was when you pressed
   Corrupt.

That's all you need. The rest of this guide is for when you want more.

## Getting back to normal

There are four ways to undo something. Only one of them undoes everything.

| Button | What it does |
|---|---|
| **Stop** (Corrupt, while it runs) | Stops corrupting. What it already changed **stays**: the game keeps running, glitched. |
| **Back to Normal** | Stops, puts the game's ROM back to clean, and takes the game back to the moment you pressed Corrupt. Everything it did is gone (and so is your play since then). |
| **Restore Clean ROM** | Puts only the ROM back. What corruption did to RAM stays. |
| **Restore This Save…** (Game Save Backups) | Has nothing to do with corruption: it restores your **in-game save** (the cartridge's battery save) from a copy and restarts the game. |

## Keeping a good one

| You want to keep… | Do this |
|---|---|
| A glitch you just saw | **Save**, right away, while it's still on. The recipe is saved with this run's dice (the seed) and the moment the run started, so loading it plays the same run again, as long as you press the same buttons. The status line says *Save to replay this run from its start* until you do. |
| The recipe, to play again | **Save**. With *Remember where each run starts*, it comes back to the same spot with the same dice. |
| One exact ROM change you liked | **Survivors…** then **Pin Selected**, then **Save**. See *Survivors and pins*. |
| Bytes you set by hand | They're kept in the recipe already (*paint*); **Save**. |
| The corrupted game, to play anywhere | **Export ROM…**: a .sfc file of the ROM as it is right now (pins, paint and whatever the rules changed), for other emulators or a flash cart. |

## The words

| Word | Means |
|---|---|
| **Recipe** | A saved set of rules: what to corrupt, how and when. Each game has its own. **Starters** are recipes that come with the app. |
| **Rule** | One line of a recipe: "every 2 seconds, put a random value in 1 byte of work RAM". |
| **Run** | Corruption switched on, from the moment you press Corrupt until you stop. |
| **Seed** | The dice. The same recipe with the same seed, from the same start, does exactly the same thing. **Re-roll** picks a new seed. |
| **Chaos** | How hard the whole recipe hits. **Strength** is the same for one rule. They multiply. |
| **Memory** | Which part of the console a rule changes (see below). |
| **Start moment** | A save state taken when a run starts. **Back to Normal** goes back to it, and **Save** stores it with the recipe (with *Remember where each run starts* on), so loading the recipe replays from the same spot. |
| **Crash guard** | Notices when the game crashes, goes back a few seconds and changes what caused it. |
| **Code map** | What the app has learned about the ROM while you played: which bytes are code, which are data, which are graphics. |
| **Survivor** | A ROM change the game kept using for a long time without crashing. |
| **Pin** | A ROM change kept in a recipe on purpose. |
| **Paint** | Bytes you set by hand in the memory viewer. |

**The memories:**

| Memory | What's in it | Corrupting it… |
|---|---|---|
| **wram** (work RAM) | Everything the game is keeping track of: positions, health, timers, enemies | The usual place. Effects come and go as the game rewrites things. |
| **vram** (graphics) | Tiles, backgrounds, tile maps | Garbled graphics, often until the next screen. |
| **cgram** (palette) | The 256 colours on screen | Colour changes; many games repaint them every frame (see Tips). |
| **oam** (sprites) | Where each sprite is and which tile it shows | Sprites jump and swap; often repainted every frame too. |
| **aram** (sound) | The sound chip's program, music and samples | Strange music, or silence. |
| **sram** (save) | The battery save | Changes your save file (it's backed up automatically). |
| **rom** (the game) | The game itself: its code, data and graphics | The classic corruption. See *Corrupting the ROM*. |

## Hotkeys

| Key | Does |
|---|---|
| **F7** | Corrupt on or off |
| **Shift+F7** | Back to Normal |
| **F8** | Re-roll |
| **F9** | Fire hotkey 1 (rules whose *When* is *Hotkey 1*) |
| *(none)* | Fire hotkeys 2-9, More / Less Chaos, Mark Crash, Save Recipe |

Change or add keys under **Options > Shortcuts…** (the *Corruption:* lines). The panel's
buttons always show the keys they're bound to.

## Recipes and rules

**The Recipe list** has your recipes for this game first, then the starters. Choosing one
loads it. **New** starts an empty one. **Save** keeps it in your library (a starter becomes
your own copy, so it asks for a name). **More** has Save As, Share, Import, and the
library folder.

**Remember where each run starts**: saving a recipe also saves its start moment, and
loading it goes back there, so the same seed plays out the same way. Untick it to load and
save recipes without touching where the game is.

**Rules** are listed under the run controls. Tick **On** to switch one off for a while,
drag **Strength** to tone it down, double-click to edit, and see how often the crash guard
blamed it under **Crashes**. **Add Rule…** adds one; the small pencil, copy and bin buttons
beside it edit, duplicate and remove the selected rule. Edits apply on the next frame, to
that rule only.

**The rule editor**, top to bottom:

- **Where**: the **Memory**, then a range **From**/**to** (offsets like `0x1A00`), or a
  **Region** you named in the memory viewer. **Shape: a rectangle** comes from drawing one
  in the viewer's pixel view.
- **What**: what happens to each picked byte. *Random value*, *Add / subtract* (−2..2 is a
  gentle nudge), *Flip bits*, *Swap bytes*, *Copy a block*, *Freeze* (stuck timers,
  infinite health), *Shuffle a window*; the whole-area ones (*Shift*, *Flip*, *Levels*,
  *Filter*, *Stamp*, *Fill*, see *Editing memory like a picture*); and for the ROM, *Swap
  opcodes*, *Flip branches*, *NOP out* (see *Corrupting the ROM*).
- **Aim** (ROM only): pick bytes by what they are. See *Corrupting the ROM*.
- **When**: every N frames (60 is about a second), a chance per frame, once at a frame,
  on a hotkey, or once when the run starts (for the ROM: the classic way, see below).
  **Only between frames** limits it to part of the run.
- **How Much**: **Bytes each time**, **Which bytes** (different ones each time, or the same
  ones every time), **Strength**, **Enabled**, and **Avoid dangerous bytes** (skip the
  bytes the crash guard has learned crash this game).

The line at the bottom says the rule in words; OK stays off until it's valid, and the
red line says why.

## When the game crashes

With **Catch crashes and go back a few seconds** ticked, the app keeps a save point every
second. When the game stops, freezes, goes black, stops drawing new frames for 5 seconds
or reboots itself, it goes back to before the corruption that caused it, changes what
caused it, and plays on. You lose a second or two. A still screen nobody is touching (a
menu, a text box) is never a crash: frozen and black screens only count while you press
buttons.

**Then** decides what happens to the rule that caused it: re-roll its dice (the default),
skip its last changes, halve its strength, turn it off, or re-roll twice then halve then
turn off. **As the recipe says** shows what the loaded recipe asks for (it's set in the
recipe file, `crash_policy`); picking another overrides it for now. Changed ROM bytes the game ran into are put back either way.

If the same crash keeps coming back, each time it goes 5 seconds further back. After five
crashes in a row that fixed nothing, it gives up: corruption goes off and the ROM goes
back to clean.

**It Crashed: Go Back** is for a crash it missed (the game is stuck but the picture
still moves): it goes back about two seconds and blames what changed.

It also learns, per game, which bytes crash it (**the crash map**): rules with *Avoid
dangerous bytes* skip those, and the memory viewer shows them in red. What it learned is
kept in your library, and adds up with your friend's.

## Sharing with a friend

**More > Share…** writes one `.s9xrecipe` file: the recipe, its pins and its start moment.
Send it any way you like. Your friend opens the same game and chooses **More > Import a
Shared Recipe…**; it lands in their recipes and loads. (Named areas travel as plain
ranges.) If their copy of the game is a different version, the recipe still loads but may
hit different things, the start moment is skipped (it starts where they are), and pins
made for the other version are left out.

You can also share a whole library folder, for example as a git repository: each game has
a folder with its recipes, named areas, code map and crash map, and the maps of everyone
who plays add up. **More > Choose the Library Folder…** picks it.

## The memory viewer

**Memory Viewer…** (under the rules) shows what's inside the game while it runs.

- **Memory** picks which one. **Show as**: **Hex** (the numbers), **Pixels** (one dot per
  byte, so a whole memory fits on screen; brighter is a bigger value) or **Tiles** (the
  bytes drawn as the game's graphics, in its own colours).
- **Colour by** paints on top of the bytes. The line under the view explains the colours;
  only the ones that mean something for the memory are offered.
  - **Activity**: what the game changes, brighter the more often.
  - **Hits**: what the recipe wrote (**Clear Hits** starts over).
  - **Danger** and **Crashes**: where corruption crashed the game.
  - **Reads** and **Writes** (wram): how often the game itself uses each byte. A byte it reads
    all the time but rarely writes is where corruption sticks.
  - **Code**, **Data**, **DMA** (graphics copied to the screen) and **Changes** (rom): see
    *Corrupting the ROM* below.
- **What's where** lists the parts of the memory (the sprite table, each background's
  tiles…); click one to select it. Italic ones are read from the game right now and can
  move.
- **Hover** a byte for what it is: its value, and for the ROM the instruction it's part of.
- **Select** by dragging; then **New Rule from Selection…** makes a rule for exactly those
  bytes. **Go to** jumps to an offset.
- **Named areas**: select bytes, **Name Selection…** ("player stats"), and the name works in
  any rule of that game.
- **Find Copies in WRAM** (wram, oam, cgram) shows where the game keeps its own copies of
  the sprite table and palette (see Tips).
- **Region** in the rule editor lists these names; with none yet, it only offers *Custom
  range*.
- In **Pixels**, **Row** sets how many bytes make one row of dots: step it until the
  picture snaps into columns, and a row of dots is a row of the game's data. In **Tiles**,
  pick the format (*bpp*, bits per pixel: how many colours a tile can have, 2 bpp = 4
colours, 4 bpp = 16, 8 bpp = 256), palette and tiles per row, or just click a tile area in
*What's where*.

## Corrupting the ROM

The ROM is the game itself. Two ways to corrupt it:

- **Classic**: a rule whose *When* is *Once, when the run starts* changes the ROM, and the
  console restarts so the game boots from it. Each seed is a different corrupted game.
  Starter: **Classic ROM sprinkle**.
- **Live**: any other *When* changes the ROM while you play. Starter: **Live ROM drip**.

A run always starts from the clean ROM. The ROM's header and the few bytes that tell the
console where the game starts are never changed.

**The code map.** While you play, the app notes what the game does with every ROM byte:
runs it as code, reads it as data, or copies it to the screen as graphics. The panel's
ROM section says how much it knows. It only knows what it has seen, so **play a while
first**; it's saved in your library and adds up with your friend's. In the memory viewer,
**Code** shows it (teal), **Data** (pink), **DMA** (graphics, yellow-green).

**Aiming at code.** In the rule editor, with the memory set to rom, **Aim** picks bytes by
what they are: **Opcodes** (which instruction it is), **Operands** (the number or address it
uses: changing those never breaks the code's flow), code, data, graphics, or anything the
game used. **Instructions** narrows it to some kinds (arithmetic, branches, jumps…) or
leaves some out; *width* and *stack* almost always crash at once, so "all but" starts with
those ticked. An aimed rule only picks bytes the code map knows.

**Code ops** (in *What*):

- **Swap opcodes**: an instruction becomes another of the same size, like add into
  subtract, increase into decrease, store into clear.
- **Flip branches**: every "if" goes the other way. Doors that never open, enemies that never
  die. The gentlest of the three.
- **NOP out**: the instruction does nothing at all: a sound, a check, a calculation that
  just doesn't happen.

Starters: **Branch flips**, **Operand nudge**, **Opcode swap**.

**When the ROM crashes the game**, the crash guard looks at which changed bytes the game
actually ran or read just before, and puts back only those, keeping the rest. That's what
lets a classic run with hundreds of changes keep playing. The **Changes** overlay shows
changed bytes in orange, bright once the game has used them; hovering one says so.

## Survivors and pins

**Survivors…** (in the ROM section) lists changes the game used in
at least 100 frames with no crash afterwards, newest first: where, what changed, and the
instruction before and after (`BNE → BEQ`). Select the ones you like and **Pin Selected**,
with a note if you want. A pin is part of the recipe: it's applied every time a run
starts, the crash guard never undoes it, and it's skipped on a different version of the
game. **Save** to keep pins. A recipe with only pins is a hand-made ROM hack.

## Editing memory like a picture

The old Photoshop way, built in. In the memory viewer's **Pixels** view:

- **Alt-drag** draws a rectangle; **New Rule from Selection…** makes a rule for it, and the
  whole-area ops (*Shift*, *Flip*, *Levels*, *Filter*, *Stamp*, *Fill*) act on all of it.
- **Paint Mode**: drag to set bytes to **Value** with a **Brush**; Alt-click picks a byte's
  value. Painted bytes go into the recipe; **Clear Paint** in the panel forgets them.
- **Export PNG…** saves the selection (or the whole memory) as a greyscale picture; edit it
  in any image editor and **Import PNG…** puts the changed pixels back as paint.

## Tips

- **Sprites and colours often bounce back.** Most games repaint the sprite table and the
  palette every frame from their own copy in work RAM, so corrupting oam or cgram only
  flashes. Corrupt the copy instead: **Find Copies in WRAM** shows where it is.
- **Low work RAM crashes fast.** `0x0000-0x1FFF` holds the game's busiest variables and its
  stack.
- **Gentle first.** *Gentle WRAM noise* is the mild starter; for the wilder ones, turn
  **Chaos** down before pressing Corrupt and up from there.
- **Same seed, same chaos.** If something great happens, Save right away: with the start
  moment it plays out the same way again (as long as you press the same buttons).

## When nothing seems to happen

- **Is it on?** The Corrupt button reads *Stop* and the status line counts changes.
- **Some memories bounce back.** Sprites and colours are often repainted every frame; see
  Tips.
- **Gentle recipes are gentle.** Turn **Chaos** up, or a rule's **Strength**.
- **An aimed ROM rule needs a code map.** If the ROM section says nothing is known about
  its code yet, play the game a while first.
- **The game must be running.** A start waits for the next frame, so nothing happens while
  the game is paused.
