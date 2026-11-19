---
title: "Sound: the port, the beeper and the system's sounds"
date: 2026-11-18
by: owner+agent
status: to-confirm
area: system
summary: "A Sound port with two voices, samples and an emulated PC beeper; silent on the board, real on the PC and in the browser (backlog 58, 59)"
---

# Sound: the port, the beeper and the system's sounds

Journal: `docs/journal/2026-11-18-sound.md`. Backlog items 58 and 59.

The owner's (2026-11-18): the core gets a sound port; the board has a
driver that plays nothing for now; the Linux program and the browser
version play for real; a welcome chime when the desktop comes up and a
short melody on Shut Down; and the PC's beeper is emulated for the BIOS's
beep at start-up. The pitch of that beep, 1,000 Hz, he accepted earlier
the same day in the parked work on a real beeper (backlog 48).

Confirmed by the owner on 2026-11-19, after hearing the Linux program's
start-up and shut-down ("los sonidos están perfectos"): the beep, the
welcome chime (`boot.pcm`), the closing melody (`close.pcm`) and how loud
they are against each other.

The rest is the agent's and waits for him (he has not heard an error code
or said anything about the other points):

- **The port** is `ports::Sound` with two voices that sound together:
  recorded samples (16 bits, one channel, 22,050 a second, the form of
  `docs/sounds/`) and the beeper, a list of steps of a pitch and a time.
  Every call returns at once. One recording at a time: a new one takes the
  place of the one playing. Not chosen: the beeper as a call of
  `ports::System`, as the parked branch has it (one output mixes both, and
  the board's future driver is one driver); several recordings at once (no
  use for it yet).
- **The beeper is emulated in `core/sound/mixer`**, not in the SDL driver:
  a square wave with its edges rounded so that no false pitches appear,
  through the filters of a 5 cm speaker (little under 350 Hz, rounded off
  above 6 kHz), rising and falling over 2 ms. The mixer is plain code that
  fills a buffer, so the PC tests check it, and a driver for the board's
  audio pin (GPIO13) can use it unchanged.
- **The beep codes** are those proposed for the real beeper and never
  answered: one short beep (200 ms) when all is well; two short for a check
  that does not stop the start; three long for memory; one long and two
  short for video; one long and three short for drive C:; long beeps when
  there is no system to start. The code is for the worst thing found. It
  sounds when the checks are done, over the countdown, and a key that skips
  the countdown does not cut it short. No beep on a fast start.
- **Both self-test screens beep**: the BIOS's (`bios::run_post`) and the
  one the PC and browser builds show (`shell::run_self_test`), through
  `BootIo::sound`. The board's two firmwares pass none, so the board stays
  silent, as the owner asked when he parked the real beeper.
- **Which sound where**: `boot.pcm` when the desktop is up and `close.pcm`
  with the "safe to power down" screen (both chosen on 2026-11-16 for these
  moments);
  `alert.pcm` with every message box and question; `error.pcm` when there
  is no memory to open a program; `bell.pcm` for the terminal's bell.
  Restart plays nothing: the machine is gone at once.
- **The recordings are not in `core/`** (390 KB): a machine that can play
  them hands them in (`sound::set_clips`). The PC and browser builds
  compile them in from `docs/sounds/`; the board's firmware does not carry
  them. Not chosen: files on C: (the more period-true home, and what a
  Sounds settings tool would want; it costs the board 390 KB of C: for
  sounds it cannot play, and an existing C: of the PC would not have them).
- **Programs** ask through `os::play_sound`, `os::beep` and
  `os::sound_playing`.
- **The browser** plays nothing on a page nobody has clicked, so the page
  starts switched off with a power button and the words "Click to start
  Chinchilla 96" (the owner's choice and words, 2026-11-18); a click
  anywhere starts the machine
  and the beep and chime are heard in their places. After Restart the page
  comes back on without the button. A sound that the browser still holds
  back for half a second is dropped rather than played late. Not chosen:
  starting at once (the beep and the chime are then never heard by a
  visitor who only looks).
- **The PC program** has `--mute`. The window's close button shuts down
  and the window stays until the closing melody has ended; closing it
  again leaves at once.
