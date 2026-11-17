# A folder is listed in one pass (backlog item 26, found on the board)

## What was asked

The board turns for PR 43 and PR 44 both came back with the same failure,
which blocks the merge: with the File manager on a task of its own, a
folder of 50 or more files does not open. Run `C:\m80`: nothing happens,
nothing in the log; 10 and 30 files open. `main`, where the File manager
runs on the UI task, opened an 80-file folder earlier the same day. Each
try left external RAM 18 to 24 KB lower (6,375,420, then 6,351,772, then
6,333,400 bytes free). It does not happen on the PC: `test_os` opens a
folder of 81 files with the File manager on a real thread, also when the
open is made late on purpose.

## What was found

`shell::Listing::read` asked for a folder's entries one by one,
`os::file::entry(folder, i)`, and on the board each of those
(`fs::PosixVolume::entry`) opens the folder, steps over the first `i`
names, asks for the size and date of the one it wants and closes the folder
again. A folder of n files costs n openings and about n squared over two
steps: 80 files, 3,240 steps; 30 files, 465; 50 files, 1,275. Between 30
and 50 files is where the board's results change, and since item 26 a
program is waited for two seconds before it is "not responding".

That the listing takes more than two seconds at 50 files is read from the
code and fits the figures; **it was not timed on the board.** What happens
after that is not understood: a late program should still finish, slowly,
through the turns the UI task gives it (`prog::Runner::serve`), and the
window should come. On the board it did not in the time the board thread
waited, and the memory each try took was not given back. That part is
still open.

## The options

| Option | Chosen | Why, or why not |
|---|---|---|
| **List the folder in one pass** | Yes | It removes the cause for any folder the listing can hold (the n squared), costs nothing elsewhere, and is right whatever the limit is |
| A longer limit while a window is being opened | No | It hides the cost; a larger folder would find the new limit |
| More turns for a late program | No, not now | Worth looking at once it is known why the late File manager did not finish; it is a design choice for the owner |

This one was not put to the owner: the first option changes nothing he
can see and the other two stay open.

## What was built

- `ports::Volume::entries(folder, first, out, capacity, count)`: a run of
  entries in one pass. A drive that does no better is asked entry by entry
  (the default); `fs::PosixVolume`, the board's drive, opens the folder
  once for the run.
- `fs::Storage::entries` and the OS call `os::file::entries`
  (`docs/os-calls.md`).
- `shell::Listing::read` asks for eight entries a call (640 bytes of the
  program's stack). 80 files are now 10 openings and about 440 steps
  instead of 80 and 3,240.

## How it was tested

`host/tests/test_fs.cpp`, on the RAM disk and on a folder of the PC: runs
of 1, 7, 20 and 32 give the same entries in the same order as asking one
by one; fewer than asked for ends the folder; a folder that is not there
gives none. The PC gate passes and the firmware compiles and links. **Not
run on the board**: the 80-file folder is in the next board turn, with the
time it takes to open.

## What is left

- If the folder opens: time it, and try the largest the listing holds.
- Why a File manager that was late never finished on the board, and what
  became of its memory. `dbg.py fault slow` is busy without asking the
  system for anything; a late program that makes OS calls has only been
  seen working on the PC.
