// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "....KKKKKKKK....", "..KKBBGGBBBBKK..", ".KBBGGGGBBBBBBK.", ".KBGGGGGGBBBBBK.",
    "KBBGGGGGBBBBGGBK", "KBBBGGGBBBBGGGBK", "KBBBBGBBBBBGGGBK", "KBBBBBBBBBBBGGBK",
    "KBBBBBBGGBBBBBBK", "KBBBBBGGGGBBBBBK", "KBBBBBGGGGGBBBBK", ".KBBBBBGGGBBBBK.",
    ".KBBBBBBGGBBBBK.", "..KKBBBBBBBBKK..", "....KKKKKKKK....", "................",
};
constexpr app::Ink kSmallInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'B', gfx::rgb(1, 3, 3)},
    {'G', gfx::rgb(1, 5, 1)},
};

} // namespace art
