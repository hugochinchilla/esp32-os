---
title: "The window server and the desktop shell"
date: 2026-11-17
by: owner
status: decided
area: desktop
summary: "The code is cut in two on one task: the window server (`core/wm`) and the desktop shell (backlog 21)"
---

# The window server and the desktop shell

Heading in the single-file log: "The window server and the desktop shell (the owner's decision, 2026-11-17)".


Backlog item 21, second part. The owner was shown three depths (the
process only; the code cut in two on one task; two tasks that talk by
messages) and chose the second.
[Journal](journal/2026-11-17-window-server-and-shell.md).

- The window server (`core/wm`) knows windows, menus, the pointer and the
  background. The taskbar and the desktop icons are the shell's
  (`core/desk`), drawn by its code and owned by the `taskbar` process.
- The order agreed with the owner: this cut, then the OS calls taken to
  handles (item 37), then separate tasks together with item 26.
- The shell's surfaces are panels (two places: behind the windows, and the
  strip in front of them), not windows of a special kind. Proposed by the
  agent, confirmed by the owner on 2026-11-17 ("yes, panels are ok").
- The agent's, to confirm: menus, the time of day and the height of the
  strip stay with the server; the desktop panel paints the background
  where it has no icon.
- Painting is not an OS call: the server calls a window or a panel to
  paint and hands it a canvas. It becomes one with item 37.
