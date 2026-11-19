---
title: "Screen sizes: 640x480 and 800x600"
date: 2026-11-18
by: owner
status: decided
area: video
summary: "Display > Resolution changes the picture at once; a box asks to keep it, back after 15 s; 640x480 the default (backlog 68)"
---

# Screen sizes: 640x480 and 800x600

The story is in `docs/journal/2026-11-18-resolution-800x600.md`. Checked on the board 2026-11-19 and merged.

Decided by the owner (2026-11-18):

- The Display tool has two pages, Appearance (what it had) and Resolution,
  with a switch to 800x600.
- The picture changes at once. A box then asks whether to keep it and counts
  15 s down; with no answer, or with "Go back", the screen returns to what
  it was. The size is saved (`[screen] mode=800x600` in
  `C:\.config\desktop.ini`) only on Keep. Not chosen: a change at the next
  start (no way back from a picture the monitor refuses, short of the debug
  link), and a change at once without the question.

- **640x480 stays the default; 800x600 is offered** (2026-11-19, after the
  board turn: a steady picture and no refill missed when idle, 270 refills
  missed under load at 800x600, 174 KB less free external RAM in both
  modes). A board with nothing kept starts in 640x480.

Chosen by the agent, to confirm:

- **800x600 at 60 Hz with a 40 MHz pixel clock** (1056x628 in all, 60.32
  pictures a second), the standard timing: 40 MHz is 160 MHz / 4 exactly.
  Not 800x600 at 56 Hz (36 MHz): gentler on the memory, but 160 / 36 is not
  a whole number and fewer monitors take it.
- **The picture memory is always the size of the largest mode** (480,000
  bytes of external RAM, 172,800 more than before, in 640x480 too). Its
  address never changes, so nothing that reads it from another task (the
  remote screen, a screenshot) can meet freed memory. Not chosen: giving the
  memory back and asking again at each change, which can fail when the
  external RAM is fragmented and needs every reader stopped first. The BIOS
  asks for one mode and keeps the small one.
- **The size is put on when the desktop starts**, so the BIOS screen and the
  starting picture stay 640x480 and a monitor in 800x600 finds the signal
  twice at power-on. Not chosen for now: the BIOS reading the desktop's
  settings file.
- **A remote viewer is dropped at a change** and connects again at the new
  size; the RFB message that resizes a connected viewer is not built.
- **Windows are refitted, not scaled**: a maximised window fills the new
  screen, a centred one is centred again, the others are pulled inside and a
  resizable one made smaller if it must. The desktop's icons are laid out
  again.
- **Tabs are a control of their own** (`wm::Tabs`, `os::ui::tabs`): the
  program hears which page is in front and places or hides its own controls
  (a control with an empty place is not shown and the keys pass it over).
  Ctrl+Tab and Ctrl+Shift+Tab turn the page, as do Left and Right on the
  tabs. Not chosen: pages that own their controls, a larger change to
  `os::ui` than one dialog justifies.
- `tools/dbg.py mode [N|WxH]` changes the size from the laptop, with no
  question and nothing saved, so a board turn can always get back.
