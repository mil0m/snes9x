# Corrupting games: the guide

Snes9x can corrupt the game you're playing: change bits of its memory, or of the game
itself, while it runs, for glitches you'd never see otherwise. This guide is also in the
app: the **?** button at the top of the Corruption panel, or **Corruption > Guide…**.

**Contents:** Start here · The panel and the Workshop · Getting back to normal · Keeping a good
one · The words · The F7 key and the buttons · Recipes and rules · When the game crashes · Sharing with a friend · The memory viewer ·
Corrupting the ROM · Survivors and pins · Editing memory like a picture · Tips · When nothing
seems to happen

## Start here

1. Open a game (**File > Open**). Show the panel: **Corruption > Show Panel**.
2. In the **Recipe** list, pick a starter, say **Starter: Gentle WRAM noise**. Starters come
   with the app.
3. Press **Corrupt** (**F7**) and play. Things start going slightly wrong.
4. **Re-roll** (next to **Seed**) for different dice: the same recipe, different glitches.
5. **Back to Normal** puts everything back as it was when you pressed Corrupt.
   **Normal, Stay Here** (under it) stops and cleans the ROM but keeps you where you are.

That's all you need. The rest of this guide is for when you want more.

## The panel and the Workshop

The **panel** (**Corruption > Show Panel**) holds what you use while you play. Under the
game's name, a **state bar** says what is going on (*Pick a recipe to start*, *Corrupting · 0:13
· 6 changes*, *Stopped · its changes stay until Back to Normal*). Then the recipe list
and **Save** (enabled when there is something to save), a message line with short news that
fades after a few seconds (hover it for the full story), **Corrupt (F7)** / **Stop (F7)** and **Back to Normal**, **Chaos** and **Seed**
with **Re-roll**, the **Buttons** of the recipe, and the **Rules** (on or off, and strength).
Below them, one line about the crash guard (with an **It Crashed** button) and one about the
ROM; each opens its Workshop page. Double-click a rule to edit it. Until a recipe is loaded, a *Start here* card shows
what to do first.

Everything else is in the **Workshop**: press the **Workshop** button at the top of the
panel, or choose **Corruption > Open Workshop**. It is one window with a list of pages on the
left. The Workshop opens on the page it was on last, at the size it had. The links in the panel
(*Add or change rules*, the crash guard line, the ROM line) open their own page.

**There is always a recipe.** A game starts with an empty one called **Untitled**, like a new
document. Everything you make lands in the recipe you have: rules, functions you turn off or
guard, changes, paint, pins. The top of the Workshop's page list names it and says whether
it's saved (*Empty · not saved*, *Not saved*, *Changed since saved*, *Saved*), and so does the
window's title. **Save** gives an Untitled recipe its name. Each page says which of its
parts are saved in the recipe and which belong to the game: function names and what the app
learns about the code are the game's, for every recipe; the Safety switches are the app's.

The list of pages is also a summary: beside each name, a few words say what's going on
there, whichever page you're on: *3 rules*, *2 off* (functions), *40 painted*, *14 changed*
(ROM bytes), *1 crash* or *guard off* (in the warning colour), *2 here* (other people in the
library) or *remote on*. No words means nothing to report.

- **Recipe**: name and notes, New Recipe, Save, Save As…, Share…, Import…, the rules in full, and
  what else the recipe keeps (switches, changes, protected functions, pins, paint).
- **Functions**: turn the game's functions off, name them, change how they work. The busiest come first each time you open the page.
- **Memory**: the memory viewer, with paint.
- **ROM**: what changed, what the app has learned, Export ROM…, Restore Clean ROM, Survivors
  and pins (the survivors update by themselves).
- **Safety**: the crash guard's switches and policy, and the game save backups.
- **Sharing**: the library folder, who is here, and remote control (the address and key
  show once you tick *Allow remote control from a browser*).

## Getting back to normal

There are four ways to undo something. Only one of them undoes everything.

