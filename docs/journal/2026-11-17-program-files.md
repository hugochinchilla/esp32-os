# Program files: a header, a task, memory and the menu (backlog item 27, second step)

## What was asked

The backlog thread was retired to save tokens and this work continues in a
thread of its own, with one goal: item 27. The hand-over
(`docs/handoff/backlog.md`) lists what was left after the first step, in
order of value: the memory of a loaded program counted as its process's; a
task of its own for a loaded program ("a rule or a mark in the file: ask
Hugo"); a picture and a Programs menu entry for programs found in a folder;
moving built-in programs out of the firmware.

On the same day the owner set a rule for the repository: "añade una regla
respecto al idioma, el código en el repo y cualquier documentación en el
repo se escriben en ingles". It is in `CLAUDE.md` now.

## What was found

- A loaded program was entered on the UI task and stayed there, while
  every built-in program but the Terminal has had a task of its own since
  item 26. A loaded program that hung would hang the desktop.
- The loader's memory (the program's code and data in external RAM) was
  nobody's: the Task Manager showed only what the program's windows asked
  for.
- The system knew nothing of a program file without loading it: no name
  but the file's, no picture, no stack figure. The built-in programs carry
  those in the program table (`shell::App`), which a file has no row in.
- The way into a loaded program was called with a pointer into the
  caller's stack. On a task of its own, with the two-second limit, a
  program late in its way in would have written through it after the
  caller had gone.

## The options

Three choices were put to the owner on cards, each with a recommendation.
The work was built on the recommended ones while he answers; all three can
be changed without undoing the rest.

| Question | Built (recommended) | The others |
|---|---|---|
| Which loaded programs have a task | **All of them**, with the stack the file asks for (4 KB if it says nothing). It is his rule for item 26 applied to files, and a program from outside is the one most worth being able to end when it hangs | Only those that mark it (the rest hang the desktop when they hang); none until the board says what a task costs |
| What a `.app` carries besides the code | **A header of ours at the front**: name, 16x16 and 32x32 pictures, stack. The menu reads 1.3 KB and loads nothing; moving a built-in program out needs exactly these three things | The ELF alone, one picture for all and the file's name; or the picture in a second file beside it |
| Where Start > Programs finds them | **`C:\Programs`, flat**, read as the menu opens | Subfolders as submenus, as Windows 95 had (more work; can be added later); or leave the menu alone |

