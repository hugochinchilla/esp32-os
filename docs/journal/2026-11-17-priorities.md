# Three priority classes, and the time of a job that is still running

Backlog item 11. Branch `claude/project-thread-8q7ksu`.

## 1. What was asked

The backlog line is "Three priority classes (Low, Normal, High) for jobs and
services". The owner decided the shape after the architecture review
(`docs/DECISIONS.md`, "Priorities"): three classes, in a narrow band below
the video, Wi-Fi and the network stack, with the FreeRTOS scheduler left as
it is. What was open was which numbers, and how a program asks for a class.
The owner's instruction for this run of work is "ve haciendo, no pares hasta
terminar el backlog, dejo a tu criterio", so the choices are the agent's and
are marked "to confirm" in `docs/DECISIONS.md`.

The same code had one thing left over from item 3 (the figures): a job's
processor time reached its process's row only when the job ended. The
Terminal's session is one job for as long as the window is open, so the
Terminal, and `top` running in it, showed no time at all. It is done here
because it changes the same few lines.

## 2. What was found

- The tasks on the board and their FreeRTOS priorities: Wi-Fi 23, the
  network stack 18, the video 10, the desktop (UI) 5, jobs 4, the network
  services (`inetd`) 4, the scan log 1, idle 0.
- A job's priority was a constant in the FreeRTOS adapter. Nothing in the
  portable code could say "this work can wait".
- A FreeRTOS task that deletes itself is freed by the idle task some time
  later. Its handle is no good from that moment, so nobody else can safely
  ask how much processor it used.

## 3. The options, and what was chosen

**The numbers.**

- *Chosen: Low 3, Normal 4, High 5.* Normal is where jobs and the services
  already were: one under the desktop, which is therefore never kept from an
  event by a save or a shell command. Low runs only when the desktop and
  every Normal task are waiting. High is level with the desktop: the two take
  turns, a tick each, so the desktop slows but never stops.
- Not chosen: *High above the desktop* (6 to 9). A High job that never
  sleeps would freeze the pointer and the windows, and nothing asks for
  that today. It can be raised later by changing one number.
- Not chosen: *a wider band* (say 2, 4, 6). The classes only have to be in
  order; gaps buy nothing with a fixed-priority scheduler.

**How a program asks.**

- *Chosen: a field of the job*, `job::Spec::priority`, Normal unless set.
  The portable code names the class (`ports::Priority`); the adapter turns
  it into the scheduler's number (`rtos_priority()`), and the services' task
  uses the same function. Nothing uses Low or High yet: the Editor's save
  and the Terminal's session are Normal.
- Not chosen: *a priority per process*, changed from the Task Manager. The
  programs' windows all run on the desktop's task, so the setting would only
  reach their jobs; it would look like more than it is.
- Not chosen: *changing a job's class while it runs*. No use for it yet.

**The time of a running job.**

- *Chosen: the task stays until its starter gives it back.* A task started
  through `ports::Tasks` no longer deletes itself: when its function returns
  it sleeps, and the job runner deletes it when the result has arrived on
  the UI task. In between its figures can be read safely at any moment. The
  process table has a hook, `bring_up_to_date()`, that the Task Manager,
  `ps` and `top` call before they read: the job runner then adds what each
  running job's task has used since the last look. The stack also goes back
  at a known moment instead of whenever the idle task gets to it.
- Not chosen: *the job reports its own time now and then.* It needs every
  kind of work to remember to, and work that is busy and silent would show
  nothing until it ended, which is exactly when the figure matters.
- Not chosen: *reading the handle and hoping.* The window in which the task
  has been freed is small, and that is the worst kind of fault to leave in.

## 4. What was built

- `ports::Priority`, `ports::TaskId`; `ports::Tasks::start()` takes a class
  and returns the task, `end()` gives it back, `run_time_us(task)` reads its
  processor time.
- `job::Spec::priority`; the job runner keeps the task and what it has
  already charged; `proc::Table::set_figures_source()` and
  `bring_up_to_date()`.
- `firmware/os/main/rtos_tasks.hpp`: `rtos_priority()`, the task that sleeps
  after its function, `end()` deletes it. `inetd` is started with the Normal
  class.
- `host/host_adapters.hpp`: the PC's threads keep a record each, so a task
  can be asked for its time while it runs and after.

Tested on the PC: the class reaches the adapter (Normal by default, Low and
High when asked); every task started is given back once; a job that spins
shows time in its owner's row while it runs, more at the next look, the rest
at the end, and never more than the time that passed. 31 tests pass. The
firmware compiles and links in the cloud; it has not run on the board.

## 5. What is left

- To check on the board: a save and a Terminal command still work (the
  task now sleeps and is deleted from the UI task, instead of deleting
  itself); the heap is the same before and after ten saves
  (`tools/dbg.py`); `top` in a Terminal shows the Terminal's own time
  growing; `type` of a big file does not make the pointer stutter.
- Nothing uses Low or High yet. The first candidates: thumbnails or a
  search in the File manager (Low), sound when there is any (High).
- The figure of a task that is running at the moment it is read is up to
  its last switch: a few milliseconds under at most.
