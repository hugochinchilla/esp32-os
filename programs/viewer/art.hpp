// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWBBBBBBBBBBWK.",
    ".KWBBBBBBSBBBWK.", ".KWBBBRBSSSBBWK.", ".KWBBRRRBSBBBWK.", ".KWBBRRRRYBBBWK.",
    ".KWBRRRRYYYBBWK.", ".KWBRRRYYYYYYWK.", ".KWRRRYYYYYYYWK.", ".KWWWWWWWWWWWWK.",
    ".KKKKKKKKKKKKKK.", "...K........K...", "...K........K...", "..K..........K..",
};
constexpr app::Ink kSmallInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(6, 4, 1)},
    {'B', gfx::rgb(3, 5, 3)},
    {'S', gfx::rgb(7, 6, 0)},
    {'R', gfx::rgb(0, 3, 0)},
    {'Y', gfx::rgb(1, 5, 0)},
};

constexpr const char *kLarge[32] = {
    "................................", "...............KK...............",
    "...............KK...............", "...............KK...............",
    "....WWWWWWWWWWWWWWWWWWWWWWWW....", "....WBBBBBBBBBBBBBBBBBBBBBBW....",
    "....WSSSSSSSSSSSSSSSSSSSSSKW....", "....WSSWWWWWWWWWWWWWWWWWWSKW....",
    "....WSSWRRRRRRRRRRRRRRRRWSKW....", "....WSSWRRRRRRRRRRYYYRRRWSKW....",
    "....WSSWRRRRRRRRRYYYYYRRWSKW....", "....WSSWRRRRRRRRRYYYYYRRWSKW....",
    "....WSSWRRRRRRORRYYYYYRRWSKW....", "....WSSWRRRRROOORRYYYRRRWSKW....",
    "....WSSWRRRROOOOORPRRRRRWSKW....", "....WSSWRRROOOOOOPPPRRRRWSKW....",
    "....WSSWRRROOOOOPPPPPRRRWSKW....", "....WSSWRROOOOOPPPPPPPPRWSKW....",
    "....WSSWROOOOOPPPPPPPPPPWSKW....", "....WSSWOOOOOPPPPPPPPPPPWSKW....",
    "....WSSWWWWWWWWWWWWWWWWWWSKW....", "....WSSSSSSSSSSSSSSSSSSSSSKW....",
    "....WKKKKKKKKKKKKKKKKKKKKKKW....", "...WWWWWWWWWWWWWWWWWWWWWWWWWW...",
    "...WKKKKKKKKKKKKKKKKKKKKKKKKW...", "...WWWWWWWWWWWWWWWWWWWWWWWWWW...",
    "........KW............WK........", ".......KW..............WK.......",
    "......KW................WK......", "......KW................WK......",
    ".....KW..................WK.....", "................................",
};
constexpr app::Ink kLargeInks[] = {
    {'K', gfx::rgb(4, 2, 0)},
    {'W', gfx::rgb(0, 0, 0)},
    {'B', gfx::rgb(7, 7, 2)},
    {'S', gfx::rgb(6, 4, 1)},
    {'R', gfx::rgb(3, 5, 3)},
    {'Y', gfx::rgb(7, 6, 0)},
    {'O', gfx::rgb(0, 3, 0)},
    {'P', gfx::rgb(1, 5, 0)},
};

} // namespace art
