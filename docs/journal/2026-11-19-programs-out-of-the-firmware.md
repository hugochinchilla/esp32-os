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

## What is left

The File manager. The Trash, copy and paste and the folder listing stay
in the system (the desktop uses them); the File manager will ask for
them, be told when folders change, and open another folder when it is
started again.
