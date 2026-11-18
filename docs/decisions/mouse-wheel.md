---
title: "The mouse wheel"
date: 2026-11-18
by: owner+agent
status: decided
area: desktop
summary: "The wheel scrolls what is under the pointer; how far, and which widgets take it (backlog 61)"
---

# The mouse wheel

Heading in the single-file log: "The mouse wheel (2026-11-18, the owner's request; the agent's choices, which the owner approved the same day)".


Journal: `docs/journal/2026-11-18-mouse-wheel.md`. Backlog item 61.

- The wheel scrolls what is under the pointer, not the window in use, and
  the window under it is not brought forward and does not get the keys.
  Not chosen: the window in use, as Windows 95 with a wheel driver did (the
  pointer has to be taken nowhere, but a window behind cannot be read
  without clicking it).
- Inside the window: the control under the pointer if it scrolls (a list,
  the Editor's text, a folder's icons, a scroll bar); otherwise the
  window's own scroll bars, the first upright one for up and down and the
  first lying one for sideways. So the Terminal, the Picture viewer and any
  program that puts a scroll bar beside its picture need no code for it.
- One notch is three lines where a bar counts lines, an eighth of the view
  where it counts pixels (never less than three steps, never more than a
  view), and one row of icons in a folder.
- Sideways: two fingers moved sideways on a trackpad, a wheel that tilts,
  or the wheel with Shift held.
- Nothing happens while a mouse button is held, while a menu is open, over
  a title bar, the taskbar or the desktop, or in a window that is locked or
  has a dialog over it. During the screensaver the wheel wakes the screen
  and scrolls nothing.
- A trackpad sends many small movements: the Linux window, the browser
  build and the browser's viewer page add them up and send whole notches
  (100 pixels of the page are one notch). A VNC viewer sends buttons 4 and
  5 for up and down, 6 and 7 for left and right.
- Programs are not told about the wheel (no new OS call): no program draws
  something that scrolls without a scroll bar. If one comes, it gets an
  event then.
- There is no USB mouse on the board yet (the USB host is "later" in the
  table at the top). When it comes, the report's wheel goes in
  `ports::MouseEvent::wheel`, and `wheel_x` if the mouse has it.
