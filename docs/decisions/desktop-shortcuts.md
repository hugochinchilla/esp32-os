---
title: "Alt+F4 on the desktop and the logo key with D"
date: 2026-11-19
by: owner+agent
status: decided
area: desktop
summary: "Alt+F4 with no window in the focus opens Shut Down; logo+D minimises every window, again brings them back; the logo key acts when released (backlog 76)"
---

Backlog item 76; the story is in
`docs/journal/2026-11-19-desktop-shortcuts.md`.

The owner's, 2026-11-19: Alt+F4 on the desktop opens the Shut Down dialog;
the logo key with D minimises everything and shows the desktop.

As built, the agent's readings. The owner confirmed all three on 2026-11-19
("si, son correctas"), the day the board check passed:

- **"On the desktop" is "no window has the focus"**: no window open, all of
  them minimised, or a click on the desktop, which already took the focus
  from every window. Windows may still be showing. Not chosen: only when
  no window shows at all (a click on the desktop followed by Alt+F4 would
  then do nothing, and 98 opens Shut Down there).
- **Logo+D pressed again brings the windows back**, stacked as they were,
  if no window shows at that moment. Only the windows it put away return:
  one the user minimised himself stays down. Not chosen: a key that only
  minimises (the way back would be a click on every taskbar button).
- **The logo key alone opens the Start menu when it is released**, not when
  it goes down as before, and only if no other key went down meanwhile.
  Not chosen: keep opening on the press and close the menu again when D
  follows (the menu would flash on the screen at every logo+D, and it is
  painted for nothing). Ctrl+Esc still acts on the press.
- **Neither key does anything behind a dialog that holds the whole
  desktop** (Shut Down itself), and neither
  repeats while held.
- A dialog that belongs to a window is put away and brought back with it.
- Not built: the Show Desktop button of 98's Quick Launch. It would be one
  call to the same function.
