# Programs loaded from files (backlog item 27), first step

## What was asked

Backlog item 27, "Loadable or sandboxed apps". The standing instruction is
to work through the backlog; this one needed the owner's choice first, and
it was put to him on a card: "Decide cómo serán los programas cargables
desde disco (punto 27)". He chose **"Código nativo cargado de C:"**.

## What was found

Every program is compiled into the firmware: a row in the program table
(`core/src/shell/apps.cpp`) with a function that opens it. Since items 20
and 37 the programs reach the system only through the OS calls
(`core/os/os.hpp`, `core/os/ui.hpp`) and are written with the thin classes
of `core/app/app.hpp`, a header compiled into each program: nothing of the
system's objects is inside a program any more. Since the last part of item
26 that is true of all twenty of them. That was done so that a program
could one day be built apart from the system, and it is what makes this
step small.

The board can run code from external RAM (the firmware itself already
does, `CONFIG_SPIRAM_XIP_FROM_PSRAM`): on the ESP32-S3 a page of PSRAM is
seen at a data address and at an instruction address. Espressif publish a
loader that uses this, the `elf_loader` component (Apache 2.0, version
2.1.0, in esp-iot-solution): it takes an ELF file, puts its code and data
in PSRAM, fills in the addresses it asks for from a table of names, and
calls its entry.

## The options

Put to the owner:

| Option | Chosen | Why, or why not |
|---|---|---|
| **Native code loaded from C:**: a file compiled on the PC, loaded into PSRAM, running at full speed against the OS calls | Yes (the owner) | It is what the calls by number and the thin classes were made for; full speed (he wants Doom one day); the programs there are could leave the firmware for files. No protection: a bad pointer in a program restarts the board, as it does today |
| WebAssembly: the same C++ compiled to `.wasm`, run by an interpreter | No | A broken program would only end itself, but it runs some 10 to 30 times slower and the firmware grows by 100 to 200 KB |
| A language of our own, interpreted (a BASIC, or Lua), written in the Editor on the machine | No | Safe and very much of the 90s, but slow, and the programs there are could not move to it |
| Do nothing | No | |

Then, the agent's, to confirm (the next card will ask the first):

| Question | Chosen for this step | The other |
|---|---|---|
| How a loaded program reaches the OS calls | **By name**: the program is linked with the calls left open, and the loader fills each in from a table of names written from the firmware's own core library. The C++ names carry the argument types, so a program built for a call that has changed is refused when it is loaded, with the name printed | By number: a table of function pointers handed to the program, each call an index into it. No names in the firmware and no lookup, but `os.hpp` would have to exist twice (real functions for the system, stubs for programs), and a changed call is not noticed |
| Whose loader | Espressif's `elf_loader` | One of our own in `core/`: Xtensa relocation and the two views of PSRAM are exactly the chip-specific work a port is for |
| What a program file is called | `name.app` (as GEM's programs were) | `.exe`: not ours to imitate |
| What a program may reach | The OS calls (`os::`), the pictures of `core/gfx/art.hpp`, the message box and the shell's dialogs, and a short list of libc and compiler helpers. The loader's own tables (all of libc, pthreads, ESP-IDF) are off | Everything the firmware has |

## What was built

- **The port.** `ports::Loader` (`core/include/core/ports.hpp`): `load`
  (the file's bytes; a module or a reason in words), `enter`, `unload`.
- **The system's side**, `core/src/shell/loaded.cpp`: `shell::run_file`
  reads the file (1 MB at most) into memory asked for the purpose, has it
  loaded, gives the memory back, makes a process named after the file
  (`C:\hello.app` is `hello`), and calls the program's way in as that
  process. The program opens its window and returns. When the process has
  ended, however it ended (its window closed, End Task, `kill`), the
  program is taken out of memory: not from inside the end of the process,
  because the last window is destroyed after that and the program's own
  code gives its memory back, but at the next timer round. Eight loaded
  programs at a time.
- **Opening one.** A `.app` file is a kind of file the system knows what
  to do with (`open_file`): a double click in the File manager or on the
  desktop, or its path typed in Run or at the shell's prompt. If it cannot
  be run, a message says why.
- **The way in.** `APP_MAIN(int, char **) { ... }` in `app.hpp`.
- **The first program**, `programs/hello/hello.cpp`: a window with two
  labels and two buttons that counts presses, written exactly like a
  built-in program. For the board it is 3,712 bytes and asks the system
  for eleven names.
- **On the board**, `firmware/os/main/elf_programs.hpp`: the port over
  `elf_loader`. `tools/gen_exports.py` writes the table of names from the
  core library as the firmware is built (294 names: 173 OS calls, the
  pictures, the dialogs, libc), so the table cannot fall behind the
  firmware. `tools/build_programs.sh` builds `programs/*` with the ESP32-S3
  compiler and checks every name each program needs against that table.
- **On the PC**, `host/dl_loader.hpp`: the same port over the PC's own
  dynamic loader, with the program built as a shared library. The test
  program exports its OS calls by name, as the firmware does.

## How it was tested

`host/tests/test_loaded.cpp`, with the real sample program: no loader, a
missing file, an empty one and a text file are each refused in words and
leave nothing behind; `hello.app` runs as a process `hello`, child of the
desktop, with its own window, and answers keys; closing the window ends
the process and unloads the program; two copies run at once; one ended
with Kill is unloaded all the same; memory blocks and processes are back
where they started. The PC gate passes (44 tests).

The firmware compiles and links with the loader (0x17b250 bytes, 38 KB
more than before: the loader and the table of names), and `hello.app` builds
for the board with every name it needs exported. **None of it has run on
the board**, and this is the step where that matters most: whether code
loaded into PSRAM runs at all with this firmware's memory set-up
(`XIP_FROM_PSRAM`, memory protection on) is what the board turn is for.

## What is left

- The board: load and run `hello.app`; memory before and after; missed
  refills while loading.
- The owner's choice of how a program reaches the calls (above).
- A loaded program on a task of its own (item 26's runner takes a pid, so
  it is a mark in the file or a rule: to decide).
- Its memory counted as the process's; an icon of its own in the File
  manager; programs in the Programs menu from a folder.
- Moving built-in programs out of the firmware into files.
