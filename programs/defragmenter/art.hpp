// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "..........KKKK..", "...KKKK...KWWK..", "...KBBK...KWWK..", "...KBBK...KKKK..",
    "...KKKK.........", "................", ".KKKKKKKKKKKKK..", ".KWWKWWKBBKBBK..",
    ".KWWKWWKBBKBBK..", ".KKKKKKKKKKKKK..", ".KKKKKKKKKKKKKK.", ".KSSSSSSSSSSSSK.",
    ".KRRRRRRRRRRYRK.", ".KRRRRRRRRRRRRK.", ".KKKKKKKKKKKKKK.", "................",
};
constexpr app::Ink kSmallInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(1, 3, 3)},
    {'B', gfx::rgb(7, 0, 0)},
    {'S', gfx::rgb(7, 7, 3)},
    {'R', gfx::rgb(5, 5, 2)},
    {'Y', gfx::rgb(4, 7, 1)},
};

constexpr const char *kLarge[32] = {
    "................................", "...............KKKKK............",
    "....KKKKK......KWWWK............", "....KBBBK......KWWWK............",
    "....KBBBK......KWWWK....KKKKK...", "....KBBBK......KKKKK....KBBBK...",
    "....KKKKKKKKKK..........KBBBK...", ".........KWWWK..........KBBBK...",
    ".........KWWWK..........KKKKK...", ".........KWWWK..................",
    ".........KKKKK..................", "................................",
    "....KKKKKKKKKKKKKKKKKKKKKKKKK...", "....KWWWKKWWWKKWWWKKBBBKKBBBK...",
    "....KWWWKKWWWKKWWWKKBBBKKBBBK...", "....KWWWKKWWWKKWWWKKBBBKKBBBK...",
    "....KKKKKKKKKKKKKKKKKKKKKKKKK...", "................................",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", "..KSSSSSSSSSSSSSSSSSSSSSSSSSSK..",
    "..KSRRRRRRRRRRRRRRRRRRRRRRRRYKY.", "..KSRRRRRRRRRRRRRRRRRRRRRRRRYKY.",
    "..KSRRRRRRRRRRRRRRRRRRRRRRRRYKY.", "..KSRRYYYYYYYYYYYYYYRRROOORRYKY.",
    "..KSRRSSSSSSSSSSSSSSRRROOORRYKY.", "..KSRRRRRRRRRRRRRRRRRRRRRRRRYKY.",
    "..KSRRRRRRRRRRRRRRRRRRRRRRRRYKY.", "..KSYYYYYYYYYYYYYYYYYYYYYYYYYKY.",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKKY.", "....YYYYYYYYYYYYYYYYYYYYYYYYYYY.",
    "................................", "................................",
};
constexpr app::Ink kLargeInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(1, 3, 3)},
    {'B', gfx::rgb(7, 0, 0)},
    {'S', gfx::rgb(7, 7, 3)},
    {'R', gfx::rgb(5, 5, 2)},
    {'Y', gfx::rgb(2, 2, 1)},
    {'O', gfx::rgb(4, 7, 1)},
};

} // namespace art
