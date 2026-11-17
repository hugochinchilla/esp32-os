# A second review: Start, the colours, the Terminal

2026-11-17. PC only; the firmware was not compiled for this and nothing here
has run on the board.

## What was asked

The owner, after a day with the desktop on the board and over VNC:

> Let's make a new round of review. The start menu button needs a bit of
> padding to the right, maybe just 1px, for symmetry with the padding on the
> left side with the logo. The display configuration needs an extended file
> chooser which allows to select any color of the 256 possible colors. And
> at least some of the default colors on the palette should be colors that
> do not render with dithering (for smoother VNC connections) Also the
> terminal app has no ability to maximize and lacks a scrollbar

Four things: the Start button, a chooser for all 256 colours, colours in the
list that the board shows without dither, and a Terminal that can be
maximised and has a scroll bar.

## What was found

- **Start.** Counted on the picture: two grey pixels between the button's
  edge and the mascot, none between the "t" of "Start" and the edge on the
  right. The button was 58 wide.
- **Colours.** The board shows 256 colours: eight levels of red, eight of
  green, four of blue. The Display list held 16 colours given in 24 bits,
  and only black and silver were among the 256; the other 14 were drawn
  with the 4x4 dither. A dithered desktop is a fine check pattern over the
  whole screen, the worst case for the remote screen's compression, and it
  shimmers when a viewer scales the picture.
- **Terminal.** The window was fixed (`set_resizable(false)`), because the
  screen behind it was a fixed grid of 80 by 25. It kept 175 lines of
  history, reached only with Page Up and Page Down; nothing showed that
  there was history, or where in it the view was.

## The options

**Start**: one pixel more (the owner's guess) or two (what the left side
has). Two makes the sides equal, which is what was asked for.

**The 256 colours**

- A. A grid of all 256 in a dialog of its own, opened with "Other..." from
  Display. Every cell is a colour the board has, so nothing picked there is
  ever dithered.
- B. Three sliders (red, green, blue) with a sample. Smaller, but slower to
  use and no overview; and blue with four steps beside two of eight is odd
  to explain.
- C. A 24-bit picker (hue and brightness). It would mostly give dithered
  colours, the opposite of what was asked.

**The list of 16**

- A. Move each colour to the nearest of the 256.
- B. Choose 15 again from the 256, by eye, keeping the names where the
  colour is still that colour; keep "Chinchilla blue" as it is and say in
  its name that it is dithered.
- C. Make the default solid too.

**The Terminal's size**

- A. The grid grows and shrinks with the window: more columns and lines
  when it is bigger.
- B. The grid stays 80 by 25 and a bigger window shows it in the middle of
  a black field. Maximise would give nothing.
- C. A larger font when maximised. There is one font (6x12); doubled it
  does not fit 80 columns in 640.

## What was chosen, and why

- **Start: two pixels** (`kStartW` 58 to 60). The owner said "maybe just
  1px"; with one the right would still be a pixel tighter than the left.
  Told to him with the picture.
- **Colours: A**, a "Colour" dialog with the 256. First built as four
  squares of 8 by 8, one for each level of blue, red growing to the right
  and green downwards. The owner on seeing it: "whats the logic behind the
  color chooser alignment of colors? I've never seen one like this". Shown
  a grid sorted by hue, he asked for the neutral greys on a strip; the
  board has only four (black, 73, 182, white: blue has four levels and a
  grey needs the three channels equal), and the hue grid stayed patchy.
  Then: "the original palette shows 4 nice gradients to the eye [...] If we
  could get the same perfect gradient but once and for all colors at once I
  would be delighted". A sheet of 32 by 8 came next, the four squares in a row
  with red running back in every other one; he answered "If you split this
  horizontally in half and invert the right part (bottom to top) I think it
  would look like a choherent gradient". Built so: 16 by 16, the first two
  squares on top, the other two under them upside down, so the bright
  greens meet across the middle. Dark at the top, light in the middle, the
  deep blues at the bottom. Under it the levels and the `#RRGGBB`
  the board really shows. It is
  modal to Display and is a window made when opened and destroyed when
  closed. It is part of Display's program (as a file dialog is part of the
  Editor's), not a row of its own: it has no life without it.
- **The list: B**. Fifteen solid colours and "Chinchilla blue (dithered)"
  first. The default was **not** changed: that is the look of the system
  and the owner's to decide; "Steel blue" (levels 2, 3, 2) is its nearest
  solid one and sits beside it. "Lilac" left the list (sky and rose are
  close on either side); a `desktop.ini` that names it gets the default.
- With a solid colour the pattern's ink is now a board colour as well, so a
  patterned desktop in a solid colour has no dither either.
- **Terminal: A**. From 20 by 4 up to 104 by 36, which is what a maximised
  window holds. It opens at 80 by 25 as before.
- **Scroll bar**: the usual one at the right, always there; its range is
  the history, the thumb at the bottom when the view is on the live screen.

## What was built

- `core/src/desk/taskbar.cpp`: `kStartW = 60`.
- `core/src/shell/display.cpp`: the new list (`exact(r, g, b)` gives a
  board colour from its levels); the choice is a colour, not an index, so
  one without a name can be held; `desktop.ini` keeps the name, or
  `#RRGGBB` for the others; `ColourDialog`; the "Other..." button (Alt+O).
  When the colour is not in the list no cell is marked and the label shows
  the `#RRGGBB`.
- `core/tty/screen`: `set_size(columns, rows)`. A taller screen takes lines
  back from the history; a shorter one gives its top lines to the history,
  so the line with the cursor stays in view. Lines are not wrapped again.
- `core/src/shell/terminal.cpp`: resizable, `resized()` sets the grid from
  the client area, the scroll bar.

## How it was tested

On the PC. 39 tests pass, with the sanitizers.

- `test_tty`: the grid grows and shrinks, the history takes and gives
  lines, the cursor stays on the screen.
- `test_terminal`: maximised, the columns and rows are what the client
  area holds; the scroll bar's arrows move the view; restored, 80 by 25.
- `render_scenes`: Display with the new list; Other..., Escape closes only
  the chooser; a colour picked there reaches Display, the desktop and
  `desktop.ini` as `#RRGGBB`; opened again Display shows it. Pictures
  `37q` to `37x` and `46b-terminal-maximized`. Every picture with the
  taskbar changed by the two pixels.

## What is left

- The board: none of this has run there. To look at: the Terminal
  maximised with `top` and `ted` in it (a program that asked the size at
  its start keeps that size until it is started again), and a solid
  desktop over VNC against the dithered one.
- Whether the default becomes a solid colour: the owner's choice.
- Old lines are not wrapped again when the Terminal changes width.
