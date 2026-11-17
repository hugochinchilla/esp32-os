# Windows and controls by number

2026-11-17. Backlog item 37, the windows.

## What was asked

After files became handles, the owner was asked on a card how far window
handles should go: at the border only (another program's window as a
number, which was built and is on the board), windows and widgets wholly
by number, or leave windows as they are. He chose the second: "Ventanas y
widgets por número". It rewrites the 17 programs and is what item 27
(loadable programs) needs.

He then asked two things in the project chat. What item 27 expects of
windows: a program built apart from the system cannot share C++ classes
with it, because it would carry their layout in memory; with numbers the
border is a list of functions taking integers and text. And whether the
window system could be "something like a DLL" that programs use directly,
with versions so old programs need not be rebuilt. The answer given: a DLL
is such a list of functions, so the numbers are what it would export;
versions are a table that only grows, with a version number, old entries
never changing; what is missing to load it is item 27's (no dynamic linker
on the ESP32).

## What was found

A program is a class that inherits from `wm::Window` and holds its
controls as members (`wm::Button`, `wm::Label`...). The window server
calls its virtual functions. Everything the program is lives in one block
the program's code lays out.

## What was built: the first cut and one program

`core/os/ui.hpp`, `core/src/os/ui.cpp`:

- `os::ui::create(look, events, state_bytes)` gives a window number. The
  object behind it is the system's (one class, `Behind`, for every such
  window), in memory counted as the program's. `state_bytes` of the
  program's own memory come with it and go with it.
- `os::ui::label`, `button` give control numbers; `set_default`, `press`,
  `show`, `close`, `alive`, `state`.
- What happens reaches the program through one function,
  `bool events(window, event, state)`: `Command` (a button), `Key`,
  `Paint`, `Closed`.
- A number is a slot and a count of the slot's uses, so a closed window's
  number is dead.

Shut Down (`core/src/shell/shutdown.cpp`) is the first program on it: no
window class, no button objects, three control numbers in a struct and a
switch.

## The options still open

Put to the owner on a card with the sample: thin C++ classes on top,
compiled into each program, so that programs read much as they do today
while the border stays numbers (recommended); the calls used directly with
a switch, as Win16 programs were written (the sample); or stop here and
take it up with item 27.

Not yet asked, and it is the hard part: drawing. Programs that paint
(the Terminal, the games, the Picture viewer, the File manager) are
handed a `gfx::Canvas` object, as the sample is for its picture. For a
program built apart that has to become calls too (text, rectangle, copy a
bitmap), in batches so that it is not a call per pixel.

## How it was tested

The 41 PC tests pass and the pictures match: the dialog is the same to the
pixel. `test_lifecycle` now counts six blocks for Shut Down while open (the
window and five controls, where the class was one block) and none after.
The firmware compiles and links (SIZE of 0x1f0000). Not run on the board.

## What was learned

One block per control is the cost of not knowing, when the window is
asked for, how many controls will follow. Each block has a 16-byte note
of whose it is. If that matters, the window can be given room for its
controls up front.
