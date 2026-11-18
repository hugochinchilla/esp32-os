---
title: "Chosen for the desktop program"
date: 2026-11-17
by: agent
status: to-confirm
area: tools
summary: "Chinchilla as an SDL2 program on the PC, `host/desktop/main.cpp` (backlog 32)"
---

# Chosen for the desktop program

Heading in the single-file log: "Chosen for the desktop program (2026-11-17, to confirm)".


Backlog item 32; the owner asked for it and left the details to the agent
("dejo a tu criterio"). Journal: `docs/journal/2026-11-17-desktop-program.md`.
Build and use: `README.md`, "Chinchilla as a desktop program".

- `host/desktop/main.cpp` is to the PC what `firmware/os/main/main.cpp` is to
  the board: adapters and the loop, on SDL2. `core/` is not changed. It is
  built only where SDL2 is installed; the PC tests need nothing new.
- The window can be any size. The picture keeps 4:3, centred, black around
  it. It is first drawn a whole number of times larger with hard pixel
  edges, then smoothed down to fit: sharp at 1x, 2x, 3x, even in between.
- Chinchilla's own pointer is shown and the PC's is hidden over the window.
  Mouse positions are absolute; the mouse is not captured.
- Keys go to the core by their place on the keyboard (USB codes, which is
  how SDL numbers them); the core types US characters, as on the board.
  Keyboard layouts belong in the core, for the board too.
- The PC keeps F11 (whole screen) and F12 (a picture of the window).
  Ctrl+Alt+End stands in for Ctrl+Alt+Del.
- C: is a folder, `~/.local/share/chinchilla/C` unless `--drive` names
  another. Ordinary files; a new folder gets the samples.
- No sound. The core has no sound port and makes no sound yet, so there is
  nothing to play. The program gets sound in the change that gives the core
  its sound port.
- No Wi-Fi: Network says it is not available. The simulator's pretend
  networks are not shown to people who are not testing. The clock is the
  PC's.
- The start-up screens show, as on the board (`--fast`, a key or a click
  skips them), without the wait for the monitor.
- Closing the window is Shut Down: programs are asked first. A second close
  within 3 seconds leaves at once. Restart starts the program again.
- The memory figures in Task Manager and the self-test are the simulator's
  pretend ones, not the PC's and not limits.
- An `inetd` that serves nothing (the owner's choice, 2026-11-17), here and
  in the browser: the start-up file is the board's, which starts inetd,
  and without one every new C: opened with a "Start-up" box. The file stays
  one file for every machine. The row has no task behind it, the one
  exception to that rule. Journal: `docs/journal/2026-11-17-empty-inetd.md`.
