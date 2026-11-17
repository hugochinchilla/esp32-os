# An inetd that serves nothing

2026-11-17, afternoon. Follows
[Chinchilla in the browser](2026-11-17-chinchilla-in-the-browser.md) and
[Chinchilla as a desktop program](2026-11-17-desktop-program.md). Only
`host/desktop/main.cpp` and its test change; `core/` and `firmware/` do not.

## What was asked

The laptop thread opened the published page in Chromium and found a box on
every first visit:

    Start-up
    1 line of C:\.config\startup.txt could not be done

The same box shows in the Linux program on a new C: folder. The owner was
shown three ways to be rid of it on a card and chose "An inetd that does
nothing".

## What was found

A new C: gets the board's start-up file, which has two lines: the desktop,
and `inetd:once`, the task that runs the network services. On the board
both names are registered with init. The PC program and the page
registered only `desktop`, because they have no network. Init then takes
`inetd` for a program's name, finds none, and reports the line.

When these builds were written the file had one line; the second came with
the network services, in another branch, and nothing in the PC tests opens
the desktop program on a new C: and looks for a box.

## The options

1. **An inetd that does nothing** (chosen). The PC and browser builds
   register an `inetd` that makes its process row and serves nothing. The
   file stays the same on every machine, and the process list looks like
   the board's.
2. The default file fits the machine: the core writes the `inetd` line only
   where the machine says it has a network. Honest, but it changes `core/`
   and the ports for the sake of two builds, and a C: folder carried from
   one machine to another would still have the wrong file.
3. The page has its own `startup.txt`: the browser build writes a file
   without the line. It leaves the Linux program with the box, and makes a
   second default to keep in step with the first.

## What was chosen and why

The owner chose the first. It is the smallest change, it touches nothing
the board runs, and the start-up file stays one file. Its cost is a row in
the process list that says `inetd` on a machine where nothing listens. The
decisions file says a row stands for a real task; this one is the stated
exception, on these two builds only.

## What was built

In `host/desktop/main.cpp`, before init boots, `inetd` is added to init's
list. Its start function makes a system process named `inetd` and gives it
a stop function that removes the row, so End Task and `kill` end it at
once, as a signal ends the real one. Nothing else: no task, no sockets.

`tools/test_desktop_program.py` now also checks that no window is open in
the middle of the screen after start-up on a new C:, which is where the
box was. It fails on the old program and passes on the new.

## How it was tested

- The Linux program on a new C: folder, without a display (SDL's dummy
  driver), before and after: the box is in the first picture and not in
  the second.
- `tools/test_desktop_program.py` passes.
- The browser build, rebuilt here with Emscripten and opened in Chromium on
  a new profile: `tools/test_web_page.js`, all six checks pass, and the
  picture of the desktop has no box.
- The PC tests: 42 of 42 pass.

## Not tested

- The published page: it is rebuilt when this is merged into `main`.
- The board: nothing it runs has changed.
