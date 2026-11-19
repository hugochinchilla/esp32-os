---
title: "The Shut Down sequence"
date: 2026-11-19
by: owner+agent
status: decided
area: programs
summary: "Shut Down closes the programs (ended after 20 s), fades the desktop, then to black, and shows the last screen on black (backlog 60)"
---

Backlog item 60; the story is in
`docs/journal/2026-11-19-shut-down-sequence.md`. Checked on the board in
batch 21. Pictures:
`host/tests/pictures/38a-shut-down-fading.png` to `38-power-down.png`.

The owner's, 2026-11-18: in order, close all the programs (let them wait,
force kill after a timeout of 20 seconds); fade the desktop to the
background colour (the icons and the taskbar disappear); then the "safe to
turn off" screen, its background full black. The melody of item 59 belongs
to it.

As built, the agent's readings. The owner confirmed all of them on
2026-11-19, after the board check and a recording of the fade ("Parece que
está todo bien así"), knowing that programs with no question to ask have
closed by the time a Cancel calls Shut Down off:

- **The 20 seconds count from the moment Shut Down is chosen, questions
  included.** A "Save changes?" nobody answers is ended with its program at
  20 seconds and the text is lost. Not chosen: a limit only for programs
  that do not answer, with a question waiting for ever (98 does that; the
  owner's words give one limit).
- **Cancel calls Shut Down off.** A program that was asked, has no question
  open and is still there has said no; nothing more closes and Shut Down
  has to be chosen again. Yes and No go on by themselves. Not chosen:
  waiting out the 20 seconds and ending it anyway, which makes Cancel a lie.
- **Force is KILL**, the signal End Task uses, so a program that is saving
  finishes its file first. What is still open 2 seconds after that is left
  behind and the fade begins.
- **The fade is a fade of colour, in place**: every pixel moves one level
  of red and green per step (blue on every second step, it has half the
  levels) towards what the plain desktop shows there, a step every 60 ms,
  seven steps at the most. Not chosen: a dissolve through a dither pattern
  (cheaper still, but it reads as a wipe, not a fade), and mixing from a
  kept copy of the picture (smoother in theory; needs a second picture in
  memory for eight levels of colour).
- **Then to black the same way**: the plain desktop rests for 300 ms,
  fades to black, and the last screen comes up out of the black in seven
  steps. Not chosen: cutting from the desktop's colour to the black
  screen.
- **The last screen keeps its drawing**: the stars, the mascot asleep and
  both lines of text, on full black instead of the night sky's blue.
- **The melody starts with the fade**, when the last program has gone, and
  plays over the fade and the last screen (the fades take about 1.6 s, the
  melody 2.9 s). Not chosen: with the last screen, as before, which leaves
  the fade silent.
- While programs close the desktop works as usual (the questions must be
  answered); a program opened meanwhile is asked to close too. While the
  picture fades all input is ignored, Ctrl+Alt+Del included. Services
  (telnet, the remote screen) are not stopped: a viewer sees the fade and
  the last screen.
- Restart is as it was: at once, asking nobody.
