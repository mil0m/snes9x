---
name: qt-widgets-ui
description: Use when building or changing any Qt Widgets window, panel, dialog or control in this repo's corruption UI (qt/src: CorruptionDock, RuleEditor, MemoryViewer, SurvivorsPanel, GuideWindow, or a new one) — layout, spacing, colours, icons, light/dark, empty states, screenshots. Triggers include "new dialog", "add a panel", "make it look right", "dark mode", "the UI looks off", "visual pass", "screenshot the windows". Gives the house rules (KDE HIG + Apple HIG + Qt's own advice), the UiKit helpers that implement them, the Mac-style pitfalls already hit, and how to verify with the UI tour.
---

# Qt Widgets UI, the house way

The corruption UI follows one look, decided in the visual pass
(`docs/corruption/2026-10-31-ui-pass-plan.md`): **the native style** (`macos` on the Mac,
Fusion with a palette elsewhere), **no app-wide style sheet**, **colours only from the
palette**, **one icon set** (Tabler outline, MIT) drawn in the palette's colours, **light and
dark both first-class**. Words and flow come first (the UX pass); this is how it looks.

## Use UiKit, don't hand-roll (`qt/src/UiKit.hpp`)

| Need | Use |
|---|---|
| Help text, explanations | `ui::setTone(label, ui::Tone::Secondary)` — readable (≥4.5:1), never `setEnabled(false)` or `PlaceholderText` grey |
| Error / warning / ok text | `ui::setTone(label, Tone::Error/Warning/Ok)` — follows light/dark; never `setStyleSheet("color:#d33")` |
| A message line | `ui::StatusLine` → `setMessage(text, tone)`: icon + colour + words together |
| An icon | `ui::icon("name")` (follows palette, disabled, dark); a fixed colour: `ui::pixmap(name, colour, size, dpr)` |
| A section | `ui::heading(title, parent)` over its controls; space between sections `ui::sectionGap(w)` |
| A form | `ui::form(parent)`: left labels, fields that grow, no wrapping; `ui::alignForms(root)` gives every form under root one label column |
| A checkbox or combo box in a vertical layout | `layout->addLayout(ui::row(w))`, never `addWidget(w)` (see the Mac pitfalls) |
| A long path | `ui::ElidedLabel` + `ui::shortPath()` (`~`, middle elision, full path in tooltip) |
| An address | `corrupt::hex_address(offset, target)` — same digits everywhere (wram/sram 5, rom 6, else 4) |
| Accessibility check | `ui::audit(widget)` → list of unnamed icon buttons and unlabelled inputs; assert empty in the widget's test |

New icon: download the Tabler outline SVG at the pinned version into
`qt/src/resources/corruption-icons/`, add it to `corruption_icons.qrc` and to `kIcons` in
UiKit.cpp (the test renders every name). The SVG keeps `stroke="currentColor"`.

## The checklist

1. **Spacing from the style.** Layout defaults inside a group; `ui::sectionGap` between
   sections; 8 px dock/dialog margins; 0 px around content views (hex, pixel, tile, the
   guide's text browser: `setFrameShape(NoFrame)`, layout margins 0).
2. **One level of sections**, headings + space. No `QGroupBox` (the tests assert none in the
   editor). Never boxes inside boxes.
3. **Text hierarchy**: body text; Secondary tone for help; bold only for headings and the
   game title.
4. **Status = icon + colour + words.** Errors say what to do next.
5. **Tables fit the width**: few columns, one `Stretch` column with word wrap +
   `resizeRowsToContents` (re-run when that column resizes), numbers right-aligned,
   `ScrollBarAlwaysOff` horizontally. Hide an empty table and show a Secondary hint instead.
6. **Buttons**: primary actions icon + text; row-contextual actions (edit/duplicate/remove)
   as icon-only `QPushButton`s with tooltip **and** accessible name. A menu button is a
   plain push button whose `clicked` pops the menu, with a `chevron-down` icon after the
   word (`setLayoutDirection(Qt::RightToLeft)`). A toggle that reads as "pressed = disabled"
   is wrong: change the text and icon instead (Corrupt ⇄ Stop) and use `setDefault(true)`
   for the obvious next step (Fusion shows it; the Mac draws it only in dialogs).
7. **Monospace** (`QFontDatabase::systemFont(FixedFont)`) for addresses, bytes, hex fields.
8. **Accessibility**: label buddies (`QFormLayout::addRow(QString, widget)` does it; a field
   inside a row layout needs `setAccessibleName`), names on icon buttons and sliders, a sane
   tab order, `ui::audit` empty.
9. **Empty states**: say what to do to fill it, in Secondary tone, and hide the controls
   that can't act yet. Add a zero-stretch spacer so hidden widgets don't spread the rest.
10. **Pictures** (pixel/tile views): the area around the picture is `QPalette::Window`, not
    `Base` (white half in light).

## Mac-style pitfalls (all hit in this repo)

- **`setMinimumHeight`/`setFixedWidth` on a `QPushButton`** turns it into a square bevel
  button. Leave push buttons at their natural size.
- **`QToolButton` is a square bevel box on the Mac, even with `setAutoRaise(true)`**, and
  `QPushButton::setMenu` draws a square-cornered pull-down. Next to rounded push buttons both
  look out of place: use icon-only push buttons, and pop menus from `clicked`.
- **A bare `QCheckBox` or `QComboBox` added straight to a vertical layout shifts that whole
  layout ~3 pt right** (its Mac layout margins), so headings stop lining up; **as a bare
  `QFormLayout` row a checkbox loses height** and its descenders are cut. Always
  `ui::row(w)`.
- **Separate forms per section get separate label columns**: `ui::alignForms(root)`.
- **Gutters**: leave room after a text label for tick marks (the pixel and tile views use
  label width + 20), or the tick lands on the last digit.
- **`QScrollArea` hides its content's minimum width**: set
  `scroll->setMinimumWidth(content->minimumSizeHint().width() + scrollbar width)` or the dock
  narrows until buttons are cut off on the right.
- **Size a dialog in `showEvent` too** (and once more one event-loop turn later): the Mac's
  controls are taller after polish. Cap height by the screen less the dialog's own chrome,
  not a fraction of the screen.
- **Overlay scrollbars hide that something scrolls**: a clipped last row looks like a bug.
- Default `QFormLayout` on the Mac keeps fields at their size hint and centres the form:
  always `ui::form()`.

## Verify (every change, both looks)

- Widget tests: `build/tests/corrupt-qt-tests`, PNGs with `CORRUPT_SHOTS_DIR=build/tests/shots`.
- The whole app: `tests/qt/ui_tour.sh <out> [rom] [--dark] [--scale 2]` (offscreen = Fusion),
  and `tests/qt/ui_tour_matrix.sh <out> --mac` for all six looks (Fusion light/dark × 1x/2x,
  real Mac light/dark). Read the PNGs; `audit.txt` in each look should be empty.
- **Offscreen can't draw the Mac style** (it paints nothing without NSViews). The real look
  needs `TOUR_PLATFORM=cocoa SNES9X_COLOR_SCHEME=light|dark`: windows show on screen ~30 s,
  muted, no screen-recording permission needed (`grab()`).
- A scratch config's general settings go under `[%General]`: QSettings ignores a plain
  `[General]` group, and the game then pauses whenever another window takes focus.
- After a pass, a fresh-eyes review of the screenshots by a subagent that gets the PNGs and
  the checklist, not your conclusions.
