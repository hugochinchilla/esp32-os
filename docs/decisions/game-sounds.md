---
title: "Sounds of the games and the Trash"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: programs
summary: "Chess knocks on a move and says check and checkmate; Culebra a berry and game over; the Trash crumples paper; Cactus Patch is silent (backlog 70)"
---

# Sounds of the games and the Trash

Journal: `docs/journal/2026-11-19-game-sounds.md`. Backlog item 70.

The owner's (2026-11-19): Chess sounds when a piece moves, with a sound of
its own for check and for checkmate; Culebra when the fruit is eaten and at
game over; Cactus Patch has no sounds; the Trash, emptied, sounds like paper
being crumpled.

The rest is the agent's and waits for him. He has not heard any of it:

- **Recordings, not the beeper.** Six more of the system's sounds
  (`sound::Id`: ChessMove, ChessCheck, ChessMate, SnakeEat, SnakeOver,
  Crumple), played with `os::play_sound` like the other five. Not chosen:
  beeper tunes for the games (no bytes, and true to a PC game of 1996, but
  crumpled paper is noise, which a beeper cannot make, and in both rounds of
  2026-11-16 the owner passed over every square-wave option).
- **Which sounds.** Made by `tools/sounds/make_game_sounds.py` from the
  timbres he chose for the system: a wooden knock for the piece; the same
  knock and two kalimba notes going up for check; the knock and the kalimba
  coming down onto a low wooden note for checkmate; two quick kalimba notes
  for a berry; wooden bars stepping down to a thud for game over; three
  squeezes of crackling paper, 0.85 s, for the Trash. Three candidates of
  each are in the project files, `look/sounds/games/`, for him to pick from.
- **When.** Chess: every piece put down, the user's and the computer's (the
  computer's as its piece begins to land); one sound a move, so check and
  checkmate begin with the knock and take the move's place. A draw sounds as
  a move. One checkmate sound, whoever won. Nothing for Take back, a new
  game or Resign. Culebra: the berry; the game's end by wall or tail;
  nothing for steering, a pause or a new game. The Trash: after it has been
  emptied without an error, not when the question is asked (that is the
  message box's knock).
- **Quieter than the system's**: the piece and the berry at -14 dB and the
  paper at -15 dB where the system's sounds are at -12 dB, because they are
  heard many times in a row.
- **240 KB more** in the PC and browser builds (630 KB of recordings in
  all); the board's firmware carries none, as before.
