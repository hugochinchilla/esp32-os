# The queue of events, and two answers from the owner

2026-11-17. Backlog items 26 and 37, the part both had left; and two
decisions that had been waiting.

## What was asked

The owner, in the project chat:

> Sí, podría seguir con 26 y 37. Respecto a las decisiones que falta por
> tomar, el comando RM no debe utilizar la papelera, borra el archivo
> directamente. Y creo que no tiene que haber un límite de ventanas común.
> Prefiero ver el sistema fallar por falta de memoria dado el caso. Me
> parece un caso divertido y de época.

Three things, then: go on with items 26 and 37; `rm` deletes directly; no
common limit on windows.

## What was found

**Items 26 and 37.** The task as handed over said that only Culebra had a
task of its own. That was true of `main`, not of the work waiting to be
merged: PR 43 and PR 44 (`claude/project-thread-oz8s1x`, on the board's
queue) give a task to every program of the Programs menu but the Terminal,
and to every program loaded from a file. So this branch starts from PR 44's
head (74fef5e), not from `main`: the queue has to sit in the very code
those change (`prog::Runner`, `proc::Table::call`, `in_program`), and
building it on `main` would have meant writing it twice.

What both items had left was one thing, the queue of events. As things
stood, the UI task hands a program each call and waits for it, two seconds
at most. A program that has not answered by then is late, and from then
until it answers every call for it is dropped: keys, clicks, and worst of
all the result of a background job. An Editor that was slow for a moment
while its save finished would never hear that the save was done, and its
window would stay locked for good.

**`rm`.** Already as the owner wants it: `rm` in the shell deletes for
good and never touched the Trash (`core/src/sh/files.cpp`). It was a
choice of the agent's marked "to confirm". It is confirmed; no code
changed.

**The window limit.** There was one, and it was the system's:
`wm::Wm::kMaxWindows = 16`, two fixed arrays in the window server, and a
third of sixteen in `os::ui` for the windows programs know by number. The
seventeenth window did not open, with no word to the user. Four places
copied windows into arrays of sixteen on the stack (ending a process's
windows, asking them to close, Shut Down, holding back the windows of a
process with a job running).

## The options for the queue

Put to the owner on a card. The handoff note of the backlog thread had
said so: "This is a design choice for Hugo: put options to him first."

1. **A queue only while a program is not responding** (recommended). The
   server goes on waiting for the program, as the owner chose for item
   26. When a program is late, what happens to it is kept in a queue of
   its own and handed over in order when it answers. Small: the programs
   and their tests do not change, there are still no locks, and a key
   sent in a test is still acted on by the next line.
2. **A message loop in every program.** Each program takes its events
   out of its queue itself, as a Win16 program did with `GetMessage`, and
   the desktop waits only while it paints. It is the truer copy of the
   period. It costs the rewrite of all twenty programs and of every place
   where the window server needs an answer at once (did the program use
   the key, may the window close, may the file be dropped here); and
   since program and desktop would then run at the same time, the lock
   around the OS calls that item 26 rejected.
3. **Leave it.** Only the lost job result is mended.

Built on the first while the card is unanswered. Nothing in it closes the
way to the second: the queue and its rules of what is kept are what a
program's own loop would read from.

## What was built

**The queue** (`core/prog`, `prog::Runner`). Each program with a task has
a queue: a chain of blocks from the Memory port (external RAM on the
board), one a call, counted as the program's memory. A call for a program
that is late goes to the end of it instead of being dropped, if whoever
makes the call says it is one to keep. When the program has finished the
call it was late in (`serve()`, once a round of the UI loop, where late
programs were already taken back), the queue is handed out: oldest first,
each as an ordinary call with the ordinary wait. If the program is late
again in one of them, the rest goes on waiting, and what comes meanwhile
goes behind it. Ending the program gives the queue's memory back.

**What is kept** is said at each place that calls into a program, with a
new last argument of `proc::Table::call` and `proc::call_as`
(`proc::Keep`):

- `kKept`, in order: a key, typed text, a button, a choice in a list, a
  click, a drop (`os::ui`, by the kind of event); the result of a job
  (`job::Jobs::finish`); the answer to a question (`desktop.cpp`); a
  message from another task (`msg::Inbox`); another program asking this
  one to open something (`in_program`).
- `latest(of)`, the last one only: it takes the place of the newest call
  waiting if that is of the same kind. Where the mouse is, a control's
  new value, the window's new size, "files changed".
- `kDropped`, the default: a paint, a menu about to open, the question
  whether a file may be dropped, a menu's action, Closing and Closed.

Only a call handed over by copy can be kept. One that is a bare pointer
into the program (a menu's action, a timer's function) cannot: what it
points at may be gone by the time the program is back.

**Timers** therefore do not go through the queue. The window server
leaves the timer of a late program alone: it stays due and fires once
when the program is back (`Wm::tick`), and it is left out when the UI
loop works out how long it may sleep (`Wm::idle_us`), or a hung game with
a 20 ms timer would have kept the loop spinning.

