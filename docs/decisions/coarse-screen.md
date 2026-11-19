---
title: "A coarse screen, 320x240, for a game"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: video
summary: "A window that has the whole screen may ask for 320x240: the 640x480 signal with the pixel clock halved and each line sent twice; Doom uses it (backlog 81)"
---

# A coarse screen, 320x240, for a game

Backlog item 81. Journal: `docs/journal/2026-11-19-doom-320x240.md`.

The owner's (2026-11-19): Doom spent 11.6 ms of each picture making every
pixel four, so "dejemos que el monitor haga ese trabajo, no podemos hacer
una señal vga de 320x240?", and, of the three ways put to him on a card,
the real 320x240 ("hagamos el truco"). Not chosen: 640x240, where the
driver repeats lines and the game still doubles across (half the cost,
a finer dither, no change of signal); waiting for the board's figures.

The rest is the agent's, to confirm:

- **To the monitor it is 640x480 still.** No monitor takes 240 lines: it
  wants about 31.5 kHz of lines, the 525 of 640x480. So the pixel clock is
  halved to 12.5 MHz (400 pixels a line in all, the same 32 us) and each
  line of the picture is sent twice, which the video refill does as it
  copies. The syncs are those of 640x480, so the monitor does not look for
  a new signal. It is what a VGA card did with the 320x200 of mode 13h.
- **It is not a mode of the screen.** It is not on the list of
  `Display > Resolution` nor of `dbg.py mode`; a screen has it or not
  (`ports::Display::set_coarse`), the board's has, the PC's windows have
  not yet.
- **Only with the whole screen.** A program asks with
  `os::ui::take_screen(window, out, true)` and reads in `out` what it got:
  320x240, or the screen as it was where there is no coarse picture. It
  ends when the loan ends, in every way a loan ends.
- **The desktop keeps its size meanwhile** (the windows are not moved to a
  screen of 320x240 and back) and is painted again afterwards. Whoever
  watches the screen from afar is told of a new size at both ends, as at
  any change of mode: a remote viewer is dropped and connects again.
- **The picture uses the memory the screen already has** (its first 76,800
  bytes) and starts black at both changes. No pointer is mixed in.
- **Doom dithers over squares of four of its own pixels** with the tables
  it had: the same levels, a grain twice as coarse. If the bands show too
  much, 640x240 is the next thing to try.
- **800x600 goes to 320x240 too** and comes back to 800x600: the monitor
  changes signal twice. Not chosen: 400x300 from the 800x600 signal (one
  more mode to prove on the bench for a game that draws 320x200).
