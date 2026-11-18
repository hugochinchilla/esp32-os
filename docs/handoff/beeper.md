# Handoff: the beeper (backlog item 48, PR 51)

Written 2026-11-18 by the thread that built it, when the owner paused the
work: "remove all beeper sound from the board. I will review this again
when I know what kind of beeper I want". Read `CLAUDE.md` and
`docs/DECISIONS.md` first. The story is in
`docs/journal/2026-11-18-beeper.md` and
`docs/journal/2026-11-18-beeper-never-stopped.md`.

## Where things stand

- **Nothing of this is on `main` or on the board.** The board runs `main`,
  which does not touch GPIO14. PR 51 (branch
  `claude/project-thread-l7amx9`, last code commit 7191d62) is a draft and
  is not to be merged until the owner takes the work up again.
- PC tests pass (44 of 44) and both firmwares compile and link. The last
  commit has **never run on the board**: the third board turn was
  cancelled. The two turns that did run were of earlier commits and both
  ended with the beeper stuck on.

## The hardware, as built

- The beeper is between **GPIO14 and ground** (the plan had said 3V3 to
  GPIO14; `docs/hardware/README.md` on the branch says what was built).
- The part fitted is a **sealed active buzzer** (its own oscillator, "+" to
  GPIO14). It sounds on any steady voltage, at its own pitch, and draws
  enough current to put lines on the VGA picture while it sounds. It can
  give beeps and beep codes, not a melody.
- For melodies: a **passive piezo** element in the same two holes, no
  polarity, optionally 100 to 220 ohm in series, no transistor. Not a
  passive magnetic buzzer (about 16 ohm), which needs a transistor and a
  diode. The owner has not decided which part he wants.

## What the branch holds

- `ports::Beep` and `ports::System::beep(steps, count)`; `os::beep`.
- `core/sound/beeps.hpp`: the self-test's codes (one short beep at
  1,000 Hz for 200 ms when all is well; two short, three long, one long and
  two short, one long and three short, long beeps without end) and the
  desktop's chime; `sound::parse_tune` for `C:\.config\chime.txt`.
- `firmware/common/beeper.h`: LED PWM on GPIO14, a one-shot `esp_timer`
  from step to step, fading by narrowing the pulse. **Silent is the pin
  held low** (the channel's idle level). Both firmwares call
  `beeper::begin()` first thing.
- `tools/sounds/make_beeps.py`: previews of every candidate, `--file` to
  hear a `chime.txt`, `--from-wav` to make one from a one-voice recording.

## What went wrong on the board, so it is not repeated

1. Silent was the pin held high, for the wiring on paper. On the real
   wiring that is 3.3 V across the buzzer whenever nothing plays. Ask how
   a thing was built before driving it.
2. The OS did not start the driver, and a restart of the processors does
   not reset the PWM or the pin: whatever the BIOS left went on.
3. A try at letting go of the pin for silence (writing its routing by
   hand) gave no beep on a cold start and a stuck sound after restarts. Not
   understood; removed. Do not bring it back without a board to try it on.

## Decided by the owner

- The BIOS beeps as a PC does; "all well" is 1,000 Hz ("accepted ok-1000").
- The chime is to be mellow, calm and long, not the short ones of round
  one.

## Not decided

- **Which part.** Everything else waits for this. With an active buzzer
  the chime has to become a rhythm of beeps, or go to the audio jack.
- **The error codes**: proposed twice, never answered. They are in the
  code as proposed.
- **The chime.** "First light" (candidate N) is in as the default because
  he had not picked one; rounds two to four are in the project's
  `look/sounds/beeper/`. He asked for the melody of a 1995 start-up sound;
  that was not done and is not to be put in the repository
  (`docs/DECISIONS.md`: no Microsoft chime). He sent a file of his own,
  `chime_buzzer_v6_preview.wav`, to use; he was asked whether it is his own
  melody and has not answered. His own board can play any tune from
  `C:\.config\chime.txt`.
- Whether the chime plays on a fast start too (it does now).

## To take it up again

1. Ask the owner which part is fitted, and how it is wired.
2. Merge `main` into the branch; run the PC tests; compile both firmwares.
3. One board turn, with the webcam's microphone: silent until the beep,
   one beep and its measured pitch, the chime's pitches and fading, silent
   after, twice through `tools/dbg.py reboot --fast`, lines on the picture
   during the chime, `tools/dbg.py misses`. The full list is in the
   project thread of 2026-11-18.
