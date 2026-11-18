---
title: "A BIOS stage in its own partition"
date: 2026-11-17
by: owner
status: decided
area: boot
summary: "The self-test is a BIOS, a small firmware in its own partition that restarts into the OS"
---

# A BIOS stage in its own partition

Decided by the owner: the self-test moves to a BIOS, a small firmware of its
own in its own app partition, which ends by restarting into the OS; init
then shows the splash. First a restart is measured on the board (what the
monitor does, how long a second start takes), with no change to the flash.
Recommended and to be checked against ESP-IDF v5.5: a small hook in the
bootloader starts the BIOS unless a flag in RTC memory says the BIOS has
just run. Not chosen: one image with two logical stages (nothing would
really be separate); the BIOS inside the bootloader (about 32 KB, no
drivers, no PSRAM). Nothing is built. The details still open are under
Still open; the whole conversation is in
`docs/journal/2026-11-17-bios-stage.md`.

Measured on the board the same day
(`docs/journal/2026-11-17-bios-restart-measured.md`): a restart loses the
picture every time, for 3.5 to 3.9 s; the video signal is back 0.76 s after
the reset and the display needs about 3 s more. So the handover from the
BIOS to the OS is a blank of about 3.8 s that ends on the desktop, and a
splash would have to stay about 4 s to be seen. The owner accepted the blank
("Yes, go ahead").
The bootloader hook was confirmed by reading ESP-IDF v5.5's source (not
compiled): our own copy of `bootloader_start.c` starts an app of subtype
`test`, the BIOS, unless the reserved RTC memory
(`CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC`) says it has just run.
