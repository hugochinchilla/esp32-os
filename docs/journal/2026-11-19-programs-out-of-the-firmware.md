# The programs leave the firmware

2026-11-19. Backlog item 82. Decision:
[The programs leave the firmware](../decisions/programs-out-of-the-firmware.md).
This entry covers the first step; the later steps add to it below.

## What was asked

After the files of C: began to be copied by the flash script (item 77), the
owner asked which programs could follow the games out of the firmware, and
answered the list himself:

> si, la terminal, vizcacha, editor de textos, desfragmentador, picture
> viewer, task manager, window gallery, incluso explorer (si no es
> complicado por lo de la papelera)

and, in the next two minutes, about the other two machines:

> y en el navegador la carpeta Programs ni existe

> haz lo mismo en linux, todo debería ser igual, deberiamos intentar que
> solo los drivers HAL sean diferentes

## What was found

- Only the games and Hello were program files, and only on the board. On
  the PC and in the web page the same sources were compiled into the
  system (`APP_FILE` chose between two endings of each source file,
  `CHINCHILLA_GAMES_IN_FILES` took their rows out of the program table on
  the board). So the three machines did not show the same thing: the
  board listed the games from `C:\Programs`, the other two from the
  table, and their C: had no `Programs` folder at all.
- A program file got no arguments (`Loader::enter(module)`), so it could
  not be told which file to open. The Editor, the Picture viewer, Vizcacha
  and the File manager all need that.
- A program was started by name only through the built-in table
  (`find_app`): Run, the shell's `run`, `os::launch` and the taskbar's
  quick launch would not have found a program that had become a file.
- Start > Programs showed the table first and then at most eight files.
- What each program asks of the system beyond the OS calls, which is what
  decides how hard it is to move:

  | Program | Asks for | Note |
  | --- | --- | --- |
  | Widget gallery | `shell::open_about` | one call, replaced by `os::launch` |
  | Defragmenter | nothing | its pretend disk is its own |
  | Editor | `fs::path`, `job::`, the file dialogs | needs an argument |
  | Picture viewer | `image::` (the decoders), `fs::Ini` | the decoders stay in the firmware: the desktop's background uses them |
  | Terminal | `tty::Screen`, `sh::TerminalSession`, `msg::` | the screen is the Terminal's alone and can go with it |
  | Vizcacha | `www::` (HTML, HTTP, addresses), `image::`, `os::tcp` | `www::` is used by nothing else: about 30 KB that can leave with it |
  | File manager | the Trash, copy and paste, the listing it shares with the desktop | see below |
  | Task Manager | `proc::` figures | stays (the owner's choice) |

- The File manager and the Trash: the desktop itself uses the Trash (its
  icon, a drop on it), copy and paste and the folder listing, so that code
  cannot leave with the File manager. It does not have to: it stays in the
  system and the File manager asks for it. The work is that the desktop
  opens folders (`open_folder`), tells folder windows to read again
  (`refresh_folder_windows`) and takes drags from them. That is the
  largest of the moves and comes last.

## The options, and what was chosen

Two choices were the owner's and went to him as cards; both were answered
within minutes.

**Where the Task Manager lives.** (a) A file that Ctrl+Alt+Del loads: the
firmware loses 429 lines, but with C: empty or broken, or no memory left,
it does not open and there is no way to end a program that hangs. (b) It
stays in the firmware. (c) A file, with a minimal list kept in the firmware
for when it cannot be loaded: two things to keep. Recommended (b); the
owner chose (b), "Se queda en el firmware".

**How the web page loads programs.** A page cannot load machine code from
a file. (a) The programs are compiled into the page, its `C:\Programs`
gets the same files, and its loader driver joins each file to the code
that is already there: the desktop is the same, the page does not shrink.
(b) Each program a real WebAssembly module, fetched and joined when
opened: truer, but the page's base grows with Emscripten's dynamic
linking and it is much more work. Recommended (a); the owner chose (a),
"Cargador que enlaza lo compilado".

The agent's, to confirm (the decision file lists them): the Linux program
loads real program files with the PC's loader and copies them to C: as it
starts; one list of programs in the order of the names; `argv[1]` is what
to open; a program's pictures move into its own folder.

Not done, and why: keeping the two forms of each program and only adding
a `Programs` folder to the PC and the page. It is the least work, and it
is exactly what the owner's second message rules out.

## What was built

- `core/app/linked.hpp`, `core/src/app/linked.cpp`: the Loader for a
  machine that cannot load code. A program compiled with `APP_LINKED` set
  to its file's name puts itself on a list before `main()` (`APP_MAIN`
  and `APP_INFO` do it); `install_linked` writes `C:\Programs\<name>.app`
  for each, the program's real header followed by `linked:<name>`; the
  loader finds the code by that name. `shell::start` does both by itself
  when the list is not empty, so the web page and the PC's tests need no
  code of their own.
