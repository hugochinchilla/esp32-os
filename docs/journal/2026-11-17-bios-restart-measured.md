# What a restart does to the monitor: measured for the BIOS stage

2026-11-17. Backlog item 46. A measurement, no code: nothing changed on the
board or in the repo.

## What was asked

The owner chose to give the BIOS a partition of its own, "measure first"
(`2026-11-17-bios-stage.md`). A BIOS in its own partition hands over to the
OS by restarting the chip, so two things had to be known before going on:
what the monitor does at a restart, and what a second start costs.

## How it was measured

By the board thread, on `main` at 68cf77e. Restarts from the debug link
(`tools/dbg.py reboot`, with and without the fast flag), the monitor filmed
by the webcam at a real 31.5 frames a second (about ±0.03 s), the times of
the start-up read from the serial log. The loss of the picture could not be
judged from the average brightness, because the webcam's automatic exposure
hides it; the largest frame-to-frame changes over the screen area were used
and checked by eye on strips of frames.

## What was found

The monitor loses the picture at every restart. Time with nothing on the
screen, from the picture going to a picture coming back:

| Restart | From | Runs | Blank |
| --- | --- | --- | --- |
| Fast | The desktop | 3 | 3.77 to 3.87 s. The picture returns as the desktop |
| Normal | The desktop | 3 | 3.52 to 3.62 s. The picture returns with the self-test's text already there |
| Fast | A white screen | 1 | About 3.77 s |
| Fast | A black screen | 1 | The going cannot be seen (black looks like no signal); the picture returned at the same point as in the other runs |

The serial log of a fast restart, in time after the reset (the ROM's own
lines are at another baud rate and are not counted):

| Moment | Time |
| --- | --- |
| Espressif's bootloader starts | about 29 ms |
| The app is loaded | 317 ms |
| Both cores running | 447 ms |
| `app_main`, our first line, and the start of the video output | 759 ms |
| The desktop drawn | 1,229 ms |

A normal restart: the self-test takes 11,259 ms, the desktop at about 11.6 s.
`tools/dbg.py misses` afterwards: 0.

## What it means

- **The blank is the display, not us.** The video signal is back about
  0.76 s after the reset; the display takes about 3 s more to show it. This
  is an inference from the two tables, not measured directly, and it cannot
  tell the monitor from the VGA-to-HDMI adapter in the chain.
- **The blank at the handover would be about 3.8 s**, not the 2.4 s guessed
  in the discussion from the cold-start figure.
- **A second start costs about 0.76 s** to our first line, 1.2 s to a drawn
  desktop. The guess was half a second.
- **A splash shown at once would not be seen.** The OS has the desktop drawn
  1.2 s after the reset and the display shows nothing until about 3.8 s, so
  with `splash_ms` at its default of 0 the picture simply returns as the
  desktop. For the splash to cover the blank and be seen it would have to
  stay about 4 s, which adds about 3 s to the start.
- **A cold start with a BIOS**, with today's 5 s countdown: about 11.3 s of
  BIOS, then about 3.8 s of blank ending on the desktop, about 15 s in all
  against 11.6 s today. With the countdown at 1 s it is about the same as
  today.
- What the screen showed before the restart (desktop, white) made no
  difference to the blank.

## The bootloader hook, read in ESP-IDF's source

The discussion recommended a small hook in the bootloader, "from memory, to
be checked against v5.5". Checked by reading the `release/v5.5` source on
GitHub; nothing was compiled.

- `components/bootloader/subproject/main/bootloader_start.c` chooses the
  partition in `selected_boot_partition()`, which ends with a comment
  "Customer implementation" at the place where a project is meant to put its
  own rule. It already knows a third kind of app besides the factory app and
  the update slots: the `test` subtype, started by returning
  `TEST_APP_INDEX` (today only when a pin is held, with
  `CONFIG_BOOTLOADER_APP_TEST`).
- A project replaces that file with its own copy in a `bootloader_components`
  folder (`examples/custom_bootloader/bootloader_override`).
- `CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC` reserves a few bytes of RTC memory
  at a fixed address, "valid when rebooted, except for power loss", that the
  bootloader and the app can both read (`rtc_retain_mem_t`). It also keeps a
  count of restarts, which is what a boot loop guard needs.

So the plan holds: the BIOS is an app of subtype `test` in its own
partition; our copy of `bootloader_start.c` returns `TEST_APP_INDEX` unless
the reserved RTC memory says the BIOS has just run, and otherwise leaves the
choice between the update slots as it is. Whether the reset after flashing
clears this memory as it cleared our own RTC variable does not matter here:
a cleared flag means "run the BIOS", which is right after a flash.

## What was chosen

Asked on a card, "Accept almost 4 s of black screen between the BIOS and the
desktop?", the owner chose **"Yes, go ahead"**. Not chosen: "No, one image
instead" (the BIOS screen inside the one firmware, no restart and no blank)
and "Keep talking". The agent's reason for recommending it: a real second
stage is what was asked for, and a monitor going black between the BIOS and
the system is true to a PC of the time.

## What is left

- How the flash is split, put to the owner next.
- The hook is only read, not built: compile it, and try it on the board.
- The clips are in the laptop session's scratchpad, for the film:
  `bios-desktop-fast-1..3.mkv`, `bios-desktop-normal-1..3.mkv`,
  `bios-white-fast-1.mkv`, `bios-black-fast-1.mkv`.
