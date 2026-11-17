# Open files are handles the system keeps

2026-11-17. Backlog item 37, first step.

## What was asked

Item 37 is the owner's: take the OS calls to "numbered calls with handles
(windows, files and timers as numbers, events through a queue, the system's
tables of what is whose)", to be looked at again at the end of the backlog.
The backlog has reached it. How to go about it was put to him on a card
(handles now and the queue with item 26, recommended; all of it at once;
or the serial console and telnet as `sh` sessions first). This step was
started on the recommendation while he decides, and is part of the first
two options alike.

## What was found

Of the three things item 37 names, two already had a table of what is
whose:

| Thing | A number? | Who knows whose it is | Taken away when the process ends? |
| --- | --- | --- | --- |
| Timers | Yes, an id | The window server | Yes |
| Windows | No, a C++ object | The window server | Yes, closed and destroyed |
| Memory | No, a pointer | A note in front of each block | No: the window's destructor gives it back |
| Open files | No, an `fs::File` in the program | Nobody | No |

So files were the one thing the system could not see. `os::file::open`
filled in an `fs::File` that lived in the program; if the program went
with the file open, the drive's slot stayed taken for good (a drive keeps
six). Nothing leaked in practice, because only the Editor opens files
piece by piece and it closes them in the same event, or in a job the
process waits for. But that was the programs behaving, not the system
knowing.

The journal of the calls had already said what stood in the way: the
Editor's save runs on another task, where "who is asking" is not known.

## The options

**Who a job's file belongs to.**

| Option | For | Against |
| --- | --- | --- |
| 1. The job says so: the call takes the `job::Job` it was given (chosen) | Nothing hidden; a job cannot name another process, only itself | One more argument in two calls |
| 2. A per-task variable set by the job runner | No argument | Hidden state on every task; one more thing for the board's tasks to carry |
| 3. Open on the UI task, write in the job | No question of whose | The opening is flash work on the UI task, which the job exists to avoid |

**How two tasks share the table.** A slot is claimed by its "in use" flag
alone (an atomic compare and set), so no lock is needed and nothing is held
while the drive works. Once claimed, a slot is its holder's. The system
closes what a process left on the UI task, after the process's jobs have
ended, so it never closes under a job's feet. A lock was the other way;
it would have had to come from the machine, as the sessions' kits do.

**Where the table lives.** Six entries of about 340 bytes (a save keeps
two paths): 2 KB. Taken once from the Memory port when the calls are
installed, so on the board it is external RAM, not the internal RAM the
video needs.

**The limits.** Six in the machine, two for one process. No program holds
more than one today; a drive keeps six. Both are guesses to be changed
when something needs more.

All of these are the agent's, to confirm.

## What was built

- `os::file::Handle`: a number. The low byte is the slot, the rest counts
  the uses of that slot, so a handle that was closed is refused even after
  the slot is in use again.
- `open`, `read`, `write`, `close`, `begin_save`, `finish_save` take and
  give handles. `open` and `begin_save` take the job, from a job.
  `os::file::open_files(pid)` counts.
- `proc::Table::on_exit`: whoever keeps something for processes is told
  when one ends. The calls use it to close what the process left: a file
  is closed, a save not finished is thrown away (the old file stays, the
  temporary one is removed), and the system log gets a line.
- `job::Job::owner()`.
- The Editor reads and saves through handles.

## How it was tested

`test_os` on the PC: a handle reads and closes; a closed handle, a handle
whose slot was taken again, and numbers that were never handles are
refused; a file that is not there takes no slot; a save given up and one
kept; two per process and six in the machine, then `TooManyOpen`; a
process that ends has its files closed and its handles dead while another
process's still read; a process that ends in the middle of a save leaves
the old file and no temporary one. The 41 PC tests pass, with the Editor's
own tests going through the new calls.

The firmware compiles and links (0x1699d0 of 0x1f0000). It has not run.
For the board: open and save in the Editor, a save of a large file with
the desktop in use meanwhile, memory before and after, `dbg.py misses`.

## What was left out

- The shell's commands open files through `sh::Env`, as before.
- Windows as numbers, and events through a queue: the rest of item 37,
  waiting for the owner's answer.

## The same day: the picture decoder

The Picture viewer was the one program that held a file across events: its
decoder (`core/image`) opened the file itself and read a piece on every
timer tick, so it had been handed the drives whole (`os::file::storage()`,
a call that existed only for it).

