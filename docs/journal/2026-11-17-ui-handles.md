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

## The same day: the Network panel

The Network panel is the fourth program on the thin classes (4 of 17),
and the first with a list that shows pictures and with texts that change
while it is open. What it needed from the system:

- `plain`: a line of text that keeps its own copy (the status lines),
  where a label points at a text the program must keep alive.
- `set_items` with a picture for each row (the signal bars and the lock).
- `on_screen` and `redraw`: the panel's timer skips its work while the
  window is hidden, and repaints only what changed.

The password and the "other network" dialogs were two classes with their
own widgets; they are one `JoinDialog` now, opened over the panel by its
number. The panel's private `Plain` widget is gone: the system has one.

PC tests pass (41 of 41), the pictures and the network scenes match.

The owner chose how programs that paint will draw: by numbered calls,
with a thin `app::Canvas` in the shape of today's `gfx::Canvas`, rather
than keeping the system's canvas object for now. It comes next, before
the programs that draw (the Terminal, the games, the viewer).

## The same day: drawing by calls

The owner's answer on the card: programs that paint draw by numbered
calls now, not through the system's canvas object.

Options that were on the card:

- **Calls by number now** (chosen, the recommended one): the Paint event
  carries a number; `os::ui::draw` has the canvas's calls one for one
  (fill, lines, edges, bitmaps, texts, patterns, a part of the drawing).
  A program built apart from the system (item 27) can paint with nothing
  but the table of calls.
- Keep the `gfx::Canvas` object for now: no work today, but every program
  that paints would hold a system object, and item 27 would have to come
  back to all of them.

What was built:

- The system keeps the drawings in progress in a small table: the event's
  canvas and up to eight parts of it (`draw::part`, what `Canvas::sub`
  was). They end when the event returns, and the next event's numbers are
  new, so a number kept too long draws nothing.
- `app::Canvas` in the thin classes: the number and the same method names
  as `gfx::Canvas`, so `paint(app::Canvas &c)` reads as the old paint did.
- Shut Down, Run and the Network panel paint through it. Pixel-identical.
- Not carried over: `Canvas::layered` (no program uses it; the window
  itself is painted in bands when it has a picture). The programs that
  write every pixel once (Terminal, Culebra, the viewer) will need a flag
  to say so when they move.

Cost: one more call per drawing operation and a table lookup. To be
measured on the board with the Terminal, which draws the most.

Tested on the PC: a program paints a square and a corner of it through a
part, the parts run out at eight, and the number does nothing after the
event (`test_os`). 41 of 41 pass, the pictures match. Firmware not
compiled since this change, and not run.

## The same day: Display and the colour dialog

Programs five and six of seventeen. Both had widgets of their own, which
a program by number cannot have: Display's preview (a small monitor) and
the grid of cells that both use for colours and patterns.

Options for a program's own widget:

- **Two system controls the program paints** (chosen; the agent's, to
  confirm): a `picture`, a rectangle painted by the program when the
  system says so (the Paint event, with the control's number), and a
  `grid` of cells to pick one from, where the system keeps the selection,
  the arrows and the frame and asks the program to paint each cell (the
  Cell event). This is Win16's "owner-drawn" control.
- Let the program build any widget out of a picture plus mouse and key
  events: more general, and the games and the Terminal will need those
  events anyway, but then each program carries its own copy of the grid.
  Not now; the events come with the programs that need them.

Also new: a window can give the size of its client area instead of its
frame (`kClientSize`), since a program no longer knows how thick the
frame is.

The grid's code moved from `core/src/shell/picker.hpp` into the system
(`core/src/os/ui.cpp`); the file is gone. Display's names of the colour
and the pattern are `plain` texts now.

PC tests pass (41 of 41) and the pictures match: both dialogs are
pixel-identical. Firmware not compiled since, and not run.

## The same day: Open, Save As and the input dialog

Programs seven and eight (`dialogs.cpp`). New in the system for them:

- `drop_down`: the "Look in" box, which was a widget of the file dialog's
  own. The program hears Command when its arrow is pressed and opens its
  menu under it (`menu_under`). The menu is still the program's table of
  texts and functions; menus by number wait for the programs with a menu
  bar.
- `set_title` (the input dialog's title is the caller's text) and
  `focus` (Backspace goes up a folder only while the list has the keys).
- The pictures of the rows are a table the dialog fills from the folder's
  listing, where the old list asked a function row by row.

PC tests pass (41 of 41), the pictures match. Firmware not compiled
since, and not run.

## The same day: the Task Manager, and a number that outlived its window

Program nine. New in the system for it: the `meter` (the bar of blocks
that the Task Manager and Defrag shared as a widget of the shell's), and
`in_front`. The graph of the last minute is a `picture`; the lines of
figures are `plain` texts; the pictures of the rows are a table.

A fault found by the PC tests (`test_paint`), in the system and not in
the Task Manager: a window that had closed still answered to its number
until the system took it apart a moment later, though the program's
object had already gone with the Closed event. Opening the Task Manager
again in that moment brought the dead window back and painted through an
object that was no longer there. Now the number is dead as soon as the
program has heard Closed, and nothing more is told to it. `test_os`
checks it.

PC tests pass (41 of 41), the pictures match. Firmware not compiled
since, and not run.

## The same day: the Defragmenter

Program ten. Its map of blocks and its legend are two `picture`s; a
picture can now be asked to paint only a part of itself again
(`Picture::redraw(part)`), which is how one block changes without the
whole map being painted. The meter is the system's; `meter.hpp` is gone
from the shell.

PC tests pass (41 of 41), the pictures match. Firmware not compiled
since, and not run.

The commit that moved the Defragmenter was pushed with `test_system` not
compiling (a semicolon lost in a count of blocks): the command that ran
the tests went on to the commit when it should have stopped, the same
kind of slip as with the Screen Saver. Fixed in the next commit, and the
checks before a commit are one script now, `tools/gate.sh`, which fails
unless the build, every test, the pictures and the OS-call check pass.

## The same day: Culebra, the first program with a menu bar

Program eleven, and the first that is not a dialog. New in the system:

- A window can say where it goes (`Look::x`, `y`), that its size is
  fixed (`kFixedSize`) and that it has a menu bar (`kMenuBar`,
  `set_menu_bar`). The menus are still the program's tables of texts and
  functions; the Menu event comes before one opens, to grey items out.
- A `picture` can say that it writes every pixel once when it paints
  (the board), so the system does not paint it through a buffer; and the
  drawing tells what part of it may be drawn (`Canvas::limit`), so the
  board paints only the cells that changed.
- `has_keys`: the snake stops while another window has the keys.

Two slips on the way, both caught by `test_snake`: the tests asked for
the game before opening the window, which used to make one and no longer
does (the tests open it first now); and a function added to the system's
window class had the name of one of the window manager's and hid it, so
the game believed it never had the keys.

PC tests pass (41 of 41), the pictures match: each step still paints
only the four cells that changed, each pixel once. Firmware not compiled
since, and not run.

## The same day: Cactus Patch, the first program that takes the mouse

Program twelve. It painted its whole window itself and read the mouse
there. By number it is one `picture` that covers the client area.

Options for the mouse:

- **The mouse goes to a picture that asks for it** (chosen; the agent's,
  to confirm): `kMouse` when the picture is made, then the Mouse event
  (left button: down, move, up, all to the picture that was pressed) and
  RightClick, in the picture's coordinates. A window with controls and a
  drawn part (the viewer, the File manager) needs exactly this.
- A flag for "the program paints and hears everything in the window":
  one more way of doing the same, only for windows without controls.

Also new: a control can be moved or made another size (`place_control`)
and a window can change the size of its client area (`resize`: it stays
where it is but inside the screen), for the three sizes of the field.

PC tests pass (41 of 41) and the pictures match with no change to the
tests. Firmware not compiled since, and not run.

## The same day: the Picture viewer, the first window that changes size

Program thirteen, with its Options dialog. New in the system:

- The Resized event: the client area has another size, and the program
  puts its controls where they go now (`place`, on any control; an empty
  place hides one). `client_size` tells the size when the window is made.
- The scroll bars of the picture are the system's, placed by the viewer
  beside the picture when it is larger than its area. They were two
  scroll bars kept inside the viewer's own widget, which passed the mouse
  on to them by hand.
- The status line is a `picture` the viewer paints.

The viewer no longer makes a window before it knows it has the memory
for a picture and throws it away: it opens, and closes at once if there
is none.

PC tests pass (41 of 41), the pictures match. Firmware not compiled
since, and not run.

## The same day: the Terminal

Program fourteen. Its grid of characters is a `picture` that takes the
keys. New in the system for it:

- `kKeys`: a picture that can have the keys. Its keys come as the Key
  event with its number, and the characters typed as the Text event.
- The text cursor: the program tells where it is (`set_caret`), which is
  all that a blink paints again, and asks while it paints whether the
  picture has the keys and whether the cursor is in the lit half of its
  blink.
- A window can be made without being shown (`app::make`, then
  `app::show`): the Terminal has to get its session first, and gives the
  window back if it cannot. `close` on a window never shown destroys it.

The grid is still read under the session's lock while it is painted; the
lock is the program's business and the system knows nothing of it.

PC tests pass (41 of 41), the pictures match. A Terminal is four blocks
of memory now (its window, the grid's picture, the scroll bar and the
session) where it was two. Firmware not compiled since, and not run.

## The same day: the Editor

Program fifteen. The text editor (`wm::TextEdit`) was an object inside
the Editor's window, with memory the Editor asked for and handed to it.
By number it is a control of the system's:

- `text_edit`: made with the sizes of its text and undo memory, which
  are the control's own, counted as the program's, and go back with it.
  No control if there is no memory.
- What the menus do is one call with a name (`edit`: undo, cut, copy,
  paste, delete, select all, clear); what the menus need to know is one
  answer (`edit_state`: changed, can undo, has a selection). The file
  goes in by pieces as before.
- Saving runs on a job of the Editor's, on another task. The job cannot
  look a number up in the system's table while the UI task may be
  changing it, so the Editor takes a reader for the text on the UI task
  before it starts the job (`text_reader`), and the job reads through it
  (`read_text`). It is good while the window is locked, which the Editor
  already did for the length of the save. The agent's, to confirm: it is
  the one place where a program holds something that is not a number.
- The Closing event (the Editor asks about unsaved changes), `set_locked`
  and `clipboard_size`.

PC tests pass (41 of 41), the pictures match. Firmware not compiled
since, and not run.
