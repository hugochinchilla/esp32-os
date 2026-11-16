# A lock on the file system

2026-11-16. Backlog item 9. Built on the PC only; nothing here has run on the
board.

## 1. What was asked

The architecture review listed the missing pieces, and the owner approved the
whole list with its suggestions:

> Mira, de la lista final de tareas que sugieres, en las que me das opciones y
> tú respondes ya con una opción sugerida, apruebo todas tal y como están, con
> sus sugerencias. [...] Empieza poquito a poco, no intentes hacer todo a la
> vez, paraleliza algunas cosas si se pueden, pero prioriza hacer las cosas
> dejando un buen detalle del trabajo hecho y de las decisiones tomadas.

Item 9 is one line in the backlog: "Lock on the file system". The review said
why: "a lock on the file system (it is written for one task today)". Saving in
the background (item 10), the shell (item 13) and telnet (item 17) all mean a
second task touching files while the desktop does. The brief for this piece
was: one lock, a test with two threads, and no change in behaviour.

## 2. What was found

Everything that touches files goes through one object, `fs::Storage`
(`core/src/fs/fs.cpp`): the Editor, the folder windows, the Trash, the picture
viewer, the settings files and the debug link's file commands. Nothing outside
the tests talks to a drive directly. That made this a small job: one door to
guard.

Behind that door there is state that two tasks would break:

- **The RAM disk** (`core/src/fs/memory_volume.cpp`) is a small FAT of our
  own: a table of 256 files and folders, a chain table that says which
  512-byte block follows which, a count of free blocks, and a table of 6 open
  files, each with its position. Taking a free block is "look for one marked
  free, mark it used". Two tasks doing that at the same moment both find the
  same block and both take it, and two files then share a piece of disk.
- **The flash drive** is LittleFS behind ESP-IDF's POSIX layer. The LittleFS
  component has a lock of its own, so single calls are already safe there.
  What it cannot know is that our save is three calls (close the temporary
  file, swap it in, tidy up) that belong together.
- **The list of drives** in `Storage` itself (four letters and four
  pointers).

There was already a lock in the ports (`ports::Lock`, three functions: lock,
try_lock, unlock), made for the debug link, with a FreeRTOS mutex behind it on
the board (`RtosLock` in `firmware/os/main/main.cpp`) and a do-nothing one on
the PC.

How real the danger is was easy to show. The new test, with the lock taken
out, crashes the RAM disk within a tenth of a second, three runs out of three:
a write lands outside the disk's memory, inside `MemoryVolume::write`.

## 3. The options

### 3.1 Where the lock goes

| Option | What it would cost |
| --- | --- |
| **One lock in `fs::Storage`, around every call** (chosen) | About 100 lines in one file. Every drive is covered, including ones not written yet. All drives wait for each other |
| A lock inside each drive | Each drive has to remember to do it (three today: RAM disk, flash, PC folder). It cannot make a save one step, because a save is several calls made by `Storage` |
| One lock per drive, held by `Storage` | Two drives could work at once. The board has one drive with files that matter (C:), so nothing is gained today, and a copy between drives later would hold two locks, which is where deadlocks come from |
| Readers together, writers alone (a read-write lock) | Several tasks could read at once. FreeRTOS has none, so we would write it; reads here take milliseconds; and the RAM disk's open-file table changes on a read too (the position moves) |
| A file system task with a queue: other tasks send it requests and wait for answers | No lock at all, and it is how AmigaOS did it (each file system was a "handler" task that got packets). Every call becomes two task switches, there is a task and a stack more, and every caller needs somewhere to wait. A lot of machinery for the same result |
| `std::mutex` straight in the core | No wiring on the board or the PC. But the core would bring in threads on its own, where everything else it needs from the machine comes through a port, and tests could not swap it |

Chosen: the first. It is the smallest thing that is correct, and it is the
only cheap one that can treat a save as one step.

### 3.2 How long the lock is held

| Option | What it would cost |
| --- | --- |
| **For one call, and for the whole of the calls that are several steps** (chosen) | A task reading or writing piece by piece lets others in between pieces |
| For one call only, compound calls included | `write_file()` from two tasks to the same file would meet in the same temporary file. Tried in the test: on the RAM disk the second task gets "it is in use" |
| From open to close of a file | Simple to explain, but a program that keeps a file open (the picture viewer does while it decodes) would stop every other task for as long as it likes |

Chosen: the first. `begin_save`, `finish_save`, `read_file` and `write_file`
hold the lock from start to end. `open`, `read`, `write`, `seek`, `close` and
the folder calls hold it for the one call.

What that means in time: the Editor saves in pieces of 1024 bytes
(`core/src/shell/editor.cpp`), so another task waits for one piece at most.
At the write speeds measured on the board earlier (370 to 408 KB/s, see
`docs/DECISIONS.md`), that is about 3 ms a piece. That figure is worked out,
not measured. `write_file()` holds the lock for the whole file; it is used
for settings files and `welcome.txt`, which are small.

