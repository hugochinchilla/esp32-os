# The BIOS's first screen

2026-11-17. Backlog item 46, part of step 2.

## What was asked

On the board the BIOS's self-test was not seen: the bench monitor shows a
picture only about 5 s after a reset, and by then the BIOS was already at
"Booting in 5" (`2026-11-17-bios-second-stage.md`, the fourth and fifth
tries). It is not the restart and not missed video refills; the monitor
simply takes that long.

Asked what the BIOS should do about it, the owner answered with an idea of
his own instead of the three choices offered (wait longer, merge as it is,
hold):

> Add a first screen like the AMI BIOS did, with the energy efficiency
> logo or such. For 2 or 3 seconds, then self-test as it is now, even
> removing the artificial memory check slow down. That should add some
> time for the monitor to wake and lock a signal, and if it is not visible
> nobody misses it.

The PCs of the nineties opened with their BIOS maker's logo and the
"Energy Star" badge in a corner before the self-test. Ours does the same,
with a badge of our own: the real one is a registered mark and is not
used.

## The drawing, and the owner's changes

The first drawing had the mascot large on the left and, on the right, a
square green frame holding a leaf with a lightning bolt, "E N E R G Y /
N I B B L E R" in spaced capitals, and two lines: "Runs on a USB port."
and "No fan. No noise."

The owner's answer: he does not like the square green border; maybe a
round one, and only around the leaf; the name below it and smaller; only
the USB line, without "no fan, no noise".

The second drawing (`docs/look/bios-text/bios-splash.png`): a ring of two
pixels around the leaf, "Energy Nibbler" under it in ordinary letters,
"Runs on a USB port" below. The name is the agent's proposal: the owner
said "Energy Enabler" aloud, taken to be the same name; to confirm.

Also on the screen: "Chinchilla BIOS" and the version, "Copyleft 2026,
built on nine resistors", "Waking the monitor..." in dim letters, which is
what the screen is for, and the two bottom lines of the self-test screen.

The owner liked the second drawing better and approved it, and added a
name: "the BIOS, instead of plainly BIOS, we will call it Rodent BIOS, so
that we do not use the same name for everything". The title on both
screens and the BIOS's line on serial now say "Rodent BIOS". The OS's own
self-test screen still says "Chinchilla BIOS": it is shown only when there
is no BIOS or after an error, its drawing is an approved one with its own
test, and step 3 removes it.

## What was built

- `bios::run_splash` and `bios::draw_splash` in `core/bios/post`: the text
  on the same 80 by 30 grid, the mascot at 8 pixels a square, the leaf
  (12 squares of 4 pixels) and the ring drawn over cells the text leaves
  empty. It stays until 3 s after the video started (`kSplashMs`); a key
  ends it. Nothing goes to serial: serial's first lines are still the
  self-test's header.
- The self-test follows as before, its own wait for the monitor (2.5 s,
  the header and the small mascot on the screen) counted from the end of
  the first screen. So the first self-test line appears about 6.3 s after
  a reset, against a picture at about 5.75 s on this monitor.
- The memory count no longer pauses between its steps (22 ms each, times
  the pace): it climbs as fast as the memory is tested, about a quarter of
  a second for the external memory. The pause had been copied from the
  OS's self-test, where it makes the count readable.
- The whole BIOS should now take about 13 s from reset to the handover
  (first screen 3.8 s, wait 2.5 s, lines about 2 s, countdown 5 s), where
  it took 11.7 s; a key still skips the waits.

## How it was tested

- PC: `tools/mockup/bios_text.py` draws the first screen from the font's
  file, the squares and the same circle rule, and `host/tests/test_bios.cpp`
  compares the BIOS's own drawing with it to the pixel. Also tested: how
  long it stays, that nothing goes to serial, that a key ends it, and that
  the self-test after it starts from a clean screen. All 42 PC tests pass.
- Cloud: the BIOS compiles (304,144 bytes).
- Not run on the board. To check there: that the first screen and then
  the self-test lines are seen on the monitor, and the times.

## Open

- The name and the words, to confirm with the owner.
- Whether the 2.5 s wait after the first screen is still wanted once the
  times are seen on the board; with it the start is about 1.3 s longer
  than before.

## The first screen on the board: not seen

Two builds were run (the board thread's report).

8d6fc61, the BIOS with the OS's two video settings and no first screen:
first light on the monitor about 5.0 s after a reset by the EN pin and
5.25 s after a restart by software, steady text at 7.0 s, the desktop at
15.0 s. Before those settings it was 5.75 s and 6.25 s, so they helped by
about three quarters of a second, no more. At 5.5 s the title, processor,
memory, video and drive lines were already drawn.

bd20569, with the first screen. Serial, from the reset: the title at
3.5 s (so the video starts at 0.5 s and the first screen ends at 3.5 s),
"Main Processor" at 6.3 s as planned, the last line at 7.83 s, the
handover at 12.89 s, the desktop at 13.85 s. A byte on serial at 1.5 s
ends the first screen and the BIOS hands over at 2.06 s. No missed
refills. But on the monitor the first image came at about 6.5 s, steady
from about 7.5 s: the first screen was never seen, and the self-test was
again not seen from its first line; its first frames had the left edge of
the lines cut off.

So the monitor showed its first image 4.5 s after the video started
without the first screen, and 6.0 s after it with one, which is 3.0 s
after the change from the first screen to the self-test. The plan
assumed a fixed time. It looks as if this monitor needs a few seconds of
a picture that does not change much, and starts again when the whole
picture changes; the cut-off left edge says it is still fitting itself to
the picture then. The OS's own self-test is seen 2.4 s after the video
starts with much the same header on the screen, and why the BIOS takes
longer is not known.

Not guessed at again: the next build is for measuring only. It holds the
first screen for 10 s and the self-test's header for another 10 s, so the
webcam shows how long the monitor takes on a still picture from the BIOS
and whether the change of screen makes it start again. `main`'s start is
to be filmed the same way, to see whether 2.4 s still holds.