| Button | What it does |
|---|---|
| **Stop** (Corrupt, while it runs) | Stops corrupting. What it already changed **stays**: the game keeps running, glitched. |
| **Back to Normal** | Stops, puts the game's ROM back to clean, and takes the game back to the moment you pressed Corrupt. Everything it did is gone (and so is your play since then). |
| **Normal, Stay Here** | Stops and puts the ROM back to clean, but the game plays on from where you are: your progress stays. What the rules did to RAM (a health value, a position) stays too, until the game writes over it. The panel says *ROM clean, playing on from here*. Also on the remote page. |
| **Restore Clean ROM** | Puts only the ROM back. What corruption did to RAM stays. |
| **Restore This Save…** (Game Save Backups) | Has nothing to do with corruption: it restores your **in-game save** (the cartridge's battery save) from a copy and restarts the game. |

## Keeping a good one

| You want to keep… | Do this |
|---|---|
| Something wonderful that just happened | **Keep This Moment (F8)**: the last 10-20 s, to play back exactly. See *Kept moments*. |
| A glitch you just saw | **Save**, right away, while it's still on. The recipe is saved with this run's dice (the seed) and the moment the run started, so loading it plays the same run again, as long as you press the same buttons. While the run starts from a new moment, the panel says so: *Save keeps it, so the run can be replayed.* |
| The recipe, to play again | **Save**. With *Remember where each run starts*, it comes back to the same spot with the same dice. |
| One exact ROM change you liked | Workshop, **ROM** page: **Survivors and pins**, **Pin Selected**, then **Save**. See *Survivors and pins*. |
| Bytes you set by hand | They're kept in the recipe already (*paint*); **Save**. |
| The corrupted game, to play anywhere | **Export ROM…**: a .sfc file of the ROM as it is right now (pins, paint and whatever the rules changed), for other emulators or a flash cart. |

## Kept moments

When something wonderful happens, press **Keep This Moment (F8)**, in the panel or on the
keyboard. It keeps the last 10-20 seconds: the game from a little before, and every button
you pressed and every byte corruption wrote since. Playing it back shows exactly what you
saw, on your machine or a friend's. It works with or without corruption on.

The game pauses for about half a second while the moment is checked: it plays once, unseen,
and is compared with the game now. The message line then says *Kept the last 14.2 s: it plays
back exactly*. If it ever says *it doesn't play back the same*, the moment is kept anyway; tell
whoever works on the app.

When the crash guard rolls back, a moment keeps what came before the point it went back to.
Playing the moment shows the game going on from there, without the crash. A moment can be
shorter than 10 s when you go back to normal, rewind or load a save: the game jumps
elsewhere, and a moment can't reach back past that. The message says where it starts.

The Workshop's **Moments** page lists this game's kept moments, yours and everyone's in the
library. Newest are first.

- **Play** turns corruption off and plays one from its start. The panel counts *Playing a
  kept moment · 6.1 of 14.2 s*.
- Press any button on the pad to take over right there.
- When it ends, the game is yours from the golden moment on, corrupted ROM and all. Back to
  Normal cleans up as usual, and Corrupt starts a new run.
- **Stop Playing** in the panel stops a playback where it is.
- The pencil renames a moment and the bin deletes it.
- The folder button shows the files: send one to share it. Its file is about 100 KB.

## Exploring a moment

Found a moment you like? On the Moments page, pick it and press **Explore**, then choose how
long: 10 minutes, an hour, or overnight. The explorer plays that moment again and again in
the background, unseen and much faster than real time (about 4,000 tries an hour). Each try
uses a different corruption, and your own button presses, so the only difference is the
corruption. You can keep playing meanwhile; it runs at low priority.

First it plays the moment about 40 times **without** corruption, most with one button press
moved a few frames, dropped or added. That's **normal play**: a game is chaotic, and a press a
frame later can send it down quite another path. Whatever normal play does on its own isn't a
find. This takes about half a minute; the running line says so.

Then each try is scored against the moment played without corruption:
- **Visible**: does the picture change? Past a tenth of the screen, more doesn't count.
- **Looks like a game**: about as many edges and colours as the scene. Tile garbage, a black
  screen or a solid colour score low.
- **Moving**: still moving at the end, as much as the scene does. A frozen picture scores 0,
  and so does a crash.
- **New code**: a bonus when it ran parts of the game's code that never ran before.

A try is kept when it goes **beyond normal play**, in one of two ways:
- **Looks different**: it scores more than any normal-play run did (and at least 0.3).
- **Acts different**: something visibly changed, and the game's code ran in a way normal play
  never does: functions normal play never calls, functions it always calls going quiet, or
  more code never run before than normal play ran.

Candidates are ranked by their **find** number: the score over normal play's best, plus 1
for each way it acts different. 1 or more is kept. A quiet scene may give just a few.

The Workshop's **Explored** page shows what it found, best first, with six pictures across
each one's run and why it was kept (*Acts different: 3 functions normal play never calls*).
The summary says how far normal play went:
- **Play** plays a candidate's run exactly as the explorer saw it. Press any button to take
  over.
- **Keep as Recipe** saves its recipe in the recipe list. It starts from the moment's start,
  so Corrupt plays it live from the same place. ROM changes the moment already had aren't in
  it; Play shows those.
- The arrow button opens the **gallery**: one page with every candidate's pictures and recipe,
  to look through or send.
- **Stop Exploring** stops early and keeps what it found so far.

Under the candidates, **What it did** shows three charts (the gallery has them too). Hover
over a bar or a dot for its numbers.
- **What each kind did**: for each kind of change (code, kill, swap...), how often it stopped
  the game, ran without showing anything, or visibly changed something. It tells you what
  works on this game.
- **How the scores spread**: the scores of the tries that visibly changed something, with
  normal play's best as the line. Tries that act different can be kept from below it.
- **When the good finds turned up**: each find by its find number, by when it was tried, and
  the best so far. Dots that keep rising to the right mean a longer exploration would likely find more; a
  flat line means this moment is played out.

Explorations live in the library (`explored/` in the game's folder), so a friend sharing it
sees yours.

## The words

| Word | Means |
|---|---|
| **Recipe** | A saved set of rules: what to corrupt, how and when. Each game has its own. **Starters** are recipes that come with the app. |
| **Rule** | One line of a recipe: "every 2 seconds, put a random value in 1 byte of work RAM". |
| **Run** | Corruption switched on, from the moment you press Corrupt until you stop. |
| **Seed** | The dice. The same recipe with the same seed, from the same start, does exactly the same thing. **Re-roll** picks a new seed; click the number next to it and type to set one. |
| **Strength** | How hard a rule hits: fewer bytes each time, smaller changes, and below one byte, less often. Each rule has one; **Strength, all rules** turns them all up or down together. They multiply: a rule at 50% with all rules at 50% hits at 25%. Hover a rule's slider to see what that means for it ("changes 2 of its 8 bytes each time it fires"). |
| **Memory** | Which part of the console a rule changes (see below). |
| **Start moment** | A save state taken when a run starts. **Back to Normal** goes back to it, and **Save** stores it with the recipe (with *Remember where each run starts* on), so loading the recipe replays from the same spot. |
| **Crash guard** | Notices when the game crashes, goes back a few seconds and changes what caused it. |
| **Code map** | What the app has learned about the ROM while you played: which bytes are code, which are data, which are graphics. |
| **Survivor** | A ROM change the game kept using for a long time without crashing. |
| **Pin** | A ROM change kept in a recipe on purpose. |
| **Paint** | Bytes you set by hand in the Workshop's Memory page. |

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

## The F7 key and the buttons

There are two corruption keys. **Corrupt / Stop (F7)** starts and stops a run, and **Keep This
Moment (F8)** keeps the last 10-20 s (see *Kept moments*). Change them under **Options >
Shortcuts…** (the lines starting *Corruption:*). Everything else (Back to Normal, Re-roll,
More / Less Chaos, Mark Crash, Save Recipe) is a button.

A recipe can also have **buttons**: its slots 1-9. The panel's **Buttons** section shows one
button for each slot in use, named after what it does (*gravity off*, *jump +3*, a rule's
name; if a slot holds several things, *Burst + 2 more*). It only shows when the recipe has
some. What a press does:

- A rule whose *When* is *When I press a button* fires once on each press.
- A kept switch or change (see *Turning the game's functions off*) is a toggle. It stays lit
  while it is on.

The remote page shows the same named buttons. To put something on a button, pick **Button 1**
to **Button 9** in the Functions page or in **Change It…**, or in the rule editor's *When*.
(Recipe files still call them `hotkey`.)

## Recipes and rules

**The Recipe list** has your recipes for this game first, then the starters. Choosing one
loads it. **New Recipe** starts an empty, Untitled one. If the recipe you have has unsaved changes, it asks first: **Save**, **Don't Save**, or **Cancel** (nothing happens). An Untitled recipe with nothing in it isn't asked about. **Save** keeps it in your library (an Untitled recipe or a starter
asks for a name first: a starter becomes your own copy). The Workshop's **Recipe** page also has Save As,
Share and Import (they're in the **Corruption** menu too), and the library folder is on the
**Sharing** page.

**Remember where each run starts**: saving a recipe also saves its start moment, and
loading it goes back there, so the same seed plays out the same way. Untick it to load and
save recipes without touching where the game is.

**Rules** are listed in the panel: tick **On** to switch one off for a while, drag
**Strength** to tone it down (hover the slider: it says what the rule does at that
setting, times **Strength, all rules**), double-click to edit. The Workshop's **Recipe** page lists them
in full, and shows how often the crash guard blamed each under **Crashes**. **Add Rule…**
is there too; the small pencil, copy and bin buttons
beside it edit, duplicate and remove the selected rule. Edits apply on the next frame, to
that rule only.

**The rule editor**, top to bottom:

- **Where**: the **Memory**, then a range **From**/**to** (offsets like `0x1A00`), or a
  **Region** you named in the Memory page. **Shape: a rectangle** comes from drawing one
  in the Memory page's pixel view.
- **What**: what happens to each picked byte, in three groups. **Bytes**: *Random value*,
  *Add / subtract* (−2..2 is a gentle nudge), *Flip bits*, *Swap bytes*, *Copy a block*,
  *Freeze* (stuck timers, infinite health), *Shuffle a window*. **Areas, like a picture**:
  *Shift*, *Flip*, *Levels*, *Filter*, *Stamp*, *Fill* (see *Editing memory like a picture*;
  best on graphics). **Code: the ROM only**: *Swap opcodes*, *Flip branches*, *NOP out*,
  *Swap pointers in jump tables*, *Turn functions off* (see *Corrupting the ROM*). The code
  ones are greyed unless **Where** is rom; switch away from the ROM with one picked and it
  becomes *Random value*, with a line saying why.
- **Aim** (ROM only): pick bytes by what they are. See *Corrupting the ROM*.
- **When**: every N frames (60 is about a second), a chance per frame, once at a frame,
  when I press a button, or once when the run starts (for the ROM: the classic way, see below).
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

The crash guard's switches and **Then** live on the Workshop's **Safety** page. The panel
keeps one line about it and the **It Crashed** button.

**It Crashed: Go Back** (also on the Safety page) is for a crash it missed (the game is stuck but the picture
still moves): it goes back about two seconds and blames what changed.

It also learns, per game, which bytes crash it (**the crash map**): rules with *Avoid
dangerous bytes* skip those, and the Memory page shows them in red. What it learned is
kept in your library, and adds up with your friend's.

## Sharing with a friend

**Share…** (Workshop, **Recipe** page, or **Corruption > Share Recipe…**) writes one
`.s9xrecipe` file: the recipe, its pins and its start moment. Send it any way you like. Your
friend opens the same game and chooses **Import…** (or **Corruption > Import a Shared
Recipe…**); it lands in their recipes and loads. (Named areas travel as plain
ranges.) If their copy of the game is a different version, the recipe still loads but may
hit different things, the start moment is skipped (it starts where they are), and pins
made for the other version are left out.

You can also share a whole library folder, for example as a git repository: each game has
a folder with its recipes, named areas, code map and crash map, and the maps of everyone
who plays add up. **Choose…** on the Workshop's **Sharing** page picks the folder. **Open
This Game's Folder** shows it, and **Look for New Recipes** picks up files your friend's sync
just brought in. **Who's Here** lists everyone who has learned files for this game and when
they last changed something (you are marked *(you)*); it warns if your sync tool left
conflict copies.

## The memory viewer

The Workshop's **Memory** page shows what's inside the game while it runs.

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
- The paint value and brush only show while **Paint Mode** is on.

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
ROM line says how much it knows, and the Workshop's **ROM** page has the details. It only knows what it has seen, so **play a while
first**; it's saved in your library and adds up with your friend's. In the Memory page,
**Code** shows it (teal), **Data** (pink), **DMA** (graphics, yellow-green). **Live**
(yellow) shows what the game runs and reads *right now*: bright this frame, fading over a
second and a half. Watch which code and tables a scene uses, or whether a corrupted spot
is in play.

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

**Pointer swaps.** Games pick what to do next from **jump tables**: lists of routine
addresses (one per enemy type, menu screen or cutscene step) that the code jumps through.
While you play, the app notes every such jump and finds the tables behind them; the ROM
page says how many (*52 jump tables found, 41 with entries to swap*), and the viewer's
**Jumps** overlay shows them (blue): entries the game used strong, likely ones lighter,
the jumping instruction in between. Hovering an entry says whose table it is and where it
goes.

**Swap pointers in jump tables** (in *What*) gives a picked entry the address of another
routine the same code jumped to. Every jump still lands at the start of a real routine,
so the game keeps going and does the wrong thing with its own parts: an enemy runs
another enemy's behaviour, a menu opens the wrong screen. **Entries each time** is how
many. **Also use entries nobody has jumped through yet** adds more variety (a bit less
safe); **Both ways** makes two entries trade routines.

Starters: **Pointer swap** (3 at the start, classic), **Pointer roulette** (button 1: one
more each press, live).

**When the ROM crashes the game**, the crash guard looks at which changed bytes the game
actually ran or read just before, and puts back only those, keeping the rest. That's what
lets a classic run with hundreds of changes keep playing. The **Changes** overlay shows
changed bytes in orange, bright once the game has used them; hovering one says so.

**Bad jumps are caught at once.** Most crashes from changed code start the same way: right
after running a changed byte, the game jumps into the middle of an instruction, into empty
ROM, or into nowhere. The app notices that the moment it happens, puts back the byte that
caused it and plays that frame again, so you see at most one frame repeat instead of a
crash and a rollback of a second or more. The message line says *Caught a bad jump: reverted 1 ROM byte* (hover for *Caught at $C0:1234, jumped
into the middle of an instruction*), and the ROM page counts them (*12 bad jumps put
back at once*). It only works where the code map knows the code, so it gets better the more
you've played. Crashes that build up slowly (a wrong number that breaks things later) are
still the crash guard's. To watch a crash happen instead, untick **Undo a ROM change
the moment it derails the game** on the Safety page (or set `"instant_revert": false`
in the recipe). Starter: **Code drip** (two bytes of code changed every two seconds).

**Live ROM changes are tried before they land.** When a live ROM rule is about to change
the game, the app first plays a few seconds ahead with that change, unseen and silent, and
comes back. If the game would have crashed, the rule rolls again and the new pick is tried
instead; after three tries that firing is skipped. You feel it as a short pause when a live
ROM rule fires (around a tenth of a second), the message line says when something was
re-rolled or skipped (*Tested ahead: drip would crash, so it picked again*), and the ROM page counts them (*30 changes tested first (3
re-rolled, 0 skipped)*). It can only vouch for what the game actually uses in those
seconds: a change in code the game won't reach for a while goes through untried, and the
instant revert and the crash guard still watch it.

It's **Test ROM changes before they reach the game** on the Safety page. In the recipe,
`"test_first": 0` turns it off, and a number of seconds looks further ahead (3 by
default).

Each switch on the Safety page has a line under it saying what it watches for, what it does
then, what it costs and what *off* means, followed by what it has done for this recipe so far
(*So far: 12 tested, 2 picked again*) or why it has nothing to do right now (*This recipe
has no ROM rules*). *After a crash* says what the chosen action does in one sentence.

## Survivors and pins

**Survivors and pins** (on the Workshop's **ROM** page) lists changes the game used in
at least 100 frames with no crash afterwards, newest first: where, what changed, and the
instruction before and after (`BNE → BEQ`). Select the ones you like and **Pin Selected**,
with a note if you want. A pin is part of the recipe: it's applied every time a run
starts, the crash guard never undoes it, and it's skipped on a different version of the
game. **Save** to keep pins. A recipe with only pins is a hand-made ROM hack.

## Turning the game's functions off

Games are made of functions: one moves the player, one runs gravity, one checks collisions,
one thinks for each enemy, one scrolls the screen. The Workshop's **Functions** page
lists the ones the game has called, busiest right now first, with how many of the last 5
seconds they ran in. Tick **Off** and the game skips that function from the next frame on:
its first byte becomes a return, so it's called and comes straight back. Untick to put it
back.

The app can't know which one is gravity: try the busy ones near the top and watch what
stops. When you find a good one, give it a **Name** (*gravity*): double-click its **Name**
cell in the table (or select it and press Return), type, and press Return; Esc cancels, and
an empty name removes yours. The **Name** field under the table does the same. What you did
with a function (its button, a timer, changes) shows in the **In play** column. Names are
saved in the library (*names.<you>.json*), so everyone who shares it sees yours, and you
see theirs with who named it: *jump (erik)*. Then pick a button (**Button 1** to **9**) and **Keep in Recipe**:
that button turns it off and on again while you play (in the panel's **Buttons**, or on
the remote page). **Starts off** has it off from the start of every run. **Save** to
keep it.

**Finding one again.** Type in **Find by address or name**, and pick what to **Show**
beside it: *Running now* (ran in the last 5 seconds), *Off*, *In play* (a switch, a button,
changes or protected), *Guarded*, *Named* or *Not named yet*. Both apply at once, and
*12 of 545* says how many are left when some are hidden. The table doesn't reorder or hide
rows while you look at it: what you change shows in place, and the list sorts and filters
again when you pick another filter or come back to the page.

A paused game (the game pauses when you click in the Workshop, unless you turned *Pause when
unfocused* off) keeps your tick until it runs its next frame: the box stays ticked and its
tooltip says *Turns off when the game runs its next frame*.

**Keeping it safe from other corruption.** A function you play with is *guarded*: its code
(the *Size* column says how much the code map knows) is left alone by every other rule and
by paint, so only your change touches it. That's automatic for a function you switched off
or kept as a switch; tick its box in the **Guard** column (or **Protect it from other
corruption** under the table) to guard one without changing it, and **And what it calls**
to guard the functions it calls too (the Guard cell then says *+ calls*). *auto* in the Guard
column means it's guarded anyway, while it's off or changed; hover it to see why. When a guard
goes up, bytes other corruption had already changed in it are put back. Paint stops at
guarded bytes and the message line says which function kept them. **Show in ROM** opens the
Memory page on its code with the **Functions** colours: function code light, guarded code
stronger, the first byte of a function that's off strongest; hover a byte to see whose code
it is.

Switches work while corruption is on (press **Corrupt**). Some functions can't be skipped
(one that reads data placed right after its call, or one the game needs to keep going):
the game breaks, and the crash guard turns that function back on. A few are called two
different ways; those can't be ticked at all.

The list grows as you play: the busiest functions show up in the first second of a scene,
rarer ones (a boss, a menu) when you get there. What everyone has learned is saved in the
library (*calls.<you>.json*), like the code map, so it carries over.

**On a timer.** Tick **Timer** before **Keep in Recipe**: *off for 2 s every 10 s* turns it
off and on by itself, counted from the start of the run. Short times make it flicker.

For chaos instead of picking: the rule type **Turn functions off** (ROM) turns random busy
functions off when it fires; **Back on after** puts each one back after a while, so with a
trigger every 10 s and 5 s here a different function is missing half the time. Starters:
**Function roulette** (each press of button 1 turns one more off) and **Function flicker**
(one off every 10 s, back 5 s later).

## Changing how a function works

Turning gravity off is one thing; changing it is more fun. Select a function and **Change
It…**: its code, line by line, and what each line can become without breaking the shape of
the code. Don't read assembly? **Hover an instruction** and it says what it does in plain
words (*Adds a number to A… / #$04 is the number itself: 4*); hover **Can Become** for what
that kind of change does to the game:

- **a number**: `LDA #$04` becomes `LDA #$01` (gravity 4 → 1), or `ADC` becomes `SBC`;
- **a decision**: a branch becomes its opposite (*if on the ground* becomes *if in the air*),
  or *always*, or *never*;
- **a sum**: `ADC` ⇄ `SBC`, `INC` ⇄ `DEC`, `ASL` ⇄ `LSR`…: the speed goes the other way;
- **a call**: skipped, or sent to another function of the game: the enemy runs the player's
  movement.

**Apply** changes it at the next frame (corruption on); **Put Back** undoes it. **Keep in
Recipe** keeps it, on a button that toggles it if you like, starting on or not, and with a
timer (*on for 1 s every 4 s*). Nothing else corrupts a function while you change it (its
code is guarded). If a change breaks the game, the crash guard goes back to before the game
first ran your change (as far as its last 30 seconds reach) and puts it back: going back
only a moment would keep what the change already did to the game's memory.

**Risk** says, for each line, how often a change like it stopped the game in tests, judged
from where the changed value goes next: *Low · a value in memory* (a speed, a timer),
*Medium · an index* (it picks an entry from a table), *High · a pointer* or *a call's input*.
Select a line and the line under the choices says why, in words, and the measured share (*about
4 in 100 changes like this stopped the game within 30 seconds*). Click the Risk header to
sort the lowest first; nothing is hidden. It's about the line, not the whole function: the
game's busiest functions (its main loop and what runs every frame) break more easily than
their lines suggest, so start a little further down the list.

**Try 5 s Ahead** plays the change you've set up five seconds ahead, unseen, and once
without it, then says what happened, under the risk: *still running* (and whether the picture
differs from without it), *froze after 1.2 s* (the picture stopped, where without the change
it kept moving), *crashed*, or *the game didn't run this code in 5 s* (try again where the
game uses it). Your game doesn't change; it pauses for about a second. The Risk column is the
average for changes like this one; the try is this change, here, now. In tests it saw about
4 in 5 of the changes that stopped a game within 30 seconds, and almost never called a
healthy one broken. What it can't see: a change that only does harm later, or somewhere
else in the game. It needs corruption on, like Apply, and runs while the game runs.

For chaos: the code rules (*Swap opcodes*, *Flip branches*, *NOP out*) and the aimed byte
rules can tick **Only inside the busiest functions**, so their changes land in the code the
game is running all the time, and **Only where a change is low risk**, so they pick the
lines Change It marks Low. Six changes a minute stacked up kept the game moving in 45 of 60
tests with it, against 33 of 60 without.

## Editing memory like a picture

The old Photoshop way, built in. In the Memory page's **Pixels** view:

- **Alt-drag** draws a rectangle; **New Rule from Selection…** makes a rule for it, and the
  whole-area ops (*Shift*, *Flip*, *Levels*, *Filter*, *Stamp*, *Fill*) act on all of it.
- **Paint Mode**: drag to set bytes to **Value** with a **Brush**; Alt-click picks a byte's
  value. Each drag is one **stroke**. Painted bytes go into the recipe; **Clear Paint** on
  the ROM page forgets them.
- **Picture ▾ > Export PNG…** saves the selection (or the whole memory) as a greyscale
  picture; edit it in any image editor and **Picture ▾ > Import PNG…** puts the changed
  pixels back as paint (one stroke).

### Painting the ROM: draw first, then Apply

Painting the game itself usually crashes it, so ROM paint is a **draft**: the view shows
the bytes as you drew them (tinted, with a mark beside their rows) but the game doesn't have
them yet. Draw as much as you like, then press **Apply Paint**: everything drafted goes in
at once.

For the next **ten seconds** the app watches the game, whether you're corrupting or not. If
it crashes, the game goes back to the moment before Apply, the painted bytes come out
again, and your strokes are back in the draft. The page says so in orange: *Your paint
crashed the game: CPU stopped… It's back in the draft*. Undo a stroke or two and Apply
again: that's how you find the stroke that did it. If the game holds up for ten seconds,
the paint stays (a crash after that is the crash guard's, as usual). With *Catch crashes*
off on the Safety page there's no watch.

**Throw Away Draft** forgets the drafted strokes. Other memories (work RAM, VRAM…) aren't
drafted: they change as you drag, as before.

### Undo, redo and the history

**Cmd+Z** (Ctrl+Z) undoes the last stroke, **Shift+Cmd+Z** (Ctrl+Y) redoes it, while the
Memory page has focus; the arrows beside the draft do the same. **Paint History** under
*What's where* lists them, newest first: *Stroke · 24 bytes at 0x01C234 → EA*, *Applied ·
4 strokes, 54 bytes*. Click one to go back to just after it; the greyed ones after it can be
redone until you paint something new. Undoing an *Applied* takes it out of the game and its
strokes go back to the draft. The history is cleared when you load another game or recipe.

### Painting before the game starts

**Corruption > Start Games Paused**: a game you open waits before its first frame (the
panel says *Paused · Emulation > Run starts it*). Paint the ROM and Apply, then **Emulation
> Run**: the game boots with your paint, and the ten-second watch starts with its first
frame.

## Remote control from a browser

A friend (or you, from your phone) can run the corruption from a web page: load one of
the starter recipes that come with the app, Corrupt and Stop, Chaos, Re-roll, *It crashed: go back*, *Back to normal*, each rule on or off and its
strength, the recipe's buttons, and a live picture of a memory to paint in (tick
**Paint**, choose a value, tap or drag). The game itself isn't on the page: stream it as
you normally would.

On the Workshop's **Sharing** page, **Remote Control**: tick **Allow remote control from a browser**. The app
then listens on this computer only (port 8095), so on its own only a browser on this
computer can reach it: **Copy Link** and open it here to try.

**From elsewhere** it needs a way in that you control, with a login in front. One way is a
Cloudflare Tunnel on a domain of yours, with Cloudflare Access letting in only the emails
you choose:

1. Route a hostname of yours to `http://localhost:8095` on this computer with
   `cloudflared`.
2. In Cloudflare Zero Trust, add an Access application for that hostname that allows only
   your and your friend's emails. Never route it without this.
3. Type `https://<that hostname>` into **Public address**, **Copy Link**, and send it.

The link carries a key after the `#`, which browsers never send to any server; the page
takes it out of the address bar and remembers it in that browser, so next time the plain
address is enough. **New Key** makes a new one: old links stop working, everyone connected
is dropped, and browsers that remembered the old key forget it. The panel says who is connected
(the email they logged in with), and the message line says what each of them does
(*Remote (erik@…): pressed button 1*).

What a remote can't do: anything with files (your own recipes, saving, ROMs, backups,
exporting), or settings; the starters it can load are built into the app. Painting only changes the emulated SNES, which the crash guard
looks after: the worst a remote can do is crash the game it's corrupting. Remote paint on
the ROM goes in at once (no draft), with the same ten-second watch.

If you change the code: `tools/install-hooks.sh` makes a pre-commit check that refuses
commits containing a word from your own `.private-words` (your domain, emails; that file
is never committed) or anything shaped like a secret.

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

- **Is it on?** The Corrupt button reads *Stop* and the state bar reads *Corrupting* and counts changes.
- **Some memories bounce back.** Sprites and colours are often repainted every frame; see
  Tips.
- **Gentle recipes are gentle.** Turn **Strength, all rules** up, or a rule's **Strength**.
- **An aimed ROM rule needs a code map.** If the ROM line says nothing is known about
  its code yet, play the game a while first.
- **The game must be running.** A start waits for the next frame, so nothing happens while
  the game is paused.
