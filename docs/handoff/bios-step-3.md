# Handoff: the Rodent BIOS, what comes after step 2

Written 2026-11-17 by the thread that built the BIOS (backlog item 46), when
the owner asked to stop there and take the rest up later in a fresh thread.
Read `CLAUDE.md` and `docs/DECISIONS.md` first. Nothing below is built, and
nothing below is decided unless it says so.

## Where things stand

On `main` and on the board (PR 40, merge ec8b694): the `bios` partition, the
bootloader's choice, the Rodent BIOS firmware with its first screen, the
self-test and the handover. The story, with every board try, is in
`docs/journal/2026-11-17-bios-second-stage.md` and
`docs/journal/2026-11-17-bios-first-screen.md`; the discussion that led to
it in `docs/journal/2026-11-17-bios-stage.md`.

Measured on the board: from a reset the first screen until 7.0 s, the
self-test's lines until about 8.5 s, the handover at 13.9 s, the desktop at
about 17 s. `tools/dbg.py reboot --fast` 3.2 s; a cable flash with a fast
start armed 1.5 s; a byte on serial during the BIOS ends its waits. The
bench monitor shows a picture 5.5 s after a reset, whatever is on it.

## The owner's direction for the next step (2026-11-17, decided)

The old plan for step 3 had the BIOS show the OS's error screen. The owner
turned that down:

> The BIOS should not have this link with the operating system. It should
> be simply a selector of the partition to start, and something to set the
> time when we have the clock chip, which for now we do not have, and other
> settings that may come later. It does not seem to me the place to manage
> the operating system's errors.

and, for the errors:

> It could be the operating system that starts with a message saying the
> last start failed, with a button to open the report, and it simply opens
> the text editor with the report. It seems more natural to me.

So:

- **The BIOS knows nothing about OS errors.** Self-test, countdown, and
  later a Setup: the partition to start, the clock when there is a clock
  chip, other settings.
- **The OS reports its own crashes on the desktop.** It already writes
  `C:\crash.txt` at its next start (`sys::crash_log_append`). Instead of the
  full-screen error screen (`shell::show_crash_screen`), a dialog once the
  desktop is up: the last session ended in an error, and a button that
  opens the report in the text editor. A new dialog is a program of its own
  (`CLAUDE.md`).

## Proposed, not yet confirmed by the owner

Put to him as a plan; he answered "it can be a next iteration" before
choosing. Ask again before building.

1. The OS loses its own self-test screen (`shell::run_self_test`, the one
   that still says "Chinchilla BIOS", with its approved drawing
   `docs/look/boot/boot-b-self-test.png` and `host/tests/test_boot.cpp`).
   With no BIOS in the flash the OS simply starts with its quiet checks.
   Mind that `tools/mockup/bios_text.py` cuts the mascot out of that
   drawing for the font mock-ups.
2. The BIOS keeps its settings in NVS, not `boot.ini`: how long the first
   screen stays (`bios::kSplashMs`, 6,500) and the countdown. The self-test
   keys leave `boot.ini` (`docs/settings.md`). Both times are for this
   monitor; another will want less, which is a reason for Setup.
3. Share the adapters: the BIOS compiles the OS's `vga.c` and has small
   display, system, memory and clock adapters of its own in
   `firmware/bios/main/main.cpp`.

**The open choice: what the board does right after the OS crashes.**

- A. Through the BIOS, like a PC (recommended to him): every restart shows
  the BIOS, then the OS says the last session failed. About 17 s. The
  bootloader and BIOS then know nothing about OS errors.
- B. Straight back to the OS, as it is today: the OS's panic handler and
  hang watchdog write `CHINCHILLA_SKIP_BIOS` and the bootloader skips the
  BIOS, also after a reset by a watchdog or a brownout
  (`firmware/common/boot_flag.h`). About 3 s, and one small rule about OS
  errors stays in the bootloader.

What A needs, from what the board showed:

- **The crash record does not survive a BIOS run.** It lives in RTC slow
  memory (`RTC_NOINIT_ATTR sys::CrashRecord s_record`, 0x50000000, 188
  bytes, `firmware/os/main/crash.cpp`). The bootloader's log line showed the
  record's remains there before the BIOS ran and six zero words after.
  Which code zeroes it was not found: the BIOS's link map puts nothing
  there and the heap does not include it. The word in the bootloader's
  reserved RTC block (RTC fast memory, 0x600fffe8) does survive, so one way
  is to enlarge `CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC_SIZE` and keep the
  record there; another is to find the cause.
- **The chip's reset reason is replaced by the BIOS's restart.** After the
  BIOS the OS sees "restarted by software", not "watchdog" or "brownout".
  The bootloader already reads the ROM's reason and prints it; it could
  leave it in the reserved block for the OS, as a fact about the hardware.
- The BIOS does not link ESP-IDF's `reset_reason.c`, so it never clears the
  "it was a panic" hint; the OS still finds it after the BIOS. Useful or a
  trap, depending on the design.

## Later, each to be put to the owner

- Setup (the footer says "there is no setup"): the partition to start, the
  monitor times, the clock.
- A boot loop guard: a BIOS or OS that fails at every start.
- The OS image over serial from the BIOS (the "server out-of-band
  management" idea from the first discussion).

## Things worth knowing that are not obvious

- Never `idf.py flash` inside `firmware/bios`: it would write a bootloader
  without our file. Use `tools/flash-bios.sh`, and delete
  `firmware/bios/sdkconfig` after changing `sdkconfig.defaults`.
- A block of zeros has the checksum 0xffffffff, which ESP-IDF reads as "no
  checksum": the boot block must be repaired with
  `bootloader_common_update_rtc_retain_mem(NULL, true)`, never cleared and
  left.
- `bios::TextScreen::reserve()` keeps cells free for pictures; text put
  there is stored and sent to serial but not painted. `clear()` gives them
  back. A picture test must draw what the board draws, in the board's
  order.
- The cloud only compiles. Each of the three faults above passed the PC
  tests and the compiler and showed only on the board; the bootloader's
  one-line log ("chinchilla: reset reason ...") was added so the next one
  shows at once.
- The record that the bench monitor shows a picture 2.4 s after the video
  starts (2026-11-16) no longer holds: 5.5 s, for `main`'s own start too.
- Not tested, because the laptop cannot: the physical button and a real
  power cut.
