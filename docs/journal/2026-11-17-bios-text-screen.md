# The BIOS's text screen, on the PC

2026-11-17. Backlog item 46, step 1 of 3. Built and tested on the PC only.
The firmware was not compiled with it, and nothing on the board changes:
the board's start-up still uses the old self-test screen.

## What was asked

After the discussion (`2026-11-17-bios-stage.md`) and the measurement
(`2026-11-17-bios-restart-measured.md`) the build was put to the owner in
three steps: the screen on the PC; the second stage for real (partition
table, bootloader hook, a BIOS firmware; the board turn where C: is wiped);
then moving in (settings to NVS, the crash screen, the OS drops its own
self-test). On the card "Start building the BIOS now, beginning with step
1?" he chose **"Yes, start step 1"**.

His earlier words that this step answers:

> The BIOS could have its own serial link with a text mode instead of the
> full debug mide the os has.

> With a true test mode can we have a logo con top?

and the font: "Spleen".

## What was found

- The old self-test (`core/src/shell/boot.cpp`) already draws in Spleen
  8x16, but at free positions: 24 pixels in, lines 18 pixels apart. It is a
  drawing made of text, not a text screen: there is no grid, so nothing to
  mirror on serial but the log lines.
- Its checks, the memory test, the waiting for the monitor and the skipping
  are sound and tested. They are used as they are.

## Options considered

**One grid for the screen and serial, or two outputs written side by
side.** Chosen: one grid with a cursor. What is printed through the cursor
goes into the cells and out as text, so the two cannot drift apart. Writing
each line twice (once drawn, once sent) is less code today and is how the
old screen logs, but every later change would have to be made twice.

**How serial shows a line that changes in place** (the memory count
climbing, the countdown). Chosen: carriage return and "erase line" (`\r`,
`ESC [ K`), then the new text: any terminal shows the count climbing on one
line, as on the monitor. Not chosen: a new line for every value (dozens of
lines for one count), or sending only the final value (the count would not
be seen on serial at all).

**Colour on serial.** Not sent: plain text, as the owner described it. A
failed check says so in words as well as in red.

**The mascot.** A picture over cells the text leaves empty (decided in the
discussion): ten columns by five rows, the same 16 squares of 5 pixels as on
the old screen, now at a cell boundary.

**The reference picture for the test.** The mock-up the owner chose from had
placeholder values and two lines that are not real yet. Chosen: the mock-up
script draws a second picture, `docs/look/bios-text/bios-post.png`, with the
final layout, from Spleen's BDF file and the mascot's squares, and the PC
test compares the C++ screen with it pixel for pixel. The two are written
separately (Python from the font's source file, C++ from the generated
font table), so a wrong glyph, column or colour shows.

## What was chosen while building (to confirm)

- **80 by 30**, text starting in column 2, the rows as in the mock-up.
- **"Detecting keyboard"** where the old screen says "Detecting mouse": a PC
  BIOS names the keyboard, and the value is the same fact (where keys and
  mouse come from).
- **The memory figures are bright**, the rest of the line grey, as in the
  mock-up.
- **A bottom line with an identity string**, as BIOSes had, given by
  whoever starts the BIOS (the build's date and the board); none if not
  given.
- **Left out until they are true**: the mock-up's "Boot device ... image OK"
  line (step 2 can really check the OS image) and "F8 for boot menu" (there
  is none). The Setup line keeps its joke, "(there is no setup)", until
  there is one.
- On serial the two bottom lines come right after the header, since serial
  has no bottom of the screen.

## What was built

- `core/bios/text_screen.hpp`, `core/src/bios/text_screen.cpp`:
  `bios::TextScreen`, 80 by 30 cells of a character and a colour on black;
  a cursor (`move_to`, `print`, `restart_line`) that also sends plain text;
  `put` for fixed places; reserved cells for the picture; painting only the
  cells that changed, each written whole so nothing flashes. 7.2 KB of
  static memory.
- `core/bios/post.hpp`, `core/src/bios/post.cpp`: `bios::run_post`, the
  self-test on that screen, with the old one's checks, memory test, pacing,
  monitor wait, countdown and skip, and the same log lines; `draw_post` for
  the picture test.
- `core/shell/boot.hpp`: `wait_until`, `format_bytes` and `draw_mascot` made
  visible so the BIOS uses the same ones. No behaviour changed.
- `tools/mockup/bios_text.py` draws the reference; the fonts that were not
  chosen are only drawn if their files are given.

## How it was tested

`host/tests/test_bios.cpp`, new, and all 42 PC tests pass.

- The grid: printing, colours, a line written again, the right edge, bytes
  that are not printable, `put` leaving the cursor alone.
- Painting: only changed cells are written, an empty cell is black, a
  reserved cell is never touched, writing the same again writes nothing.
- The finished screen against the reference: 0 pixels differ, at the first
  run.
- A whole run with a simulated board: every row's text; the failed check in
  red and counted; the log lines; the countdown's time. What a terminal
  would show from the serial bytes is, row for row, what the screen's grid
  holds; only printable characters, line ends and the erase-line sequence
  are ever sent.
- The picture after a run equals a fresh paint of the grid (nothing painted
  late or missed).
- The monitor wait, and a key that skips the waiting while every check still
  runs.

Not tested: on the board (nothing there uses it yet); compiled for the
board (the cloud machine had no ESP-IDF installed for this step).

## What was learned

- The old screen was two steps from a text screen already: same font, same
  lines. What was missing was the grid, and with the grid the serial copy
  comes almost free.

## What is left

- Step 2: the partition table (the BIOS in 1 MB taken from C:, `nvs` where
  it is so the saved Wi-Fi networks stay), our copy of the bootloader's
  start file, a BIOS firmware that runs this screen and restarts into the
  OS, serial as plain text. A board turn; C: is wiped in it. The board
  thread wants to be told in advance.
- Step 3: settings to NVS, the crash screen in the BIOS, the old self-test
  removed from the OS.
