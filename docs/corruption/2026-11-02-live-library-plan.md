# Playing together live: a shared library that syncs while you play (plan, roadmap #62)

**Status (2026-11-02):** done. Asked for by Mikael: when playing together over Discord in
an evening, nobody should have to push and pull the library through GitHub; the files
should travel by themselves, through his server, and go into git when the night is over.

**Decided with Mikael:** Syncthing moves the files; git gets them **when we say so** (a
command), not on a schedule.

## Why this is easy here

Everything the app learns is written to one file per person: `codemap.<you>.bin`,
`crashmap.<you>.json`, `jumps.<you>.json`, `calls.<you>.json`, `names.<you>.json`,
`journal.<you>.jsonl`. Two computers never write the same file, so syncing them live can't
conflict. Only recipes (`recipes/*.json`) and named areas (`regions.json`) are shared files;
two people editing the same recipe at the same moment is rare, and Syncthing keeps both
versions when it happens.

## The pieces

- **Syncthing** (free, open source, no account, encrypted, connects out through relays when
  it must, so no open ports) on the server, Mikael's computer and Erik's, sharing one folder:
  the library. Each app's *Library Folder* points at its copy. The server is always on, so
  the other two sync through it even when they're not on at the same time.
- **The server's copy is the git clone** of the shared library repo. A `.stignore` keeps
  `.git`, the ROMs (`roms/`, `*.sfc`, `*.smc`), battery saves (`*.srm`) and
  `save-backups/` out of the sync: each person's saves and ROMs stay theirs.
- **`push`**: one command, run when a session is over, that commits everything new in the
  server's copy ("Session 2026-11-02: …") and pushes it. It lives in the library repo
  (`tools/push.sh`), so it's there for whoever runs it; from a computer it's
  `ssh <server> <library>/tools/push.sh`.
- Server specifics (where the folder lives, the device ids) go in the private homelab repo,
  never in this one.

## What the app has to do (the code part)

Snes9x stays self-contained: without Syncthing it's a folder like today. Three changes:

1. **Pick up the others' files while playing.** Today the others' code maps, crash maps,
   jump logs, call logs and names are read when a game loads. Every 30 seconds (between
   frames, a directory listing: cheap) the session checks the other people's files in the
   game's folder (name, size, time); when one changed, it saves its own files and loads
   everyone's again (the same path as switching library folders). Erik's crash map steers
   your rules, his functions and names show in your window, mid-session.
2. **Ignore what syncing leaves behind.** A file Syncthing couldn't merge becomes
   `name.sync-conflict-<date>-<id>.json`; its temporary files are `.syncthing.*.tmp`. The
   library skips both everywhere (a conflict copy of `calls.erik.json` must not count as a
   third person's calls, and a recipe's conflict copy must not show as a recipe). The panel
   says when a conflict copy exists, so someone can look.
3. **The recipe list follows.** When the game's `recipes/` folder changes, the panel's list
   refreshes by itself (keeping the selection), so a recipe Erik saves shows up for you.

## Tests

- **Unit:** the library skips `.sync-conflict-` and `.syncthing.` files for every kind of
  per-person file and for recipes, and reports conflict copies; a "what changed" stamp of
  the others' files (name, size, time) changes when one is written, not when your own is.
- **Qt / app:** the session reloads when another person's file changes (a test library
  written to while a fake game runs is too heavy; covered by the stamp unit tests plus a
  manual QA section: two app instances on one machine, each with its own user name, sharing
  one folder).
- **No smoke numbers**: nothing here changes how a game is corrupted.

## How it went

Built as planned, the same day.

- **The app:** `is_sync_leftover` is applied in every library scan and in `list_recipes`;
  `conflict_copies` feeds the panel's mention; `others_stamp` / `recipes_stamp` (name, size,
  time) are checked every 30 s between frames, and a change reloads everyone's files and
  refreshes the recipe list. `SNES9X_PLAYER` names the player, so two instances on one
  machine can share a folder (QA 37). Unit tests: `tests/corrupt/live_library_test.cpp`.
- **The library repo:** `tools/stignore` and `tools/push.sh` (commits only the game folders,
  summary per game; `tests/library/push_test.sh` runs it on a scratch repo in
  `./build-mac.sh test`); `.gitignore` keeps conflict copies and Syncthing's folders out.
- **The setup:** Syncthing on the server (its copy is the git clone, with a deploy key that
  may push to the library repo only) and on Mikael's Mac. Verified both ways: the README and
  `tools/` reached the Mac, Goof Troop's `codemap.mikael.bin` and `jumps.mikael.json` reached
  the server, ROMs and save backups stayed local, nothing went to GitHub until a push.
  Server details are in the private homelab repo.
- **What bit:** the server's ssh used another key first for github.com, so the push alias has
  to come before `Host *`; on the Mac, adding a folder under `~/Documents` waits on macOS's
  folder-access prompt (it looks like a hang until someone clicks Allow).
- **Still to do:** add Erik's device when he sends its id.