- `programs/linked.cmake`: compiles every folder of `programs/` that way,
  for the tests (`host/CMakeLists.txt`) and the page (`web/CMakeLists.txt`).
- The Linux program (`host/desktop/main.cpp`): `host::DlLoader`, until now
  only the tests', is its loader; `host/program_files.hpp` copies
  `programs/*.app` from beside the program to `C:\Programs` where they
  changed. The build makes those files for every folder of `programs/`
  and keeps all of the core in the program with its names visible, since
  a program finds the OS calls in it by name.
- `core/src/shell/programs.cpp`: the one list. `program_count`,
  `program`, `find_program` (by name, or by the only name that starts so)
  and `start_program`. Start > Programs, Run, the shell's `run` and
  `os::launch` use it. The reading of the headers moved here from
  `desktop.cpp`.
- Arguments: `Loader::enter(module, argc, argv)` on the three loaders,
  `shell::run_file(path, why, capacity, argument)`. The words are kept
  with the loaded program and given back when it is unloaded.
- The games have one form: `programs/culebra`, `programs/cactus_patch`,
  `programs/chess`, each with its rules beside its window
  (`core/src/games/` is gone, as are `APP_FILE` and
  `CHINCHILLA_GAMES_IN_FILES`).
- The Widget gallery and the Defragmenter are `programs/gallery` and
  `programs/defragmenter`, with their pictures in `art.hpp`; their rows
  left the program table and their pictures left `core/src/gfx/art.cpp`.
- `tools/gen_exports.py --pc` and the PC test `program_exports`: tells on
  the PC that a program asks for a name the firmware does not export. It
  found one at once: the Widget gallery called `shell::open_about`.
- `tools/check_os_calls.py` checks every file of `programs/`.

## How it was tested

On the PC: the 59 tests pass (`program_exports` is new; Doom's came with
the merge of `main`, where it had arrived meanwhile). The
tests have the programs linked in, so they start them by name from
`C:\Programs` as a user would; `test_loaded` loads the real program files
of the PC with the PC's loader, as the Linux program does, and checks the
copy to `C:\Programs` (written once, not again, written again when a file
differs).

55 reference pictures changed and were looked at: the root of C: has a
`Programs` folder (four folders, so `readme.txt` is on the second row);
the free space shown is a few kilobytes less; Start > Programs is in the
order of the names with each program's own picture; the Terminal's `dir`
lists the folder.

With the board's compiler (in the cloud): `tools/build_programs.sh` builds
the six program files. Hello 6,480 bytes, Widget gallery 10,700,
Defragmenter 11,132, Culebra 15,064, Cactus Patch 19,500, Chess 52,160.

Not compiled: the Linux program and the web page (the cloud container has
neither SDL2 nor Emscripten); their parts that need no window are the
ones `test_loaded` and the linked tests cover. Not run on the board.

Doom (item 81) was merged into `main` while this was built. It is a
program file like the others on the board and in the Linux program, but it
is not linked into the tests or the web page: its engine is C that stands
in for the C library and can only live in a file of its own. The folder
says so by having an `engine.txt`.

## What was learned

- The files the system writes for linked programs make a notice from the
  file system, and the notice starts a timer. Written before the desktop
  had its process, that timer belonged to nobody, and a test that counts
  timers by owner said so.
- A loaded program now keeps one more block while it runs (its
  arguments): a test that counted the blocks given back when the
  Defragmenter closes had to count eleven instead of ten.

## Step 2: the Editor, the Picture viewer and the Terminal

The same day. These three are the first programs that are told what to
open, and the first of which several run at once.

**Opening something in a program that is already running.** Two cases,
two answers:

- The Editor runs many copies (`app::many`). Asked for a file that one of
  them has open, starting another would show the same file twice. Each
  window says which file it has (`os::ui::set_document`); the system
  looks among the windows of the program's copies before it loads
  another, and brings the one that has the file forward.
- The Picture viewer runs one copy. Started again with another picture it
  must show that one. It hands the system a function
  (`os::accept_open`), which is called as the program with the new
  argument. Considered and not done: a message for the running copy to
  read when it next turns, which would need the argument kept by the
  system in between; the direct call needs nothing kept.

