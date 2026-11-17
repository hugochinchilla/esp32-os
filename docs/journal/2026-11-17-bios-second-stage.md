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

What the first try did show (the board thread's report, on 1bf5acd):

- The OS on the new table with no BIOS passed: `idf.py flash` wrote only
  0x0, 0x8000, 0x100000 and 0x4e0000; the bootloader said "no BIOS in the
  flash: starting the OS"; the OS's self-test took 11,286 ms with nothing
  failed, C: was 11 MB, formatted with its samples, and the saved Wi-Fi
  network joined.
- With the BIOS, `tools/flash-bios.sh` wrote only 0x10000 and the
  bootloader said "starting the BIOS". The BIOS printed its title, the
  bottom line, the processor line and counted external memory to 5760K
  before the overflow, every 4.9 s. The monitor never locked on in that
  time, so there was no picture.
- The way back is tested: a cable flash of `main` and the saved raw image
  of the old C: written back. All 19 files on C: came back byte for byte,
  the Wi-Fi networks survived both changes of table, no missed refills.
- The laptop's ESP-IDF builds slightly smaller files than the cloud's
  (bootloader 21,168, BIOS 287,296, OS 1,480,832 bytes): another patch
  release of 5.5.

## Second try on the board: the BIOS runs, the handover does not

With the 16 KB task (commit de26d90) the BIOS ran every line on the monitor
and on serial: 5760K external, 284K internal, the video, "drive C: 11 MB",
"keyboard: serial console", "Boot device ... ota_0, 1.4 MB, image OK",
"Starting Chinchilla..." and the countdown from 5 to 1, about 6.7 s from
reset. A byte on serial skipped the countdown within about 50 ms.

Then the board restarted and the bootloader said "starting the BIOS" again,
never "BIOS done: starting the OS": a loop every 11.7 s. The word the BIOS
left was not there when the bootloader looked, the one thing named as the
main risk before the turn. The board went back to `main` the tested way.

Reading ESP-IDF's source again did not find the cause. Checked and ruled
out on paper: the block is at the same address in the bootloader and in
the BIOS (0x600fffe8, the word at 0x600ffff4, from the BIOS's link map);
both builds reserve the same 8 bytes and leave them out of the checksum;
the heap does not include the block, so the memory test cannot write over
it; `esp_restart()` on this chip resets the processors only; nothing in
the bootloader's start clears RTC memory. So the next flash measures
instead of guessing (the board thread's suggestion):

- the bootloader prints the reset reason, whether the block's checksum was
  right, and the block's six words as found, before it changes anything;
- the BIOS prints the block before and after it writes the word, and the
  address;
- the BIOS also leaves a copy of the word in the last word of RTC slow
  memory (0x50001ffc), which the bootloader prints but does not use. If
  the fast memory loses the word and the slow one keeps it, that is the
  way out.

The handover itself is unchanged. Compiled; not run.

## Third try: the cause, in the board's own words

The diagnostic build (88a8d47) printed, after a power-on and then at every
loop:

```
chinchilla: reset reason 1, block at 0x600fffe8 NOT valid: 16879b54 a5131b2c 1c3bbf25 143e40a3 1a6a6b2d 29f150a3, slow copy 84a20258
handover: block at 0x600fffe8 before 00000000 00000000 00000000 00000000 00000000 ffffffff
handover: word at 0x600ffff4 after  00000000 00000000 00000000 534f4843 00000000 ffffffff, slow copy 534f4843
rst:0xc (RTC_SW_CPU_RST),boot:0x8 (SPI_FAST_FLASH_BOOT)
chinchilla: reset reason 12, block at 0x600fffe8 NOT valid: 00000000 00000000 00000000 534f4843 00000000 ffffffff, slow copy 534f4843
```

So the memory keeps the word across the restart ("534f4843" is "CHOS"),
in both places. What loses it is the checksum, the last word. After a
power-on our bootloader file cleared the block to zeros and had the
checksum computed. The checksum of twelve zero bytes, the way the chip's
ROM computes it, is 0xffffffff, and ESP-IDF reads exactly that value as
"no checksum": `crc != UINT32_MAX` in `is_retain_mem_valid()`. So at the
next start the block was "not valid" again, and our own repair cleared it,
the word included, before reading it. Checked on the PC:
`zlib.crc32(bytes(12), 0xffffffff)` is 0xffffffff.

Espressif's code never meets this because it counts each start in the
block before computing the checksum, so the block is never all zeros. The
BIOS is started as the "test" app, a path that does not count.

The fix: when the block is not valid, the bootloader file now repairs it
the way Espressif does, counting the start (`reboot_counter` becomes 1;
the checksum is then 0x4743989a). The count stops at 65,535 and never
returns to zero. The copy in RTC slow memory and the BIOS's two "handover"
lines are removed; the bootloader keeps one line with the reset reason and
the block, which costs nothing and would have shown this at once.

The board thread found the cause from the same lines at the same time and
named three other ways past it (read the word before the repair, have the
BIOS write a checksum, use RTC slow memory). Not taken: the first would
read rubbish after a power-on unless guarded by the reset reason; the
second puts Espressif's checksum rule into our BIOS; the third uses memory
nothing reserves for us.

## Fourth try: the handover works; two things it showed

With 6ff2dde the board did what was meant. On serial, word for word:
"chinchilla: reset reason 12, boot block valid: 00000000 00000000 00000001
534f4843 00000000 4743989a", "BIOS done: starting the OS", "started by the
BIOS: its self-test is not repeated", "init: started desktop". A byte on
serial skips the countdown and the desktop is there 8.2 s after the reset;
`dbg.py reboot --fast` goes straight to the OS (desktop 3.2 s after the
request); a restart from the Start menu passes the BIOS; a reset by the
board's EN pin gives the BIOS and then the OS; an update over Wi-Fi goes
through the BIOS, "on trial for 10 s", then "confirmed", so the pass
through the BIOS does not count against the trial. Wi-Fi, telnet, the web
viewer and VNC work, memory is as on `main`, no missed refills. Not merged,
for the first of these two:

**1. The error report was lost.** After `dbg.py fault abort` the OS's error
screen said only "The machine restarted by itself: a panic that left no
record", where `main` names the program, the task, the place and the chain
of calls. Two things were found by reading:

- Why "a panic": the BIOS does not use ESP-IDF's reset-reason code, so the
  linker leaves it out, and with it the start-up step that clears the
  "it was a panic" mark. The mark was still there when the OS started.
- Why "no record": not found. The record lives at the start of RTC slow
  memory (0x50000000, 188 bytes). The BIOS's link map puts nothing there,
  the heap does not include it, and the third try showed the end of that
  memory surviving a restart. Something between the panic and the OS's
  start spoils it all the same.

Options for the error report:

- A. After an error the OS restarts straight into the OS, not through the
  BIOS, and starts exactly as on `main`: its own self-test, the error
  screen with the whole record. A reset by a watchdog or a brownout does
  the same, so the OS still learns the cause from the chip (a pass through
  the BIOS would turn it into "restarted by software").
- B. Find what spoils the record first, and keep the BIOS in the path.
- C. Move the error screen into the BIOS now (step 3's plan).

Chosen: A, to confirm with the owner, because it is the behaviour of
`main` unchanged, it does not depend on the unknown, and step 3 replaces
it anyway. Not B alone: an unknown cause could take several board turns.
Not C: step 3 has not been put to the owner yet, and it would meet the
same unknown. B is still wanted for step 3, so the bootloader's line now
also prints the first six words of RTC slow memory: a restart from the
Start menu after an error shows them before and after the BIOS ran.

Built: a third word, `CHINCHILLA_SKIP_BIOS`, written by the OS's panic
handler and by its watchdog when they stop the machine; the bootloader
starts the OS and clears the word. The bootloader treats the chip's own
watchdog and brownout reset reasons the same way. Both rules are in
`firmware/common/boot_flag.h` and run in the PC tests.

**2. The self-test is not seen after a restart on this monitor.** From the
webcam, for `dbg.py reboot`: picture gone at about 2.0 s, the monitor's "no
signal" box at 5.5 s, the BIOS visible only at about 8.5 s and already at
"Booting in 5"; BIOS until 13.5 s; blank from 14.0 to 17.0 s; desktop at
17.5 s. So the first blank is about 6.5 s, the second 3 s as measured
before, and the monitor took about 5.7 s to show the BIOS after its signal
returned, against 2.4 s on a cold start and about 2.2 s at the second
handover. The BIOS waits 2.5 s for the monitor, as the OS does, so the
whole self-test passed unseen. Not explained yet. One thing that could do
it is a missed video refill during the BIOS's memory test, which breaks
the signal's timing and makes a monitor search again; the BIOS now says on
serial how many refills were missed, if any. This is the owner's to
decide once the cause is known: a longer wait costs time at every start.

Both firmwares compile (bootloader 22,176 bytes, BIOS 301,280, OS
1,488,288); the PC tests pass. Not run on the board.

## Fifth try: every check passes; the monitor is still late

With fe550f4 (and `main` merged in on the laptop) the board passed all the
checks, 44 gate tests included:

- `fault abort`, `fault hang` (the watchdog after 15.5 s) and `fault
  pointer`: the bootloader says "the OS stopped on an error: starting the
  OS without the BIOS" and the error screen shows the whole record again.
- An update over Wi-Fi broken on purpose, `fault abort` 2 s into its trial:
  the board goes back to the old version in `ota_0`, without the BIOS.
- No "Video: N refills missed" line, and no misses counted.

What spoils the error record is now half known. Around a restart from the
Start menu after an error screen, the bootloader printed the start of RTC
slow memory with the record's remains before the BIOS ran, and six zero
words after it. So the BIOS run leaves RTC slow memory zeroed, all of it or
at least its start; which code does it is not found. It matters for step 3,
where the BIOS is to read that record: it will have to be read at the very
start, or kept elsewhere. Open.

The monitor, from the webcam: after a restart by software the screen is
black until about 6.25 s, the BIOS shows from 6.25 to 11.5 s, 3.6 s of
black, the desktop at 15.1 s. After a reset by the EN pin the BIOS shows
from about 5.75 s. So the BIOS is seen only from "Booting in 5" either
way, it is not the restart, and it is not missed refills.

One difference from the OS was found in the BIOS's build: it lacked the two
settings the OS has for the video, code run from PSRAM
(`CONFIG_SPIRAM_XIP_FROM_PSRAM`) and the video interrupt in internal RAM
(`CONFIG_LCD_RGB_ISR_IRAM_SAFE`). Without them every look at the flash
(the "Boot device" check reads the whole OS image) stops the video
interrupt for a moment. They are added, since the BIOS runs the OS's video
code and should run it the same way. Whether this is why the monitor is
late is not known: the checks that read the flash come after the wait, not
during it. Compiled (BIOS 302,928 bytes); not run.

Not tested on the board, and cannot be from the laptop: the physical
button and a real power cut.
