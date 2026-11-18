# The desktop stops reading its folder every two seconds

2026-11-18. Backlog item 65. Built and tested on the PC (51 of 51 tests).
Nothing here has run on the board: the figures that prove it come from the
board turn. Whether the firmware compiles in the cloud is said in the pull
request.

## What was asked

The 60 fps benchmark (backlog item 63,
[journal](2026-11-18-frame-budget.md)) found that an idle desktop stands
still for 35 ms every two seconds, 84 ms just after a start: a timer reads
`C:\Documents\Desktop` again to see whether the icons changed. The owner,
in the project chat:

> veo que ya tenemos un problema solo por listar los ficheros de Desktop, es
> algo que podamos solucionar con el listado en un proceso en segundo plano
> o implementando algo similar a inotify?

He was offered both together and chose, on a decision card, to fix this
before the other finding of the benchmark (painting large windows).

## What was found

- `refresh_desktop_files` in `core/src/shell/desktop.cpp` ran from a 2 s
  timer on the desktop's task, whatever else was going on. It was chosen on
  2026-11-16 so that a file put on the desktop any way (the File manager, a
  save, `tools/dbg.py put`) would show "without hooks in every program that
  writes files" (`docs/decisions/desktop-files-details.md`).
- **It walked the folder once for every file in it.** It asked for the
  files one at a time by index (`Storage::entry(folder, i)`), and on the
  board's drive (`fs::PosixVolume`) each such call opens the folder, steps
  to the i-th name and closes it again, after checking each part of the
  path. `trash_count()` did the same in `C:\.trash`, counting every item
  only to know whether there is at least one. With n files on the desktop
  and m things in the Trash that is n + m + 2 walks every two seconds. The
  File manager had the same fault and lost it earlier
  (`Volume::entries`, one walk for a whole listing); the desktop never
  moved over. This is read from the code and counted on the PC (the test
  drive counts the walks); how the 35 ms divides between the walks was not
  timed on the board.
