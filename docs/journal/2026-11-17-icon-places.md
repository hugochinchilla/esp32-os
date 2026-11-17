# The icons stay where they are put

2026-11-17. PC only; the firmware was not compiled for this and nothing here
has run on the board.

## What was asked

The owner, while the second review round was being built:

> Also I want the icons dragged onto the desktop to be able to sit freely on
> any place they are dragged into.
>
> Right click on the desktop should show options:
>
> * snap icons to a grid (checkbox)
> * realign icons (reorders them top to bottom, left to right while snapping
>   them to a perfect grid)
>
> With snap to grid enabled, any newly moved icon will snap to an invisible
> grid, but enabling the toggle does not realign everything at once.
>
> The position o each icon will need to be stored by the desktop program, I
> guess

## What was found

- The icons panel (`core/desk`, `Icons`) worked out each icon's place from
  its number: columns down the left, six to a column. Nothing had a place
  of its own.
- Only the icons that stand for a file (those in `C:\Documents\Desktop`)
  could be dragged, and only onto something: the Trash, a folder, a window.
  Let go on the empty desktop, nothing happened. My Board, Documents,
  Network and the Trash could not be dragged at all.
- A file dragged out of the File manager onto the desktop went into the
  Desktop folder, and its icon appeared at the end of the column, not where
  it was let go.
- The window manager's drag carries a file's path and offers it to the
  window under the pointer.

## The options

**Who keeps the places**

- A. The desktop (the shell) keeps them in a settings file, and gives them
  to the panel with the icons; the panel tells it when one has moved.
- B. The panel keeps them. But the panel is `core/desk`, which knows
  nothing of files or settings, and the icons are numbered again each time
  the Desktop folder changes.

**Where**

- A. `desktop.ini`, a section of places.
- B. A file of its own, `icons.ini`. A settings file holds 2 KB and keeps
  the lines it does not know; the places are up to 24 lines with file
  names in them, and names of files that are gone must leave. A file of
  its own can be written whole each time.

**Dragging an icon that is no file** (My Board, the Trash)

- A. The same drag as a file's, under a name no file has (`*My Board`),
  marked so that no window is offered it.
- B. A second kind of drag inside the panel, with its own outline. Two
  ways to do one thing.

**What "realign" keeps**

- A. The order they are in on the screen: by column, then down it. An icon
  the owner put first stays first.
- B. The first order (the system's four, then the files by name). Throws
  away what he arranged.

**An icon let go on another**

- A. Allowed when free; with snap on, the nearest free cell.
- B. Never allowed. Then "any place" is not any place.

## What was chosen, and why

All the A's, and B for the file.

- The desktop keeps the places, as the owner guessed, in
  `C:\.config\icons.ini`: `[system]` by label and `[files]` by file name,
  each `x,y`. Nothing is written until the first icon is moved, so a
  desktop nobody touched looks as it did. From the first move on every
  icon has its place there.
- Snap is in `desktop.ini`, `[icons] snap`, off by default: free placing
  was the thing asked for. Turning it on moves nothing.
- The grid is the one the columns always used (76 across, 70 down, from 2,
  8): 8 by 6 cells.
- A new file with no place takes the first cell nothing is on.
- An icon is never let off the screen or under the taskbar.
- A file's icon let go on the Trash or on a folder still goes into it.

## What was built

- `core/desk`: `Icon` has `x, y`; `Icons` holds each icon's rectangle,
  `set_snap`, `realign`, `set_moved`; `set_drop` now also gives the place.
  Every icon can be dragged.
- `core/wm`: `begin_drag(path, desktop_only)`.
- `core/src/shell/desktop.cpp`: reads and writes `icons.ini`; the desktop's
  menu is "Snap icons to grid" (ticked when on), "Realign icons", a line,
  "Display...".

## How it was tested

On the PC, 41 tests.

- `test_wm`, `test_icon_places`: free placing to the pixel, kept on the
  screen, a click is not a move, an icon that is no file is not offered to
  a window, a file from elsewhere reaches the shell with its place, snap
  on moves nothing, the nearest cell and the nearest free cell, realign's
  order, places given with the icons, nothing stale on the screen.
- `render_scenes`: My Board dragged and let go, its line in `icons.ini`;
  snap from the menu, in `desktop.ini`; Documents snapped; the menu with
  its tick; realign. Pictures `39a` to `39d`. `readme.txt` dragged out of
  a folder now stays at the bottom right where the test lets it go.

## What is left

- The board.
- Two icons on one another: the upper one's box cuts the lower one's
  picture, since each pixel of the desktop is painted once.
- A file whose name has `=` in it, or begins with `[`, `;` or `#`, is not
  remembered (the settings file's own marks).
- Nothing arranges by name or by kind.
