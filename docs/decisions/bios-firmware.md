---
title: "Chosen for the BIOS firmware"
date: 2026-11-17
by: agent
status: to-confirm
area: boot
summary: "The `bios` app partition and how our bootloader starts it (backlog 46, step 2)"
---

# Chosen for the BIOS firmware

Heading in the single-file log: "Chosen for the BIOS firmware (2026-11-17, to confirm)".


Step 2 of backlog item 46, compiled but not run on the board
(`docs/journal/2026-11-17-bios-second-stage.md`).

- The BIOS is the app partition `bios` (subtype `test`, 960 KB at 0x10000);
  C: is 11 MB; `nvs` did not move. (Since 2026-11-19 the partition is 896 KB
  and C: is 9 MB: [App slots of 3 MB](app-slots-3-mb.md).)
- Our bootloader file starts the BIOS at every start unless a word in the
  reserved RTC memory asks for the OS. The BIOS writes the word and
  restarts. With no good BIOS image in the flash the OS starts as before.
- The OS started by the BIOS does not repeat the self-test. Until step 3
  the OS keeps its own, for a flash with no BIOS.
- An armed fast start (`tools/dbg.py reboot --fast`, after a cable flash)
  passes the BIOS without a picture.
- Any byte on serial counts as a key in the BIOS.
- A "Boot device" line names the slot and checks its image; with nothing to
  start the BIOS says so and stops.
- The BIOS compiles the OS's `vga.c` and has small adapters of its own;
  sharing the adapters is step 3.
- The BIOS is flashed with `tools/flash-bios.sh`, never `idf.py flash` in
  `firmware/bios`, which would write a bootloader without our file.
