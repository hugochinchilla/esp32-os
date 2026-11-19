// For the tests and the reference pictures, which have the programs linked
// into them (APP_LINKED, core/app/linked.hpp): a look inside a program
// while it is open. Each is defined in the program's own source; a program
// built into a file does not have them.
#pragma once

#include "core/games/cactus.hpp"
#include "core/games/chess.hpp"
#include "core/games/snake.hpp"

namespace shell {

// Culebra: the game in its window.
const games::Snake &culebra_game();

// Cactus Patch: the game in its window, a repaint after changing it
// directly, and its clock.
games::CactusField &cactus_patch_field();
void cactus_patch_refresh();
int cactus_patch_seconds();

// The Picture viewer: decode the picture shown to the end now; the timings
// of the last picture (shell::kPictureTimings of them).
void finish_picture();
const uint32_t *picture_timings();

// Chess: the game in its window, whether the computer is thinking, the
// side the user plays, the status line, and whose turn it is worked out
// again after the game was changed directly.
games::chess::Game &chess_game();
bool chess_thinking();
games::chess::Color chess_user();
const char *chess_status();
void chess_refresh();

} // namespace shell
