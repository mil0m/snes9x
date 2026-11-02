# Paint with a draft, a history and a crash watch (plan, roadmap #66)

Status: done, 2 November 2026.

## What's wrong now

Mikael and Erik painted the ROM in the Memory page and the game crashed at once, which
nobody minds. What they minded: **the rollback never came**, and there was no way to finish
a drawing before it went into the game.

Why the guard stayed quiet:

1. **The crash guard only watches while a recipe is corrupting** (`CrashGuard::between_frames`
   returns when the engine is off). Painting without pressing Corrupt has no guard at all.
2. **Paint is rollback-proof on purpose.** Each brush dab goes straight into the ROM with
   `RomKeeper::write_at_start` and into the recipe as a patch, so a rollback leaves it. With
   corruption on, a crash in the paint would roll back, crash again, and end in *Kept
   crashing: corruption turned off* with the paint still there.
3. **Every dab is live.** There's no moment where the drawing is "done", so nothing knows
   which bytes belong together, and there's no undo.

Mikael's asks, in his words: *a "save" or "activate" so one can make all changes in the
drawing before they activate. THEN if we crash, we know exactly what was implemented at
that time and can be rolled back*; *a better history so one can ctrl z every stroke or
change*; and *load the ROM without the game running* (decided: a paused game is enough).

## The design

### Strokes, and a history of them

- A **stroke** is one drag of the brush, from press to release (an imported picture is one
  stroke too). The history lists strokes newest first: *Stroke 4 · 24 bytes at $C0:1234 →
  EA*.
- **Cmd+Z / Ctrl+Z** undoes the last entry, **Shift+Cmd+Z / Ctrl+Y** redoes it, while the
  Memory page has focus. Clicking a history row goes back to just after it (later entries
  stay redoable until something new is painted, as in any drawing app).
- The history lives for the session and the game: switching pages keeps it, loading another
  game or recipe empties it. Only what's applied is saved in the recipe, as today.

### The ROM is drafted; Apply Paint puts it in

- **ROM strokes go into a draft.** Nothing is written to the game. The pixel view shows the
  draft's values with a mark (an outline in the highlight colour), so drafted and applied
  bytes are told apart. The hex view shows what the game has.
- **Apply Paint** writes the whole draft at once: one history entry, *Applied · 4 strokes,
  54 bytes*. **Throw Away Draft** empties it (undoable).
- **Undo after Apply** takes the batch out of the game (each byte back to what it was, and
  its patch back to what it was, or gone) and returns its strokes to the draft.
- **RAM and the other memories stay live** (decided: draft for the ROM only). Their strokes
  are written as you drag, as today, and undo writes the old values back.
