// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWWWWWWWWWWWWK.",
    ".KBBBBBBBBBBBBK.", ".KBKKKKKBBBBBBK.", ".KBKSSKKBRRRRBK.", ".KBKKSKKBBBBBBK.",
    ".KBKSKSKBRRRRBK.", ".KBKKKKKBBBBBBK.", ".KBKKKKKKKKKKBK.", ".KBKSSSSSSSSKBK.",
    ".KBKBBBBBBBBKBK.", ".KBKKKKKKKKKKBK.", ".KKKKKKKKKKKKKK.", "................",
};
constexpr app::Ink kSmallInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(0, 0, 2)},
    {'B', gfx::rgb(5, 5, 2)},
    {'S', gfx::rgb(7, 7, 3)},
    {'R', gfx::rgb(2, 2, 1)},
};

constexpr const char *kLarge[32] = {
    "................................", "................................",
    "................................", "..KKKKKKKKKKKKKKKKKKKKKKKKKKK...",
    "..KWWWWWWWWWWWWBBBBBBBBBBBBBK...", "..KWWWWWWWWWWWWBBBBBBBBBSSSBKR..",
    "..KWWWWWWWWWWWWBBBBBBBBBSSSBKR..", "..KWWWWWWWWWWWWBBBBBBBBBBBBBKR..",
    "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..",
    "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KSSKKKKKKKSSSSSSSSSSSSSSSSKR..",
    "..KSSKYYYYKKSSSSSSSSSSSSSSSSKR..", "..KSSKYYYKKKSSRRRRRRRRRRRRSSKR..",
    "..KSSKKYKKYKSSSSSSSSSSSSSSSSKR..", "..KSSKKKKYYKSSRRRRRRRRRSSSSSKR..",
    "..KSSKYKYYYKSSSSSSSSSSSSSSSSKR..", "..KSSKKKKKKKSSSSSSSSSSSSSSSSKR..",
    "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..",
    "..KSSKKKKKKKKKKKKSSSSKKSSSSSKR..", "..KSSKYYYYYYYYYYKSSSKYYKSSSSKR..",
    "..KSSKSSSSSSSSSRKSSKYKKYKSSSKR..", "..KSSKSSSSSSSSSRKSSKYKKYKSSSKR..",
    "..KSSKSRRRRRRRRRKSSSKYYKSSSSKR..", "..KSSKKKKKKKKKKKKSSSSKKSSSSSKR..",
    "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KKKKKKKKKKKKKKKKKKKKKKKKKKKR..",
    "....RRRRRRRRRRRRRRRRRRRRRRRRRR..", "................................",
    "................................", "................................",
};
constexpr app::Ink kLargeInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(0, 0, 2)},
    {'B', gfx::rgb(1, 3, 3)},
    {'S', gfx::rgb(5, 5, 2)},
    {'R', gfx::rgb(2, 2, 1)},
    {'Y', gfx::rgb(7, 7, 3)},
};

} // namespace art
