# The explorer measures against normal play (#82)

#81 showed the explorer's keep line (score 0.3+) measures divergence in scenes with real play:
30% of runs that were only *played a little differently* (placebos) passed it, against 11% of
real corruptions. It also showed what works instead: the bar has to be **what normal play
reaches in this scene**, and the game's **code** shows behaviour (functions normal play never
calls, functions it always calls going quiet, code it never runs). This puts both into
`snes9x-explore` and the Explored page. Plan of the test: `2026-11-06-placebo-plan.md`.

## Normal play, before the variants

After the control (as today: learning the game's code, functions and jump tables), the
explorer plays the moment **37 more times without corruption**:

- the control once more, recording the functions it calls per frame;
- **16 natural runs**: placebos (one press shifted 1-4 frames, dropped, or added, in the first
  quarter; `perturb`), recording their calls, their pictures and what code they run (the map
  keeps it: code normal play runs isn't new);
- **20 bar-setting placebos**, each measured like a variant. Normal play's **best** is the most
  any of them reached: picture score, new functions, silenced functions, new code.

About 25 seconds optimized, under 1% of an hour. The app's running line says *Measuring
normal play (12 of 37)* meanwhile.

**Why 16 and 20.** As in #81, where with these counts a held-out placebo passed a code signal's
bar 1% of the time and the score's 0%. Fewer natural runs make more functions look new; a bar
set by fewer placebos is passed more by chance (1 in n+1).

## What's kept

A variant is a **find** when it stays running and

- **looks different beyond normal play**: its score is over normal play's best (and at least
  0.3, the old floor, for scenes where buttons change nothing and the best is 0), or
- **acts different**: it changed something you can see (2%+ of the picture differs) and at
  least one code signal is past normal play's best: functions no natural run called, functions
  every natural run called in a quarter of the frames now called in under half as many, or more
  code never run before than any placebo ran.

Finds are ranked by **how far past normal play** they are: score ÷ the picture's keep line,
plus 1 for each code signal past its bar (only when visible). A picture find starts above 1, a
behaviour find at 1 plus its picture share; both together rank highest.

**Why this rule.** Tried on #81's 192 blind-rated strips before building (in-sample: the bars
and the strips came from the same runs, so it's checked again below):

| rule | kept | their mean rating | placebos kept |
|---|---|---|---|
| today's line (0.3+) | 72 | 2.33 | 9 of 48 |
| beyond normal play (looks *or* acts) | 78 | **2.64** | **1 of 48** |
| of which picture only | 46 | 2.63 | 0 |
| of which behaviour only | 32 | 2.66 | 1 |

Ranking against the ratings (Spearman): score +0.46, picture vs the bar +0.48, code signals
+0.50, **both added, code only when visible +0.58**.

## What the page says

- The Explored page's summary: *Normal play: 37 runs with one press moved; their best score
  0.62, 1 new function.* The keep line in the charts is that best.
- Each candidate says why it was kept, in words: *Looks different beyond normal play (1.40,
  normal play's best 0.62)* and/or *Acts different: 3 functions normal play never calls; 2 it
  always calls went quiet; 412 bytes of code normal play never ran.* The table's column is
  **Find**, with the score in its tooltip.
- The charts: *How the scores spread* draws the keep line at normal play's best; *When the good
  finds turned up* plots each find's strength (1 = normal play's best, +1 per code signal), so
  behaviour finds show too.
- Older explorations (no normal play) read and draw as before.

`candidates.json` gains `normal` (runs and the four bars) and per candidate `find`, `acts`, the
code signals and the picture signals (`apart`, `new_colours`: at chance in #81, kept for tuning
against people's ratings later). `tried` gains a fourth number per try: its find strength × 1000.

## Code

- `corrupt/exploration.{h,cpp}`: `NormalBest` (normal play's best, from the bar-setting
  placebos), `Verdict judge(score, signals, best)`; the file format and the charts.
- `snes9x-explore`: the 37 runs, the calls tracer on every run, finds by `judge`; prints
  `normal <done> <of>` while measuring and `normal ...` once done.
- `corrupt-smoke --placebo`: the new rule's pass rates beside today's line.
- The app: `ExploreRunner` reads the `normal` lines; the Explored page and `ExploreCharts`.

## Measuring

1. `corrupt-smoke --placebo` with a new seed on the three real-play scenes (Pocky's temple,
   Zelda's field and Link's house) and the five without play: the new rule against today's
   line, held-out placebos against corruptions. Success: placebos near 0-5%, corruptions well
   above.
2. `snes9x-explore` on those scenes' moments: how many finds, how long normal play takes,
   candidates play back exactly.
3. Fresh blind raters on strips of the new top 12 against today's top 12 and placebos (12
   frames each, the #81 question).
4. Real kept moments (F8) need a person to record them; until then this stays measured on
   pseudo-player scenes.

## How it went

**1. Placebos and corruptions, a new seed** (`corrupt-smoke --placebo 300 2`, variants seed
82082, the same eight scenes as #81):

| | held-out placebos | corruptions |
|---|---|---|
| real play (Pocky's temple, Zelda's field, Link's house): today's line (0.3+) | 30% (27 of 90) | 11.5% (98 of 851) |
| real play: **beyond normal play** | **1.1% (1 of 90)** | **7.2% (61 of 851)** |
| all eight scenes: today's line | 11% (27 of 240) | 7.7% (177 of 2,299) |
| all eight scenes: beyond normal play | 0.4% (1 of 240) | 6.7% (155 of 2,299) |

Of the corruptions kept, about a quarter by the picture and three quarters by code only. By
family: code 41%, kill 14%, ram 1-12% depending on the game, swap 1-4%, data and nudge ~0.

**2. The eyes.** Four fresh raters, one per game, 156 strips of 12 frames, the #81 question.
Per scene: 6 placebos, today's top 6 by score, the new top 6 by find (a run in both counted
for both), 6 random survivors.

| group | all scenes | broken | rated 4+ | real-play scenes |
|---|---|---|---|---|
| placebos | 1.06 | 2% | 2% | 1.00 |
| random survivors | 1.46 | 19% | 10% | 1.56 |
| today's top 6 | 2.46 | 54% | 29% | 2.11 |
| **the new top 6** | **2.70** | **66%** | **34%** | **2.28** |
| only in today's top 6 | 1.94 | 38% | 6% | 1.67 |
| only in the new top 6 | 2.67 | 75% | 17% | 2.17 |
| kept by the new rule | 2.55 | 60% | 29% | 2.04 |
| not kept | 1.30 | 12% | 6% | 1.29 |

The new top beats today's, mostly by what it swaps out: the runs only today's score picked
rate 1.94 (normal play winning on the picture), the ones only the new rule picked 2.67. The
new rule's best: Link dying fast into a garbled game over (ram, kept by code), FE4's dialogue
frames swapped for another pattern (code). The one placebo rated broken is the paused Goof
beach exactly as the control: a rater mix-up, as in #81.

**3. The explorer itself** (`snes9x-explore --variants 600` on the three real-play scenes as
kept moments):
- Normal play takes about **25 seconds** (37 runs) before the first variant; a variant still
  takes 0.6 s.
- 24 of 24 candidates' moments play back exactly, in all three.
- Zelda's best in Link's house: a kill that sends Link through rooms that don't connect (his
  house, a hall of beds, the tavern), the sort of behaviour #81 wanted found.
- **The bars are noisy per scene.** The same Link's house, two sets of 20 placebos: in the
  placebo run one of them walked out of the house and set the bars at score 2.4, 60 new
  functions and 3,201 bytes of new code; in the explorer's run none did, and the bars were
  1.0 and zeros. With zero bars any function a corruption calls that 17 natural runs didn't
  counts, and 21-29% of the explorer's variants were finds (5-10% in the placebo run). The
  24 kept were all past normal play's picture best too, so what's kept is fine; the count of
  finds isn't comparable between scenes. A rare path (1 in 20 or so) either happens among
  the placebos or doesn't.

**What it says.** Measured against normal play, the explorer stops keeping normal play (30%
of placebos down to 1%) and its top rates higher. The bar's noise is the next thing to look
at, together with people's ratings from the Explored page and real kept moments.
