---
title: "Chosen for the Task Manager, Shut Down and the Defragmenter"
date: 2026-11-16
by: agent
status: to-confirm
area: programs
summary: "Task Manager on Ctrl+Alt+Del, the Shut Down and power-down screens, the pretend Defragmenter"
---

# Chosen for the Task Manager, Shut Down and the Defragmenter

Heading in the single-file log: "Chosen for the Task Manager, Shut Down and the Defragmenter (to confirm)".


Built on a branch from the owner's backlog (2026-11-16), tested on the PC
only; not yet run on the board. Each is easy to change. Pictures:
`host/tests/pictures/34-task-manager.png` to `38-power-down.png`.

- Task Manager: Ctrl+Alt+Del opens it from anywhere (the key never reaches
  the program in use), and it is in Programs. It lists every window that
  has a taskbar button, except itself. Switch To (or Enter, or a double
  click) brings one forward. End Task closes the program at once, with its
  open dialogs and without its "save changes?" question: unsaved text is
  lost, as with a program that hangs. The other option is to ask the program
  to close as its close button does. Ctrl+Alt+Del does nothing while the
  Shut Down dialog is open.
- Its figures are all measured, once a second. Programs here are windows
  of one task, so there is no figure per program. "Desktop" is the share
  of one core the UI task spent on input, timers and painting
  (`Wm::busy_us`); "Video out" is the time spent filling video lines
  (`scanout_us` of the System port); the graph shows their sum over the
  last minute. Memory is used against total for internal and external RAM.
  The other option for the processor is FreeRTOS's own run-time statistics
  (every task, both cores), which needs two settings in `sdkconfig` and
  code in the firmware adapter; it can replace this without changing the
  window. On the PC the video share is 0 and free memory is a fixed,
  plausible figure.
- (2026-11-19: what follows about Shut Down calling itself off, and the
  night sky, was changed by "The Shut Down sequence",
  `shut-down-sequence.md`. The dialog and the wording are as below.)
- Shut Down: the dialog now has Shut Down (the default), Restart and
  Cancel. Shut Down asks every program to close as its close button would.
  An unsaved file in the Editor calls it off and leaves the Editor's
  question on screen; after answering, Shut Down has to be chosen again
  (98 carries on by itself). Then the screen is handed over for good: the
  mascot asleep under a night sky, "It is safe to power down the system
  now." (the owner's wording) and a line saying that Ctrl+Alt+Del starts
  Chinchilla again, since the board has no power switch to press. The
  pointer goes and all other input is ignored. Nothing needs flushing:
  the storage port has no open-ended writes (a file is on the flash once
  it is closed), and every program has closed by then.
- Defragmenter: a show only. Its disk is made up (864 blocks, about 60 %
  full, some 30 files in pieces, laid out from a fixed sequence of seeds);
  no drive is read or written, and the window says so. Each file has its
  own colour, so scattered colours gather into bands (98 coloured blocks by
  state). A move shows the blocks being read (yellow), then written (red),
  30 ms a step, up to 6 blocks at a time: a full run takes about a minute.
  Start, Pause and Resume are one button; Scramble makes a new mess;
  closing the window pauses. The other option is a map that follows the
  real drive's fill level and file count.
- Icons reused: the Task Manager has the control-panel icon, the
  Defragmenter the drive icon.
