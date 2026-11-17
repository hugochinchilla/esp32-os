# The command shell

2026-11-17. Backlog item 13. Built and tested on the PC only, where it also
runs in a terminal. It compiles and links into the firmware (ESP-IDF v5.5 in
the cloud); nothing here has run on the board.

## 1. What was asked

The same permission as for the TTY (item 12, [its entry](2026-11-17-tty.md)):

> ve haciendo, no pares hasta terminar el backlog, dejo a tu criterio el orden
> de las tareas y el paralelismo

The backlog line: "Shell with the file and process commands (Linux names, DOS
aliases, both slashes), tested on the PC". What it must be was decided after
the architecture review (`docs/DECISIONS.md`, "OS architecture"):

> **Shell**: in the portable core, commands built in, Linux names with DOS
> aliases (`dir`, `type`, `del`); paths keep drive letters and accept both `\`
> and `/`. One session is one task. It runs on a TTY object whose other end is
> the serial port, a telnet connection or a terminal window.

The choices below that were not already in that paragraph were made by the
agent and are in `docs/DECISIONS.md` as "to confirm".

## 2. What was found

- The word "shell" was taken. `core/shell/` and the namespace `shell` are the
  desktop: the taskbar, the Start menu, the programs. The command shell
  needed another name.
- Files: everything goes through `fs::Storage`, which has had a lock since
  item 9, so a second task may use it. There is no "list this folder" call
  that gives a list: `entry(folder, index)` gives one entry at a time.
- Processes: `proc::Table` is "one task only": the UI task makes and ends
  every process, with no lock. `docs/DECISIONS.md` already says what that
  means here: "A signal sent from another task (the shell's `kill`, later)
  must go through the UI message queue".
- No heap is used anywhere in the core's own objects, and a session's task
  on the board will have a small stack.

## 3. The options, and what was chosen

### The name

`core/sh/`, namespace `sh`, class `sh::Session`. Short, and what Unix calls
it. The other candidates were `cli` (says how, not what) and `console` (that
is the device, not the program).

### One blocking loop, or a state machine

- **A. `run()`: a loop that sleeps in `tty.read()`.** What "one session is one
  task" asks for. A command is a plain function that returns when it is done.
- **B. A state machine** that is given each line and returns at once. Needed
  if a session had to live on the UI task; every long command (`type` of a
  big file, `rm -r`) would have to be cut into resumable steps.

Chosen: **A**, with the loop's body also offered alone: `greet()` once, then
`step()`, which waits (or not) for a line and runs it. A terminal window that
has no task to give (the browser version has no threads) can call `step()`
from a timer. Its commands would still block the desktop while they run; that
is item 15's problem to weigh.

### DOS switches or Linux options

DOS writes options with a slash: `dir /w`. But paths were decided to take
both slashes, so `dir /docs` is a folder. One of them had to give.

- **A. Options with `-` only.** `/` is always a path. `dir /w` answers "W: not
  found".
- **B. Options with `/` only**, paths with `/` only after a letter or a dot.
  Breaks `cd /`, which every Linux hand types.
- **C. Guess**: `/` and one letter is a switch if no such folder exists.
  `dir /s` would mean two things depending on what is on the disk.

Chosen: **A**. The commands have both names; the options have one spelling.
There are few: `ls -l -a`, `cp -r`, `rm -r -f`, `kill -9`.

### One current folder, or one per drive

DOS remembers a folder for each drive, so `copy a.txt d:` goes to wherever D:
was left. That is state nobody can see. Chosen: one folder for the session;
a path with a drive letter counts from that drive's root, and `D:` alone goes
to `D:\`.

### Wildcards

`del *.txt` is as DOS as `dir`. Two ways to do it:

- **A. The shell expands them** before the command runs (Unix). Needs
  somewhere to put the list: 100 names of 60 bytes is 6 KB, with no heap.
- **B. Each command expands its own** (DOS): it walks the folder and takes
  the names that match, one at a time.

Chosen: **B**, through one helper (`each()` in `core/src/sh/files.cpp`), in
the last name of a path only. Two details:

- A star does not match a name that starts with a dot, as on Unix. Here it
  matters more than there: `del *` at the root would otherwise take
  `C:\.config` (every program's settings) and `C:\.trash`.
- Deleting while walking. `entry(folder, 3)` after deleting entry 3 is the
  one that was 4. So after a visit that removed the entry the index stays,
  and after one that did not (an error, a name that did not match) it moves
  on. `rm -r` uses the same rule on the way down.

### The process table from another task

`ps` reads the table and `kill` writes to it; both belong to the UI task.

- **A. A lock on the table.** Every use of it in the window manager would
  take the lock, for the sake of two commands.
- **B. Ask the UI task to do it**: post a message, sleep until it has run.
  `ps` formats its whole list into the session's buffer over there and prints
  it back here; `kill` calls `Table::signal()` there, which is also what
  wakes the sleeping UI loop.

Chosen: **B**, as the decision already said for signals. It is a port,
`sh::UiCall`, with one implementation over the message queue
(`sh::QueueCall`). With none, the session calls straight in, which is right
when it is on the UI task itself. The wait has no time limit: the job lives
on the session's stack, so giving up early would leave the UI task with a
pointer to nothing.

### Walking folders without a heap

`rm -r` and `cp -r` go down a tree. The plain way puts a 160-byte path on the
stack at every level. Instead the session owns two path buffers that grow by
`\name` on the way down and are cut back on the way up; a level costs one
directory entry (72 bytes). Depth is bounded by the path length.

### Smaller choices

- `rm` deletes for good. The Trash is the desktop's; a shell that moved files
  to `C:\.trash` would surprise in the other direction.
- `rm -r`, `rmdir` and `mv` refuse the folder the session is in (or one above
  it), and a drive's root.
- `mv a b` where `b` is a file replaces it in one step (`Storage::replace`),
  as Unix does; DOS would ask. Between drives it copies, then deletes, and
  only files.
- `cp` writes through `begin_save`: a copy that fails (disk full, Ctrl+C)
  leaves no half file.
- `dir` and `ls -l` share one format, DOS-like, without dates (files have
  none until item 24). `ls` alone is names in columns, a `\` after folders,
  read across. Not sorted: neither was `dir`.
- `type` turns the file's Latin-1 into UTF-8 for the terminal, since that is
  what the Editor takes a text file to be.
- Messages are in the words of the rest of the system (`fs::error_text`):
  `cat: C:\docs\x.txt: not found`.
- Left for their own items: redirection and pipes (23), `more`, dates (24),
  `top`, `dmesg`, `edit`, `open`, `run` (16).

## 4. What was built

- `core/include/core/sh/sh.hpp` and `core/src/sh/`: `sh.cpp` (the session,
  splitting a line into words, the table of commands, `help`), `path.cpp`
  (what was typed into a full path; `*` and `?`), `files.cpp`, `system.cpp`.
  A session is about 3 KB: 2 KB of working room for copying and listing, the
  two walking paths, the line.
- The commands, each by any of its names and in any case:

  | Linux | DOS | |
  | --- | --- | --- |
  | `ls [-l] [-a]` | `dir` | list a folder |
  | `cd`, `pwd` | `chdir`, `X:` | go to a folder, say where |
  | `cat` | `type` | show a file |
  | `cp [-r]` | `copy` | copy |
  | `mv` | `move`, `ren`, `rename` | move or rename |
  | `rm [-r] [-f]` | `del`, `erase` | delete |
  | `mkdir`, `rmdir` | `md`, `rd` | folders |
  | `touch`, `df`, `echo` | | empty file, drives, text |
  | `ps`, `kill [-9]` | `tasklist`, `taskkill` | processes |
  | `free`, `uptime`, `uname` | `mem`, `ver` | the machine |
  | `clear`, `exit` | `cls`, `logout` | |
  | `help` | `?` | |

- `host/console/main.cpp`: **`chinchilla-console`**, the shell in a terminal
  of the PC. The PC's terminal is put in raw mode and becomes the TTY's
  device, so the editing and Ctrl+C are Chinchilla's. With a folder as its
  argument, that folder is C:. This is the first place the TTY and the shell
  can be used by hand.

Tested (`host/tests/test_sh.cpp`, `ctest` name `sh`): paths as typed, 35
cases; wildcards; every command, with what it prints checked to the letter
and what it leaves on the drive read back, including the refusals (onto
itself, into itself, the session's own folder, a full disk, a protected
process); a session typed into through the TTY (prompt, echo, a file with no
line end, Ctrl+C, the up arrow, "año.txt", Ctrl+D); and a session on a thread
of its own with a second thread as the UI task, where `ps` and `kill` are
counted in the UI thread's inbox and a `type` of 200,000 bytes is stopped by
Ctrl+C. `tools/test_console_program.py` (`console_program`) runs the PC
program with lines piped in, on the RAM disk and on a real folder, and then
on a pseudo-terminal with keys sent as a person types them. The two-thread
tests ran 200 times in a row. All 26 PC tests pass, under the address and
undefined-behaviour checkers.

Not tested: anything on the board. Not measured: how much stack a session
needs there.

## 5. What was learned

- Writing the first real user of the TTY found two faults in it within the
  hour: the characters (Latin-1 where UTF-8 was needed) and a Ctrl+C that
  could not get past a command that was printing. Both are told in the TTY's
  entry. Neither showed in the TTY's own tests, which passed at the first
  run; that should have been a warning, not a comfort.
- The help list is checked to be no wider than 80 columns. Two lines were 82.
- The board's compiler refused what the PC's accepted. Built with ESP-IDF
  (`-O2`, GCC 14 for the Xtensa), `help` failed with "'%s' directive argument
  is null": the table of commands had no summary for the second names (`dir`,
  `type`...), and the compiler, unrolling the loop over the table, could see
  a path that printed one. The code never took that path, but the fix is to
  have no null to print: the second names now have an empty summary. The PC
  tests build without optimisation and said nothing. Worth the ten minutes
  of installing ESP-IDF in the cloud.

## 6. What is left

- Something to start it on the board: the serial console (item 14), the
  terminal window (item 15), telnet (item 17). Each gives the session a task,
  a row in the process table and a TTY.
- `top`, `dmesg`, `edit`, `open`, `run` (item 16); pipes and redirection
  (item 23). Until item 23 the shell can make an empty file (`touch`) but not
  put text in one.
- Tab to complete names: a hook from the TTY.
- Ctrl+C as the INT signal, when commands are processes.

## On the board

Checked on the board on 2026-11-17 with the rest of its branch, up to commit 2c060a3; the results are in [Run..., and open, run and edit](2026-11-17-run.md#on-the-board-added-by-the-board-thread-2026-11-17).
