# Processor time and memory for each process, and top

Backlog item 3, and the `top` of item 16 that was waiting for it. Branch
`claude/project-thread-8q7ksu`.

## 1. What was asked

The backlog line is "Per-process processor time and memory. Task Manager
shows PID, time and memory columns". Item 16 left `top` for when there was
something for it to show. The owner's instruction for this run of work is
"ve haciendo, no pares hasta terminar el backlog, dejo a tu criterio", so
the choices are the agent's and are marked "to confirm" in
`docs/DECISIONS.md`.

## 2. What was found

- A process here is a row of the table, not a task. Almost everything a
  program does, it does on the UI task, inside a call the window manager
  makes into one of its windows (a key, a click, a timer, a repaint). Around
  every such call the window manager already makes that program the
  "current" process (`proc::Scope`). Since item 10 a program may also have
  jobs, each a task of its own.
- So FreeRTOS's per-task run time, which the Task Manager's "Processor"
  group already uses, cannot tell one program from another: they share the
  UI task.
- Every block a program's windows ask for goes through two functions of the
  desktop (`program_memory`, `free_program_memory`): the window objects
  themselves, the Editor's text and undo buffers, the File manager's
  listing, the Picture viewer's picture and decoder. Windows are made when
  they open and destroyed when they close, so what a process holds is what
  its open windows asked for.

## 3. The options, and what was chosen

**Processor time.**

- *Chosen: charge the UI task's time to whoever is current.* The table is
  given a clock; each time the current process changes, the time since the
  last change goes to the one that was current. It costs one reading of
  the clock per change and no memory beyond a counter per row. Time spent
  with no process current is the OS's own (the window manager itself, the
  compositor) and is not kept in any row.
- *A job's task is added when it ends*: the task reads its own run time as
  its last act and it is added to the owner's row with the result.
- Not chosen: *a task per program*, so the scheduler's figures could be
  used as they are. It is the model of a far bigger change (every program
  on its own stack, the window manager talking to them by messages) and is
  not what the item asks.
- Not chosen: *sampling* (a timer that notes who is current a hundred
  times a second). Cheaper per call, but coarse and wrong for short calls,
  and the exact way costs little.

**Memory.**

- *Chosen: a note in front of each block.* `program_memory` asks the Memory
  port for 16 bytes more and writes the owner and the size there;
  `free_program_memory` reads them back and takes the bytes off the owner's
  row. A job's stack is counted as its owner's while the job runs.
- Not chosen: *asking the heap who owns what*. ESP-IDF can trace
  allocations per task, but again the programs share a task, and the
  portable core may not know about ESP-IDF.
- Not chosen: *counting windows' sizes by hand in each program*. Every new
  program would have to remember to.

What is not counted: memory the firmware gave out at start (the frame
buffers, the fonts, the RAM disk) belongs to no process, and code and
constants are in flash. The figure is "what this program asked for while
open", which is the part that changes and the part that ending it gives
back.

**How it is shown.**

- Task Manager: two columns, "Time" and "Mem", between the name and the
  window title; the window is 64 pixels wider to fit them. Time reads
  "4.2s", then "3:07", then "1:02:45"; memory "640K" or "1.2M", and "-" for
  none.
- `ps` has the same two columns.
- `top`: the table sorted by share of the processor since its last look,
  every two seconds, until `q` or Ctrl+C; `top -n 3` shows three screens
  and returns. Where it cannot wait for a key (a console with no task of its
  own) or its output goes to a file or a pipe, it shows one table of totals
  and returns, so `top > file` and `top | grep` do something sensible. The
  share is of one core, in tenths of a percent.
- Not chosen for `top`: a full-screen layout with bars and a header of
  totals. `free` and `uptime` give those; the table is what was missing.

## 4. What was built

- `core/proc/table`: `cpu_us` and `memory` in each row; `set_clock`,
  `add_cpu`, `add_memory`; `set_current` charges the time. `time_text` and
  `memory_text` so the three places that show the figures agree.
- `ports::Tasks::run_time_us()`: the calling task's processor time (FreeRTOS:
  `vTaskGetInfo`; PC: the thread's CPU clock).
- `core/job`: the stack charged while the job runs; the task's time added
  when it ends.
- `core/src/shell/desktop.cpp`: the note in front of each block; the table's
  clock is the window manager's.
- `core/src/shell/task_manager.cpp`: the two columns.
- `core/src/sh/system.cpp`: the columns in `ps`; `top`.
- The session's working buffer is now aligned to 8 bytes: `top` keeps its
  readings in it, and the PC's checker caught the misaligned access.

Tested on the PC: the table with a clock the test moves by hand (nested
scopes, the 32-bit clock wrapping, memory never below nothing, a reused
row starting from nothing); a job's stack counted while it is held and
given back after, and its thread's time in the owner's row; the Editor's
row over 100 KB while open; `ps` and `top` text, `top > file`, wrong
options; on a session with a task, `top -n 1`, `top` left with `q` and with
Ctrl+C. The Task Manager and Terminal reference pictures were looked at
and updated (`docs/look/figures/`, `docs/look/terminal/`). 31 tests pass.
The firmware compiles and links in the cloud; it has not run on the board.

## 5. What is left

- A job that is still running shows no time yet: it is added when the job
  ends. The Terminal's session is one long job, so the Terminal's row shows
  the time of its window only until it closes. Reading a live task's time
  from another task needs its handle kept per job; small, and worth doing
  with the priority classes of item 11, which touch the same code.
- On the board the running task's counter is only brought up to date when
  it is switched out, so a job's figure can be a few milliseconds under.
- The services (the debug server, VNC, the web server) are rows without
  figures: they run on tasks of their own and ask the heap directly.
- To check on the board: that the figures move as expected (Culebra playing
  should show time growing; the Picture viewer about 2.3 MB with a picture
  open), and that the wider Task Manager still fits at 640x480.
- `dmesg` still waits for a log kept in memory.
