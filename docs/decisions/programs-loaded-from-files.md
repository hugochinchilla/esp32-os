---
title: "Programs loaded from files"
date: 2026-11-17
by: owner
status: decided
area: system
summary: "Native code loaded from C:, with no protection; the file format and the loader (backlog 27). How the files get there: backlog 77; which programs: backlog 82"
---

# Programs loaded from files

Backlog item 27. [Journal](journal/2026-11-17-loaded-programs.md).

- **Native code loaded from C:** (the owner's choice on a card,
  2026-11-17: "Código nativo cargado de C:"). Rejected: WebAssembly (safe
  but 10 to 30 times slower), an interpreted language of our own, nothing.
  A loaded program has no protection, as a built-in one has none.

The rest is the agent's, to confirm:

- A program file is `name.app`: **a header of ours, then the code** (an
  ELF shared object for the ESP32-S3). The header
  (`core/include/core/app/file.hpp`, 1,332 bytes) carries the program's
  name, the stack its task asks for and a 16x16 and a 32x32 picture; the
  program writes it with `APP_INFO` and the build copies it to the front
  of the file (`tools/pack_app.sh`). The system reads it without loading
  the program. A file without it is refused. Put to the owner on a card
  (2026-11-17), built on the recommended option, answer pending; the
  others: the code alone with one picture for every program, or the
  picture in a file beside it.
- Programs are built from the same thin classes as the built-in ones
  (`programs/`, `tools/build_programs.sh`), with `APP_MAIN` as the way in.
- It reaches the system **by name** (the owner's choice on a card,
  2026-11-17: "Por nombre (como está hecho)"; a table of calls by number
  was the other): the loader fills in the calls from a table written out
  of the firmware's core library at build time (`tools/gen_exports.py`).
  Only the OS calls, the pictures, the shell's dialogs and a short list
  of libc are in it.
- The loader on the board is Espressif's `elf_loader` (2.1.0, Apache 2.0)
  behind `ports::Loader`; its own symbol tables are off.
- A loaded program is a process with the name its header gives (its
  file's if it gives none), windowed like the others. **Every loaded
  program has a task of its own** where the machine has tasks, with the
  stack its header asks for (4 KB if it says nothing): the owner's rule
  for item 26, "los programas, no los diálogos", applied to files. Put to
  him on a card (2026-11-17), answer pending; the others: only programs
  that mark it, or none for now.
- What a loaded program takes in memory is counted as its process's.
- It is unloaded after its process has ended, its last window is
  destroyed and its task has gone. 1 MB a file, eight at a time (sixteen since backlog 82).
- **Start > Programs lists the program files of `C:\Programs`**, after
  the built-in programs (since backlog 82: among them, in the order of the
  names), flat, by the name and picture of each header;
  the folder is read as the menu opens. Put to the owner on a card
  (2026-11-17), answer pending; the others: subfolders as submenus, or
  the menu left alone. In the File manager a `.app` has one picture for
  the kind, not each program's own (the listing keeps no pixels).

- (Replaced on 2026-11-19 by [The files of C: are copied when the board is
  flashed](files-copied-when-flashing.md), backlog 77; kept as the record.)
  **Programs that come with the system are carried in the firmware and
  installed** (the owner's choice on a card, 2026-11-17: "Dentro del
  firmware, se instalan"): the firmware build makes `programs/*` into
  `.app` files and carries them as data; at start-up the system writes to
  `C:\Programs` those that are missing or from another build
  (`C:\Programs\installed.txt` holds the build's stamp). Rejected: a
  read-only system partition (the partition table changes, the network
  update does not write it) and copying with `dbg.py` (a wiped C: loses
  them). A bundled program deleted by the user comes back at the next
  start.

- (Replaced on 2026-11-19 by [The programs leave the
  firmware](programs-out-of-the-firmware.md), backlog 82: every machine has
  the programs as files; kept as the record.)
  **Culebra and Cactus Patch are files on the board and built in on the
  PC** (the owner's choice on a card, 2026-11-17: "Archivo en placa,
  integrado en PC"): one source with two forms (`APP_FILE`). Rejected: a
  file everywhere (the browser build cannot load files and would lose
  them) and leaving them built in. A program file runs one copy at a time
  unless its header says `many`. The firmware does not shrink by this:
  the files are carried in it.
