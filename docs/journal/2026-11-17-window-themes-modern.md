# Window themes, second round: three more modern ones

## What was asked

After seeing the one-bit and workbench pictures
([first round](2026-11-17-window-themes.md)), the owner, 2026-11-17:

> come up with some original design ideas, a bit more modern, but looking
> good on low res. Try an XP theme, and a couple original themes

He did not pick between B and C; this round adds to the choice.

## What was found

A more modern look is flat, and two things the first round left alone had
to move for it:

- **The text and field colours.** Widgets name `gfx::kBlack` for text and
  `gfx::kWhite` for the inside of fields directly. A dark theme needs both
  to change, so the prototype now has an ink and a paper colour, and the
  window manager, the widgets, the text editor, `os/ui.cpp`, the taskbar,
  the File manager and the dialogs read those. What a program paints for
  itself still uses the fixed colours.
- **The taskbar.** It draws its buttons with the same bevel calls as a
  dialog button, so it could not have its own colours. The flat themes give
  it a bar colour, a Start colour, and colours for a window's button on and
  off.
- A button's face was the window's face colour. The flat themes need a
  button face of its own (white on a tinted window, grey on a dark one).

The board's palette decides the colours: red and green have eight levels
and blue four, so there are only four exact neutral greys, but many exact
tinted ones. Every flat colour below is an exact board colour, so only the
one fade (the title bar of D) is dithered.

## The pictures

In `docs/look/themes/`, drawn by the same scratch patch, now 441 lines over
15 files (`tools/mockup/themes-prototype.patch`). Themes A, B and C came out
byte for byte as in the first round.

- **D, sky**, after the blue and green desktops of the early 2000s. A blue
  title bar that fades from a lighter blue at the top, white bold title with
  a dark shadow, title buttons as small tiles with a white edge and the
  close one in red, a thick blue frame, cream faces (board colour 6, 6, 2),
  white buttons with a navy outline, pale blue scroll thumbs, a blue taskbar
  with a green Start button. Our own drawing: no Microsoft bitmap, icon or
  font.
- **E, glacier**, original. Flat and light: pale periwinkle faces (6, 6, 3),
  the frame and title bar of the active window in one solid indigo
  (2, 2, 2) and of the others in a grey-blue, title buttons as bare white
  shapes with no box, white buttons and fields with a one-pixel slate
  outline, solid scroll tracks with a slate thumb, a dark navy taskbar where
  the active window's button is a light tab.
- **F, midnight**, original. Dark: navy faces (1, 1, 1), black fields and
  title bars, white text, and amber (7, 5, 0) for what is active: the
  one-pixel outline of the active window, its title, the selection, the
  Start button, the line under the active taskbar button.

Rough edges of the prototype, not of the designs: the cream of D is
greener than the original era's (the nearest exact colour); the text caret
is black in F and cannot be seen; program icons with black outlines lose
them on F's black title bar; rounded window corners are not possible while
a window is a rectangle to the window manager, so D's are square.

## What it adds to the cost

The first round's table of about ten colours grows to about twenty (ink,
paper, button face, taskbar colours), and the direct uses of `kBlack` and
`kWhite` in the widget code have to be told apart: text and fields follow
the theme, drawings keep their own. That sorting is the larger part of the
work for a dark theme; D and E need less of it. Still small to medium.

## What was chosen

Nothing yet. Six looks are on the table (A to F); the owner picks.

## What is left

Backlog item 47.
