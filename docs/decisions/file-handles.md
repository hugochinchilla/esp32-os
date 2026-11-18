---
title: "Open files are handles the system keeps"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "Open files are numbers; the system keeps the table and closes what a dead process held (backlog 37)"
---

# Open files are handles the system keeps

Heading in the single-file log: "Open files are handles the system keeps (2026-11-17, the agent's, to confirm)".


Backlog item 37, first step. Journal:
`docs/journal/2026-11-17-file-handles.md`. Started on the recommended
option of a card the owner has not answered yet.

- `os::file::open` and `begin_save` give a number, not an `fs::File` in
  the program. The system keeps the table of which process holds which
  file, and closes what a process leaves when it ends; a save not finished
  is thrown away and the old file stays.
- From a background job the call is given the job, which says whose the
  file is. Not chosen: a per-task variable (hidden state), or opening on
  the UI task (flash work where the job was meant to keep it away).
- No lock: a slot is claimed by an atomic flag. The table (2 KB) comes
  from the Memory port once, so it is external RAM on the board.
- ponytail: six open in the machine, two for one process.
- The picture decoder reads from an `image::Source`; the Picture viewer
  hands in one over a handle it opened, held only while it decodes.
  `os::file::storage()`, which handed a program the drives whole, is gone.
- **Handles now, the queue with item 26** (the owner's choice on a card,
  2026-11-17).
- Another program's window is a number (`os::WindowId`, the number the
  window server already gave each window): `os::main_window(pid)` no
  longer hands out a pointer into another program's object. A program's
  own windows stay C++ objects. The agent's, on the recommended option of
  a second card (how far window handles go) that the owner has not
  answered yet.
- **Windows and widgets wholly by number** (the owner's choice on the
  second card, 2026-11-17). `core/os/ui.hpp`: a program asks for a window
  and controls and gets numbers; the objects are the system's; events
  reach it through one function. First cut built with Shut Down as the
  sample. [Journal](journal/2026-11-17-ui-handles.md).
- The list of calls is what a "DLL" of the window system would export (the
  owner's idea): versions are one table that only grows, with a number.
  Loading it is item 27.
- **Thin C++ classes on top of the calls** (the owner's choice on a card,
  2026-11-17; the calls used directly, Win16 style, was the other
  option): `core/app/app.hpp`, one header compiled into each program.
  `app::Button` is a number and the functions that call the system with
  it. Programs are written with these, never with `core/wm`'s classes.
- **Drawing by calls on a number** (the owner's choice on a card,
  2026-11-17; keeping the system's canvas object for now was the other
  option). `os::ui::draw`: the canvas's own calls, one for one, on a
  number good only while the Paint event is answered. Programs write
  `app::Canvas`, a thin class of the same shape, so a paint reads as
  before. Bitmaps and texts are still handed over as pointers into the
  program's memory, as Win16 did.
- What the programs needed that was not a plain control became controls
  of the system's (the agent's, to confirm): the grid of characters and
  the picture a program paints (Terminal, games, viewer), the text
  editor with its own text and undo memory (Editor), and the view of
  icons with names (File manager). A program's toolbar buttons and
  panels are pictures it paints itself. The one thing a program holds
  that is not a number is the Editor's reader for its text while a save
  job runs (good only while the window is locked).
- All 17 programs are written with the thin classes, and since item 26
  About, Welcome and the widget gallery too (20 files). What is left on
  `core/wm`'s classes is the desktop itself and the message box.
- Events through a queue: decided against ("No queue of events" below).