- Paint on guarded bytes (#58) is still skipped, with the same message, at Apply.

### The crash watch after Apply

- Just before Apply writes, the app saves a **restore point** (a save state; the ROM bytes it
  changes are kept by the batch itself, since states don't hold the ROM).
- For the next **10 seconds of play** it watches for a crash with the crash guard's detector
  (CPU stopped, stuck, wild jumps), **whether or not a recipe is corrupting**, unless *Catch
  crashes* is off on the Safety page.
- **On a crash:** load the restore point, take the batch out (as Undo does), put its strokes
  back in the draft with their history, and say *Your paint crashed the game (…): it's back
  in the draft*. Then Cmd+Z a stroke or two and Apply again: that finds the bad stroke.
  (An automatic search, half the strokes at a time, is a later idea.)
- **After 10 quiet seconds** the batch is kept, like paint today (rollback-proof, in the
  recipe). A crash after that is the crash guard's, as before.
- With corruption on, the paint watch looks first; when it rolls back, the crash guard starts
  over (`guard.reset()`), so it doesn't roll back to before the restore point. ROM changes
  the rules made in those seconds stay; if they crash, that's the guard's job.
- Applying while paused: the restore point is taken at once (the emulation thread is idle)
  and the 10 seconds count from when the game runs. While running, Apply happens between two
  frames, where the guard takes its states.

### Start games paused

- **Corruption > Start Games Paused** (a checkbox, saved): a game that's opened doesn't run
  its first frame until you press **Emulation > Run** (or the Pause key). Paint and Apply
  before it starts; the watch counts from the first frame.
- The state bar says *Paused · Emulation > Run starts it*. The Memory page shows the bytes
  right away: watching a memory reads it once at the start, not only between frames, and so
  does every write (Apply, Undo).

### The remote page

The remote page paints too (#53). Its ROM paint is a stroke and an Apply in one go, so it
gets the crash watch; its RAM paint stays live.

## How it's built

- `corrupt/paint_history.{h,cpp}`: `PaintHistory` over a small `PaintStore` interface (read a
  byte, write one, read and set a patch), so tests use a fake. Entries: a draft stroke, a live
  stroke (with old bytes and old patches), an applied batch (its strokes, old bytes, old
  patches). `add_stroke`, `end_stroke`, `undo`, `redo`, `go_to`, `draft()` (offset → value,
  later strokes win), `apply()` → the batch, `unapply()`, `discard_draft()`, `rows()` for the
  list.
- `corrupt/paint_watch.{h,cpp}`: `PaintWatch` arms with the restore point, then `observe(frame,
  signals)` per frame for 600 frames → nothing, kept, or crashed (with the verdict).
- Session: the history and the watch; `paintStroke/endStroke/undoPaint/redoPaint/goToPaint/
  applyPaint/discardDraft/paintRows`; the draft overlaid on ROM snapshots (`snap.draft`);
  the watch in `betweenFrames` before the guard; a frame counter that runs with corruption
  off; `watch.observe` on watch() and after writes; an `is_paused` hook from the app.
- Viewer: Apply Paint and Throw Away Draft beside the paint value (ROM only), Undo/Redo
  buttons and the shortcuts, a History list under the view while painting, draft marks in
  `PixelView`, `onStrokeEnd` on mouse release.
- App: `corruption_start_paused` and its menu item; after `startGame`, a manual pause.

## Tests

- Unit (`paint_history_test`): strokes merge into the draft, undo/redo/go_to, a new stroke
  drops redo, apply writes bytes and patches and records the old ones, unapply restores bytes
  and patches (a byte painted twice, a byte that had a recipe patch), live strokes write and
  undo, discard is undoable, guarded bytes skipped.
- Unit (`paint_watch_test`): a crash inside the window → crashed; quiet 600 frames → kept;
  disarmed when the guard's *Catch crashes* is off; counting starts at the first frame after
  a paused Apply.
- Real core (`guard_integration_test`): the test ROM, paused before its first frame; paint
  `STP` over code it runs, Apply, run: the watch loads the restore point, the ROM byte is
  clean again, the draft holds the stroke, and the game runs on. With corruption off.
- Widgets: the viewer's Apply/Throw Away only for the ROM, the history rows, Cmd+Z and
  Shift+Cmd+Z, draft marks; the state bar's paused text.
- No smoke numbers: nothing about corrupting by rules changes. The tour paints a stroke,
  shows the draft and the history, and applies.

## How it went

Built the same day, test first: 13 unit cases (history and watch), two real-core cases, four
widget cases, and a tour step that paints a real game.

- **On Pocky & Rocky, with corruption off:** a stroke of STP over the ROM's first 1 KB,
  applied, stopped the CPU at $00:81F7 within a frame; the watch loaded the restore point,
  the ROM was clean again (0 bytes changed) and all 1,024 bytes were back in the draft. The
  crash guard never saw it, as designed.
- **The store had to batch.** The recipe's patch list is rebuilt on every change, so a patch
  per byte would have been quadratic on an imported picture; `PaintStore::set_patches` takes
  a stroke's worth at once.
- **Found in the pictures:** after a crash the Memory page still said *watching ten seconds*
  (the message was set on Apply and never replaced). The session now keeps a *paint news*
  line (applied, held up, crashed) that the page shows, the crash in the warning colour,
  until the next stroke.
- **Found by the build:** `signals` is a Qt keyword (a parameter named so broke every Qt file
  that includes the watch).
- **Not done:** the automatic search for the bad stroke (apply half, then half again); paint
  of a running RAM value can't be "applied" (it's live by choice); a crash caught first by
  the instant revert (#51, only while corrupting with ROM rules) fixes the byte without the
  history knowing, so that batch shows as kept.
