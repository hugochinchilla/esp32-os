---
title: "Chosen in the screensaver milestone"
date: 2026-11-16
by: agent
status: to-confirm
area: desktop
summary: "How the saver takes the screen, what wakes it, the three savers, its dialog and settings file"
---

# Chosen in the screensaver milestone

Heading in the single-file log: "Chosen in the screensaver milestone (to confirm)".


- The saver takes the whole screen: the window manager paints nothing while
  it shows (timers go on; everything is painted once when it ends) and the
  pointer is hidden. It does not start while a mouse button is held.
- What wakes it: any key press, any mouse button, or the pointer moving 4
  pixels or more from where it was (a nudge of the desk does not). The key or
  click that wakes the screen does nothing else. Letting a key go does not
  wake it. A full repaint asked for over the debug link ends it too.
- 30 frames a second, in fixed steps, from the window manager's tick. The
  savers change only the pixels that move, never the whole 300 KB screen,
  and use whole numbers only, so the PC and the board draw the same frames
  from the same seed (the seed is the clock when the saver starts). There is
  no link to the monitor's refresh yet, so a moving face may tear now and
  then; to look at on the bench.
- Starfield: 260 stars coming out of a point that drifts slowly, as if
  steering; dim, grey, then white as they near, two pixels wide when close;
  one in eight pale blue, yellow or orange.
- Flying Chinchillas: 14 mascot faces flying out of the distance, drawn at 1
  to 5 times the 16x16 picture, in four fur colours (white, grey, beige and
  pale "sapphire" blue). Each place a face was or is gets its final pixels
  in one pass, so nothing flashes and overlapping faces stay whole.
- Curves and Colours: two closed curves, each round four points that bounce
  off the edges of the screen, each with a trail of 12 copies running through
  the colour wheel, the two on opposite sides of it.
- A remote screen (VNC, the browser viewer) shows the saver too: each saver
  reports the parts of the screen it changes, never the whole screen, and a
  key or click from the remote side wakes it like any other.
- Default saver: Flying Chinchillas. No password. Both confirmed by the
  owner, 2026-11-16.
- The dialog: a list ((None) and the three), the wait in minutes (1 to 99),
  Preview (Alt+P), OK and Cancel. After a run it shows real figures from it:
  frames a second and the slowest frame.
- Settings are kept in `C:\.config\screensaver.ini` (section `[saver]`:
  `saver`, `wait_minutes`; see `docs/settings.md`), written by OK and read
  at start-up.
- Not measured on the board yet: frame times, tearing, how it looks on the
  monitor. The firmware was not compiled for this change (no ESP-IDF in the
  cloud); no firmware file changed, the shell installs the saver itself.
