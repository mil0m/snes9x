# Visual UI pass: plan

**Status (2026-10-31):** done (a6a9fabc..). Choices confirmed by Mikael, all three as proposed. Real-Mac checks in Light, Dark and Increase Contrast, the Accessibility Inspector and a keyboard-only run still need Mikael. Follows the UX pass
([its plan](./2026-10-31-ux-pass-plan.md)), which fixed words and flow; this one is about
how it looks: spacing, hierarchy, density, colour, icons, light and dark.

## What the research found

- **There's no published skill for Qt Widgets design.** Qt's own agent skills
  (github.com/TheQtCompanyRnD/agent-skills) have `qt-ui-design`, but it targets QML, web and
  embedded; only its accessibility checklist (contrast 4.5:1 text, 3:1 UI, keyboard focus)
  carries over. Web frontend-design skills transfer as principles only (hierarchy,
  restraint), not their habits (custom fonts, cards, CSS everywhere).
- **The KDE HIG is the one written for Qt Widgets** (develop.kde.org/hig): 4 px between
  related controls, 6 px between toolbar items, 8 px between groups and to the window edge,
  0 px around frameless content views; group by tightening inside and widening between;
  no nested group boxes ("frame overload"); tables only for 3+ compared columns; slider
  plus a trailing spin box for precise values; help inline, never only in tooltips; colour
  by role (blue highlight, orange warning, red danger) and never colour alone; every error
  says what to do next; "…" on actions that open a window.
- **Apple's macOS HIG for the numbers on the Mac**: 13 pt body, 11 pt secondary, 10 pt
  minimum; important things top and leading; semantic colours only, so dark mode and
  Increase Contrast work; the default button is never the destructive one.
- **Qt craft**: keep the native `macos` style on the Mac and Fusion with a palette
  elsewhere; no application-wide style sheets (Qt's own docs call them for prototyping; they
  cost speed, fight the native style and override fonts); spacing from the style
  (`QStyle::pixelMetric`, `layoutSpacing`) rather than hard-coded pixels; colours from the
  palette (`PlaceholderText` for hints, `Accent` for emphasis, a small helper for
  error/warning/ok that works in light and dark); dark mode through `QStyleHints::colorScheme`
  (6.5+) and repainting on `ApplicationPaletteChange`; `SmallestReadableFont` for secondary
  text, `FixedFont` for addresses.
- **Icons**: the PCSX2 pattern (a Qt Widgets emulator): one permissive SVG set shipped as a
  black and a white icon theme in a resource file, switched by colour scheme
  (`QIcon::setThemeName`). Candidates: Tabler (MIT), Lucide (ISC), Phosphor (MIT), Remix
  (Apache-2.0).
