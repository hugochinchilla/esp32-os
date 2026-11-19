---
title: "Watchdog, crash record and error screen"
date: 2026-11-17
by: agent
status: to-confirm
area: system
summary: "Heartbeat watchdog, a crash record kept over the restart, and the error screen (backlog 7)"
---

# Watchdog, crash record and error screen

Backlog item 7. Chosen by the agent under the owner's go-ahead to work
through the backlog; the options and reasons are in
`docs/journal/2026-11-17-watchdog.md`. Each is easy to change. Built and
tested on the PC, compiled for the board; not yet run on the board.

- **The watchdog is a heartbeat**: the UI loop reports each time it comes
  round, and a supervisor checks once a second how old the last report is.
  This catches a program that spins and one that waits for ever. The
  supervisor is a callback on ESP-IDF's timer task (priority 22, core 0): no
  task and no stack of its own, nothing added to the video interrupt, the
  Wi-Fi task or the network task. ESP-IDF's own task watchdog stays as it
  was (a warning on the serial port, no restart).
- **15 seconds** of silence, `[watchdog] timeout_s` in `C:\.config\boot.ini`
  (5 to 600; 0: off). The start-up, before the loop runs, is not watched.
  `sys::Watchdog` has room for four tasks; only `ui` is watched today.
- **After a hang the machine restarts at once**, without the shutdown
  handlers (they could wait on what the stuck task holds).
- **Crashes** (bad pointer, `abort()`, assert, stack overflow, interrupt
  watchdog) are caught by wrapping ESP-IDF's `esp_panic_handler` at link
  time; ESP-IDF's own handler runs afterwards as before, so the serial
  report is unchanged.
- **The crash record** is 188 bytes in memory a restart does not clear
  (`RTC_NOINIT_ATTR`), checked by magic word, version and checksum, reported
  once. It names the program (from the process table, when the failing task
  is the UI task), the task, running or waiting, the uptime, and for a crash
  the reason, the addresses and up to 16 return addresses. For a hang it
  said nothing about where the program was stuck; since 2026-11-19 it says
  what the task was doing and, when the task was waiting, its chain of
  calls (`screenshot-requests.md`, backlog 75). Nothing is written to flash at
  the moment of the crash. With no record, an abnormal reset reason from the
  chip (brownout, hardware watchdog) is reported instead.
- **The error screen** is a full screen at the next start, before the
  self-test and also on a fast start, in the self-test's style with the
  mascot on its back (own design). It counts down 15 seconds and goes on by
  itself (`[boot] crash_hold_s`; 0 waits for a key), because a bare board has
  nothing to press a key on; a key or mouse button ends it. The debug link
  answers while it shows.
- **Reports are kept** in `C:\crash.txt`, added to at the start after the
  crash; the file starts again past 16 KB.
- **Testing**: `tools/dbg.py fault hang|pointer|abort [--pid N]` makes the
  board fail on purpose, charged to a program if asked; `tools/dbg.py boot
  crash` draws the screen.
- Not taken: ESP-IDF's task watchdog with panic switched on (it would make
  every 5 s of honest work fatal and names no program); a supervisor task of
  its own (a stack for nothing); a core dump partition (can be added
  later); a message box on the desktop instead of the screen; a longer limit
  while the hourglass shows.