**An event names its window by number** now, not by pointer
(`os::ui`'s `Told`): one kept for a window that an earlier event closed
finds nothing and is not made.

**No window limit.** The window server's lists and `os::ui`'s list begin
with room for sixteen, inside the object as before, and grow out of the
Memory port when full: twice the room, in one block, given back when
sixteen or fewer windows are left. The four arrays on the stack are gone:
windows are walked by their numbers, oldest first (asking a process's
windows to close, Shut Down), or chained through a pointer of their own
(those held back for a job). With no memory for a longer list the window
does not open, and the program that asked says so the way it already did
when the server was full. The taskbar's buttons shrink as before, now
down to four pixels.

## Choices made by the agent, to confirm

- 32 calls a program in the queue; past that they are lost.
- A menu's action for a program that is not responding is dropped, not
  kept.
- A key kept for a program has already been seen by the window's own
  controls when the program hears of it. Normally the program is asked
  first. So text typed into the Editor while it is away appears at once
  (the text control is the system's), and a shortcut of the program's
  acts when it is back.
- The caps programs set for themselves stay (four Editors, three File
  manager windows), and so do the 32 process rows and 8 program tasks.
  The owner's sentence was about windows; whether the per-program caps
  should go the same way is a question for him.

## Tested

On the PC, and the firmware compiled and linked in the cloud container
(ESP-IDF v5.5; it says that it compiles, not that it runs).

- `test_prog`, with real threads: nothing waits for a program that
  answers; kept, dropped and latest while it hangs; the order when it is
  back, before anything new; late again in the middle of the queue; the
  queue full at 32; ended while it hangs, with every block back; no
  Memory port, nothing kept.
- `test_os`: the program that hangs on a key now has B and C typed at it
  meanwhile. It is handed both, in order, after it wakes; the memory
  figure of its process goes up by the queue and comes back; a timer of
  its own that comes due meanwhile does not fire, does not keep the loop
  awake, and fires once afterwards; a key kept for it when its window is
  closed goes with it.
- `test_job`: a job that finishes while its owner hangs. The result
  arrives when the owner is back.
- `test_wm`: seventy windows. Sixteen without a Memory port and the
  seventeenth refused; all seventy with one, in order, painted without
  stale pixels; the block back once they are closed.
- `test_lifecycle`: what used to be "the window manager full" is now
  twenty-two windows open and closed with every block returned.
- `tools/gate.sh`: all passed, 44 tests, the reference pictures unchanged,
  the OS-call check.

## Not tested

Nothing here has run on the board. What only the board can say: that a
program made slow with `dbg.py fault slow --pid` gets the keys and clicks
of those seconds afterwards; that a save finishing meanwhile unlocks the
Editor; what the queue costs when nothing is late (it should be nothing:
one more test of a pointer per call); `dbg.py misses`; and seventeen and
more windows on the real screen.

## What was learned

- A task description can be a day old. Reading the open pull requests
  before the code saved building item 26 a second time.
- A queue of calls is only safe for what was handed over whole. The
  first thought was to queue everything; timers and menu actions carry
  pointers into the program, and an event earlier in the queue can free
  what they point at. For timers the better answer was not a queue at
  all: the timer itself remembers that it is due.
- The old test said "nothing more reaches it" and pressed H, the key
  that hangs, to prove it. With the queue that press is kept, and the
  program hung again the moment it woke: the test failed for the right
  reason.

## The owner's answer: leave it (2026-11-17, half an hour later)

The owner answered the card with the third option, "Dejarlo como está":
what happens while a program does not respond is lost, and only the lost
result of a background save is mended. Not the recommended one. The
queue described above was pushed by then (d0d40be, c76e952) and is taken
out again in the next commit; this entry is left as it was written, and
this section says what stands.

**What stands.**

- The result of a job is kept for an owner that is not responding and
  handed over when it answers again (`job::Jobs::finish` says
  `proc::kKept`; nothing else does). The mechanism is the small part of
  the queue that this needs: a chain of blocks in `prog::Runner`, counted
  as the program's memory, handed out in `serve()`, given back if the
  program is ended. Eight a program at most, as many as there can be
  jobs.
- No window limit, and `rm` confirmed, as above.

**What was taken out.**

- Keeping keys, text, button presses, clicks and drops; the "latest
  only" rule (`proc::latest`) for mouse moves, values and sizes; keeping
  messages from other tasks, the answer to a question, `in_program` and
  "files changed". All of these are dropped for a late program, as they
  were before.
- The timer that stayed due: a late program's timer ticks are lost again,
  a one-shot timer included.
- Events naming their window by number: with nothing queued the pointer
  is safe again, and the code is as it was.

**Tests now.** `test_prog`: kept and dropped while a program hangs, the
order when it is back, late again in the middle, full at eight, ended
while it hangs, no Memory port. `test_job`: the result of a job waits for
a late owner. `test_os` is as it was on PR 44: nothing reaches a program
that hangs, and nothing waits for it. `test_wm` and `test_lifecycle` for
the windows, as above. `tools/gate.sh`: all passed (44 tests, pictures
unchanged). The firmware compiles and links in the cloud container
(ESP-IDF v5.5); nothing has run on the board.

**What was learned.** The card should have come before the code, or the
code should have been only the part every option shared, which here was
the job's result. Building the recommended option "while waiting" cost
an hour of work that was then removed, and a push that had to be
followed by another.

## The caps of the programs go too (2026-11-17, later)

Asked whether the caps that programs set for themselves should go the
way of the system's limit, the owner answered in the project chat:

> Quítalos.

- **The Editor**: there were four at most, kept in a table of four
  numbers, and the fifth got "Four Editors are open already". Now any
  number: the Editors that exist are a chain through their own windows
  (each joins it in its constructor and leaves it in its destructor),
  walked to find a file that is already open. Each still takes 576 KB of
  external RAM, so the board has room for about ten; the one that finds
  none says "There is not enough memory to open Editor".
- **The File manager**: three folder windows at most, and a fourth folder
  took over the first window. Now every folder opened gets a window of
  its own, chained the same way; a folder already shown still comes to
  the front.
- New windows of either still step down and to the right, eight places,
  then from the start again.

`test_lifecycle`: a fifth Editor opens and gives everything back when it
closes; a fourth folder window opens among twenty-three windows. Gate: all
passed (44 tests, pictures unchanged); the firmware compiles and links; not
run on the board.

## On the board, and a message that went missing (2026-11-17)

The board thread ran 0e8bae8 (with `main`, where PR 44 now is) and did
not merge it. Its figures, as the coordinator passed them on:

- **The save's result is kept.** A 300 KB save with `fault slow` on the
  Editor: afterwards the title is "big.txt" again, the window can be used,
  the file is 303,798 bytes. On `main` the same leaves the window locked
  with "big.txt*" for good.
- **A hang and End Task during a save**: the Editor gone, no task left,
  internal RAM exactly back, external 3.9 KB less (the taskbar's).
- **More than twenty windows**, six Editors and five folder windows among
  them; the taskbar's buttons shrink. With the Picture viewer closed, ten
  Editors opened, with 232 KB of external RAM left.
- **Cost**: a repaint with Culebra open 26.6 ms against 26.2 to 27.9 on
  `main`; the UI task 2.0 % on both; no missed refills.
- **The failure**: the eleventh and twelfth Editor opened nothing and said
  nothing. No "not enough memory" box on the screen. The desktop went on
  answering, and the memory came back when all were closed.

**The cause** was not in this branch's work, but only this branch could
show it: before, the fifth Editor was refused by the cap and never
reached the end of the memory. The Editor does say "There is not enough
memory for the Editor". A message box is made modal to the window in use
at that moment, whoever's it is, and a window that is destroyed takes its
dialogs with it. Started from the Run dialog, the Editor put its message
over the Run dialog, and the Run dialog closes the moment it has started
something: the message went with it, a fraction of a second after it
appeared. From the Start menu the same message stays. It was reproduced
on the PC only once the Editor was started the way the board thread
starts programs.

**The fix** (`Wm::dispose_closed`): a window that is destroyed closes the
dialogs over it that are its own process's, as before. A dialog of
another process is let go instead: it stands alone from then on. Tried
first and dropped: making a message modal only to a window of the asker's
own. It is the tidier rule, but it gave the Picture viewer's "damaged
picture" message a taskbar button of its own (one reference picture
changed), which nobody asked for.

`test_lifecycle`: an Editor refused for lack of memory, started from the
Run dialog: the dialog is gone, the message is there, and pressing OK
ends the Editor's process with everything back. It failed before the
fix. The test of the Task Manager's question now asks as the Task
Manager, as the program does. Gate: all passed (44 tests, pictures
unchanged). The firmware compiles and links. Not run on the board again
yet.

## What is left

- The board turn.
- The debug link's window list still answers "too many windows" at about
  ten windows: its reply buffer, not a limit on windows.
- Item 27 goes on in its own thread.

## On the board (2026-11-18, e5f6ed9)

App-flashed on the BIOS layout. Passed:

- A 300 KB Editor save followed at once by `dbg.py fault slow --pid` ended
  unlocked, title without the star, file the right size. On `main` (PR 44)
  the same run left the title `big.txt*` with the hourglass: the result was
  lost there.
- `fault hang --pid` during a save, then End Task: Editor, window and task
  gone, internal memory exactly as before.
- With ten Editors open (232 KB of external RAM left) the eleventh try from
  Run, a second try from Run and one from Start > Programs > Editor each
  left a box titled "Editor", "There is not enough memory for the Editor.",
  on the screen after Run had gone; no process left behind, memory as before
  the attempt (the box itself 772 bytes while it is open).
- Editor with unsaved text and Alt+F4 (the "Save changes?" question), then
  End Task from the Task Manager: the question comes back to the front, the
  second End Task asks "has not closed", End Now: Editor, its window and its
  question all gone, no task. A Task Manager question cannot be left open
  while the Task Manager is closed, because the question is modal to it;
  Alt+F4 closes the question first.
- Everything closed: internal memory identical, external 3.9 KB lower (the
  taskbar's Start menu), as with PR 44.
- Earlier, on the commit before this one (batch 6): more than twenty windows
  at once with the taskbar buttons shrinking, and an open Culebra costs
  nothing measurable.
