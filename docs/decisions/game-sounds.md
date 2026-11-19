---
title: "Sounds of the games and the Trash"
date: 2026-11-19
by: owner+agent
status: to-confirm
area: programs
summary: "Chess knocks on a move, warns the user of check, says checkmate; Culebra a berry and game over; the Trash crumples paper (backlog 70)"
---

# Sounds of the games and the Trash

Journal: `docs/journal/2026-11-19-game-sounds.md`. Backlog item 70.

The owner's (2026-11-19): Chess sounds when a piece moves, with a sound of
its own for check and for checkmate; Culebra when the fruit is eaten and at
game over; Cactus Patch has no sounds; the Trash, emptied, sounds like paper
being crumpled.

He heard the candidates the same day and picked: the wooden knock for a
move, the kalimba coming down for checkmate and the two kalimba notes for a
berry (as first built); the electric piano note that droops an octave for
game over; the quick, darker scrunch of 0.42 s for the Trash. Check as first
built "suena alegre": it is for when the player is put in check and must
sound like an alert. Of three new candidates that warn he picked e: the
piece's knock, then two kalimba notes falling a tritone.

Still the agent's, waiting for him:

- **Check sounds only when the user is put in check.** A check the user
  gives the computer sounds as any move: a warning for one's own good move
  would be the wrong way round. (His words can also be read as the same
  alert for both.)
- **Recordings, not the beeper.** Six more of the system's sounds
  (`sound::Id`: ChessMove, ChessCheck, ChessMate, SnakeEat, SnakeOver,
  Crumple), played with `os::play_sound` like the other five. Not chosen:
  beeper tunes for the games (no bytes, and true to a PC game of 1996, but
  crumpled paper is noise, which a beeper cannot make, and in both rounds of
  2026-11-16 the owner passed over every square-wave option).
- **When.** Chess: every piece put down, the user's and the computer's (the
  computer's as its piece begins to land); one sound a move, so the warning
  and checkmate begin with the knock and take the move's place. A draw
  sounds as a move. One checkmate sound, whoever won. Nothing for Take back,
  a new game or Resign. Culebra: the berry; the game's end by wall or tail;
  nothing for steering, a pause or a new game. The Trash: after it has been
  emptied without an error, not when the question is asked (that is the
  message box's knock).
- **Quieter than the system's**: the piece and the berry at -14 dB and the
  paper at -15 dB where the system's sounds are at -12 dB, because they are
  heard many times in a row.
- **216 KB more** in the PC and browser builds (606 KB of recordings in
  all); the board's firmware carries none, as before.