- Nine places in the shell called `files_changed()` by hand after moving or
  deleting something, so that the desktop and the File manager's windows
  would not wait for the timer. Anything that forgot to (a save in the
  Editor, the Terminal's `copy`, the debug link) waited up to 2 s on the
  desktop and for F5 in a File manager window.

## The options

1. **Read less often** (10 s, say). The stall stays, five times rarer, and
   a new file takes up to 10 s to show.
2. **Only the cheap fix**: one walk instead of n + m + 2. Less time, still
   on the desktop's task, still every 2 s for nothing.
3. **Read on a job's task** every 2 s. The desktop no longer waits, but the
   drive is read 30 times a minute for nothing, a task is made and thrown
   away each time, and the file system's lock is taken from under whatever
   else wants a file.
4. **Change notices** (the owner's "similar to inotify"): the file system
   says which folder changed; the desktop reads only then. Nothing at all
   happens on an idle machine. The reading itself is still on the
   desktop's task when it does happen.
5. **4 and 3 together**: read only when told, and on a job's task.

## What was chosen

Option 5, recommended to the owner and taken by him, with the cheap fix of
option 2 inside it. An idle desktop does nothing; when the folder does
change, the walk happens off the desktop's task; and the walk is two short
ones instead of n + m + 2.

Smaller choices made while building it, each easy to change:

- **Where the notice comes from: `fs::Storage`**, not the drive under it
  and not the OS calls above it. Everything that writes goes through
  Storage (programs through `os::file`, the shell commands, the debug
  link), so one place covers all of them, and the
  RAM disk, the PC's folder and the flash drive need no code of their own.
  LittleFS has nothing like inotify to lean on anyway.
- **What a notice says: the folder whose list changed**, nothing more. Not
  the file's name and not what happened to it: every listener so far reads
  the folder again, and a name would have to be kept somewhere between the
  task that wrote and the task that looks.
- **When**: after a folder is made, a file or folder removed, a rename or
  replace (both folders if they differ), a file opened for writing (its
  name is there from that moment) and closed after writing, and a safe
  save at the moment the new file takes the name. Not for the hidden
  temporary file of a save, not for anything that failed, not for reading.
- **On which task: the one that made the change**, after the file
  system's lock is given back. The listener must not read or paint there.
  The shell's keeps the folder as a 32-bit key (`fs::path::key`: the path
  without regard to case, separator or a trailing one) in one of eight
  slots, without a lock, and posts one message to the desktop. More than
  eight folders at once means "every folder".
- **On the desktop's task** the message settles the slots: if the
  desktop's folder or the Trash is among them, a job reads them
  (`job::Jobs`, 8 KB of stack, one at a time; a change during the reading
  has it read once more) and the icons are set when its result comes back.
  The File manager's windows that show one of the folders, or a folder
  inside one, read theirs again, so a window on a folder that was deleted
  from somewhere else notices.
- **A pause of 250 ms after settling** when changes come from other tasks:
  a paste of a hundred files tells a hundred times, and the windows are
  read at most four times a second. The first change is not delayed.
- **The 2 s timer stays, and reads nothing.** It looks at the slots (a few
  loads of a word) for a notice whose message found the queue full, or a
  listing whose job could not be started, and settles those. So the worst
  case is what it was before, 2 s, and the usual case is one round of the
  loop.
- **`files_changed()` is gone**, and its nine calls with it. Nobody has to
  remember to say that files changed.
- A machine with no queue and no tasks (most PC tests, the plain desktop
  program) has one task: the notice sets a timer of the desktop's that is
  due at once, and the job's work is done inside `start()`.

Not taken: a list of watchers that programs sign up to through an OS call.
There are two listeners, both in the shell; the call can come when a
program needs it (a picture viewer that follows its folder, say).

## What was built

- `core/include/core/fs/fs.hpp`, `core/src/fs/fs.cpp`:
  `Storage::set_changed`, the notices, `fs::path::key`. `fs::File` now
  carries the folder of a file opened for writing (160 bytes more).
- `core/src/shell/desktop.cpp`: `list_desktop` (any task, two walks),
  `show_desktop` (the UI task), the job, the slots, `settle`. The first
  listing when the taskbar starts is still made at once, so the first
  picture has its icons.
- `core/src/shell/file_manager.cpp`: `refresh_folder_windows(changed)`.
- `desk::Icons::count()`, for the tests.

## How it was tested

On the PC only.

- `host/tests/test_fs.cpp`, `test_change_notices`: which calls tell, with
  which folder, and that failures and reads tell nothing; the key.
- `host/tests/test_shell.cpp`, `test_change_notices`, with a drive that
  counts the walks: 20 s of idle desktop walk no folder (before: 10 ticks
  of n + m + 2 walks); a file written straight to the drive has its icon
  after one round, for two walks; changes in other folders cost nothing;
  three changes in one round are one reading; a File manager window
  follows its folder and leaves the others alone, and falls back to My
  Board when its folder is removed. Then with a queue and real threads, as
  on the board: nothing is read until the desktop takes the message, the
  icons are set when the job is back, a change during the reading reads
  once more, and a job that cannot be started is tried again by the timer.
- The tests' own `key()` and `mouse()` now call `tick()` after each event,
  as the real loops do: the notices settle between two rounds, and the
  tests had no rounds. Two reference pictures changed by the taskbar
  clock's last digit for that reason (77j, 77k), and nothing else.

## What was not tested

The board. To check there: `dbg.py frames` after 20 s of idle shows no
piece over budget; `tools/bench/desktop_frames.py` loses about 27 of its
64 to 66; a file put with `dbg.py put` shows on the desktop; the job's
stack is enough (8 KB asked; the Editor's save does the same kind of calls
in 6 KB); `dbg.py misses` stays at 0 while the job reads.

## What is left

- The File manager's own commands (New Folder, Rename, Delete) still read
  their window themselves and are then told as well: two readings where
  one would do, on the desktop's task. Small, and it shows only in a
  folder window, but it is a piece over budget twice. Not changed here.
- A folder window still reads its folder on the desktop's task. The same
  job could do it; that is the File manager's, and item 56 (views) will be
  in that code.
- The other finding of the benchmark, painting large windows, is untouched.