**Three Terminals.** The limit was an array in the system. A program file
keeps nothing between its copies, so each Terminal counts the others in
the process list and takes the first of the three places none of them
has. The fourth is now a process for the moment its message is on the
screen (before, it never became one); the test says so.

**The grid goes with the Terminal.** `tty::Screen` (the 80 by 25
characters, the history, the escape codes) was used by the Terminal
alone: its source is `programs/terminal/screen.cpp` now. The session, the
TTY and the shell stay in the system; the Terminal reaches them through
`os::open_session`, and the session's own functions are exported to it.

What was built: `programs/editor`, `programs/viewer`,
`programs/terminal`, each with its pictures in `art.hpp`; the new calls
above and `os::ui::has_dialog`; `shell::open_in_editor` and
`open_picture`, which the File manager and the desktop use, start the
programs by name. Their rows left `core/src/shell/apps.cpp`.

What the tests and the compiler found:

- The picture decoder's and the converter's constructors were written by
  the compiler into whoever used them, and with them a need for the file
  system's own functions. They are made and ended in the system's code
  now, so that a program needs only `image::`.
- The debug link read the Picture viewer's timings out of the viewer's
  own variables. The PC did not mind (its tests have the viewer linked
  in); the board's compiler refused it. The figures are the system's now
  (`os::counters()`), and the viewer writes them there.
- `TerminalSession::set_size` was written in its header and so asked the
  program for the TTY's code; it is in the system's source now.
- The PC's program files are large (built with the checkers, 100 to 600
  KB each): the test that copies them to a C: of 4 MB ran out of room at
  the tenth. Its C: is 16 MB.

## Step 3: Vizcacha

`programs/vizcacha`: the browser's window and, with it, the three
sources of `core/src/www` (addresses; HTML and its layout; HTTP), which
nothing else in the system used. Its headers stay in `core/include` for
the tests of those parts. It runs one copy on a task of its own (12 KB
of stack, said in its file's header), and an address it is asked for
while it runs opens in its window (`os::accept_open`). The File manager
opens a page on a drive with it by name.

Found by the board's compiler: the firmware did not export the C
library's `isalnum`, `isalpha`, `isspace`, `isxdigit`, `tolower` and
`strcspn`, which the HTML reader uses. The PC's test of the exports
checks this project's own names only, so it could not tell.

Run takes a program and what it is to open ("editor readme.txt",
"vizcacha http://example.com"): the longest beginning of what was typed
that names a program is the program, the rest its argument, and a file of
the drives goes as its whole path.

The test that opens and closes every program and counts the memory
(`test_os`) went through the system's table; it goes through the one
list now, files included.

## The firmware, compiled after each step

In the cloud, with the board's compiler; none of it has run.

| | Firmware | Less than `main` |
| --- | --- | --- |
| `main` (ad36da3) | 1,699,392 bytes | |
| Step 1: games, Widget gallery, Defragmenter | 1,690,272 | 9,120 |
| Step 2: Editor, Picture viewer, Terminal | 1,674,368 | 25,024 |
| Step 3: Vizcacha, HTML and HTTP | 1,627,216 | 72,176 |

The program files, for the board: Hello 6,492 bytes, Widget gallery
10,704, Defragmenter 11,172, Editor 13,488, Terminal 14,872, Culebra
15,104, Picture viewer 17,708, Cactus Patch 19,504, Chess 52,252,
Vizcacha 56,680, Doom 357,688. "exports: 456 names; 11 program(s)
checked, 0 name(s) missing".

On the PC: the 59 tests pass after each step. The reference pictures
that changed (about 48 at each step) changed in the free space shown
and nothing else: one more file in `C:\Programs`.

## Step 4: the File manager

The owner's "incluso explorer (si no es complicado por lo de la
papelera)". It was not, because the Trash already had a source of its own
(`trash.cpp`) and so had the file commands (`file_ops.cpp`) and the
listing: the File manager only called them.

**What goes and what stays.** The window on a folder (toolbar, address,
the three views, the menus, the drags) is `programs/file_manager`. The
Trash, Delete, Copy, Cut and Paste, a drop on a folder and the listing of
a folder stay in the system: the desktop's Trash icon, its own icons and
the file dialogs use them. `core/shell/files.hpp` gathers what the File
manager asks of them, and the firmware exports those names to it.

Considered for that boundary:

- (a) Export the system's functions by name, as the message boxes and
  the file dialogs already are. Chosen: nothing is rewritten, and what
  the File manager may ask is one header.
- (b) Make each of them an OS call (`os::trash::...`). Not done: an OS
  call is a promise to every program, and no other program is meant to
  empty the Trash or paste files; the header can become that the day a
  second program needs it.