- **Reference apps**: PCSX2 and DuckStation (Qt Widgets emulators, light/dark icon themes),
  Okteta (KDE's hex editor: thin toolbars, monospace columns, docks), Krita's dockers (dense
  but quiet: spacing and separators instead of frames), Qt Creator (flat panels, 4/8
  spacing).

## Choices (confirmed by Mikael, 2026-10-31)

1. Native style on macOS, Fusion with a palette elsewhere; no app-wide style sheet.
2. One icon set, about 20 icons, as light and dark themes: **Tabler** (MIT, the widest
   choice of outline icons) unless Mikael prefers another.
3. Light and dark both first-class: no hard-coded colours (today: the red error text in the
   panel and the rule editor, the overlay colours stay as they are, they're data).

## The checklist

1. Spacing from the style; 4-6 px inside a group, 8-12 px between groups; 8 px dock and
   dialog margins; 0 px around the hex, pixel and tile views.
2. Fewer frames: one level of sections, set apart by bold headings and space, not boxes
   inside boxes.
3. Text hierarchy: body text, smaller secondary text in the placeholder colour (readable,
   not greyed out like something disabled), bold only for section headings.
4. Status with icon, colour and words together; errors say what to do next.
5. Tables: fit the panel without a horizontal scroll; numbers right-aligned; strength as a
   slider with its value next to it.
6. Toolbars: icons with tooltips and accessible names, related controls grouped.
7. Monospace for addresses and hex, padded the same everywhere.
8. Accessibility: accessible names on icon buttons and slider cells, label buddies, a sane
   tab order, a keyboard-only run.

## The known problems it fixes (from the second UX review)

The rules table needs a horizontal scroll at the panel's width; "What's where" cuts names in
the middle while it has room; the rule editor first opens too short; the Tiles view leaves
a white half; the survivors tables are narrow; the pressed Corrupt button reads as
disabled; "It Crashed: Go Back Now" weighs as much as Corrupt; the More button's arrow
overlaps its text; the library path is a long raw path; grey hints are hard to read;
address padding differs between lists and the gutter.

## Verifying

- `tests/qt/ui_tour.sh <out> [rom] [--dark] [--scale N]` runs the UI tour
  (`SNES9X_UI_TOUR`) with a scratch config and a ROM copy.
- **Offscreen draws Fusion, not the Mac style**: the `macos` style paints nothing offscreen
  (it needs real NSViews), though its sizes still apply. So the matrix is:
  - `TOUR_PLATFORM=cocoa` with `SNES9X_COLOR_SCHEME=light|dark`: the real native look, at
    Retina 2×. `grab()` needs no screen-recording permission, but the windows do appear on
    screen for about 30 seconds (muted, scratch config).
  - Offscreen Fusion, light and `--dark`, at `QT_SCALE_FACTOR` 1 and 2: what Erik sees on
    Linux or Windows, and the scale check.
- First native run (the Mac in dark mode) found more than the review's list: form labels
  right-aligned with fields that don't grow (the Mac `QFormLayout` defaults), squashed spin
  boxes, the Seed and note fields drawn with a bright square frame, the "Go to" field too
  narrow to show its hint, and the More button drawn as a small square bevel.
- Real macOS screenshots in Light, Dark and Increase Contrast, by Mikael.
- Xcode's Accessibility Inspector over the panel and dialogs; one keyboard-only run.
- A fresh-eyes visual review of the final tour, like the UX pass had.

## Afterwards

Write the checklist above as a small project skill (`.claude/skills/qt-widgets-ui`), so the
next window or panel starts from it instead of from scratch.

## How it went

- **UiKit** (`qt/src/UiKit.*`) carries the rules: tones that keep 4.5:1 in light and dark,
  `ui::icon` (one Tabler set recoloured from the palette at paint time, instead of PCSX2's
  two copies: same result, follows a live theme switch), headings, forms with one label
  column, `ui::row`, `ElidedLabel`, `StatusLine`, and `ui::audit` for accessibility.
- One address padding everywhere (`corrupt::hex_address`: wram and sram 5 digits, rom 6,
  the rest 4).
- Every known problem above is fixed. The first native-Mac tour found more (see
  "Verifying"), and so did the fresh-eyes review of the six-look tour. Fixed from the
  review: the gutter tick on the last digit, the cut-off recipe name (the list now has its
  own row), a ~3 pt shift in sections that start with a bare checkbox or combo box,
  different label columns per section in the editor, a blank strip under the rules table,
  bevelled icon and menu buttons on the Mac, the empty copies box, the Named-areas buttons
  spread apart, the stark guide table lines in dark, an error with no icon, the guide's
  "Corrupt (again)" row.
- Left as they are: Mac text fields are square with a light edge in dark mode (the native
  style draws them that way; no style sheet); Corrupt isn't accent-coloured on the Mac (it
  draws default buttons only in dialogs); the Tiles view at 16 tiles a row still leaves room
  beside the picture (now in the window colour, framed quietly; a "fit to width" choice
  would be a feature, not a fix).
- The lessons are in the project skill `.claude/skills/qt-widgets-ui`.
