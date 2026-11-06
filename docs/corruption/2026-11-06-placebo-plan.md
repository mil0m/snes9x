# Does the explorer's score see corruption, or just another path? (#81)

The explorer (#45) scores a variant against **one** run of the moment without corruption.
Looking at Pocky's finds, Mikael saw the top one was nothing special: Pocky had dashed or
walked another way. A game is chaotic. Anything that nudges it, a corruption that changes
nothing interesting included, sends it down another path, and another path *looks different*
from the one control. So the score may be measuring divergence, not breakage. And the blind
raters were asked how strange a strip looked *against the control*, which a different path
also wins, so the validation could be fooled the same way.

There's a second gap. A great corruption changes how the game **behaves** (gravity gone,
enemies acting wrong, a mode the game never enters), not only how it looks. Today the score
sees behaviour only through the pictures and a small bonus for code never run before.

Before building more on the score (breeding, #45's follow-ups), measure whether it means
anything. Nothing here goes into the app yet.

## The test

**Placebos.** A placebo is the same moment with no corruption at all, only played a little
differently: one button press shifted 1-4 frames, one held press dropped, or one short press
added (never Start or Select), somewhere in the first quarter of the window so the path has
time to split. A placebo is **normal play by definition**. Whatever a placebo scores, a
corruption scoring the same has shown nothing.

**Normal play's range (the envelope).** Per scene, the control and 15 placebos (*natural
runs*) are played first and record what normal play does: their pictures, the values each WRAM byte takes, the
functions they call and how often, how sprites move. Then 20 more placebos set each signal's
**bar** (the most any of them reached), and 30 held-out placebos and 300 corruptions are
measured against it. A held-out placebo passes a bar set by 20 others about 1 time in 21 by
chance alone (5%), so the measure is the **share** past it, placebos against corruptions.

**The signals.** For each run:

*Looks different* (the pictures):
- **score**: today's score (against the one control), and its *different* share.
- **apart**: the share that differs over the second half against the **nearest** natural
  run, not the one control. A different path should be near some natural run.
- **new colours**: the share of the picture in colours (4 bits a channel) no natural run ever
  showed.