- (c) Move the Trash into the File manager and have the desktop ask the
  program. Not done: the desktop would lose its Trash icon whenever the
  file is missing, and a drop on it would have to start a program.

**One program, many windows.** The folder windows were one process and
still are: the program runs one copy, and `shell::open_folder` starts it
by name with the folder as its argument. Started again while it runs, it
is handed the folder (`os::accept_open`) and opens another window, or
brings forward the one that shows it.

**Told when folders change.** The desktop gathered the file system's
notices and called the File manager's `refresh_folder_windows` itself.
That is `os::file::watch` now: a program says it shows folders and is
called, as itself, with the folders that changed.

**Not reading the file to find it running.** `run_file` read a program's
whole file before it looked whether the program was running already. For
a folder opened from the desktop that was the File manager's 26 KB read
from the flash each time, to do nothing with them. It reads the header
first now, and the rest only if it must load. The same pass no longer
brings the program's main window forward before the program is told what
to open: the program brings forward the window it chooses (else opening
a folder raised an unrelated folder window first).

Which program opens which kind of file (`open_file`, by extension)
stayed in the system, in `run.cpp`: the desktop's icons and Run ask it,
and the File manager asks it for the words of its Kind column
(`file_kind`).

Tested on the PC: 59 of 59. `test_shell` (the File manager's menus, the
Trash's view, copy and paste, the change notices), `test_lifecycle` and
the pictures all drive it through `shell::open_folder` as before and did
not have to change, beyond one line that opened My Board from the
system's table. One reference picture changed beyond the free space: a
process number in the Task Manager (30 where it was 28).

## The firmware after step 4

Compiled in the cloud, not run: 1,614,192 bytes, 85,200 less than
`main`. (Step 3 with Run's arguments was 1,627,408.) `file_manager.app`
is 26,160 bytes. "exports: 479 names; 12 program(s) checked, 0 name(s)
missing".

The firmware shrank less than the programs weigh (about 180 KB of files
without Doom), because much of what a program is made of was never its
own: the window manager, the widgets, the decoders and the file system
stay.

## After the first board turn (2026-11-20)

The board ran the branch at `fac6291`. The fifteen programs are in Start >
Programs and open, the memory comes back, and the Linux program starts on
the laptop. Six things were reported; what was done with each:

- **Run "gallery" found nothing.** The program is called Widget gallery,
  and Run looked for a name that starts with what was typed. The board
  request itself had promised "gallery", wrongly. Run, the shell's `run`
  and Run with an argument now also take the name of the program's file
  ("gallery", "viewer", "file_manager") and, when no name starts so, the
  only name with a later word that does ("gallery", "patch", "viewer";
  not "manager", which two have).
- **The File manager had 3,120 bytes of its 8 KB of stack never used.**
  12 KB now. The stack is internal memory, which fell to 40,288 bytes
  with three Terminals open, so it is not made larger than that without
  a measurement: the next turn reads both figures again.
- **`flash.py --files-only` sent the game data at every run**, six
  minutes each. Not reproduced on the PC: the tool's test, with a file
  whose sum and size are known beforehand, sends nothing the second time.
  Two things were done. A file whose name, size and sum are fixed (the
  game data only) is taken as there when the board has a file of that
  name and exactly that size, whatever the board's list says: the list
  can be lost, or the tool stopped during the six minutes, after which
  it had already struck the file off the list. And the tool now says why
  it sends each file ("not on the board", "1234 bytes on the board",
  "not in the board's list", "changed"), so the next run names the cause
  if it happens again.
- **`cactus_patch.app` has no date in Details.** Not this change's: it is
  the first file the tool sends, three seconds after the start, before
  the network has told the board the date, and a file written without a
  date shows none. Backlog item 83.
- **FrogFind's logo is not shown** ("One picture could not be shown").
  Not this change's either: the logo is a GIF and there is no GIF
  decoder, as the journal of the pictures in pages says ("GIF stays a
  box", 2026-11-18).
- **`dbg.py` had no `ps` and no `counters`**, which the board requests
  asked for. Both exist now: `ps` is a new request of the debug link
  (0x1A: the process table as text, a line each), `counters` prints the
  three figures of `os::counters()` that `stats` already carried.

PC: 59 of 59, with new checks for each of the three that are code.

## After the second board turn (2026-11-20)

The re-test at `5adaae0` passed every fix, and the drags, the Trash, Cut
and Paste and the timings. It found two things.

**Renaming a file to a name that was taken wrote over the other file.**
`b (2).txt` renamed to `b.txt`: the first `b.txt` was gone. A folder onto
a folder was refused, but as "the folder is not empty". Not this branch's
(the File manager's code is the same as on `main`), and not in the File
manager: the drive's code asked whether the new name was taken with
`access()`, and ESP-IDF's LittleFS layer has no `access()`; there it
always answers no. So on the board `rename` took every name for free, and
the C library's rename then replaced the file. The PC, where `access()`
works, refused as it should, which is why no test saw it. The same call
decided whether a path could be used as written, so every path on the
board was being looked up by reading its folders.

Both ask with `stat()` now (`core/fs/posix_volume.hpp`). The File
manager, the desktop's own rename and the shell's `ren` and `mv` all go
through it. The File manager's message for a taken name is "There is
already a file or a folder named 'b.txt' here. Choose another name.", for
files and folders alike. Paste and a drop were looked at: both ask the
system whether the name is there with `stat` already (`exists()` in
`trash.cpp`, `free_name_in` for Paste, which numbers the copy), so they
could not write over a file. Doom's saved games delete the old file
before they rename, on purpose.

**27,680 bytes of internal memory at the least**, with three Terminals,
Vizcacha and three folder windows. One cause was this branch's: the
Terminal's file asked for a task of its own with 8 KB of stack, which the
Terminal never had (its window is a device and ran on the UI task; its
session has the task). Three Terminals were 24 KB of internal memory for
nothing. It asks for none now. Other margin, not taken without a
measurement: the stacks of the programs' tasks could come from external
memory (a board experiment: a task whose stack is there must not be the
one writing to the flash), and the File manager's 12 KB can be trimmed
once `dbg.py tasks` has shown what it uses after a deep copy.

PC: 59 of 59, with a File manager test that renames onto a taken name.

## After the third board turn (2026-11-20)

At `5ddf6e5` the File manager's rename, a folder's, Paste and the drags
were right, and opening `C:\Programs` in Details went from 1,157 to 773
ms (paths are no longer looked up by reading folders). Two things were
left.

**The shell's `ren` still wrote over a file.** That one was written so:
`ren`, `rename`, `mv` and `move` are one command, and the command shell's
journal (2026-11-17) chose "`mv a b` where `b` is a file replaces it in
one step, as Unix does; DOS would ask". The drive's fix did not touch it
because it calls `replace`, not `rename`. The options:

- (a) `ren` and `rename` refuse a name that is taken ("already exists");
  `mv` and `move` keep replacing, as Unix does and as was chosen. Built:
  the word that means "give a new name" loses nothing, and the one a
  script uses to put a file in place of another still does it.
- (b) All four refuse; a file is replaced with `rm` first. Safest, and it
  takes back the choice of 2026-11-17.
- (c) All four replace, as it was: the board's finding stands.

Built (a) and put to the owner on a card, since (b) reverses a recorded
choice. The owner chose (a) the next morning: "ren rechaza, mv
reemplaza".

**The Terminal still had a task of its own**, 4 KB each: a stack of 0 in
a program's header does not mean "no task" but "the least there is". A
program built into the system could go without one (the Terminal's row
did); a file could not say so. It can now: `app::no_task(...)` writes a
value in the header's stack that the loader takes as "serve its windows
where the desktop's are served". Only the Terminal uses it.

**The File manager's stack is 10 KB**, from what the board measured:
about 5.1 KB used after drags, the Trash, Paste and a copy of 355 KB.

PC: 59 of 59, with `ren` onto a taken name in the shell's test.

## After the fourth board turn (2026-11-20)

At `af84df2`: `ren` of files refuses, `mv` replaces, no Terminal task, and
the least internal memory with the usual windows was 47,348 bytes (27,680
two turns before). One thing left: `ren two one`, two folders, left only
`one`. Nothing was deleted: the command took `one` for a place to move
into, as `mv` does, and `two` was inside it. For `ren` a folder of that
name is a name that is taken: it says "already exists" now, for a file or
a folder being renamed. `mv two one` still moves into it.

## What is left

- The board has run none of this. Two turns are asked for: steps 1 to 3
  at one commit, step 4 at the next.
- The Linux program and the web page have not been compiled (no SDL2 and
  no Emscripten in the cloud). The owner's laptop has both.
- With an empty C: the desktop's My Board, Documents and Trash icons say
  that the File manager is not on the machine. A way to get the programs
  onto a board without a computer is backlog item 78 (the store).
- `core/include/core/games`, `core/www` and `core/tty/screen.hpp` are
  headers of code that now lives in `programs/`; they stay there for the
  tests that include them.
