# Window themes: sky, glacier and midnight built

## What was asked

After the [second round](2026-11-17-window-themes-modern.md) of pictures, the
owner, 2026-11-17:

> do D, E, and F

So: the three modern looks as themes to choose in Display, beside the
current one. The two rough spots of the pictures were to be fixed with it:
the text caret that could not be seen in the dark theme, and the program
icons whose black outlines were lost on its black title bar.

## What was found

As the [first round](2026-11-17-window-themes.md) said, the look lived in a
few places: five bevel functions in `gfx::Canvas`, `Wm::paint_frame`, the
taskbar, and three constants (`kGrey`, `kBlack`, `kWhite`) named directly by
everything that draws a part of the user interface. The selection colour
was a private constant in five files.

## The options considered

**Where the theme lives.**

1. A table of rows in `core/gfx`, and one pointer to the row in use, read by
   whatever draws. One memory read per colour.
2. The window manager owns the theme and hands colours to each paint.
   Cleaner on paper, but every widget and every program's paint would need
   the theme passed in, through the OS calls too.
3. Themes as files on C:, read at start-up. Nothing asks for that yet, and
   a bad file could leave a desktop that cannot be read.

**What happens to `kGrey`, `kBlack` and `kWhite`.**

1. Keep the names and make them variables, as the prototype did. A
   two-line change, but a name that says constant would change under the
   code, and a drawing that wants real grey would get the theme's face.
2. Keep them as the fixed colours for drawings, and give the user
   interface four questions to ask instead: `gfx::face()`, `ink()`,
   `paper()`, `dim()`. Every use has to be looked at once.

**How the flat themes differ from the classic one.**

1. One paint function per theme for each part. Free, but four copies of
   the title bar and the taskbar to keep right.
2. Two kinds of edge (bevel, flat), three kinds of title bar (fade across,
   fade down, one colour) and three kinds of title button (buttons, tiles,
   bare shapes), each a field of the row; the rest is colours.

**Where the choice is made.** A list in the Display dialog, where the
owner asked for it. A drop-down would have been smaller but needs a menu of
its own; a list of four rows fits beside the preview once the preview is
narrower, so the dialog keeps its size.

## What was chosen, and why

The owner chose the three themes. The rest is the agent's, to confirm:

- The table in `core/gfx/theme.hpp` and `theme.cpp` (option 1): the least
  to pass around, and a program on its own task reads it with no lock,
  since a change is one pointer written by the UI task.
- The four questions (option 2). The constants stay for drawings: icons,
  the games' boards, the boot screens, the screensavers, the Terminal.
- Kinds of edge, title bar and title button as fields (option 2): the
  classic path stays byte for byte what it was, and the three flat themes
  share one path.
- Sizes and places are not in the table. Every theme has the same border,
  title height and button places, so no layout and no hit test changes.
- `[look] theme` in `desktop.ini`, by key (`classic`, `sky`, `glacier`,
  `midnight`), beside `[background]`: Display writes both.

Not done: icon themes (the owner left it open, and icons are drawn art),
themes that move the title buttons, a theme's own desktop colour (the
background stays the owner's choice in the same dialog).

## What was built

- `core/include/core/gfx/theme.hpp`, `core/src/gfx/theme.cpp`: the `Theme`
  row and the four themes. Each flat one starts from the classic row and
  changes what differs. All their flat colours are exact board colours; the
  only dither is the title fade of sky and the classic fades.
- `gfx::Canvas`: `raised`, `sunken`, `button`, `button_frame` and `checker`
  draw one outline and one colour when the theme is flat. A new `dent`
  (a thin edge pressed in) replaces nine places that drew a dark and a
  white line by hand; it is an OS drawing call too (`os::ui::draw::dent`).
  The caret is the theme's text colour, not black: that was the caret that
  could not be seen.
- `Wm::paint_frame` reads the fades from the theme; `Wm::paint_flat_title`
  draws the flat title bars. Midnight's bar is the face colour (navy), not
  black, so the icons' outlines show: the second rough spot.
- The widgets, the text editor, `os/ui.cpp`, the File manager, the message
  boxes and the status bars ask the four questions. The taskbar has a flat
  path with the theme's bar, Start and window-button colours.
- The five copies of the selection colour are `gfx::theme().select`.
- `os::set_theme(index)` and `Wm::set_theme`: the theme changes and the
  whole screen is painted again.
- Display: a list of the themes beside a narrower preview, which shows a
  small window and taskbar in the picked theme's colours. Apply shows it,
  Escape takes it back, OK keeps it.

## How it was tested

On the PC only. The firmware compiles and links with ESP-IDF v5.5 in the
cloud container; nothing here has run on the board.

- The 100 reference pictures that do not show the Display dialog are byte
  for byte what they were: the classic look did not move. The seven that
  show the dialog changed (its new list) and were looked at.
- Nine new reference pictures: for each new theme, the Display dialog after
  Apply, the Start menu, the widget gallery. The scene also checks that
  nothing changes before Apply, that Escape takes it back, that OK writes
  `desktop.ini`, and that the classic theme comes back.
- Every scene was also drawn once in each new theme with a temporary
  switch and looked at (File manager, Editor, Task Manager, Defragmenter,
  the games, the Picture viewer, the Terminal, the dialogs). That found the
  message boxes' text still fixed black, which is fixed.
- Display holds two more controls, so the two tests that count its memory
  blocks expect 15, not 13. All 43 PC tests pass.

## What was learned

- The board's palette suits flat looks: blue has four levels, so there are
  only four neutral greys, but tinted greys are plentiful and exact.
- XP's cream has no exact match; sky's face (6, 6, 2) is greener.
- The prototype patch in `tools/mockup/` no longer applies: it was for the
  tree before this change. It stays as the record of how the pictures of
  the first two rounds were drawn.

## What is left

- A board turn: the look on the real monitor, the time a full repaint
  takes on a theme change, `tools/dbg.py misses`.
- Rounded corners for sky need windows that are not rectangles.
- What a program paints for itself keeps its own colours: the Terminal,
  the games' boards, the Defragmenter's map.

Backlog item 47.