*Acts different* (the game's state and code):
- **steady bytes broken**: WRAM bytes the game reads that held at most 4 values in every
  natural run (modes, counters, flags, lives), which now take a value never seen. Bytes the
  variant's own rules write are left out: writing a value there proves nothing.
  Another path keeps such bytes steady; a broken rule doesn't.
- **out of range**: read bytes that vary in normal play (positions, timers) going well outside
  their natural range (by more than a quarter of it).
- **new functions**: functions called that no natural run called.
- **silenced**: functions every natural run called in at least a quarter of the frames, now
  called in under half as many frames as the fewest natural run.
- **sprite jumps**: per frame, a sprite whose nearest sprite with the same tile in the frame
  before is over 48 pixels away (teleporting), against normal play's most.
- **new code**: today's count, but against the control **and** the natural runs.

**What would say yes.** A signal is useful if held-out placebos rarely exceed normal play's
maximum (near the ~6% chance level) while a real share of corruptions do. A signal where
placebos score like corruptions measures divergence and is dropped.

**Then the eyes.** For each scene, 24 strips (12 frames each, so movement shows) shuffled
under letters: 6 placebos, 6 top by today's score, 6 top by *acts different* (fixed before
looking: the most behaviour signals beyond normal play's maximum, then the largest margins),
6 random surviving corruptions. Fresh raters, a new question: *is the game itself broken or
acting wrong, in an interesting way? A different path through normal play (the character
somewhere else, other enemies, another scroll position) counts as no.* Today's top beating
the placebos says the score sees something. The behaviour top beating both says the new
signals see more. Strips are still pictures, so the last word on behaviour belongs to
people pressing Play (later: a rating on the Explored page).

**Scenes.** Goof Troop, Pocky & Rocky, FE4 and Zelda (an action game, where behaviour should
show), two scenes each, from a pseudo-player's play as before (no kept moments exist yet).

## Code

- `corrupt/envelope.{h,cpp}`: `StateTrace` (one run's WRAM samples, OAM per frame, calls per
  function), `Envelope` (built from natural runs, then measures a run: `Signals`), and
  `perturb` (makes a placebo's buttons). Unit tests with fakes in `tests/corrupt/envelope_test.cpp`.
- `corrupt-smoke --placebo [variants] [moments]`: the test above; writes strips and a key to
  `CORRUPT_EXPLORE_OUT`.

## How it went

`corrupt-smoke --placebo 300 2` on Goof Troop, Pocky & Rocky, FE4 and Zelda (A Link to the
Past), two scenes each: 366 runs a scene, about 3.5 minutes each optimized. Then 24 strips a
scene (192 in all) rated blind by four fresh raters, one per game, with the new question.

**First, the scenes.** Only three of the eight are real play, where the buttons matter:
Pocky's temple (1 min), Zelda's field and Link's house. In the other five, every placebo
played exactly like the control: both Goof scenes are a **paused** game (the lead-in pressed
Start, and the window leaves Start out, so it stays paused), FE4's are the enemy's turn and a
dialogue, and Pocky's second is a waiting room. Where buttons change nothing, another path
can't happen, so *any* difference is the corruption's. That's also why #45's blind test
held: its best scenes were FE4's.

**In the real-play scenes, today's score prefers normal play.**

| real play (Pocky 1, Zelda 1, Zelda 2) | held-out placebos | corruptions |
|---|---|---|
| today's keep line (score 0.3+) | **30%** (27 of 90) | 11% (95 of 858) |
| score 1+ | 20% (18 of 90) | 6% or less |

A third of runs that are *only played differently* would make the gallery, three times the
rate of real corruptions. Mikael's Pocky find (just a dash) is what this looks like.

**The signals against the placebos' bar** (real-play scenes; chance is about 5%):

| signal | placebos past | corruptions past |
|---|---|---|
| score (against the placebos' best, not 0.3) | 0% | 2.0% |
| new functions | 1.1% | 2.9% |
| silenced | 1.1% | 8.2% |
| new code (against the natural runs too) | 1.1% | 5.7% |
| new colours | 7.8% | 12.5% |
| steady bytes broken | 6.7% | 7.6% |
| sprite jumps | 6.7% | 7.1% |
| apart (nearest natural run) | 6.7% | 7.8% |
| different | 5.6% | 4.8% |
| out of range | 10% | 9.2% |

The **code signals** (new functions, silenced, new code) are the clean ones: a different
path almost never runs functions normal play doesn't, or stops one it always runs. The WRAM
signals, sprite jumps and the picture against the nearest natural run are at chance: another
path changes them as much as a corruption does. And today's score works once its bar is what
normal play reaches instead of a fixed 0.3. In the scenes where buttons change nothing, every
signal separated (placebos 0-3%, corruptions up to 28% on FE4).

**The eyes** (mean of 1-5; *broken*: the rater said it isn't normal play):

| group | all 8 scenes | broken | rated 4+ | real-play scenes |
|---|---|---|---|---|
| placebos | 1.23 | 8% | 2% | 1.11 |
| random survivors | 1.31 | 12% | 6% | 1.33 |
| top 6 by today's score | **2.42** | 60% | 17% | 2.28 |
| top 6 by behaviour | **2.42** | 56% | **25%** | 2.28 |

- **At the very top, today's score does find breakage**: its top 6 rate far above placebos
  and random survivors, even in real play. What fails is everything below the top: the keep
  line lets normal play in.
- **The behaviour pick was not better on still pictures**, the same mean, but it had more 4s
  and the clearest "the game acts wrong" finds: Zelda's Link at game over, then in a dungeon
  cell (a pointer swap); Pocky jumping to the intro story (a pointer swap). The picture score
  found Link walking through a wall and Pocky scrolling to a night level. Still frames show
  behaviour poorly; *broken but dull* (garbage, black) rates 3 on pictures.
- The four placebos rated broken were rater mistakes (an FE4 cursor elsewhere, a paused Goof
  beach): about 1 in 12, the raters' own noise.
- Per signal against the ratings (rank correlation): apart +0.63, new colours +0.59,
  different +0.59, steady bytes +0.54, silenced +0.50, new code +0.50, new functions +0.47,
  score +0.46, sprite jumps +0.10. These mostly separate corruptions from the rest, which
  the groups already do.

**What it says.**
1. Mikael was right: in real play the keep line measures divergence. A fixed line against
   one control can't work; the bar has to be **what normal play reaches in this scene**,
   from natural runs (36 more runs per exploration, under 1% of a night).
2. Behaviour is measurable without an LLM, through **code**: functions never called and
   functions silenced, against normal play. Memory values and sprite movement aren't, at
   least as measured here.
3. Scenes from a pseudo-player are poor (5 of 8 had no play in them). Real kept moments (F8)
   are what the explorer should be tested on.
4. Raters looking at strips can't judge behaviour. A 👍/👎 on the Explored page, from people
   pressing Play, is the ground truth to tune against.