Where the header comes from was the agent's to settle. A manifest file
beside the source, packed by a Python tool, was the other way; the one
built keeps it in the program's source, `APP_INFO(app::file_info("Hello",
0, kSmall, kInks))`, worked out by the compiler into a section of its own
(`.appinfo`) that `objcopy` copies to the front of the file. The picture is
drawn as rows of text exactly as the system's own are, and nothing but the
compiler's own tools is needed.

## What was built

- **The header**, `core/include/core/app/file.hpp`: `app::FileInfo`, 1,332
  bytes: magic `CHINAPP1`, its own size, the stack, a name of 31 letters,
  the see-through colour and the two pictures. `APP_INFO` in `app.hpp`.
  `tools/pack_app.sh` makes the file: header, then the code. The PC build
  and `tools/build_programs.sh` both use it. No older form is understood:
  the bare ELF of the first step is refused as "not a program".
- **`shell::program_info(path, info)`** reads the header alone.
- **`shell::run_file`** now: checks the header, hands the loader what
  follows it, names the process as the program names itself (`Hello`, not
  `hello`), counts what the loader took as the process's memory
  (`ports::Loader::bytes`; on the board the difference in free heap around
  the load, since Espressif's loader does not say), gives the process a
  task (`prog::runner().start(pid, info.stack)`) and calls the way in
  there. The result of the way in is kept in the system's own table, not
  on the caller's stack; a program late in its way in is left to it and
  looked at again every 100 ms.
- **Unloading waits for the task.** A program ended while it was late
  keeps its task until the task can go (`prog::Runner::task_left`); its
  code is not taken out of memory before that.
- **Start > Programs** (`core/src/shell/desktop.cpp`): as the menu opens
  (`wm::Wm::set_start_menu_opening`) the taskbar lists `C:\Programs` and
  adds an entry for each program file, eight at most, with the header's
  name and small picture. The folder is listed every time; a header is
  read only for a file that is new or changed (name, size, date). The 4 KB
  it needs are asked for the first time the menu opens and given back when
  the taskbar ends.
- **Pictures.** `gfx::program_icon_16/32`, original: a window with a title
  bar and lines of text. It stands in the menu for a program with no
  picture of its own, and in the File manager for every `.app`.
  `programs/hello` has a picture of its own.

## How it was tested

`host/tests/test_loaded.cpp`, with the real `hello.app`: the header is
read without loading; a bare ELF with no header is refused; the process is
`Hello` and its memory figure includes the code; `C:\Programs\hello.app`
appears in the menu's Programs entries with its name and picture (twice
over, the second time from what was remembered), a text file and a broken
`.app` beside it do not, the entry runs it, and it leaves the menu when the
file is deleted; with a real thread as its task the program starts, answers
a key, closes, and is unloaded only after the task has ended. The PC gate
passes.

The firmware compiles and links with the change in the cloud container
(0x17beb0 bytes of 0x1f0000, 3 KB more than the first step), and
`hello.app` builds for the board: 6,480 bytes (3,712 before; the header is
1,332 and the program holds a second copy of it, with its picture), every
name it asks for among the 296 exported.

**Not run on the board.** Whether loaded code runs there at all is still
the open question of the first step; the board turn for it was asked for
before this step began, on the first step's commit.

## What was learned

- `__attribute__((used))` on an `extern` declaration is ignored (and an
  error under `-Werror`); it goes on the definition. A symbol with default
  visibility in a shared object is kept by `--gc-sections` by itself.
- The first step's test passed with the process named after the file; the
  name is now the program's own, which is what `find_by_name("Editor")`
  and the like will need when built-in programs move to files.

- The board's compiler caught what the PC's did not: a process's name is
  shorter (24) than a program's (32), and `-Werror=format-truncation`
  refused the `snprintf` that cut one to the other. The cut is meant; it
  is now written so.

## What is left

- The board: everything above, after the first step's proof.
- The owner's three answers.
- Each program's own picture in the File manager (the listing would have
  to keep pixels).
- Moving built-in programs out of the firmware into files, and with it how
  programs reach a board whose C: has been wiped: to put to the owner.

## The board's first answer (the same day)

The board turn asked for on the first step's commit came back: **loaded
code runs from external RAM.** The table of 294 names was taken, `hello.app`
opened with its window, its labels and its buttons. Then the first press of
"Press me" stopped the board, twice of twice: `LoadStoreError` in
`_svfprintf_r`, called by `snprintf`, called by the loaded program, reading
0x421ED179, an address in instruction space.

What was found, reading the loader (Espressif's `elf_loader` 2.1.0,
`esp_elf_map_sym` in `src/esp_elf.c`): on the ESP32-S3 it keeps the code
and the data apart, and decides whether an address of the file is code by
`.text`'s size **rounded up to a whole word**. `hello`'s `.text` was 0x4d1
bytes and its `.rodata` began on the very next byte, so the first three
bytes of read-only data counted as code. The first string there was the
format `"Pressed %d time%s"`: `snprintf` was handed its address in
instruction space, which cannot be read a byte at a time. The address
fits: 0x421ED179 less 0x4d1 is 0x421ECCA8, a whole word, where the code
began. Nothing else of the program was wrong, which is why the window and
its fixed labels (strings further on) were fine.

Options: change the loader (a copy of ours to keep in step with
Espressif's), or build programs so that the case cannot arise. The second
was done: `tools/program.ld`, added to the linker's own script, starts
`.rodata` on a word of its own, and `tools/check_program_elf.py`, run by
`tools/build_programs.sh` on every program, refuses one in which any
section begins inside the last word of `.text`. `hello`'s `.rodata` is now
at 0xa34. Not run on the board yet; a new turn is asked for.

The loader's own serial lines were not captured and `C:\crash.txt` was
lost in that turn; the next one asks for both.

## How the programs reach the board (the same day)

Asked on a card, "Elige cómo llegan a la placa los programas sacados del
firmware", the owner chose **"Dentro del firmware, se instalan"**.

| Option | Chosen | Why, or why not |
|---|---|---|
| **In the firmware, installed at start-up**: the firmware carries the `.app` files as data and copies them to `C:\Programs` when they are missing or from another build | Yes (the owner) | Nothing of the partitions or the saved Wi-Fi networks is touched; a wiped C: and an update over the network both end with the programs in place. It saves no flash: the files are in the firmware and again on C: |
| A system partition of its own, read-only, written beside the firmware | No | The firmware would shrink, but the partition table changes and the update over the network does not write it |
| Copied with `dbg.py` after flashing | No | The simplest, but a wiped C: leaves the board without those programs |

Built:

- `tools/gen_bundle.py` writes the program files as a C source file with a
  stamp, the first 16 hex digits of the SHA-256 of their names and bytes.
- The firmware build (`firmware/os/main/CMakeLists.txt`) now builds
  `programs/*` itself with its own compiler, checks them against its table
  of exports and carries the result: `hello.app` today. The firmware is
  0x17da90 bytes of 0x1f0000 (7 KB more: the file, the installer).
- `shell::install_programs()` (`core/src/shell/loaded.cpp`), called by
  `shell::start`: if `C:\Programs\installed.txt` holds this build's stamp
  and every file is there with its size, nothing is written (the cost is
  one small read and a `stat` a file). Otherwise the files that are
  missing, of another size or of another set are written, then the stamp.
  A file that cannot be written leaves no stamp, so the next start tries
  again. A bundled program the user deletes comes back at the next start.

Tested on the PC (`test_loaded`): installed at start; nothing written the
second time; a missing file and one of the wrong size put back; another
stamp writes the set again, once. The firmware compiles and links with it.
**Not run on the board.**

No built-in program has moved yet. The two games are first, once the board
has run `hello.app` through a press of its button.

A correction to the PR's first description: the one-glyph difference in
Terminal pictures was not an old instability, it was this change. `ps` in
those scenes shows the taskbar's memory, and the taskbar now holds the list
of program files once the Start menu has opened: 1K became 6K. The header
being read was moved out of that block (asked for and given back around
each read), so the figure is 4K, and the four reference pictures that show it
(`host/tests/pictures/45-terminal-commands.png`,
`46b-terminal-maximized.png`, `34b-task-manager-all.png`,
`34c-task-manager-protected.png`) were drawn again: one digit each.

## The board's second and third answers (the same day)

With the link fix the board ran everything of this entry: `Hello` is the
last entry of Start > Programs with its picture, "Press me" counts to
five, the process has a task of its own (2,260 bytes of its 4 KB stack
never used), the Task Manager shows 8K a copy, internal memory comes back
exactly over five rounds, End Task on one of two copies leaves the other
working, a text file named `x.app` has no menu entry and is refused in
words, and no refill was missed. The firmware installs `hello.app` and
puts it back after it is deleted.

One thing to change: the desktop came up 83 ms later than on `main`. The
install check is no longer made on the way to the desktop but two seconds
after it (`docs/journal/2026-11-17-folder-listing.md` has that turn's
other finding).

## The two games leave the firmware (the same day)

Moving built-in programs out is the point of item 27, and it raised a
question the owner had not been asked: the PC simulator and the build that
runs in a browser cannot load program files, so a program that is only a
file is gone from both. Asked on a card, "Decide cómo sacar Culebra y
Cactus Patch del firmware a archivos", he chose **"Archivo en placa,
integrado en PC"**.

| Option | Chosen | Why, or why not |
|---|---|---|
| **A file on the board, built in on the PC**: one source, compiled as a `.app` for the board and into the system for the PC simulator and the browser | Yes (the owner) | The board's firmware is where the room matters; the games stay in the browser; the 450 lines of tests of the games keep their worth |
| A file everywhere | No | The simulator would need the PC's loader, the browser build would lose the games, and their tests and reference pictures would be written again |
| Leave them built in | No | Files only for new programs: item 27 would end here |

Built:

- `core/src/shell/culebra.cpp` and `cactus_patch.cpp` end in two forms.
  With `APP_FILE` defined they have `APP_INFO` and `APP_MAIN`; without, the
  `open_...` function of the program table and the tests' accessors, as
  before. `programs/culebra/culebra.cpp` and
  `programs/cactus_patch/cactus_patch.cpp` are three lines each: they
  define `APP_FILE` and include the game (`core/src/games/`) and its
  window, so each program is one file with its own copy of its game.
- On the board (`core/CMakeLists.txt`, `CHINCHILLA_GAMES_IN_FILES`) the
  four sources are not part of the core library and the two rows are not
  in the program table. The firmware carries `culebra.app` (14,748 bytes)
  and `cactus_patch.app` (19,500) beside `hello.app` and installs them.
  The firmware is 0x182ae0 bytes: 19 KB **more** than with the games built
  in, as the owner's choice of delivery said it would be (the files are
  data in the firmware, with a 1.3 KB header and their own tables each).
  What is gained is not flash: the games' code is in memory only while
  they run, and they are replaced without a new firmware.
- **The pictures** of the two games moved to
  `core/include/core/gfx/game_art.hpp` as rows of text; `art.cpp` makes
  the PC's bitmaps from them and the files' headers are written from the
  same rows. The test compares the two byte for byte.
- **One copy at a time.** The header's spare byte is `many`: 0, the
  default, and a program opened again has its window brought forward
  instead of being loaded twice (`shell::run_file`). `hello` says
  `app::many(...)`.
- **`os::launch(program)`**, a new OS call: Cactus Patch opens About
  Chinchilla from its Help menu, and reached for the program table to do
  it. `docs/os-calls.md` has it.
- **The check of exported names was checking nothing.**
  `tools/gen_exports.py --check` asked `nm -u` what a program needs, and a
  stripped program file answers nothing to that: every program passed. It
  asks `nm -D -u` now. The first honest run found what Culebra and Cactus
  Patch need beyond the OS calls: the settings file (`fs::Ini`,
  `shell::load_settings`, `save_settings`) and rectangles (`rect.cpp`).
  They are exported: 319 names. `hello` had been fine by luck.

Tested on the PC (`test_loaded`): each game's file carries the name and
both pictures of the built-in one; it runs through the loader as its own
process, takes a key, lives through two seconds of its timer; opened again
nothing is loaded and its window is in front; closed, it is unloaded. The
gate passes; the firmware compiles and links without the games in it and
with the three files. **Not run on the board**: there the two games have
only ever run built in. Their stacks are the 4 KB minimum, of which the
board saw about 1.4 KB never used when they were built in.

What is left: the board; then the other programs, which need more than
the games did (a path to open handed to `APP_MAIN`, programs that call
each other by name).