The decoder now reads from an `image::Source`: read some bytes, go to a
place. The viewer opens the file through the OS calls and hands in a
source over the handle (`os::file::seek` was added for it); the decoder no
longer knows about drives when a program uses it. Opening by path stays
for the code that is not a program (the boot picture, the tests): it makes
a source over the drives itself. `os::file::storage()` is gone.

The viewer closes its handle when the picture is done, so it holds a file
only while it decodes. `test_os`: one file open while a picture is read,
none when it is on the screen, none after the viewer is ended half way.
PC tests pass (41), pictures match, firmware compiles and links
(0x169b00 of 0x1f0000), not run. For the board: open a BMP, a PNG and a
JPEG, and close the viewer while a large one loads.

## The same day: another program's window is a number

The owner answered the card: handles now, the queue with item 26. The next
question, how far window handles go, was put on a second card (at the
border only, recommended; windows and widgets all by number, which
rewrites the 17 programs and is what item 27 needs; or leave windows).
This was built on the recommendation while he decides.

What was found: a window already had a number, given when it opens and
never given again (`wm::Window::id()`); nothing used it outside the window
server. The calls handed out pointers instead: `os::main_window(pid)` gave
the Task Manager a `wm::Window *` into another program's object, which it
read the title and icon from and passed back to bring it forward. Within
one event that is safe; kept for longer it would be a pointer to a window
that may have been destroyed.

What was built: `os::WindowId`. `os::main_window(pid)` gives the number;
`os::window_title`, `os::window_icon` and `os::bring_forward` take it, and
answer "nothing" for a window that has closed. The Task Manager uses only
those. A program's own windows stay C++ objects, and the calls on them
(`os::show`, `os::activate`, `os::blocker`) are unchanged.

`test_os`: the number of a window seen from outside, its title and icon,
bringing it forward, numbers that are not windows, and the number dead
after the window closes. PC tests pass (41), pictures match, firmware
compiles and links (0x167080 of 0x1f0000), not run. For the board: Task
Manager lists titles and icons, Switch To brings a program forward, and
its dialog if it has one open.


## On the board (added by the board thread, 2026-11-17)

Branch commit 8474364 (file handles, the Picture viewer through a handle,
another program's window as a number) with main 781cf99 merged in; only the
journal index conflicted. 41929bc was never flashed on its own: this turn
covers all three steps. PC tests 42 of 42, firmware 0x165300, flashed by
cable, the app only.

1. Free RAM on the idle desktop: internal 206,035 bytes against 206,059 on
   main (24 less), external 6,478,056 against 6,479,768 (1,712 less: the
   handle table).
2. The Editor: a file opened (`edit` over telnet), a line added, saved,
   closed and opened again: the line is there. Save As to a new name wrote
   the new file and the title followed.
3. A 400 KB file saved from the Editor: 4.1 s, the Start menu opened and
   closed meanwhile, the file byte for byte right afterwards.
4. `kill -9` on the Editor 0.6 s into a 400 KB save, three times: the
   window went, and the file was each time whole and new: by design the save
   job finishes and closes its file before the process ends, so no handle was
   left for the system to close and no "os: closed" line was written. Ten
   more open, edit, save and close rounds afterwards all worked (the table
   does not fill up).
5. The Picture viewer closed 0.3 s into loading earthrise.jpg and another
   viewer opened with mascot.bmp at once, three times: no crash, mascot.bmp
   shown each time, internal RAM back to the byte (external +424). No "os:
   closed" line here either. A case that makes the system close a handle
   for a process that ended was not found from outside.
6. Normal use: `type` with `>`, `cp`, `mv`, a pipe and `rm` over telnet, a
   30 KB put and get, the Editor's Open and Save, `ted`, the File manager
   moving a file into a folder (crash.txt into Documents, by a slip of mine;
   put back byte for byte, its date is now today's) and a file onto the
   Trash: no "os: closed" line in the log.
7. The Picture viewer, timed by the board (decode / total): mascot.bmp
   454 / 662 ms (main 449 / 641), test card.png 389 / 622 (389 / 598), blue
   marble.png 652 / 1,190 (645 / 1,156), earthrise.jpg 896 / 1,484 (892 /
   1,440). Memory back to the byte after the four.
8. The Task Manager: titles and icons as before. Switch To on the Editor
   while its Save As box is open brings the Save As box forward; on a
   minimised Picture viewer it restores it; on Culebra it brings it forward.
   End Task works. A stale row: Culebra's row selected, Culebra killed from
   telnet; the Task Manager dropped the row and its selection at once, and
   Switch To and End Task then did nothing and nothing crashed.
9. `dbg.py misses`: none. Repaint 14.0 ms. VNC and telnet answered.
