---
title: "Doom"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: programs
summary: "Doom is a program file, doomgeneric's engine with the whole screen; the WAD is the player's, in C:\\Programs; no sound yet (backlog 81)"
---

# Doom

Backlog item 81. Journal: `docs/journal/2026-11-19-doom.md`.

The owner's (2026-11-19): Doom, as a program for the system ("habrá que
portarlo como app para mi OS"), with the whole screen to itself (his choice
on a card; `docs/decisions/lent-screen.md`).

The rest is the agent's. He was told the first four in the thread the same
day and has not answered; all of it is his to overrule:

- **The engine is doomgeneric's** (`third_party/doomgeneric`, GPL 2,
  unchanged files; its `README.md` says which were left out). It is
  Chocolate Doom's code, which keeps to the game of 1993, with the machine's
  side cut down to a few functions. Not chosen: an ESP32 port such as
  esp32-doom or Retro-Go's PrBoom (each is bound to its own board, display
  and a WAD mapped from flash); PrBoom itself (larger, and more than the
  game of 1993).
- **It is a program file**, `doom.app`, built from `programs/doom` like
  the other games, and not part of the firmware's code: about 350 KB as a
  file, and 240 KB more of memory while it runs. It is the first program
  with C in it and the first that is not ours: `tools/build_programs.sh`
  compiles what a program's `engine.txt` lists.
- **The program is under the GPL, version 2**, as its engine is. Nothing
  else in the repository is linked with it: it reaches the system through
  the OS calls by name when it is loaded.
- **No game data is in the repository.** The game looks in `C:\Programs`
  for `DOOM1.WAD` (the shareware episode, 4.2 MB, which id Software lets
  anyone pass on unchanged), then `DOOM.WAD`, `DOOM2.WAD`, `FREEDOOM1.WAD`,
  `FREEDOOM2.WAD`. Without one it says what to copy where. Freedoom, the
  free data, is not suggested because it does not fit: its first part is
  28 MB and C: is 9 MB. Not chosen: the shareware WAD in the repository
  (it may be passed on, but it is not freely licensed, and "nothing is
  copied" is the project's rule).
- **Each pixel of the game becomes two by two**: 640 by 400 in the middle
  of the screen, black around it, at 640x480 and at 800x600. The board
  shows 8 levels of red and green and 4 of blue, so the four pixels of
  each square take levels on either side of the colour wanted (an ordered
  dither). Not chosen: 640 by 480 with the game's tall pixels (every
  picture resampled, for a little more height).
- **No sound**, on the board or on the PC: the board plays nothing yet,
  and the Sound port has recordings and a beeper, not a stream to mix into.
  A separate piece of work.
- **The keys of 1993**: arrows, Ctrl fires, Space opens, Shift runs, Alt
  strafes, comma and full stop step sideways. **And W, A, S and D as the
  arrows, Q and E to step sideways** (the owner's, 2026-11-19: "make wasd
  move as arrows and q and e strafe"). In the name of a saved game and in
  a cheat code they are still letters; in the menus W and S move the
  skull, so the menu's own letters for those six are gone. **The mouse turns** and fires
  with the left button; its movement forward and back, which the old game
  took as walking, is left out.
- **Saved games are files in `C:\Documents\Doom`.** The game's settings
  (screen size, messages, mouse speed) are not kept between runs:
  doomgeneric has the settings file turned off, because its key names do
  not survive the file.
- **A small window stays behind the game**, as the program's window: it
  says what is wrong when there is no WAD, and after the logo key (which
  minimises it) or Ctrl+Alt+Del it holds a Play button that goes back to
  the game where it was.
- **Memory**: the game's zone is 4 MB, or less down to 2.5 MB if that much
  is not free, taken when the game starts and given back when it ends. The
  task's stack is 16 KB. Both to be checked on the board.
- **The engine is run from a timer**, 35 times a second, a round of the
  game and a picture each time; a slower machine gets fewer pictures and
  the same speed of play. `dmesg` gets a line of figures every ten seconds.
- **It stays at 35 pictures a second, as the original** (the owner's, on
  a card, 2026-11-19, once the coarse screen had brought a round down to
  11 or 12 ms and 60 had room). Not chosen: drawing between two moves of
  the game to reach 60, now or on a branch of its own; the engine stays as
  doomgeneric has it.
- **F12 in the game changes how the picture is shown** (the owner's,
  2026-11-19: the key was made to find a fault of the 320x240 signal, and
  he asked to keep it, "me parece una buena feature"). It goes round
  320x240 dithered, 320x240 with colours rounded and no dither, 640x240
  where the screen only makes the lines tall, and the screen as it is
  with each pixel made four by the program. The agent's, to confirm: the
  choice is kept in `C:\.config\doom.ini` (`[picture] look=`), written when
  the program closes; 320x240 dithered is the one a new board starts with;
  where the screen has no coarse picture (the PC) all four show the last.
