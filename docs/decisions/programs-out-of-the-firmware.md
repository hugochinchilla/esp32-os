---
title: "The programs leave the firmware"
date: 2026-11-19
by: owner+agent
status: decided
area: system
summary: "The programs are files in C:\\Programs on every machine; only the loader differs. The Task Manager stays in the firmware (backlog 82)"
---

# The programs leave the firmware

Backlog item 82. [Journal](../journal/2026-11-19-programs-out-of-the-firmware.md).

The owner's, on 2026-11-19:

- **The programs that came built into the firmware become program files**
  (`programs/`, `.app` files in `C:\Programs`): "la terminal, vizcacha,
  editor de textos, desfragmentador, picture viewer, task manager, window
  gallery, incluso explorer (si no es complicado por lo de la papelera)".
  It follows from the lighter firmware and the software store of backlog
  item 78.
- **Every machine is the same; only the drivers differ**: "haz lo mismo en
  linux, todo debería ser igual, deberiamos intentar que solo los drivers
  HAL sean diferentes", and of the web page, "en el navegador la carpeta
  Programs ni existe". The board, the Linux program and the web page all
  have the programs as files in `C:\Programs`, listed and started the same
  way by the same code. This replaces "Culebra and Cactus Patch are files
  on the board and built in on the PC" in
  [Programs loaded from files](programs-loaded-from-files.md): no program
  has two forms any more (`APP_FILE` and `CHINCHILLA_GAMES_IN_FILES` are
  gone).
- **The web page's loader finds code that is linked into the page** (his
  choice on a card: "Cargador que enlaza lo compilado"). A page cannot load
  machine code from a file, so the programs are compiled into it; its
  `C:\Programs` gets a file for each all the same, and its Loader driver
  (`core/app/linked.hpp`) finds the code by the name in the file. Not
  chosen: real WebAssembly modules fetched and joined when opened (the page
  grows with Emscripten's dynamic linking, and much more work).
- **The Task Manager stays in the firmware** (his choice on a card: "Se
  queda en el firmware"). It is what Ctrl+Alt+Del opens to end a program
  that hangs, and it must open when C: is empty or broken or there is no
  memory left to load a file. Not chosen: a file that Ctrl+Alt+Del loads,
  and a file with a minimal list kept in the firmware beside it.

The rest is the agent's, to confirm:

- **What each machine's driver does.** The board: Espressif's ELF loader,
  files copied by `tools/flash.py` (as before). The Linux program: the
  PC's own loader (`host/dl_loader.hpp`); its build makes the program
  files beside it (`programs/*.app`) and it copies those that changed to
  `C:\Programs` each time it starts (`host/program_files.hpp`), which is
  its `flash.py`. The web page and the PC's tests: the linked loader; the
  system writes their files itself as it starts, because nobody else can.
- **Doom is not in the web page.** Its engine stands in for the C library
  and cannot be linked into another program; `programs/linked.cmake`
  leaves out a folder that has an `engine.txt`. The board and the Linux
  program have it as a file like the rest.
- **One list of programs** (`core/src/shell/programs.cpp`): the system's
  own few and the program files, in the order of their names. Start >
  Programs shows it; Run, the shell's `run` and `os::launch` start a
  program by its name through it, whichever kind it is. A file with the
  name of one of the system's own programs is left out. Up to 24 program
  files are listed and 16 loaded at a time (they were 8 and 8).
- **A program gets arguments**: `argv[0]` is its file and `argv[1]`, if
  given, what it is asked to open. `ports::Loader::enter` passes them and
  `shell::run_file` takes the argument.
- **A program's sources are its folder**, `programs/<name>/*.cpp`, with
  its pictures in its own `art.hpp`; the pictures of a program that left
  are taken out of `core/src/gfx/art.cpp`, which the firmware exports
  whole. The rules of the games moved with them (`core/src/games/` is
  gone).
- **The tests look inside a program through hooks** that exist only in
  the linked form (`#ifdef APP_LINKED`, `programs/test_hooks.hpp`).
- **A PC test tells what the board would refuse**: `program_exports` runs
  `tools/gen_exports.py --pc` on the PC's program files, so a program that
  asks the system for a name the firmware does not export fails there.

- **Opening something in a program that is already running** (step 2).
  A program that runs many copies (the Editor) says which file each
  window has (`os::ui::set_document`); asked for a file that one of them
  has, the system brings that window forward and starts nothing. A
  program that runs one copy (the Picture viewer) hands the system a
  function (`os::accept_open`) that is called with the new argument when
  it is started again. Not done: a message the running copy would have to
  wait for, which needs the argument kept somewhere between the two.
- **The Terminal's grid goes with it.** `tty::Screen` was used by the
  Terminal alone, so its source is part of `programs/terminal`; its
  header stays where the tests find it. The session (the TTY and the
  shell) is the system's and stays, reached with `os::open_session`.
- **Three Terminals, counted by their processes.** A program file has no
  memory of its own between copies, so each Terminal counts the others
  in the process list. The fourth is a process for as long as its "Three
  Terminals are open already" is on the screen.
- **Vizcacha takes the HTML, HTTP and address code with it** (step 3):
  `core/src/www` was the browser's alone. Its headers stay in
  `core/include` for the tests.
- **A program's figures for the debug link are the system's**
  (`os::counters()`): the firmware cannot read a variable of a program
  that is a file.
- **A program file may go without a task of its own**
  (`app::no_task`): its windows are served on the UI task, as a program
  built into the system could choose. For a program that never works for
  long in an event; the Terminal is the only one.
- **The decoders of pictures stay in the firmware** (the desktop's
  background uses them); the Picture viewer asks for them by name
  (`image::`). The quick launch's three pictures (File manager, Editor,
  Terminal) stay in the firmware's art too, so the taskbar draws them
  without reading a file.

- **The File manager is one program with a window for each folder**
  (step 4). `shell::open_folder` starts it by name with the folder as its
  argument; started again while it runs, it opens another window or
  brings forward the one that shows the folder (`os::accept_open`). It is
  told when folders change (`os::file::watch`) and reads them again
  itself.
- **The Trash, the file commands and the listing stay in the system**:
  the desktop's Trash icon and its own icons, the file dialogs and a drop
  on the desktop use them. `core/shell/files.hpp` is what the File
  manager asks of them (`in_trash`, `delete_asking`, `copy_files`,
  `paste_files`, `move_to_folder`, `Listing`...), found by name like the
  message boxes; they are not OS calls, because no other program is meant
  to use them. Which program opens which kind of file (`open_file`) stays
  in the system too: the desktop's icons and Run ask it.
- **A program that already runs is told from the header of its file**,
  before the rest is read: opening a folder does not read the File
  manager's file again. A program that takes what it is asked to open
  brings forward the window it chooses.

What it costs: with an empty C: the desktop has the Start menu, Settings,
Run, About, Welcome and the Task Manager, and nothing else until
`tools/flash.py --files-only` has run: its My Board, Documents and Trash
icons say "File manager is not on this machine. Its file belongs in
C:\Programs.", and so does a folder typed in Run. An update over the
network still brings the firmware alone (see
[The files of C: are copied when the board is flashed](files-copied-when-flashing.md)).
