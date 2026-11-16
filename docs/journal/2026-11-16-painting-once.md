# Painting each screen pixel once

## What was asked

After the Editor's flicker fix (text drawn in a single pass), the coordinator
asked to finish the same work everywhere: title bars, desktop icon labels,
taskbar buttons, menus, status bars, message boxes, the File manager's icon
view; to count pixel writes in tests for activation, moving and dropping a
window, menus and the taskbar; to keep the reference pictures byte-identical;
and to check on the board with the webcam.

## What was found

There is one picture in memory (no double buffering, a decision that saves
300 KB of PSRAM), so a pixel painted twice in one update (background, then
text or an icon on top) can be scanned out in between and flash. A new test,
`host/tests/test_paint.cpp`, counts every screen write over the whole shell.
Before: a full repaint wrote 20,920 pixels twice; dropping a window 6,765;
opening the Start menu 5,754; minimising 12,401; even the clock 264. Window
drag outlines were already clean.

## Options considered

- Rewriting each element to draw opaque text and icons (many new drawing
  routines, many sites).
- A second full screen buffer: against the decision, 300 KB.
- A small scratch band: run the existing drawing calls into a strip of 12
  screen lines (7.5 KB of internal RAM), then copy each strip to the screen
  once. The pixels are the same because the same code draws them.

## What was chosen and why

The scratch band (`Canvas::layered`): small, generic, and byte-identical. It
is used for title bars, menu bars, menus, the taskbar, desktop icons (the
desktop background is filled around them), every opaque widget, and small
windows that draw over their widgets (group frames, the message box and
About icons). Big widgets that already paint each pixel once (the Editor's
text, the viewer's picture, the Culebra board) skip it, since the band runs a
widget's painting once per 12 lines. Listed in `DECISIONS.md` for the owner
to confirm.

## What was built and tested

`core/include/core/gfx/canvas.hpp`, `core/src/wm/wm.cpp`,
`core/src/wm/window.cpp`. After: `test_paint` finds no pixel written twice
for a full repaint, activating, moving and dropping a window, menus and
submenus, minimising and restoring, the clock, dragging a file icon, and
opening and repainting every program and dialog. All reference pictures are
unchanged. On the board, with Documents and the Editor open, a full repaint
takes 35.0 ms (33.8 ms before).

## What was learned

The webcam (about 30 different frames a second) showed no flashes either
before or after: the double writes last microseconds. The write counter is
the real evidence. A first count was wrong because canvases made inside the
band still counted their writes; `Canvas::sub` now carries the flag.

## What is left

Nothing for now. Escape cancelling a file drag and dropping on the empty
desktop were folded in (see [the Trash](2026-11-16-trash.md)).
