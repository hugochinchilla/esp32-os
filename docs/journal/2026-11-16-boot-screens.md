# Start-up screens and timing

## What was asked

The start-up screens (a self-test and a splash, drawn from the approved
designs in `docs/look/boot/`) were built earlier the same day. Then, in the
owner's words:

> The self test screen is looking good, but the spash looks nothing like the
> screen I approved on the design phase

and, about the self-test's values:

> use accurate data, the approved design is for guidance

Later:

> I want the self-test screen to hold the last state for 5 seconds, with a
> press any key to continue and a countdown (booting in 5,4 3, 2, 1).
>
> And hide the splash screen, make it's time configurable and default to 0.
>
> Make the self-test countdown configurable too, 5 seconds default value, all
> in boot.ini

## What was found

- After a restart the webcam never showed the start-up screens: the bench
  monitor needs about 2.4 s after the video signal starts before it shows a
  picture, and the screens were over by then (self-test 624 ms, splash
  511 ms, desktop 1.25 s after the UI task started). A VGA monitor cannot
  tell the board it is ready: the signal is one-way, and DDC is not wired on
  the adapter board.
- An all-black screen made the monitor slower still: 5.4 s instead of 2.4 s
  to show a picture.
- The splash had drifted from the approved drawing.

## Options considered

- A fixed wait for the monitor (a time, since there is no signal back), with
  a key to skip, against no wait. The wait was chosen.
- Drawing the self-test with the drawing's own made-up numbers, or with the
  board's real ones. The owner chose the real ones.

## What was chosen and why

- The self-test runs for real and its lines start appearing
  `monitor_wait_ms` (2500) after the video starts, one by one at
  `selftest_pace` (130 %); the header is drawn at once so the monitor locks
  sooner. Any key or mouse button skips to the desktop; the checks still run
  and are logged. `fastboot = true` shows neither screen.
- Real values: the version, the processor, the KB the memory tests really
  covered (a quick test of free internal RAM joined the external one), "12 MB
  flash disk", "serial debug link", "none".
- The splash was redrawn to match the approved drawing pixel for pixel, with a
  PC test against `docs/look/boot/boot-e-colour-ribbon.png`; then, at the
  owner's request, hidden by default (`splash_ms = 0`: not drawn at all, no
  blank frame). Its code and its test stay.
- When the lines are done the screen stays with "Press any key to continue"
  and "Booting in 5" down to 1 (`selftest_hold_s`, default 5, 0 for none).
- All in `C:\.config\boot.ini`, listed in `docs/settings.md`.

## What was built and tested

`core/src/shell/boot.cpp`, `boot_settings()`, the BootInput sink in
`firmware/os/main/main.cpp`. PC tests: the splash and self-test against the
approved drawings, the waits, the skip, the countdown's length, no splash
drawn at 0. On the board, filmed with the webcam: the monitor shows a picture
about 3.1 s after reset; the memory count climbs to about 5760K over 1.7 s;
the countdown is readable on the monitor; a key during the countdown goes
straight to the desktop. Boot to desktop with the new defaults: 11.7 s after
reset (11.2 s after the video starts), the last 5 s the countdown.

## What was learned

The monitor, not the board, sets the pace: anything shown in the first 2.4 s
is never seen, and a black screen delays it further.

## What is left

Nothing open beyond the provisional values in `DECISIONS.md` (Start-up
timing).
