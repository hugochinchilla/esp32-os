# The BIOS as a second stage: partition, bootloader, firmware

2026-11-17. Backlog item 46, step 2 of 3. Compiled and linked for the board
on the cloud machine (ESP-IDF v5.5), tested on the PC. **Not run on the
board.** The board turn for it is a whole cable flash that makes C: anew.

## What was asked

The owner, to "Shall I go on to step 2? That is the one that changes the
partition table and wipes C:":

> Go

Step 2 as it was put to him: "The new partition table, the bootloader hook,
and a BIOS firmware that runs step 1 and restarts into the OS."

## What was found

Reading ESP-IDF v5.5's bootloader before writing ours turned up three
things the plan had not counted on.

- **Choosing a slot is not a harmless question.** The function that reads
  the update record (`bootloader_utility_get_selected_boot_partition`) also
  writes it: a new version becomes "on trial", and a version that was on
  trial becomes "failed". If our code asked it first and then started the
  BIOS, an update would be put on trial by the pass through the BIOS and
  thrown away as failed by the restart into the OS. So the choice between
  BIOS and OS is made first, and the update record is only read on the way
  to the OS.
- **A BIOS that does not load would be a reset loop.** Told to start the
  `test` app, Espressif's loader resets the chip if that image is bad, for
  ever. Our code checks the BIOS's image itself first and starts the OS if
  there is none.
- **The reserved RTC memory is wiped when its checksum is wrong**, which it
  is after every power-on, and the wipe happens while the OS is being
  loaded, after our code has left its word there. Our code puts the
  checksum right before using the block.

And one about flashing: `idf.py flash` writes an app to the first OS slot
whatever the project is. Run in the BIOS's folder it would put the BIOS
where the OS lives.

## Options considered

