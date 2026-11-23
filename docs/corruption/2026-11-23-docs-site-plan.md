# A documentation page with big visual cards (roadmap #87)

Mikael, 23 Nov: a documentation page for the project on his own domain, with large visual
cards for the core corruption features. Only he and Erik can open it.

## What it is

One static page, built from this repo, served from Mikael's home server behind a login.

- **A hero**: what the fork does in one sentence, over a clip of a corrupted game.
- **One large card per core feature**, each with a picture that shows it (a clip of a real
  game, clean beside corrupted, or the app's own window), what it does in two or three
  sentences, and how to use it (the key, the button, the Workshop page). The cards follow
  GUIDE.md's words.
- **A recipe gallery**: the starters, each with a short clip.
- **What's next**: the open roadmap cards worth looking forward to.

## Design choices

1. **The pictures are made, not committed.** Game frames are the games' art, and the repo is
   public. `tools/site/build.sh` makes every picture fresh from the ROMs on this machine into
   `build/site/`; the repo holds the page's text and layout, the list of pictures to take,
   and the recipes used for them. Re-run it after a feature lands and the pictures are
   current. A missing ROM leaves that picture out, not the build.
2. **Clips come from the real core, headless.** `corrupt-smoke --clip <dir> [seeds]` plays the
   game with the pseudo-player into real play (Start and Select only in the lead's first
   half, so a title is passed and the game isn't paused after), saves a state, and plays the
   same buttons from it clean and then once per seed with the recipe. The same buttons make
   clean and corrupted comparable frame by frame. It prints how far each seed is from clean
   and whether it still moves, so a lively seed is picked by number, then looked at.
   Classic ROM recipes boot the corrupted ROM, so for them the lead is played again after
   the power cycle, the same buttons both ways.
3. **Stronger recipes for the clips.** The starters are gentle on purpose, and several hit
   memories games repaint every frame: in six-second clips most show nothing. The clips use
   `docs/site/recipes/` (stronger versions of the same kinds); the cards say which starter
   gives the same effect at play strength.
4. **The app's windows from the UI tour**, run on a copy of the real library so the
   Functions table and the moments have real data.
5. **Plain HTML and CSS, no framework.** One page, the roadmap pages' type so the project's
   pages look like one system, its own colours from the console (its grey and purple; the
   four face buttons mark a card's kind: memory, the game's code and data, staying playable,
   keeping and finding), light and dark, no horizontal scroll at phone width.
   Clips as short muted looping H.264 videos (nearest-neighbour scaled, so pixels stay
   crisp) with a still poster.
6. **Hosting stays out of this repo.** The host name, the server and the deploy live in the
   private homelab repo (a runbook beside the roadmap hub's). The page is behind Cloudflare
   Access (Mikael's and Erik's emails), and the route is added only after the Access
   application exists.

## Not now

- Pages per feature, search, versioned docs. GUIDE.md stays the full manual (the page links
  to it on GitHub).
- A public page: game art would need another look first.

## How it went

Built the same day. The page is live behind Access for Mikael and Erik, with a tile on the
domain's front page; hosting, the server's files and the deploy script are in the homelab
repo.

**The starters show almost nothing in a short clip.** A first batch, 10 starters on four
games, 4 seeds each, 6 s: in 32 of the 40 pairs no seed moved more than 3% of the picture
from clean (three of those games weren't in play yet either, see below). They're gentle on
purpose (a byte every 2 s), and sprite and palette rules hit memories the games repaint every
frame. Hence `docs/site/recipes/`, stronger versions of the same kinds, which the cards
attribute to the starters that do the same at play strength.

**Pseudo-play needs a lead per game.** 1800 frames reach Goof Troop's beach, but Pocky is
still choosing a player, FF6 is in its intro and FE4 in its prologue. Leads of 3000 (FE4's
map), 5000 (Pocky's temple fight, FF6's Magitek march) were picked by looking at the first
frame of each. Start and Select are allowed only in the first 70% of the lead, so a title
is passed without pausing the game after.

**Three things the first clips got wrong:**
- ROM rules need an explicit range (its size depends on the game); the clip recipes use the
  first 512 KB, which every game here has.
- Pointer swaps and functions turned off did nothing: the clip mode learned only the code
  map. It now notes jumps and calls during the lead and gives the engine the jump tables,
  functions and busy code, as the explorer does.
- Clips started before anything showed: code changes took 3-6 s to break the picture, so the
  hero's corrupted half looked clean. Each run now plays twice the clip's length and keeps
  the window that starts a second before the first visible difference.

**Video needs byte ranges.** Python's `http.server` doesn't answer `Range`, which Safari
needs to play a video at all, so the server runs a 60-line `serve.py` that does (the Frost
site does the same).

The whole build takes about five minutes: 12 clips three at a time (each 2-3 runs of
~1,000 frames plus encoding), then the UI tour at 2x. 26 MB of media.
