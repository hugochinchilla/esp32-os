// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "................", "................", "....KKKKKKKK....", "..KKKWWWWWWK....",
    ".KBBKWSSSSWK....", ".KKKKWWWWWWKK...", ".KBBKWSSSSWKK...", ".KBBKWWWWWWKK...",
    ".KBKKKKKKKKKKKKK", ".KBKRRRRRRRRRRRK", ".KKRRRRRRRRRRRK.", ".KKRRRRRRRRRRRK.",
    ".KRRRRRRRRRRRK..", ".KKKKKKKKKKKKK..", "................", "................",
};
constexpr app::Ink kSmallInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(7, 7, 3)},
    {'B', gfx::rgb(6, 5, 0)},
    {'S', gfx::rgb(2, 2, 1)},
    {'R', gfx::rgb(7, 6, 1)},
};

constexpr const char *kLarge[32] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "......KKKKKKKKKKKKKKK...........",
    ".....KKWWWWWWWWWWWWWK...........", "....KBKWWWWWWWWWWWWWK...........",
    "...KBBKWSSKKKKKKKKKKKKKKK.......", "...KBBKWWWKWWWWWWWWWWWWWK.......",
    "..KKKKKWWWKWWWWWWWWWWWWWKKK.....", "..KBBBKWSSKWRRRRRRRRRRWWKBK.....",
    "..KBBBKWWWKWWWWWWWWWWWWWKBK.....", "..KBBBKWWWKWWWWWWWWWWWWWKBK.....",
    "..KBBBKWSSKWRRRRRRRRRRWWKBK.....", "..KBBBKKKKKKKKKKKKKKKKKKKKKKKKK.",
    "..KBBBKYYYYYYYYYYYYYYYYYYYYYYOK.", "..KBBKOOOOOOOOOOOOOOOOOOOOOOOK..",
    "..KBBKOOOOOOOOOOOOOOOOOOOOOOOK..", "..KBBKOOOOOOOOOOOOOOOOOOOOOOOK..",
    "..KBKOOOOOOOOOOOOOOOOOOOOOOOK...", "..KBKOOOOOOOOOOOOOOOOOOOOOOOK...",
    "..KKOOOOOOOOOOOOOOOOOOOOOOOK....", "..KKOOOOOOOOOOOOOOOOOOOOOOOK....",
    "..KKOOOOOOOOOOOOOOOOOOOOOOOK....", "..KOOOOOOOOOOOOOOOOOOOOOOOK.....",
    "..KKKKKKKKKKKKKKKKKKKKKKKKK.....", "....SSSSSSSSSSSSSSSSSSSSSSSS....",
    "................................", "................................",
    "................................", "................................",
};
constexpr app::Ink kLargeInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(7, 7, 3)},
    {'B', gfx::rgb(6, 5, 0)},
    {'S', gfx::rgb(2, 2, 1)},
    {'R', gfx::rgb(3, 5, 3)},
    {'Y', gfx::rgb(7, 7, 2)},
    {'O', gfx::rgb(7, 6, 1)},
};

} // namespace art