**Where the BIOS's code is shared with the OS.** Chosen: the BIOS compiles
the OS's video driver file as it is (`firmware/os/main/vga.c`) and has its
own short adapters (display, facts, memory, clock: about 80 lines that also
exist in the OS's `main.cpp`). Moving the OS's adapters into a shared
component is cleaner and was not done: it rewrites the 1,200-line file the
board runs today, in the same change that already moves the flash around.
It belongs to step 3, when the OS loses its self-test anyway.

**How the BIOS gets into the flash.** Chosen: a script,
`tools/flash-bios.sh`, that builds it and writes it at 0x10000 with
esptool. Not chosen: a partition table of its own for the BIOS project that
calls the BIOS the "factory" app, so that `idf.py app-flash` lands in the
right place; then a plain `idf.py flash` there would write a wrong table
and a bootloader without our code, a worse accident than the one avoided.

**What the OS does after the BIOS, before step 3.** The OS still has its
own self-test. Chosen: the bootloader tells the OS that the BIOS has just
run, and the OS then starts as it does for a fast start (the cheap checks,
no screens). With no BIOS in the flash the OS shows its own self-test as
before, so a board without the BIOS still works. Not chosen: removing the
OS's self-test now (step 3), or showing it twice.

**Restarts that should not pass through the BIOS.** `tools/dbg.py reboot
--fast` is what the agents use dozens of times a day; through the BIOS each
would cost its 11 seconds and a second blank. Chosen: arming a fast start
also leaves the "start the OS" word, so that restart goes straight to the
OS. After a flashing the RTC memory is gone (found on this board on
2026-11-16), so there the BIOS does start: it reads the one-shot word in
NVS, prints one line on serial and hands over without starting the video.
A restart from the Start menu, an update, a crash and the watchdog all go
through the BIOS.

**A key.** There is no keyboard yet, so any byte received on serial counts
as a key and skips the waiting, as any key does on the OS's screen. A
consequence: `tools/dbg.py` talking to the board while the BIOS shows makes
it hand over early. That suits the agents; for filming, leave the port
alone until the desktop is up.

**The "Boot device" line**, left out in step 1 until it was true. The BIOS
asks the update record which slot starts next, reads that image and checks
its checksum and hash, and prints for example "ota_0, 1.4 MB, image OK". If
that slot was on trial and never confirmed itself, the bootloader is about
to go back to the other one, and the line says so. With no valid system the
screen says "No operating system found." in red, does not count down, and
the BIOS stays there instead of restarting for ever.

## What was built

- `firmware/os/partitions.csv`: the BIOS, an app of subtype `test`, 0x10000
  to 0x100000; `ota_0` from 0x100000 and `ota_1` from 0x2f0000, 1.94 MB
  each as before; `otadata` at 0x4e0000; C: from 0x500000, 11 MB. `nvs` and
  `phy_init` have not moved.
- `firmware/os/bootloader_components/main/bootloader_start.c`: Espressif's
  file with `selected_boot_partition()` replaced, and what this project does
  not use taken out.
- `firmware/common/boot_flag.h`: the two words, and the choice itself as a
  small function so the PC tests run it.
- `firmware/bios/`: the BIOS. `app_main` installs the serial driver, hands
  over at once on an armed fast start, starts the video on core 1, runs
  `bios::run_post` with the screen's text also sent to serial, and restarts
  into the OS. Only warnings from ESP-IDF are logged, so serial shows the
  BIOS's lines. The bottom line is built from the build's date and the
  chip's flash and RAM: `17/11/2026-ESP32S3-N16R8-CHINCHILLA`.
- `core/bios/post`: the drive's size given by the caller when no drive is
  mounted, the "Boot device" line, the stop when there is nothing to start.
  The rows moved down by two to make room, as in the owner's mock-up.
- `firmware/os/main/main.cpp`: reads and clears the word at start; a start
  after the BIOS is a fast start; arming a fast start also asks for no
  BIOS.
- `tools/flash-bios.sh`, `firmware/bios/README.md`, and the OS firmware's
  README.

## Sizes

| Piece | Size | Room |
|---|---|---|
| BIOS (`chinchilla-bios.bin`) | 301,040 bytes (294 KB) | its partition is 960 KB: 69 % free |
| OS (`esp32-os.bin`) | 1,488,240 bytes (1.42 MB) | each slot is 1.94 MB: 27 % free, as before |
| Bootloader, with our file | 21,664 bytes | it has 32 KB before the partition table |

The BIOS is small because it carries no Wi-Fi, no file system and no
programs: the video, the text screen, the self-test and the mascot.

## How it was tested

- PC: `host/tests/test_bios.cpp` gained the bootloader's choice over the
  restarts of a normal start, a crash before the OS clears the word, and a
  flash with no BIOS; the "Boot device" line, good and bad; the drive from
  the caller. The reference picture was redrawn with the new rows and still
  matches to the pixel. All 42 PC tests pass.
- Cloud: both firmwares compile and link with ESP-IDF v5.5, the OS's with
  our bootloader file.
- Not tested: anything on the board. In particular, that the reserved RTC
  memory keeps the word across `esp_restart()` on this chip, that the
  monitor behaves at the handover as it did in the restart measurement,
  that updates over Wi-Fi still go on trial and come back, and the times.

## For the board turn

1. `tools/dbg.py` copies of anything on C: worth keeping; C: is made anew.
2. In `firmware/os`: delete `sdkconfig`, `idf.py build`, `idf.py -p PORT
   flash` (bootloader, table, `otadata`, the OS in `ota_0`). Not
   `erase_flash`: `nvs` stays, with the Wi-Fi networks.
3. Without the BIOS yet: the board should start as before, with the OS's own
   self-test, and the bootloader's log should say "no BIOS in the flash".
   C: is formatted and gets its sample files.
4. `tools/flash-bios.sh PORT`. From now a reset shows the BIOS, then a blank,
   then the desktop with no second self-test.
5. To check: the BIOS's lines on serial at 921,600 baud; the countdown; a
   byte on serial skips it; `tools/dbg.py reboot --fast` goes straight to
   the desktop; a restart from the Start menu passes through the BIOS; the
   time from reset to desktop; an update with `tools/ota.py` goes on trial,
   is confirmed, and a build that hangs comes back to the old one;
   `tools/dbg.py misses`.

## What was learned

- The plan said "a hook of about 20 lines". The lines are about that many;
  the three paragraphs of "What was found" are why they are in the order
  they are. Each would have shown on the board only as something rare and
  confusing: an update that never sticks, a board that resets for ever
  after a bad BIOS flash, an OS that shows its self-test again after the
  BIOS on a cold start only.

## What is left

- The board turn above.
- Step 3: the BIOS's settings in NVS (until then it uses the built-in
  defaults: 2.5 s for the monitor, pace 130 %, a 5 s countdown; `boot.ini`'s
  keys only reach the OS's own screen, which shows only without a BIOS);
  the crash screen in the BIOS; the OS's self-test removed; the adapters
  shared.
- Later: Setup, the boot loop guard, an OS image over serial.

## First try on the board: a stack overflow

The first flash of the BIOS (commit 1bf5acd) restarted about every 5 s with
"stack overflow in task main", right after the external memory test. The
handover was not reached, so it was not the word in RTC memory.

The cause: the BIOS ran the self-test in ESP-IDF's main task, which has a
3.5 KB stack. The OS runs the same self-test in its `ui` task, with 16 KB.
The cloud can only compile and link, so it could not show this.

The fix: the BIOS starts a task of its own, `bios`, with 16 KB, for the
self-test and the handover; the main task only starts the serial driver
and the video. Raising the main task's stack in `sdkconfig.defaults` would
also work, but only if the old `sdkconfig` is deleted first, and a task in
the code cannot be forgotten. Compiled; BIOS 301,088 bytes. Not yet run on
the board.
