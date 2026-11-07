# People's ratings, steadier bars, real moments (#83, #84, #85)

#82 left three things open (`2026-11-06-normal-play-plan.md`): the score has only ever been
checked against blind raters looking at **still strips**, which can't see behaviour; the bar
normal play sets is **noisy per scene** (one placebo walking out of Link's house moved it from
zero to 60 new functions); and everything was measured on **pseudo-player scenes**, five of
eight with no play in them. In that order.

## #83: 👍/👎 on the Explored page

The ground truth is a person who pressed Play and watched the run. So the Explored page asks
them, per candidate: **Good Find** or **Not a Find**, with #81's question in the tooltips and
help: *is the game itself broken or acting wrong in an interesting way? Just another path
through normal play is not a find.*

- **Where it goes.** One file per person beside the exploration, `ratings.<user>.json` in its
  folder, like every other per-person file in the library (crash maps, names): Erik's and
  Mikael's ratings sync without ever writing the same file, and the explorer, which rewrites
  `candidates.json` every 30 s while it runs, never touches them. A rating is the candidate's
  **id** (its variant number, which doesn't change when ranks do), good or not, when, and a
  copy of its find and score (a candidate rated while the explorer runs can drop out of the
  top 24 later; the rating keeps what it was rated on).
- **The page.** Two buttons beside Play: *Good Find* (thumb up) and *Not a Find* (thumb down).
  Pressing one rates the selected candidate; the one you chose then reads *Rated Good Find* /
  *Rated Not a Find* with a check (text and icon change, never a pressed look), and pressing it
  again takes the rating back. A **Rated** column shows your rating as a thumb; its tooltip
  and the details line say everyone's (*You: good find. erbl: not a find.*).
- **What it's for: a report.** `explore-ratings <library game folder>` reads every
  exploration and every person's ratings and prints, for the rated candidates: how many good
  and not, per person; where two people rated the same candidate, how often they agree (the
  ceiling any score can reach); and for each measure the explorer has (find, score, the
  picture share, each code signal, apart, new colours, kept by the picture or the code, the
  family), how well it separates good from not: the chance a good find ranks above a
  not-find (AUC: 0.5 is a coin, 1 perfect). The tuning of #82's rule waits for that.
- **Not in it.** Ratings only see kept candidates, so they tune the ranking, not the keep
  line. Keeping a few near misses to rate is a later step if the ranking looks right.

## #84: steadier bars

Normal play's best is the most 20 placebos reached. A rare path (1 in 20 or so) either
happens among them or doesn't, so the bar, and with it how many variants count as finds,
swings per scene. Two ways to steady it: **more placebos** (the chance a held-out placebo
passes is 1 in n+1, and a rare path is likelier to turn up), or a **robust bar** (a percentile
instead of the most), which a rare normal path would then pass.

- **Measure first** (`corrupt-smoke --placebo` with `CORRUPT_PLACEBO_BAR=120`, the three
  real-play scenes): from 120 bar-setting placebos, draw 200 sets of 20 and see how far the
  share of variants kept swings between draws, against sets of 60 and all 120; and how many
  held-out placebos each passes.
- **Build: placebos keep coming.** If more placebos steady it, the explorer keeps playing
  them **between the variants**, one every 20 tries (5% of its time), after the 20 it plays
  first. Each is judged against the bar so far before it joins it, so the page can say how
  often normal play would have passed (*Of 180 runs without corruption played along the way,
  1 would have been kept*): each scene's own measure of how much of what's kept is just normal
  play. When the bar rises, candidates are judged again and those no longer past it leave.
  `candidates.json` keeps the later runs (`normal.later`, `normal.later_kept`); the charts
  judge every try against the final bar.

## #85: real moments

The explorer was built for moments people keep with F8 (real play, buttons that matter), and
has never been tried on one: the library has none. This needs Mikael or Erik to play.

- **What to record** (in GUIDE.md): 20 s or more of real play in an action scene, Pocky,
  Zelda, Goof Troop out of the pause menu; walking, fighting, jumping. Not a menu, a dialogue
  or a pause, where buttons change nothing.
- **A tool to explore them all** overnight: `tools/explore_moments.sh <library> <rom>
  [minutes]` runs `snes9x-explore` on every moment of the ROM's game in the library, at most
  three at once and niced, into each game's `explored/`. Then rate them on the Explored page
  (#83) and run `explore-ratings`.
- **Measured** once there are moments: placebos kept per scene (#84's number), finds that play
  back exactly, and the ratings' report.

## How it went

**#83.** Built as planned: the two buttons and the Rated column on the Explored page, one
`ratings.<user>.json` per person beside the exploration, `explore-ratings` for the report.
There are no ratings yet, so there's nothing to tune against: tried on a copy of #82's
Link's house run with made-up ratings, the report reads them, the agreement and the AUCs come
out, and a rating whose candidate left the top counts for find and score only. The numbers
come once people have pressed Play and rated (#85).
