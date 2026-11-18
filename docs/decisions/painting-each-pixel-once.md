---
title: "Chosen for painting each pixel once"
date: 2026-11-16
by: agent
status: to-confirm
area: video
summary: "Layered painting through a scratch band of 12 lines, so nothing flickers without double buffering"
---

# Chosen for painting each pixel once

Heading in the single-file log: "Chosen for painting each pixel once (2026-11-16, to confirm)".


- Title bars, menu bars, menus, the taskbar, desktop icons, opaque widgets
  and small windows that draw over their widgets are painted through a
  scratch band of 12 screen lines (7.5 KB of internal RAM, `Canvas::layered`):
  the usual drawing calls run into the band, then each band is copied to the
  screen once. The screen itself stays a single picture (no double buffering,
  as decided); only the band is extra.
- Big widgets that already paint each pixel once (the Editor's text, the
  pictures in the viewer, the Culebra board) skip the band, because it runs
  a widget's painting once per 12 lines.
- test_paint counts screen writes for a full repaint, activating, moving and
  dropping a window, menus and submenus, minimising and restoring, the clock,
  dragging a file icon, and opening every program and dialog: no pixel is
  written twice. Measured on the board with Documents and the Editor open: a
  full repaint takes 35.0 ms (33.8 ms before).
- Escape cancels a file drag; a file let go on the empty desktop moves into
  C:\Documents\Desktop.
