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
  read only for a file that is new or changed (name, size, date). The 6 KB
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
