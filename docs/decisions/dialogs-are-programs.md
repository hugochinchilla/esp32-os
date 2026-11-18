---
title: "Dialogs and settings tools are programs"
date: 2026-11-17
by: owner
status: decided
area: system
summary: "A dialog or settings tool is a program of its own: own process row, own file, no memory while closed"
---

# Dialogs and settings tools are programs

Decided by the owner; the story is in
`docs/journal/2026-11-17-dialogs-as-programs.md`.

- A new dialog or settings tool (Display is the example) is a program of its
  own, not a part of the desktop: its own row in the process table while open;
  no memory kept while closed (window, widgets and buffers created when it
  opens, given back when it closes); it reaches the desktop only through calls
  any program could make; its code is a file of its own.
- When this was decided every program was one fixed object on the UI task,
  and closing it freed nothing. Since "Program lifecycle" below (backlog
  items 19 and 34, the same day) no program or dialog is: all are made when
  opened and destroyed when closed, so nothing is owed any more.
- Not taken: a FreeRTOS task per dialog now (the stack costs more internal RAM
  than the dialog frees, and the widgets assume one task); waiting for
  loadable programs (item 27) before changing how dialogs are written.
