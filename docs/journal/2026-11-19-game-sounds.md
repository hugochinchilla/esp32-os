# Sounds for the games and the Trash

2026-11-19. Backlog item 70. Built and tested on the PC by what the code
asks the sound output for; nobody has heard the six sounds yet, the agent
included, which cannot listen. On the board nothing plays, by design, and
nothing here has run on the board.

## 1. What was asked

The owner, in the project chat:

> Añade y comienza también una tarea para que los juegos tengan sonido.
>
> chess -> sonido al mover una pieza y sonido especiál para jaque, y jaque mate
> serpiente -> sonido de comer fruta y de game over
> buscaminas -> sin sonidos
>
> La papelera al vaciarse debe hacer un sonido como de papel siendo arrugado

## 2. What was found

- The sound system of items 58 and 59 (PR 71): `ports::Sound` with two
  voices, recordings and an emulated beeper; `os::play_sound(sound::Id)`
  for five recordings in `docs/sounds/` that the PC and browser builds
  compile in and the board does not carry. One recording at a time: a new
  one takes the place of the one playing.
- The five recordings were made by `tools/sounds/make_sounds.py` from
  oscillators and noise, and chosen by the owner on 2026-11-16 in two
  rounds of four candidates each. He chose a glass pad, a mellow electric
  piano, wooden knocks and a kalimba, and none of the four square-wave
  ("chip") candidates.
- Chess (`core/src/shell/chess.cpp`) has two places where a move is played:
  `play_user` and `play_answer`. The computer's piece lands with a bounce of
  about a quarter of a second (item 64), lowest 80 ms in.
- Culebra (`culebra.cpp`): `Snake::step()` answers true when a berry was
  eaten; the state says when the game is over.
- The Trash is emptied in one place, `FolderWindow::empty_answer` in
  `file_manager.cpp`, after a question that already knocks (the message
  box's `alert`).

## 3. The options considered

**Recordings or the beeper.** The beeper costs no bytes and is what a PC
game of the time had. But crumpled paper is noise and the beeper has one
pitch; and games on the beeper beside a desktop in wood and kalimba would
be two machines. The owner's own picks say which he likes. Recordings were
chosen (the agent's, to confirm).

**Where the sounds live.** In the table of the system's sounds
(`sound::Id`), or each game carrying its own samples. The games are
programs that can be loaded from C:, where a sound of their own would one
day belong; but today a machine hands in one table of recordings and the
board hands in none, and a second way in for six short sounds is not worth
it. They joined the table.

**Three candidates of each**, as on 2026-11-16, since the choice is by ear
and is his:

| Sound | a (the one built in) | b | c |
| --- | --- | --- | --- |
| Chess, a move | wood: a wooden piece on a wooden board, 0.24 s | felt: lower and softer, no click | stone: a small hard click |
| Chess, check | the knock, then two kalimba notes going up, 0.77 s | two knocks, the second higher | the knock, then one small glass bell |
| Chess, checkmate | the knock, then the kalimba coming down to a low wooden note, 1.84 s | the knock, then a glass chord | the knock, then two low notes of the Shut Down piano |
| Culebra, a berry | two quick kalimba notes going up, 0.34 s | a gulp: one note sliding up, 0.14 s | a bite: a crunch and a wooden pop |
| Culebra, game over | wooden bars stepping down to a low thud, 1.42 s | one piano note drooping an octave | the kalimba falling, three notes |
| The Trash | paper crumpled in three squeezes, 0.85 s | two squeezes and the ball dropped in the bin | one quick, darker scrunch, 0.42 s |

They are in the project files, `look/sounds/games/`, with a page to listen
to them.

**When the computer's piece sounds**: when its move is played (the piece
begins to land) or when the landing ends, 230 ms later. At once was chosen:
the piece is at its lowest 80 ms in, a sound card adds some tens of ms by
itself, and a sound kept back needs a pending state that a move, Take back
or a closed window would each have to clear.

**One sound a move.** Check and checkmate could be a second sound after the
knock, but the output plays one recording at a time. So each of the two
begins with the knock and takes the move's place.

## 4. What was chosen

Column a throughout, by the agent, to confirm: the family the owner chose
for the system. Recorded in `docs/decisions/game-sounds.md`. Cactus Patch
is untouched, as he asked.

The paper is made of a great many tiny cracks, each a decaying wave of its
own pitch (1.8 to 8 kHz), length (0.3 to 1.8 ms) and loudness (a few loud
among many quiet), thick where the hand squeezes and thin between, over a
quiet rustle of filtered noise.

## 5. What was built

- `tools/sounds/make_game_sounds.py`: the eighteen candidates as WAV files
  and the six built in as `docs/sounds/*.pcm` (240 KB; 630 KB of recordings
  now). It has a random source of its own, so the five system sounds come
  out of `make_sounds.py` byte for byte as before.
- `sound::Id` has eleven entries; `tools/sounds/embed_sounds.py` and the two
  CMake lists that compile the recordings in know the six new files.
- `chess.cpp`: `move_sound()` after every move played, the user's and the
  computer's: ChessMate on checkmate, ChessCheck when the side to move is
  in check, ChessMove otherwise (the draws too).
- `culebra.cpp`: SnakeEat when a step eats a berry, SnakeOver when a step
  ends the game.
- `file_manager.cpp`: Crumple when the Trash has been emptied without an
  error.

Tested on the PC (54 of 54 tests pass):

- `test_chess_window`: two knocks for a move and its answer; the mate sound
  when the computer mates and when the user does; silence for a click that
  moves nothing and for Take back; the check sound for a queen's check.
- `test_sound`: Culebra steered to its first berry is silent until the
  berry and then plays SnakeEat once; run into the wall it plays SnakeOver;
  a new game is silent.
- `test_shell`: Empty Trash answered No plays nothing; answered Yes, the
  question's knock and then Crumple.
- The Linux program was built with the eleven recordings compiled in and
  its own test passes (the beep and the chime in its recorded output). No
  test plays a game in the Linux program and records it.

Not done: the firmware was not compiled (the change to it is two `case`
lines in `core/` and three calls that reach `sound::silence()` there);
nothing ran on the board; nobody has listened.

## 6. What was learned

- The chess window's tests depend on how many games were played before
  them: the computer's opening comes from a number that changes each game.
  A game added in the middle of the test changed a later one's opening and
  broke it; the new game went to the end.
- Emptying the Trash removes `C:\.trash` itself until something is deleted
  again, so a test cannot write a second file there straight afterwards.

## 7. What is left

- The owner listens and picks, or asks for changes, as on 2026-11-16.
- The browser build was not rebuilt here (no Emscripten in the container);
  its CMake list has the same change as the PC's.