### 3.3 Which lock

The existing `ports::Lock`, handed to `Storage` with `set_lock()`. With no
lock set, `Storage` works as before, which is what the simulator and most
tests do: they have one thread. The board gets an `RtosLock` of its own (a
second FreeRTOS mutex, not the debug link's). A FreeRTOS mutex lends its
holder the priority of whoever is waiting, so a Low job holding the lock
cannot keep a High one out for long (that matters from item 11 on).

The lock is not recursive: a task that holds it and asks again would wait for
itself for ever. So inside `Storage` the work is in private `do_...`
functions that never take the lock, and each public function takes it once
and calls those.

### 3.4 Who chose

The owner approved item 9 as listed. The choices in 3.1 to 3.3 were made
while building, by the agent, because the brief fixed the shape ("one lock
around `fs::Storage` and its open-file table") and none of them shows on the
screen. They are listed in `docs/DECISIONS.md` as "to confirm" and each can be
changed without touching the programs: the lock is behind `Storage`'s calls.

## 4. What was built

- `core/include/core/fs/fs.hpp`, `core/src/fs/fs.cpp`: `Storage::set_lock()`,
  the lock around every public call, the `do_...` functions.
- `core/include/core/ports.hpp`: the comment on `ports::Lock` now says it
  guards more than the debug link, and that it is not recursive.
- `firmware/os/main/main.cpp`: four lines, a second `RtosLock` given to the
  storage before the drive is mounted.
- `host/host_adapters.hpp`: `host::ThreadLock`, a real lock for tests
  (`std::mutex`).
- `host/tests/test_fs.cpp`: two new tests. `host/CMakeLists.txt` links the
  test with the threads library.

No program changed. One task uses the file system today, so on the board the
lock is taken and given back with nobody waiting.

### The test

`test_two_tasks` starts two threads on one RAM disk of 256 KB. Each does 300
rounds in its own folder: write a file whole and read it back, save one piece
by piece the way the Editor does and read it back in pieces, look at the
folder, rename, remove. Each round both also write the same file,
`C:\shared.bin`, 3000 bytes of the task's own letter, and read it.

It checks four things:

1. Every call answers Ok and every file reads back as written.
2. `shared.bin` is always 3000 bytes of one letter, never a mix and never "in
   use".
3. No two calls are ever inside the drive at once. The drive in the test is
   wrapped in a `WatchedVolume` that counts who is inside, and waits a moment
   on the way in to give a second task every chance.
4. Afterwards nothing is lost: the free space is what it was at the start,
   the root is empty, and all 6 open-file slots can be used.

A second test, `test_the_watch_sees_two_tasks`, runs the same watch with no
lock and checks that it does see two tasks meet. Without it, a watch that
never fires would look the same as a lock that works.

How it was run:

- The whole PC suite: 21 of 21 pass (`ctest`, with the address and
  undefined-behaviour checks that are always on).
- The file system test 20 more times in a row: no failure.
- The same test built once by hand with the thread checker
  (`-fsanitize=thread`): no report.
- With the `set_lock()` line taken out of the test: a crash inside the RAM
  disk, three runs out of three. Put back.

Not tested: anything on the board. The firmware change is four lines that use
a class already there; whether it was compiled is in the pull request.

## 5. What was learned

- The flash drive was already half safe (LittleFS locks itself) and the RAM
  disk not at all. The lock in `Storage` treats them the same, so nobody has
  to know which is which.
- A test for a lock needs its own control. "Two threads ran and nothing
  broke" proves little: they may never have met. Counting who is inside the
  drive, and proving that the count works when the lock is missing, is what
  makes the result mean something.
- Locking each call is not enough when one operation is several calls. The
  safe save (item 6) made `write_file()` three steps; they have to be one
  step for other tasks too.

## 6. What is left

- **Two tasks on the same file.** The lock keeps the file system's own tables
  whole. It does not stop two tasks from saving the same document piece by
  piece at once: both would use the same temporary file. On the RAM disk the
  second is refused ("it is in use"); on the flash both would write. Item 10
  locks the window while its save runs, which covers the Editor. The shell
  (item 13) is where this comes back.
- **KILL and held locks.** The decisions say a KILL never takes effect while
  a task holds an OS lock. This is the first such lock outside the debug
  link. Whoever builds forced kills for real tasks (items 5 and 10) has to
  honour it: a task destroyed inside `Storage` would leave the lock held for
  ever.
- **Never from an interrupt.** A mutex cannot be taken in an interrupt
  handler. Nothing does file work there today.
- **A board turn** is not needed for the lock itself: nothing waits on it
  yet. It will get its real test with item 10, the first second task.
