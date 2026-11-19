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

What it costs: with an empty C: the desktop has the Start menu, Settings,
Run, About, Welcome and the Task Manager, and nothing else until
`tools/flash.py --files-only` has run. An update over the network still
brings the firmware alone (see
[The files of C: are copied when the board is flashed](files-copied-when-flashing.md)).
