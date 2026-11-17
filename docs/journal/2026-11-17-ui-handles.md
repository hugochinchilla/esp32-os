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

## The same day: thin classes on top (the owner's choice)

The owner chose the first option of the card. `core/app/app.hpp` is one
header, compiled into each program and not into the system:
`app::Window` with virtual `command`, `key`, `paint` and `closed`, and
`app::Button` and `app::Label`, each of which is a number and a few
functions that call `os::ui` with it. `app::open<T>(look)` asks the system
for a window with room for a `T`, builds the `T` in that room, and shows
it; when the window has closed the `T` is destroyed and the room goes
with the window.

Shut Down is written with it and reads much as it did before: a class
with five members and three small functions. The difference is in what
the members are. Before, they were the system's label and button objects
inside the program; now they are numbers, and the objects are on the
system's side.

One thing had to be settled for the classes to feel like before: members
are made in the constructor's list (`cancel_(button(...))`), so the
constructor must know which window it is building for. `open()` says so
just before it runs the constructor.

A window that never reaches the screen (the window manager full) is told
`Closed` as well, once, so the program's object is destroyed either way.

PC tests pass (41), pictures match. The firmware compiles and links (0x167c60 of 0x1f0000); not run. `dbg_tool` failed once in a full run and passed alone and in the next full run; nothing in it touches this change.

## The same day: the rest of the standard controls

`os::ui` and the thin classes now cover every control the system has:
check box, radio button, text field, scroll bar, list, group frame and
text area, beside the label and the button. Two more events: `Changed` (a
check box, a scroll bar, a list's selection) and `Chosen` (a row
double-clicked, or Enter on it).

Two things came out differently from the system's own classes:

- Radio buttons shared an `int` that lived in the program. The system
  cannot point into the program, so a window has four groups of its own
  and the program asks which value is chosen (`radio_value(group)`).
- A call with the number of a control of another kind, or of no control,
  does nothing and answers nothing. There is no error to check after
  every call; a wrong number is the program's mistake and harms only it.

`test_os` has a program with one of each: what the controls hold is read
and set by number; a tick, a move down a list, Enter on a row and the
default button reach the program as events; closing gives back the ten
blocks (the window and nine controls), destroys the program's object and
kills the numbers; a process ended with its window open does the same.

Still missing for the programs to come, and to be added as each needs it:
a menu bar, a window that can be sized, a dialog over another window, icons
in a list, and an area the program draws and takes the mouse in.

## The same day: one number per window, dialogs over a number, Run

- **One number.** The first cut gave a window made by number a number of
  its own (a slot and a count), beside the one the window server gives
  every window (`os::WindowId`, what the Task Manager sees). Two numbers
  for one window was one too many. A window made through `os::ui` now
  takes the window server's number when it is made, before it opens, and
  that is its number everywhere.
- **Dialogs take the number of the window they are over**, not a pointer:
  `open_file_dialog`, `save_file_dialog`, `input_dialog`, `colour_dialog`.
  A program written with the thin classes has no window object to point
  at. The dialogs themselves are still classes of the old kind and look
  the window up (`window_numbered`, for them only, until they are
  rewritten); `ui::Look::over` is the same thing for a dialog written by
  number.
- **Run** is the second program on the thin classes: a text field, three
  buttons, a picture, Browse opening the file dialog over it.

PC tests pass (41) and the pictures match after each of the three.

## The same day: Screen Saver, and a mistake caught by the tests

Screen Saver is the third program on the thin classes. It needed one new
event, `Painting`: the window is about to be painted, the last moment to
change a text the controls show (the figures of the last run).

The commit that moved it (3 of 17) was pushed with two PC tests failing,
because the command that ran the tests looked only at the last lines of
their output. What they had found:

- `test_saver`: Enter on the list no longer pressed OK. A list made by
  number always reported Enter on a row to the program (`Chosen`), and so
  swallowed it; the old dialog had no use for that and let Enter fall
  through to the default button. Now a program answers whether it used
  `Chosen`; if not, the default button is pressed. The thin class answers
  "no" unless the program says otherwise, so a dialog with a list behaves
  as before.
- `test_os`: a count of blocks that had to follow the dialog's new shape.

Fixed in the next commit; 41 of 41 pass, pictures match.
