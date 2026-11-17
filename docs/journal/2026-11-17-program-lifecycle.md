# 2026-11-17: Programs are made when they open and gone when they close

Backlog items 19 (program lifecycle) and 34 (Display and the other built-in
dialogs become programs with a life of their own), done together on one
branch. PC tests only so far; the board figures are asked for at the end.

## 1. What was asked

The owner, in the project chat, handing over the whole backlog:

> ve haciendo, no pares hasta terminar el backlog, dejo a tu criterio el orden
> de las tareas y el paralelismo, no tengas más de 4 cosas activas a la vez

So the design choices in this entry were made without asking him first, which
is not the usual rule here. Each is written down with the options it was
picked from so he can overrule any of them.

The task itself comes from his rule of the same day
([dialogs as programs](2026-11-17-dialogs-as-programs.md)): a program, and a
dialog or settings tool above all, keeps no memory while it is closed. That
entry found that only one of the three things "a separate program" can mean
is needed for this: a program with a life, made when started and destroyed
when closed. No task of its own, no file loaded from C:.

## 2. What was found

On `main` at `c664bd1`:

- Every window was a fixed object: `static EditorWindow w;` inside a function
  that hands it out, 17 of them across `core/src/shell/` (the ten Start menu
  programs, three File manager windows in an array, the Trash, Display,
  Screen Saver, Network and its two password dialogs, Shut Down, the message
  box, the file dialog, the input dialog, the Picture viewer's Options). All
  their widgets are members, so each object is the whole window.
- Closing a window took it out of the window manager's list and, for a
  window program, ended its process row. The object stayed, and so did what
  it had asked the Memory port for: the Editor's text and undo (576 KB), the
  Picture viewer's picture and working memory (about 2.4 MB), a folder list
  of 11 KB for each File manager window and the file dialog.
- The Memory port (`ports::Memory`) had `allocate()` and no way back for it:
  "asked for once, never freed" was written into it.
- Since the day before, all of these fixed objects live in external RAM, not
  internal ([statics in external RAM](2026-11-17-statics-in-psram.md)).
- Some programs counted on never dying: Culebra kept a game paused and its
  best score across a close, Cactus Patch kept the size of the patch, the
  Defragmenter kept its map, the widget gallery its counter.
- One object each meant one of each: the process table's entry says so
  ("there is still only one Editor object", 2026-11-16).

## 3. The choices

### Where the new objects live: external RAM

| Option | Taken? | Why |
| --- | --- | --- |
| External RAM (PSRAM), through the Memory port | **Yes** | It is where these same objects were already, as fixed ones, and that placement was measured on the board yesterday: repaints within 1 % of before. Internal RAM is the scarce kind (about 219 KB free with Wi-Fi on) and Wi-Fi needs it in large pieces; a heap that programs come and go in would cut it into small ones. External RAM has megabytes free |
| Internal RAM | No | Faster by a little for the window's own fields, but each open program would take 3 to 6 KB of the memory Wi-Fi needs, and the fragments left by opening and closing would lower the largest free block, which is the figure that matters to Wi-Fi |
| Internal for small windows, external for big ones | No | Two rules to explain and no measured need |

What this means for the figures: **free internal RAM should not change**,
before or after, with programs open or closed, because none of this was in
internal RAM since yesterday. What changes is external RAM: the fixed
objects' share of it (section 6) is free while the programs are closed, and
an open program's big buffers come back when it closes, which they never did.
The window manager object itself stays internal, as decided yesterday.

### Who destroys a window, and when

A window cannot destroy itself the moment it closes: `close()` is usually
called from inside its own code (a button's handler), which still has to
return through the widget and the window.

| Option | Taken? | Why |
| --- | --- | --- |
| The window manager destroys it at its safe point: the end of the event or tick, where process rows are already removed and signals take effect | **Yes** | That point exists and is exactly "none of the program's code is running". One list of closed windows, one loop |
| The window deletes itself in `closed()` | No | Its caller is still using it: a use after free on every close button |
| A one-shot timer set by the program to destroy it later | No | Uses one of 16 timers per close, and KILL stops a process's timers, so a killed program would leak |
| The process row owns the memory; freeing happens when the row goes | No, not alone | Right for a program's main window, but a dialog inside a living program (a message box, Save As) must go when it closes, not when the program does. The first option covers both, and the row still goes at the same safe point |

How it works: `Window::set_dispose(fn)` marks a window as made for one
showing. `Wm::close()` puts such a window on a list; `Wm::reap()` calls `fn`
for each one still closed. Before it does, the manager makes sure nothing of
its own still points there: dialogs that belonged to the window are closed
first (their `closed()` runs while the owner still exists), a free-standing
menu of that process is closed, and a timer whose context is the window is
stopped. A window opened again before the safe point is taken off the list
(a message box whose answer asks the next question does this).

### What the shell does with it

`core/src/shell/internal.hpp` has the few lines every program uses:

- `make_window<T>(...)`: memory from the Memory port, the object built in
  it, `set_dispose` pointing at a function that runs the destructor and gives
  the memory back.
- `one<T>(name)` and `open_one<T>(name)`: for a program that runs once at a
  time. One pointer per kind says whether it exists; it is cleared when the
  object is destroyed. This pointer is all that is left of a closed program.
- A window's destructor gives back what the window asked for beyond itself.
  The Memory port's `release()` now takes back `allocate()` blocks too.

No program lost a line of what it does; each lost its `static` and gained a
constructor that sets it up from scratch.

### One at a time, or several

| Program | Now | Why |
| --- | --- | --- |
| Editor | **Several**, each its own process, window, text and dialogs; at most four | The case the backlog item names ("two Editors become possible"), and what Notepad did. Four because each takes 576 KB of external RAM. A file that an Editor already has open comes to the front instead of opening twice |
| File manager | As before: one process, up to three folder windows and the Trash, each window made and destroyed by itself | Already more than one window; no reason to change |
| Everything else | One at a time; launching again brings it forward | A second Task Manager or Display has no use. Any of them can be switched with one word in the table (`App::many`) |
| Open, Save As, the text question | **One per asker**, no longer one for the whole machine | With one shared object, a second program asking for Open took the dialog away from the first. Each call now makes its own, which costs nothing while none is open |
| Message box | One at a time, as before; a new message replaces the one shown | Several call sites count on it (an answer that asks the next question). Left alone |

### Whose process a dialog is

The owner's rule says a dialog or settings tool is a program of its own. Two
kinds came out of that:

- **Settings tools and system dialogs** are programs with their own row:
  Display, Screen Saver, Network, Task Manager already were; **Shut Down**
  now is too (it was the desktop's window).
- **Dialogs a program asks for** (a message box, Open, Save As, a new name,
  the viewer's Options, the Wi-Fi password) are made when asked for and gone
  when closed, but belong to the process that asked. "Save changes?" is the
  Editor's own question: End Task and Shut Down rely on that to find it, and
  killing the Editor must take its question with it. This is also how the
  common dialogs worked in Windows.

### What a program remembers

With nothing kept in memory, what should outlive a close goes in the
program's settings file, as `docs/settings.md` already lays down:

- Culebra: best score and speed, in `culebra.ini`.
- Cactus Patch: the size of the patch, in `cactus.ini`.
- Not kept, on purpose: a game half played (Culebra used to wait, paused),
  the Defragmenter's pretend disk, the gallery's counter, Task Manager's
  processor graph. Closing a program ends it; that is the point.

## 4. What was built

- `core/include/core/ports.hpp`: `Memory::release()` gives back any block.
- `core/include/core/wm/window.hpp`, `core/src/wm/wm.cpp`:
  `Window::set_dispose`, `Wm::discard` (for a window that was made but never
  opened, because the manager was full), the list, and the work at the safe
  point described above.
- `core/src/shell/internal.hpp`: `make_window`, `one`, `open_one`, `discard`,
  `program_memory`.
- Every file under `core/src/shell/` with a window in it: no fixed window
  objects are left. `core/src/image/`: the decoder and the converter can give
  their working memory back.
- `core/src/shell/editor.cpp`: up to four Editors. `shell::App` has a
  `many` flag so `launch()` leaves the process to the program.
- `core/src/shell/dialogs.cpp`: file and input dialogs per call.
- `host/host_adapters.hpp`: the PC's Memory port really frees, and counts.

## 5. How it was tested

On the PC only. `host/tests/test_lifecycle.cpp` is new (24 tests in all with
`main` merged, all passing, built with the address checker, which stops on
any touch of a destroyed window):

- The window manager alone: a window closed from its own key handler lives
  to the end of the event and no longer; one that reopens itself is kept; a
  dialog whose owner goes is closed first; a timer left pointing at a
  destroyed window never fires.
- Every Start menu program, twice: open, close by Alt+F4, and the count of
  blocks out of the Memory port is back where it started, with no process
  row left. The Editor and the Picture viewer by bytes as well (more than
  576 KB and 1.9 MB out, zero after).
- Four Editors with different dialogs open at once; KILL on one with a
  dialog up and unsaved text; the fifth refused.
- Display, Screen Saver, Network, Shut Down and Task Manager: one block
  while open, none after, each with its own process row.
- No memory at all, then no memory for big buffers, then a full window
  manager (16 windows): nothing opens half made, nothing is left behind.
- Old tests changed where the behaviour did: a closed game is over, Shut
  Down is its own process. Seven reference pictures changed by a PID digit
  (Shut Down now takes a number) or by 1 KB less free on the test disk (the
  new settings files).

The whole firmware was built and linked in the cloud container with ESP-IDF
v5.5 (`idf.py build`, no errors). That says it compiles; it has not run.

**Not tested: anything on the board.** Asked of the board thread: free
internal and external RAM and the largest free internal block, on `main` and
on this branch, at the desktop with nothing open, with Editor + Picture
viewer + Task Manager open, and again after closing them (it should return
to the first figure); repaint times with a window open; that programs open
and close as before; that Culebra's best score survives a restart.

## 6. The figures

Measured here by compiling `core/src/shell/` and `core/src/wm/` for the
ESP32-S3 with the board's compiler (`xtensa-esp32s3-elf-g++ -Os`), on `main`
at `c664bd1` and on this branch, and reading each object file's sizes. `.bss`
is the fixed memory a file holds for the whole life of the machine (external
RAM, since yesterday).

| File | What was fixed in it | `.bss` on main | on the branch |
| --- | --- | --- | --- |
| `file_manager` | three folder windows, the Trash | 13,984 | 300 |
| `defrag` | the Defragmenter and its pretend disk | 7,792 | 4 |
| `desktop` | the message box, Shut Down; the desktop's own icon lists stay | 7,464 | 6,368 |
| `network` | the panel, two password dialogs | 4,816 | 20 |
| `cactus_patch` | Cactus Patch | 4,744 | 4 |
| `dialogs` | Open / Save As, the text question | 4,640 | 8 |
| `viewer` | the Picture viewer, Options | 4,300 | 64 |
| `task_manager` | Task Manager | 3,144 | 124 |
| `screensaver` | the dialog; the settings and the saver itself stay | 2,976 | 2,164 |
| `display` | Display | 2,736 | 12 |
| `apps` | About, Welcome, the gallery | 2,137 | 173 |
| `culebra` | Culebra | 1,988 | 4 |
| `editor` | the Editor | 1,620 | 20 |
| the rest (`wm`, `window`, `boot`, `trash`...) | unchanged | 9,198 | 9,198 |
| **Total** | | **71,539** | **18,463** |

So **53,076 bytes (51.8 KB) of external RAM are no longer held for ever**,
and what is left belongs to the desktop and the window manager, not to any
program. The code grew by 2.9 KB (134,860 bytes from 131,935), which is
flash, not RAM.

Beyond the fixed objects, memory that used to be taken on first use and
never given back now comes back when the program closes (from the code, and
the PC test checks the first two by bytes):

| Program | Given back on close |
| --- | --- |
| Editor | 576 KB (512 KB of text, 64 KB of undo), each copy |
| Picture viewer | about 2.4 MB (the picture shown, up to 1600 x 1200; the decoder's 384 KB; the converter's rows) |
| Each folder window, the Trash, each Open / Save As | 10.9 KB (the folder's list of 160 names) |

Expected on the board, to be confirmed there: internal RAM free unchanged
(nothing here was internal); external RAM free at the empty desktop up by
about 52 KB, and by megabytes compared with a `main` on which the Editor and
the Picture viewer had been opened once.

## 7. What was learned

- The safe point built for removing process rows and delivering signals was
  the piece that made this small. "When is no program code running?" had
  already been answered.
- One shared object for a dialog was hiding a bug: a second program opening
  the file dialog silently took it from the first.
- Programs that "kept" things across a close were not saving anything; they
  were just never ending. Making them end showed which of those things are
  worth a settings file.
- A reference kept across a close is now a real bug. Five tests held one;
  the address checker named each line.

## 8. What is left

- The board run above, then the merge.
- Per-process memory in Task Manager (backlog 3) can now add up real blocks:
  everything a program takes goes through `program_memory()`.
- A few small text buffers for messages are still `static` (a few hundred
  bytes in all); the message box copies its text, so they can become locals
  when their files are next touched.
- The screensaver and Display dialogs still read the "what is in force now"
  variables that sit beside them in the same file. They are that file's, not
  the desktop's, but a later pass could have the dialog read the settings
  file instead.
- Tasks of their own (26) and loadable programs (27) build on this: a
  program is now something with a start and an end.
