---
title: "The hourglass on demand"
date: 2026-11-18
by: owner
status: decided
area: desktop
summary: "A program asks for the hourglass with `os::ui::set_hourglass`; it shows over its client area only (backlog 49)"
---

# The hourglass on demand

Heading in the single-file log: "The hourglass on demand (the owner's decision, 2026-11-18)".


Journal: `docs/journal/2026-11-18-hourglass-on-demand.md`. Backlog item 49.

- A program that is working something out asks for the hourglass with
  `os::ui::set_hourglass(window, true)` and gives the arrow back with
  `false`. It is a switch, not a count, and it goes with the window.
- The hourglass shows over the window's client area only (the owner's
  choice of three). Over the title bar, its buttons and the menu bar the
  pointer is the arrow, because they still answer; the borders keep their
  sizing arrows. Not chosen: the whole window, as a locked window shows it
  (the pointer would say "wait" over a Close button that works), and a call
  that sets any pointer shape (more than asked; no program needs it).
- The window is not locked: keys, clicks, menus and Close work as before.
  `os::ui::set_locked` stays what it was, for a program whose job is using
  what the window holds, and `os::Busy` stays the whole-screen wait.
- To try it before Chess exists: Widget gallery > Options > Hourglass.
- To confirm (the agent's): the name `set_hourglass` rather than `set_busy`.
