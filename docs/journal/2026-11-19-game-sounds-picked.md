# The games' sounds, heard and picked

2026-11-19, the same night as `2026-11-19-game-sounds.md`. Backlog item 70.
PC only; the board check of the first version had passed (batch 19: Chess,
Culebra and Empty Trash as on main, no missed refills, 208 bytes more).

## 1. What was asked

The owner listened to the eighteen candidates and answered:

> Papelera C
> Ajedrez A
> Jaque, suena alegre, debe ser un sonido para cuando el jugador recibe jaque, como un sonido de alerta
> Culebra gane over B
>
> De los que no digo nada usa los que ya están en la rama

## 2. What was chosen, by whom

- His: the Trash is candidate c, the quick, darker scrunch (0.42 s in place
  of 0.85 s); a chess move stays a, the wooden knock; Culebra's game over is
  b, one electric piano note drooping an octave; checkmate and the berry
  stay as built.
- His, and the lesson of the round: all three check candidates went up or
  rang, which reads as good news. Check is bad news for the one who hears
  it.
- The agent's, to confirm: three new check candidates that warn. d, the
  knock and then the same high wooden note twice; e, the knock and two
  kalimba notes falling a tritone; f, the knock and the message box's two
  wooden notes turned over, going down. d is built in: a note repeated is
  the plainest shape of a warning, and it stays in the wood of the board.
- The agent's, to confirm: the warning sounds only when the user is put in
  check. He wrote "para cuando el jugador recibe jaque"; an alert for the
  user's own checking move would be the wrong way round, so that move now
  sounds as any other. The other reading, one alert for every check, is a
  one-line change in `ChessWindow::move_sound`.

## 3. What was built

- `tools/sounds/make_game_sounds.py`: `check_d` to `check_f`, made after
  everything else so that the random source gives the sounds he had heard
  the same noise, sample for sample; the final table follows his picks.
  `docs/sounds/chess_check.pcm`, `snake_over.pcm` and `crumple.pcm` changed;
  the recordings are 606 KB now.
- `chess.cpp`: ChessCheck only when the side in check is the user's.
- `test_chess_window`: the user's queen check is a plain knock; a bishop
  that takes a queen left beside the king plays the warning. 55 of 55 PC
  tests pass.

Not done: nothing here ran on the board. The change since the board check
is three recordings the board does not carry and one condition in
`chess.cpp` that picks between two calls which do nothing there.

## 4. The check sound, picked

The next morning the owner heard d to f and answered "jaque E": the knock
and two kalimba notes falling a tritone. `chess_check.pcm` is now that one
(0.92 s). He said nothing against the warning sounding only for the user,
which stays the agent's choice, to confirm.

## 5. What is left

Nothing but that confirmation. All six sounds are the owner's picks.

## 6. Confirmed

Later that morning, after PR 75 was merged, the owner answered the open
point: "Si, solo suena si me lo dan a mi". The check warning for the user
alone is his decision now (`docs/decisions/game-sounds.md`); the code
already did that and did not change.
