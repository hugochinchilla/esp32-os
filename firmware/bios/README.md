# The BIOS

A small firmware of its own, in the `bios` partition (the first megabyte of
the app area, `../os/partitions.csv`). After every reset the bootloader
starts it. It brings up the video, runs the self-test on its text screen
(80 by 30 characters, Spleen 8x16, the mascot on top) and as plain text on
the serial port, checks that there is a system to start, counts down, and
restarts the chip into the OS.

How the two firmwares hand over is in `../common/boot_flag.h`; the
bootloader's part is `../os/bootloader_components/main/bootloader_start.c`;
the story is in `docs/journal/2026-11-17-bios-second-stage.md`.

## Build and flash

The bootloader, the partition table and the OS are built and flashed from
`firmware/os` as before. The BIOS is added with:

```sh
get_idf
tools/flash-bios.sh /dev/ttyACM0
```

Do not run `idf.py flash` in this folder: it would write the BIOS where the
OS lives and Espressif's bootloader over ours. If that happens, flash
`firmware/os` again and then the BIOS.

With no BIOS in the flash the bootloader starts the OS, which then shows its
own self-test as it did before.

## On the serial port

921,600 baud, plain text: the same lines as the screen. A line that changes
(the memory count, the countdown) is written again in place. Any byte
received counts as a key and skips the waiting. There is no debug protocol
here; `tools/dbg.py` talks to the OS.

A restart asked for with `tools/dbg.py reboot --fast` does not pass through
the BIOS. After a flashing it does, for a moment: the BIOS sees the one-shot
word in NVS, prints one line and hands over without starting the video.
