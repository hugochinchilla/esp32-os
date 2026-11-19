---
title: "The whole screen lent to a game"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: system
summary: "A window can borrow the whole screen: it writes the picture itself, gets every key raw and the mouse as movement; Ctrl+Alt+Del takes it back (backlog 81)"
---

# The whole screen lent to a game

Backlog item 81. Journal: `docs/journal/2026-11-19-doom.md`.

The owner's: a game that fills the screen draws straight into the picture
and the window server stands aside, as DirectDraw's exclusive mode (his
question of 2026-11-17, with Doom in mind, recorded in "Programs on tasks
of their own"); and Doom uses it (his choice on a card, 2026-11-19:
"Pantalla completa exclusiva"; a window of 320 by 200, and a window now
with the full screen later, were the others).

The rest is the agent's, to confirm:

- **A loan to one window**, asked for with `os::ui::take_screen(window,
  out)` and ended with `give_screen_back`. The program gets the picture's
  address and size and writes it as it likes; the window manager paints
  nothing and shows no pointer (`Wm::lend_screen`, built on `hold_screen`,
  which Shut Down and the start-up fade already use). Not chosen: a canvas
  through the paint path with a faster blit (the server still in the way of
  every picture); a second picture to draw in and a swap (there is one
  picture in memory, "Decided: video").
- **Every key goes to the window's Key event as it is**, down and up, the
  modifier keys too, and none repeats. Start, Alt+Tab, Alt+F4 and the menu
  keys do nothing while the screen is lent.
- **The mouse comes as movement** (the new event `MouseMoved`: how far, and
  which buttons), since there is no pointer. A mouse that reports places (a
  remote screen) moves by the difference between them.
- **Ctrl+Alt+Del is the way out that is always there.** It ends the loan
  and then does what it always does (the Task Manager). The owner asked on
  2026-11-17 for "one combination kept by the system to come back"; this
  one was already the system's whatever has the focus. Not chosen: the
  Start key (a game may want it, and it is easy to hit); Alt+Tab (the same).
- **The loan also ends** when the window closes or its process is ended,
  and when the screen changes its size. The program is not told: it asks
  `has_screen()` before each picture. Not chosen: an event (one more thing
  a program can miss; the question costs nothing).
- **The screensaver does not start** while the screen is lent, whatever the
  keyboard says (a demo plays by itself).
- **A remote screen** shows the game if the program says which part it
  changed (`screen_changed`).
- With it came two small calls a program without a window needs:
  `os::log` (a line for `dmesg`) and `os::rest` (gives the processor away
  for a while, from the program's own code).
- The screen is lent at whatever size it has. A game that wants another
  size centres its picture.
