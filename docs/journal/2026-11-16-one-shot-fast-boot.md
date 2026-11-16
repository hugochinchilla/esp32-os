# A one-shot fast start for debug restarts

## What was asked

The owner asked (through the coordinator) for a way to make the agents' own
restarts skip the self-test and everything that slows start-up on purpose, so
that a debug restart does not cost 11 seconds, without changing boot.ini:
`tools/dbg.py reboot --fast`, and a way to arm it before flashing.

## What was found

A normal start reaches the desktop 11.3 s after the video starts (the monitor
wait, the self-test lines, the memory test, the 5 s countdown). `fastboot =
true` in boot.ini already skips all of that, but it is a setting, and the
board must keep starting normally for the owner.

## Options considered

- RTC no-init memory (`RTC_NOINIT_ATTR` with a magic value): no flash write,
  survives software restarts, lost on power-off. Built and tried first.
- NVS: survives every reset and flashing (flashing writes the app, not NVS);
  costs one small flash write when armed and one when taken.

## What was chosen and why

NVS. On the board the RTC flag survived `reboot --fast` but not the reset
after flashing (esptool's reset into the download mode and back cleared it),
and arming before flashing is half of the request. The magic-word logic is
portable (`shell::take_one_shot_fast_boot`), read once at start-up and cleared;
the flash is written only when the flag was armed.

The protocol gained no new message: Reboot takes an optional flag byte (bit 0:
the next start is fast), and Boot screen takes a third value, 2, that shows
nothing and only arms (docs/debug-protocol.md).

## What was built and tested

`core/include/core/shell/boot.hpp`, `core/src/dbg/protocol.cpp`,
`ports::System::arm_fast_boot()`, `firmware/os/main/main.cpp`, `tools/dbg.py`.
PC test in `host/tests/test_boot.cpp`: taken once, then not; any other value
is not taken. On the board: `reboot --fast` reached the desktop 0.27 s after
the video started and logged "one-shot fast boot"; the restart after it ran
the full self-test (11.3 s); `boot fast-next` followed by a flash gave a fast
start, and the start after that was normal again.

## What was learned

RTC memory is not a safe place for anything meant to survive flashing on this
board.

## What is left

Nothing. The agents use it for their restarts from now on, except when the
start-up screens are being tested or shown.
