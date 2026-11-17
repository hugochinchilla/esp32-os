# The desktop is one flat colour by default

2026-11-17, midday. PC only; the firmware was not compiled for this and
nothing here has run on the board.

## What was asked

The second review round (`2026-11-17-review-round-2.md`) left one thing
open: the list in Display had 15 solid colours, but the default stayed the
dithered blue the desktop had from the start, because the look of the
system is the owner's to change. Asked "should the default background
become the solid Steel blue?", he answered:

> yes, solid steel blue

## What was found

- The default was in two places: the first entry of Display's list
  (`chinchilla-blue`, 58, 110, 165, drawn with the 4x4 dither) and the
  window manager's own starting backdrop, used before any settings are
  read.
- On the board, in the turn that checked the review round, a full screen
  over VNC took 2.3 to 3.4 s on a solid colour against 5.3 to 10.7 s on the
  dithered blue.

## The options

- A. Steel blue (red 2, green 3, blue 2 of the board's levels: 73, 109,
  182) as the default, the old blue kept in the list under its name.
- B. Change the old blue's own value to the solid one. Then nobody could
  have the first look back, and a `desktop.ini` that says
  `chinchilla-blue` would silently mean another colour.

## What was chosen, and why

A. Steel blue is first in the list and is what a machine with no
`[background] colour` in `desktop.ini` shows. "Chinchilla blue (dithered)"
is second. A machine whose `desktop.ini` names a colour keeps it, the
dithered one too: the default is for those who never chose.

## What was built

- `core/src/shell/display.cpp`: the first two entries of the list changed
  places.
- `core/include/core/wm/wm.hpp`: the starting backdrop is 73, 109, 182.
- `docs/settings.md`.

## How it was tested

On the PC: 41 tests pass. `render_scenes` checks the default and the way
back to it by the new value. 96 reference pictures changed, every one that
shows the desktop; `37q-display-dialog` shows the list with Steel blue
first and chosen.

## What is left

- The board. A board whose `desktop.ini` has no colour line will show the
  solid blue after this; one with `colour = chinchilla-blue` stays
  dithered until changed in Display.
- The mock-ups in `tools/mockup/` still draw the dithered blue.
