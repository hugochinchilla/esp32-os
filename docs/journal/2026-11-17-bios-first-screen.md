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

## Measured: the monitor takes 5.5 s, whatever is on it

The measuring build (6feca43), after a reset by the EN pin: the first
screen's first image at about 5.5 s, steady at about 6.0 s and whole (the
monitor's "no signal" box from 3.0 to 3.5 s). The change of screen at
about 10 s keeps the picture: no black, no new search. The self-test's
lines were all seen from about 20 s. `main`, filmed the same way: first
image at about 5.5 s too, desktop at 11.5 s.

So the guess of the last entry was wrong, and so is the "2.4 s" the
project has carried since 2026-11-16: today this monitor takes about 5.5 s
from a reset to a picture, for the BIOS and for `main` alike, and a change
of screen does not make it start again. The first image at 6.5 s with the
3 s first screen was the monitor's usual time plus the half-shown frames
of a screen that had just changed. (After a restart by software the film
showed the first screen from 3.0 to 7.5 s and then 2 s of black; not
understood, and to be looked at again with the real times.) The monitor's
make could not be read from the webcam; the owner has been asked.

A fault of the first-screen build was also seen: on the self-test screen
every line had a hole ("Co      lt on nine resistors"), from column 4 to
19 in rows 2 to 9. That is where the first screen's big mascot is. The
text screen keeps cells free for pictures, and `clear()` kept them free,
so the self-test's text there was stored and sent to serial but never
painted. The PC tests missed it: they checked the text of the rows, which
was right, and compared the self-test's picture only on a fresh screen.
`clear()` now gives the cells back, and the picture test draws the first
screen before the self-test, as the board does; with the fault put back it
fails by 1,706 pixels.

## The times, from the measurement

Options:

- A. First screen 3 s as asked, then the self-test's header for another
  3.5 s before its lines. The first screen is never seen on this monitor.
- B. First screen until 7 s after the reset, so it is seen steady for
  about a second, then the self-test's lines at once.
- C. No first screen: the self-test's header until 7 s.

Chosen: B, by the owner on a card ("Until 7 s, then self-test", 2026-11-17 19:06 UTC), after it was built as the recommendation. The three take the same time, and
only B shows both the screen he asked for and the self-test from its first
line. Built: `kSplashMs` is 6,500 (the video starts 0.5 s after the
reset) and the BIOS's own wait before the lines is 0. From reset: first
screen until 7.0 s, lines until about 8.5 s, countdown until 13.5 s. A key
still ends each wait. Both times belong in Setup one day: another monitor
will want less.

Compiled (BIOS 304,128 bytes), all 42 PC tests pass. Not run on the board.

## On the board: seen

e98209c on the board (the board thread's report). After a reset by the EN
pin: the first screen's first image at 5.5 s, steady from about 5.7 to
6.5 s, the change of screen between 6.5 and 7.0 s without black, the
self-test's lines from 7.0 to 8.5 s, whole, and seen from "Main
Processor" on. On serial the title at 6.97 s, "Main Processor" at 7.33 s,
the handover at 13.95 s, the OS loaded at 14.25 s; the screen black from
13.0 to 17.0 s and the desktop at about 17.0 s. After a restart by
software the picture is continuous, without the 2 s of black of the
measuring build. Handover, `fault abort` and the count of missed refills
(0) as before.

The quick ways round the BIOS, which the owner asked about ("do we still
have a quick restart way for the agents?"):

| Way | From | Desktop after |
|---|---|---|
| `tools/dbg.py reboot --fast` | the request | 3.2 s |
| A byte on serial during the BIOS (sent at 3.0 s) | the reset | 4.5 s (handover at 3.56 s) |
| Cable flash with a fast start armed, then the EN reset | the reset | 1.5 s (the BIOS says "fast start asked for" at 0.50 s, hands over at 0.55 s) |
| The whole BIOS | the reset | about 17 s |

Not tested, and not possible from the laptop: the physical button and a
real power cut.

## Merged, and where the BIOS stops for now

The owner chose "Yes, merge it" on a card (2026-11-17, 19:59 UTC). PR 40 is
on `main` (merge ec8b694) and the board stays on the new layout with the
BIOS: C: back with its 17 files, the Wi-Fi network kept, the BIOS's
self-test at 7.0 s, the handover at 13.9 s, no missed refills.

Asked whether to go on to step 3, whose plan had the BIOS show the OS's
error screen, the owner wanted to discuss it first: BIOSes do not usually
write to disk (here it is the OS that writes the report, the BIOS would
only have read the note left in memory), and more to the point, "the BIOS
should not have this link with the operating system": it should be a
selector of the partition to start, the place to set the time once there
is a clock chip, and other settings later, not the place to manage the
OS's errors. The OS itself could start with a message that the last start
failed and a button that opens the report in the text editor.

That is the better design, and it was taken as his decision: the point
recorded as "to confirm" in the first discussion (the BIOS shows the crash
screen) is reversed in `docs/DECISIONS.md`. A revised plan was put to him
with one choice (after a crash, through the BIOS like a PC, or straight
back to the OS as today). Before choosing he asked to leave it for a next
iteration in a fresh thread, so this one would not go on for ever. The
plan, the open choice and what the board taught are in
`docs/handoff/bios-step-3.md`. Nothing of it is built.
