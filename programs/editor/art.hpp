// The program's pictures, as rows of text: each character is the colour its
// Ink gives it, and one with no Ink is seen through. Drawn for this project.
#pragma once

#include "core/app/file.hpp"
#include "core/gfx/canvas.hpp"

namespace art {

constexpr const char *kSmall[16] = {
    "................", "...K.K.K.K......", "...K.K.K.K......", "..KKKKKKKKKK..KK",
    "..KWWWWWWWWK.KKB", "..KSSSSSSSSKKKBB", "..KSSSSSSSSKKRRK", "..KSYYYYYYKKRRKK",
    "..KSSSSSSKKRRKK.", "..KSYYYYKKRRKK..", "..KSSSSKKOOKK...", "..KSYYYKOOKK....",
    "..KSSSSKKKKK....", "..KSSSSSSSSK....", "..KKKKKKKKKK....", "................",
};
constexpr app::Ink kSmallInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(1, 3, 3)},
    {'B', gfx::rgb(7, 4, 2)},
    {'S', gfx::rgb(7, 7, 3)},
    {'R', gfx::rgb(7, 6, 0)},
    {'Y', gfx::rgb(3, 5, 3)},
    {'O', gfx::rgb(6, 4, 1)},
};

constexpr const char *kLarge[32] = {
    "................................", "................................",
    "................................", ".......K..K..K..K..K............",
    ".......KW.KW.KW.KW.KW...........", ".....KKKKKKKKKKKKKKKKKK.........",
    ".....KBKBBKBBKBBKBBKBBK......KK.", ".....KBKBBKBBKBBKBBKBBKS....KKRR",
    ".....KBBBBBBBBBBBBBBBBKS...KKRRR", ".....KYYYYYYYYYYYYYYYYKS..KKRRRR",
    ".....KYYYOYYYYYYYYYYYYKS.KKWWRRO", ".....KYYYOYYYYYYYYYYYYKSKKWWWWOO",
    ".....KYYPOPPPPPPPPPPYYKKKGGWWSSK", ".....KYYYOYYYYYYYYYYYYKKGGDDSSKK",
    ".....KYYYOYYYYYYYYYYYKKGGDDLLKK.", ".....KYYPOPPPPPPPPPPKKGGDDLLKK..",
    ".....KYYYOYYYYYYYYYKKGGDDLLKK...", ".....KYYYOYYYYYYYYKKGGDDLLKK....",
    ".....KYYPOPPPPPPPKKGGDDLLKK.....", ".....KYYYOYYYYYYKKGGDDLLKK......",
    ".....KYYYOYYYYYKKGGDDLLKK.......", ".....KYYPOPPPPKKNNDDLLKK........",
    ".....KYYYOYYYKKNNNNLLKKS........", ".....KYYYOYYYKNNNNNNKKKS........",
    ".....KYYPOPPPKNNNNNKKYKS........", ".....KYYYOYYYKKNNNKKYYKS........",
    ".....KYYYOYYYKKKKKKYYYKS........", ".....KYYYOYYYYYYYYYYYYKS........",
    ".....KYYYYYYYYYYYYYYYYKS........", ".....KKKKKKKKKKKKKKKKKKS........",
    ".......SSSSSSSSSSSSSSSSS........", "................................",
};
constexpr app::Ink kLargeInks[] = {
    {'K', gfx::rgb(0, 0, 0)},
    {'W', gfx::rgb(5, 5, 2)},
    {'B', gfx::rgb(1, 3, 3)},
    {'S', gfx::rgb(2, 2, 1)},
    {'R', gfx::rgb(7, 4, 2)},
    {'Y', gfx::rgb(7, 7, 3)},
    {'O', gfx::rgb(7, 3, 2)},
    {'P', gfx::rgb(3, 5, 3)},
    {'G', gfx::rgb(7, 7, 2)},
    {'D', gfx::rgb(7, 6, 0)},
    {'L', gfx::rgb(6, 5, 0)},
    {'N', gfx::rgb(6, 4, 1)},
};

} // namespace art
