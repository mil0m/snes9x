# The headless explorer (plan, roadmap #45 and #46)

Mikael (5 Nov) picked this after #79: *anything really cool, with tokens to spare and limits
that reset at night*. You keep a moment you like (#47). Overnight, the explorer plays that
moment unseen hundreds of times, much faster than real time, each time with a different
corruption, and scores every variant. In the morning there is a ranked gallery of the
strangest ones that kept running, each with its recipe, and each one plays back exactly.

## What exists already

- **Kept moments** (#47, #79): a keyframe, the ROM then, and every frame's buttons. They're
  exact starting points, and they're shared through the library.
- **Unseen runs** (#52, #78): frames nobody sees, drawn on request, with everything put back
  after. `CrashDetector` notices crashes, and `froze_at` tells a freeze from a still scene by
  comparing with a run without the change.
- **Every kind of corruption is a recipe rule**: noise ops, area ops, code ops (opcode swap,
  branch flip, NOP-out), pointer swap (#32) and kill (#34), aimed by the code map, the jump
  tables and the call graph that the tracer files in the library hold.
- **`corrupt-smoke`** plays recipes headless on real games and measures them.

## Design

**Where a variant starts.** From a kept moment's keyframe, with the moment's ROM, playing
the moment's own buttons frame by frame for its length (capped at 20 s). The moment's
corruption writes are left out: the variant's corruption replaces them. Every variant
presses exactly the same buttons, so the only difference between two variants is the
corruption. The **control** is the same start and buttons with no corruption, and every
picture is compared with it frame by frame. A moment that started a run from its saved
moment works too; for measuring, `corrupt-smoke` makes its own moments the same way the app
does: 20 s of a pseudo-player, kept.

**What a variant is: a recipe.** That way *Keep as recipe* is just saving it. The generator
makes variants in families, in equal shares, so the ranking compares across kinds and the
report can say which families the explorer favours:

| Family | What | Needs |
|---|---|---|
| reroll | the moment's own recipe with another seed | a recipe in the moment |
| nudge | the moment's recipe with one rule changed: strength, range moved, op swapped | a recipe in the moment |
| ram | one rule: WRAM/VRAM/OAM/CGRAM, op from Add, Random, Bitflip, Freeze, Shuffle, Copy, Swap; a random region (WRAM regions the game uses most, from the WRAM counters, #23), count, how often | nothing |
| code | one code rule (opcode swap, branch flip or NOP-out) aimed at busy functions, firing a few times | the code map |
| swap | one pointer swap firing once | jump tables |
| kill | one busy function turned off | the call graph |
| data | ROM data bytes the game reads (aim: data), changed slowly | the code map |

Families that lack what they need are left out and their share goes to the rest. Each
variant also gets its own seed, so two variants of the same template differ.

**Running one.** Load the keyframe, put the ROM back as it was, set the variant's recipe and
start the engine. Then for each frame: set the recorded buttons, run it drawn (the picture
is needed), and take a thumbnail. A crash (`CrashDetector`, as the guard sees it) ends the
run. There is no guard. A candidate that only survives with the guard's help isn't what the
gallery should offer, and the guard's rollbacks would make the pictures meaningless. The
tracer runs, so the code map learns which ROM bytes ran as code for the first time.
Everything is put back after each variant.

**Thumbnails.** `CoreAccess::thumbnail()` (new): the drawn screen shrunk 4 times to 64 × 56,
RGB. That's small enough to keep every frame of every run (about 10 KB a frame). The full
256 × 224 picture is taken only for the frames that go into a strip.

**The score.** Each run gets these numbers, which are kept and reported:
- **alive**: no crash in the window, and no freeze at its end (`froze_at` against the
  control). A run that isn't alive scores 0.
- **different** (0..1): over the second half of the window (an effect that lasts), the mean
  share of thumbnail pixels that differ clearly from the control's at the same frame.
- **lively** (0..1): what keeps a black or frozen screen from winning. It's the smaller of
  two numbers. *Motion*: how often the picture changed in the last 2 s, relative to the
  control's, capped at 1. *Detail*: how many distinct colours and edges the last thumbnails
  have, relative to the control's, capped at 1.
- **new code**: ROM bytes run as code that the map had never seen run.

**score = alive × different × lively × (1 + 0.25 × log2(1 + new code / 64))**. New code is a
bonus, not a goal: a variant that looks the same but runs strange code is invisible to a
player. The weights are a first guess, and step 1 tests whether the order means anything.
(It didn't, quite: the score that shipped is in *How it went*.)

**What comes out.** One folder per exploration:
`<library>/<game folder>/explored/<moment name> <date time>/`. It holds:
- `candidates.json`: every variant, with its family, recipe, seed and numbers;
- for the best 24: the recipe (`.json`), a **moment** (`.s9xmoment`: the variant's run
  recorded with `MomentRecorder` as it plays, so it plays back exactly what the explorer
  saw) and a strip (`.png`, 6 full pictures across the window);
- `gallery.html`: self-contained, with the strips inline. It opens in a browser, and I
  publish it as an artifact on request.

**In the app (#46).** A Workshop page, *Explored*:
- it lists the explorations and their candidates with the strips;
- **Play** plays a candidate's moment (#47's player);
- **Keep as recipe** saves the recipe, with the moment's keyframe as its saved moment, so
  Corrupt (F7) starts it from the same place;
- **Explore…** on the Moments page starts the explorer on a kept moment as a separate
  low-priority process (the Snes9x core is one per process; the game you play keeps the
  app's). Candidates show up as it goes.

How the app finds and starts the explorer binary is settled in step 3, after step 1 shows
what the explorer costs.

## Steps

1. **Measure first: does the score mean anything?** `corrupt-smoke --explore [variants]`, a
   minimal explorer inside the smoke tool. On Goof Troop, Pocky & Rocky and FE4, two
   starting moments each, 300 variants per moment. It reports:
   - speed: variants an hour, so how many fit in a night;
   - how many survive, per family;
   - how the numbers spread.

   Then **the blind test**. From each moment, the 12 top-ranked survivors and 12 survivors
   picked at random are shuffled and labelled. A fresh subagent that doesn't know the
   ranking rates each strip from 1 to 5: *how strange is it, while still a playable game*.
   **Go** if the top 12 are rated clearly higher: a mean at least 1 point above, and at
   least 8 of the 12 above the random ones' median. If not, the components say what to
   re-weight: the ratings are compared with each number on its own.

   A second check: do top-ranked survivors keep running past the window? Each one is
   played on for 60 s more, unguarded, with the pseudo-player, against the random ones.
2. **The explorer for real**: `corrupt/explore.{h,cpp}` (the generator, the scoring, the
   runner over `CoreAccess`, `Engine` and `RomKeeper`), tests first with the fake game. Then
   the output folder, candidate moments, strips and `gallery.html`, and the explorer as a
   command (`snes9x-explore <rom> <moment> [--variants N] [--hours H]`).
3. **The app (#46)**: the *Explored* page, Play, Keep as recipe, Explore… Then the UI tour.

## How it went

### Step 1: does the score mean anything?

`corrupt-smoke --explore 300 2` on Goof Troop, Pocky & Rocky and FE4: two scenes each, 300
variants per scene, 20 s windows. The scenes came from a pseudo-player's play after the
warmup, picked from a scout of 1-4 minutes in so that each is real play (a beach, a temple
court, a map and a dialogue), not menus. The code map, jump tables and functions were learned
from that same play.

**Two fixes before anything could be measured:**
- The detector's guesses (black screen, frozen picture, NMI off) misread unseen frames. Every
  control "crashed" at 480 frames. The explorer now uses only the definite crashes, as #52's
  runs ahead do. A freeze is told from the pictures (`froze_at` against the control), and a
  black screen scores low as not lively.
- A pseudo-player pressing Start pauses the game for the whole window, so the window leaves
  Start and Select out. The lead-in still presses them, so the game gets going.

**Speed.** About 1.9 s per 20 s variant in the test build, which is unoptimized. The optimized
explorer the app runs takes **0.55-0.9 s: 4,000-6,000 variants an hour in one process**
(the ram family aimed at busy WRAM is the slow end), so 30,000-50,000 a night. The results
are the same in both builds.

**Survival is high; visible change is the hard part.** 92-99% of variants kept running for
20 s. In the first run only the game-aware families changed anything you could see:

| Family | visibly changed (Goof / Pocky / FE4) |
|---|---|
| code (opcode swap, branch flip, NOP-out in busy functions) | 22% / 38% / 40% |
| kill (one busy function off) | 5% / 29% / 35% |
| swap (one pointer swap) | 3% / 19% / 5% |
| ram (after aiming it at the most-read WRAM, below) | 3% / 21% / 6% |
| data (ROM data the game reads) | 8% / 7% / 5% |
| reroll / nudge of the moment's own gentle recipe | 0-14% |

The RAM family at first hit random windows of WRAM, one to eight bytes now and then. Under 2%
of the picture changed on average. It now aims at the WRAM the game reads most during the
control (#23's counters, `busy_wram`, clear of the direct page and the stack) and hits harder.

**The blind test, first round: not a Go.** For each of the six scenes, the 12 best by score
and 12 random survivors that visibly changed something were shuffled under letters. A fresh
subagent per game rated each strip 1-5 against the control: *how strange and interesting,
while still a running game*. The top 12 averaged **2.65**, the random ones **2.34**. Only
one scene (Pocky's temple) cleared the bar of +1 and 8 of 12. The raters' reasons showed why:
- *how much* differs barely tracked the ratings (correlation 0.02). Pure tile garbage and a
  screen that goes black near the end both count as "very different";
- strips the score called lively rated higher (2.9 against 1.8);
- in a quiet scene only a handful of variants change anything, and the top 12 was padded with
  ones that look like the control.

**Tuned on those ratings.** The same variants were played again (the same seeds give the same
runs) with the raw numbers recorded beside each rated strip. Two things predicted the ratings:
- **edges** against the scene's: about as many, 2.6; up to twice as many, **3.9** (sprites gone
  wild, a different level); more than twice, 2.0 (tile garbage); far fewer, 1.3 (blank);
- **colours**: under 30% of the scene's rated 1.2.

Once a change is visible, its size stopped mattering (2.4-2.7 from 2% to 100% of the picture).
The score that shipped:

> **score = alive × visible × looks like a game × moving × (1 + 0.25 × log2(1 + new code / 64))**
>
> *visible*: the share that differs over the window's second half, counted up to 10%.
> *looks like a game*: edges within 0.7-2× the scene's (falling off outside), times colours up
> to 60% of the scene's. *moving*: picture changes at the end against the control's.

On the tuning ratings, its correlation with the raters was 0.46, against 0.39 for the first
score, and it no longer fell apart on FE4 (0.33 against 0.04). Each scene's top 4 averaged
3.4. Those are the ratings it was tuned on, so they prove nothing.

**The blind test, held out: Go, with a floor.** Six new scenes (Goof at 1 and 4 min, Pocky at 3
and 4, FE4 at 2 and 3), a new seed, three new raters:

| Scene | top 12 | random visible | top above the random median |
|---|---|---|---|
| Goof 1 | 2.08 | 1.00 (only 3) | 7/12 |
| Goof 2 | 2.25 | (none left over) | 12/12 |
| Pocky 1 | 1.75 | 2.00 (only 1) | 3/12 |
| Pocky 2 | 2.92 | 1.00 (6) | 8/12 |
| FE4 1 | **3.42** | 1.75 (12) | **11/12** |
| FE4 2 | 3.00 | 1.92 (12) | 7/12 |
| all | **2.57** | **1.62** | |

That's +0.95 overall, against +0.3 for the first score. In the two scenes with a full random
group the margins were +1.7 and +1.1. The score's levels held on the held-out ratings:

| score | mean rating | rated 3 or more |
|---|---|---|
| under 0.05 | 1.3 | 1 of 27 |
| 0.05-0.3 | 1.9 | 3 of 25 |
| 0.3-1 | 2.3 | 11 of 24 |
| 1 and up | **3.4** | **21 of 30** |

So a candidate needs at least **0.3** (`kMinScore`). The weak spot was quiet scenes, where only
3-16 of 300 variants change anything; such a scene now keeps a short list instead of 24 that
look like nothing.

### Steps 2 and 3: the explorer for real, and the app

- `corrupt/explore.{h,cpp}`: `make_variants` (families in equal shares; each variant a
  recipe with its own seed), `busy_wram`, `start_from` (a moment's keyframe, ROM and
  buttons), `play_variant` (unseen and drawn, the moment's buttons, a thumbnail every frame,
  definite crashes end it, new code counted against the map, everything put back),
  `score_run`. `corrupt/picture.{h,cpp}`: thumbnails, `differing_share`, `detail_of`.
  `CoreAccess::picture` (the screen, or a 64 × 56 thumbnail; `SnesCoreAccess` from
  `GFX.Screen`). `corrupt/exploration.{h,cpp}`: `candidates.json` and the gallery page.
  `Library::explored_dir`, `list_explorations`, `new_exploration_dir`, `delete_exploration`.
- `snes9x-explore <rom> <moment> [--library DIR] [--minutes M | --variants N]`
  (`tests/headless/snes9x_explore.cpp`). It reads the game's tracer files from the library, and
  the control learns the moment's scene. Each variant is recorded with `MomentRecorder` as it
  runs, and a candidate's moment, recipe and strip are written as it enters the best 24 (and
  removed when it drops out). `candidates.json` is rewritten every 30 s, and Ctrl-C keeps what
  was found. At the end each candidate's moment is checked against its run's end (#47's
  check): every one played back exactly (6/6, 17/17). `./build-mac.sh qt` builds it optimized
  beside the app.
- The app: the Workshop's **Explored** page (page 8): the explorations, the candidates best
  first with each one's strip, **Play** (its run, as a kept moment), **Keep as Recipe** (its
  recipe with the moment's start as its saved moment), the gallery, delete, show in folder.
  **Explore** on the Moments page (10 minutes, an hour, overnight) starts `snes9x-explore` as a
  separate, low-priority process (`ExploreRunner`). The page shows its progress, and **Stop
  Exploring** ends it, keeping what it found. The sidebar says *exploring* while it runs. The UI
  tour explores its kept moment for 15 s and shows the page while it runs and after.

**Not done:** the gallery isn't published anywhere by the app: it's a file to open or send.
Keep as Recipe doesn't carry ROM changes the starting moment already had (its candidate's
moment does). Families stay one change each; stacking two good finds is an idea for later.

## Follow-up (5 Nov, roadmap #80): seeing what an exploration did

Mikael: *shouldn't we have a way to visualise these summaries?* The Explored page and the
gallery say "671 variants, 539 kept running, 222 visibly changed" and list the best 24. That
leaves the useful questions unanswered. Three charts, in the app and in the gallery, each
answering one:

1. **What each kind did.** One horizontal bar per family (code, kill, swap, ...), split into
   three: *stopped* (crashed or froze), *ran, nothing showed*, *visibly changed*. Shares of its
   own tries, so a kind tried 90 times compares with one tried 100 times. Sorted by the
   visible share, best first. The count kept as candidates is printed at the end. This tells
   what works on this game: on Goof the code family, on Pocky kill and swap as well.
2. **How the scores spread.** A histogram of the scores of the tries that visibly changed
   something, in bins that widen as they go up (0-0.05, 0.05-0.1, ... 1-1.5, 1.5-2, 2-3, 3+),
   with the keep line at 0.3. Bins above it are in the accent colour, below it in grey (one
   hue plus grey: the point is above or below the line). Tries that changed nothing score 0
   and are counted in the caption, not drawn: they would dwarf every other bar.
3. **When the good finds turned up.** Every try that scored 0.3 or more, as a dot at (its
   number in the run, its score), with a step line for the best score so far. One axis (the
   score), so no second scale. A cloud that keeps rising to the right says longer runs pay
   off; a flat line after the first few hundred says this scene is played out.

**The data.** `candidates.json` gains `tried`: one entry per variant, its family, outcome
(stopped, unchanged, visible) and score, in the order tried: `[family, outcome, score ×
1000]`, about 12 bytes each, 600 KB for a night. Files from before have no `tried`. They
get chart 1 from the family totals they already hold, and the other two are left out with a
line saying why.

**Where the numbers come from.** `corrupt/exploration`'s `stats_of(Exploration)` is pure and
tested. The Qt page draws it with QPainter in the palette's colours (the house rule: no fixed
colours), with a tooltip per bar and dot. The gallery draws the same in inline SVG, with
theme tokens and a `<title>` per mark.

**Done when:** stats_of is tested on a fake exploration and an old file; the page shows the
charts in the widget tests and the tour (with a real exploration); the gallery shows them in
light and dark at phone width; the chart colours pass the dataviz validator.

### How it went (#80)

Built as planned. The colours were checked with the dataviz validator before use. The two
hues, blue for *visibly changed* and orange for *stopped*, pass every check in light and dark
(the nearest pair is ΔE 24.7 for colour-blind readers, 33.6 for everyone else). *Nothing
showed* is a recessive grey, with a legend, end labels and the table beside it, so colour is
never the only way to read a bar. In the app the charts are drawn from the palette: the
highlight colour, the warning tone and greys mixed from the window and text colours.

On a 4-minute exploration of Pocky's temple (430 tries), the charts said what the totals
couldn't:
- ROM data changes and pointer swaps showed most often (48% and 44%). Code changes found the
  most keepers (11) but stopped the game about half the time.
- Most visible changes scored between 0.5 and 1.5.
- Strong finds kept turning up to the very end, so this moment wasn't played out after 4
  minutes.

The gallery renders in light and dark, and at 390 px it doesn't scroll sideways; on a phone
the SVG text gets small, and the table under the first chart carries the numbers.
