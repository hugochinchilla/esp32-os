---
title: "Chosen in the shell milestone"
date: 2026-11-16
by: agent
status: to-confirm
area: desktop
summary: "Start menu, menus and how they open, Alt+Tab, double click, desktop icons, keyboard layout, program icons"
---

# Chosen in the shell milestone

Heading in the single-file log: "Chosen in the shell milestone (to confirm)".


- Start menu: Programs (the built-in programs), Settings (Control Panel and
  Taskbar, both greyed for now), Run... (greyed), a line, Shut Down....
  Rows are 24 pixels with 16x16 icons (98 uses 32x32 icons). The banner
  fades from near-black at the top to blue at the bottom, with
  "Chinchilla" in bold white reading upwards. The menu sits on the left edge
  of the screen, just above the taskbar.
- The Start menu and menu-bar menus open when the mouse button goes down; an
  item is chosen when it comes up, so press-drag-release works too.
- Submenus open as soon as the pointer is over their entry (98 waits a
  moment).
- A click outside an open menu closes it and goes no further.
- Alt+Tab brings the window at the bottom of the stack to the front at each
  press, with no switcher window (98 shows a row of icons while Alt is held).
- Double click: two presses within 0.5 s and 4 pixels.
- Desktop icons sit in a fixed column (as in the target picture) and cannot be
  dragged yet. A selected icon has a navy label and every other pixel of the
  picture tinted. Network and Recycle Bin open a "not available yet" message
  box. (My Board and Documents open the File manager since the files
  milestone.)
- ~~Message boxes and dialogs are not modal~~: replaced, they are modal now.
- ~~Fixed-size dialogs keep all three title buttons~~: replaced, dialogs show
  only the close button.
- ~~The caret does not blink, scroll arrows do not repeat~~: replaced, both
  work with the timers.
- Key presses type through a US keyboard layout. Accented letters and ñ are
  typed with the debug link's Text command (`tools/dbg.py text "año"`) until
  real keyboards and layouts arrive.
- Window corners grab both ways within 16 pixels of the corner. Minimum size:
  128 pixels wide, title bar (and menu bar) plus 24 pixels high.
- Welcome now opens at x = 96 so it does not cover the desktop icons; About
  at (300, 190).
- The desktop's background is one of 16 named colours with one of 10
  patterns of 8x8 over it (the first is none), drawn in a darker shade of
  the colour. Chosen in the Display dialog, from Start > Settings >
  Display or a right click on the desktop; kept in `desktop.ini`. Asked for
  by the owner on 2026-11-17; the lists and the shade are the agent's
  defaults, his to change. The default is the blue it always was.
- Every program has its own icon at 32x32 and 16x16, drawn by
  `tools/mockup/icon_proposals.py` and chosen by the owner on 2026-11-16
  (sheets in `docs/look/icons/`). The Editor's is a spiral notepad and a
  pencil, his pick of three. The 16x16 serves the Start menu, title bar,
  taskbar and Task Manager; there is no smaller size. A process with no
  window shows a cog. This replaces the borrowed icons listed below.
- Icons reused where there is no special one yet: Run uses the text icon,
  Shut Down the mascot; Settings has a new control-panel icon.
