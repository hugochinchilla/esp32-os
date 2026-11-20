// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "................", ".DDDDDDDDDDDDDD.", ".DBBBBBBBBBGGGD.", ".DDDDDDDDDDDDDD.",
    ".DKKKKKKKKKKKKD.", ".DKKKKKKKKKKKKD.", ".DKWWKKKKKKKKKD.", ".DKKKWWKKKKKKKD.",
    ".DKKKKKWWKKKKKD.", ".DKKKWWKKKKKKKD.", ".DKWWKKKWWWWKKD.", ".DKKKKKKKKKKKKD.",
    ".DKKKKKKKKKKKKD.", ".DDDDDDDDDDDDDD.", "................", "................",
};
constexpr app::Ink kSmallInks[] = {
    {'D', gfx::rgb(2, 2, 1)},
    {'B', gfx::rgb(0, 0, 2)},
    {'G', gfx::rgb(5, 5, 2)},
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(7, 7, 3)},
};

} // namespace art
