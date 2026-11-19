---
title: "The desktop fades in at start-up"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: boot
summary: "At start-up the screen shows the desktop's plain colour, rests 300 ms, and the desktop comes up in seven steps; not on a debug fast start (backlog 74)"
---

Backlog item 74; the story is in
`docs/journal/2026-11-19-start-up-fade.md` and
`docs/journal/2026-11-19-no-splash.md`. Previews: `look/startup-fade/` in
the project files. Since the same day there is no splash (`no-splash.md`):
where this file says "the splash", read "what the screen showed before",
which is black after the BIOS's restart.

The owner's, 2026-11-19, after seeing Shut Down's fade
(`shut-down-sequence.md`): the same for switching on. Read the desktop's
background colour, show that colour first, then fade the loaded desktop in,
the same way the desktop fades to the last screen.

As built, the agent's readings, each his to confirm or overrule:

- **The colour comes at once, as a cut from the splash**
  (`Wm::Arrival::FromColour`), when the window server starts: the plain
  desktop as Display has it set, pattern included. Not chosen, but built
  and one word away (`Wm::Arrival::FromPicture`): the splash fades into the
  colour in seven steps first, which mirrors Shut Down's two fades and
  adds 420 ms to every start. Put to the owner with both previews.
- **The desktop comes up when everything that starts with the system has
  been started**: init has read its start-up file, so the taskbar, the
  icons and any window a line of that file opened come up together. The
  steps are timers of the window server, and timers first run when the
  machine is in its loop.
- **A rest of 300 ms on the plain colour, then seven steps of 60 ms**:
  Shut Down's figures, backwards. Each step shows exactly what Shut Down's
  fade shows at the same distance from the plain desktop (the PC test
  compares the two picture by picture). About 0.7 s more from splash to a
  desktop that takes input.
- **A second picture, kept for the 420 ms of the fade.** Shut Down fades in
  place because what it fades from is on the screen. Here what is to come
  up is not on the screen yet, so the desktop is painted once into a
  picture of its own (307 KB at 640x480, 480 KB at 800x600, from the Memory
  port) and each step is worked out from it; it is given back at the last
  step. Not chosen: painting the desktop seven times through a filter (the
  screen would show each piece in full colour for a moment before the
  filter passed: flicker), and a filter inside every drawing call (touches
  all of `core/gfx`). If there is no memory for the picture the desktop
  shows at once after the rest.
- **Input is ignored and the pointer hidden until the desktop is whole**,
  as while Shut Down fades. A window that opens during the fade shows when
  it is over.
- **Not on a fast start that was asked for** (`dbg.py reboot --fast`,
  `boot fast-next`, `fastboot = true` in boot.ini, the PC program's
  `--fast`): the desktop at once, as before, so the board checks and the
  tools keep their timing. A start that comes from the BIOS does fade,
  though the OS treats it as a fast one in every other way (the first
  board turn found that it did not).
- **The welcome chime stays where it is**, when the desktop has started:
  it begins with the plain colour and the desktop comes up about 0.3 s
  into it. Not chosen: holding it back to the first step of the fade.
