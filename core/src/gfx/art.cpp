#include "core/gfx/art.hpp"

namespace gfx {

namespace {

constexpr int kClear = rgb(7, 0, 3); // magenta: never used in the pictures

struct Ink {
    char key;
    Colour colour;
};

// Builds a 16x16 bitmap from text rows; characters not in `inks` are clear.
// The pixels of a picture drawn as text rows, worked out by the compiler so
// the result sits in flash, not in RAM. Characters not in `inks` are clear.
template <int S> struct Pixels {
    uint8_t p[S * S] = {};
};

template <int S, int N> constexpr Pixels<S> paint(const char *const (&rows)[S], const Ink (&inks)[N])
{
    Pixels<S> out;
    for (int r = 0; r < S; r++) {
        for (int q = 0; q < S; q++) {
            uint8_t v = kClear;
            for (int i = 0; i < N; i++) {
                if (rows[r][q] == inks[i].key) {
                    v = inks[i].colour;
                }
            }
            out.p[r * S + q] = v;
        }
    }
    return out;
}

constexpr const char *kMascot[16] = {
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................",
};
constexpr Ink kMascotInks[] = {{'G', kDarkGrey}, {'L', kWhite}, {'P', rgb(7, 4, 2)},
                           {'K', kBlack},    {'N', rgb(7, 3, 2)}, {'D', kDarkGrey}};






constexpr const char *kBoard32[32] = {
    "................................", "................................",
    "................................", ".....KKKKKKKKKKKKKKKKKKKKKK.....",
    ".....KPPPPPPPPPPPPPPPPPPPPK.....", ".....KPPPPPPPPPPPPPPPPPPPPK.....",
    "..OOOOPPPKKKKKKKKKKKKKKPPPOOOO..", ".....KPPPKGGGGGGGGGGGGKPPPK.....",
    ".....KPPPKGGGGGGGGGGGGKPPPK.....", ".....KPPPKGGGGGGGGGGGGKPPPK.....",
    "..OOOOPPPKGGGGGGGGGGGGKPPPOOOO..", ".....KPPPKGGGGGGGGGGGGKPPPK.....",
    ".....KPPPKGGGGGGGGGGGGKPPPK.....", ".....KPPPKGGGGGGGGGGGGKPPPK.....",
    "..OOOOPPPKGGGGGGGGGGGGKPPPOOOO..", ".....KPPPKKKKKKKKKKKKKKPPPK.....",
    ".....KPPPPPPPPPPPPPPPPPPPPK.....", ".....KPPPPPPPPPPPPPPPPPPPPK.....",
    "..OOOOPPPPPPPPPPPPPPPPPPPPOOOO..", ".....KPPPPPPKKKKKKKKPPPPPPK.....",
    ".....KPPPPPPKKKKKKKKPPPPPPK.....", ".....KPPPPPPKKKKKKKKPPPPPPK.....",
    "..OOOOPPPPPPKKKKKKKKPPPPPPOOOO..", ".....KPPPPPPKKKKKKKKPPPPPPK.....",
    ".....KPPPPPPKKKKKKKKPPPPPPK.....", ".....KPPPPPPKKKKKKKKPPPPPPK.....",
    "..OOOOPPPPPPPPPPPPPPPPPPPPOOOO..", ".....KPPPPPPPPPPPPPPPPPPPPK.....",
    ".....KKKKKKKKKKKKKKKKKKKKKK.....", "................................",
    "................................", "................................",
};
constexpr const char *kBin32[32] = {
    "................................", "................................",
    "................................", ".............KKKKKK.............",
    ".............KGGGGK.............", "......KKKKKKKKKKKKKKKKKKKK......",
    "......KGGGGGGGGGGGGGGGGGGK......", "......KGGGGGGGGGGGGGGGGGGK......",
    "......KKKKKKKKKKKKKKKKKKKK......", "........KGGGGGGGGGGGGGGK........",
    "........KGGGGGGGGGGGGGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGDGGGDGGGDGGK........", "........KGGGDGGGDGGGDGGK........",
    "........KGGGGGGGGGGGGGGK........", "........KGGGGGGGGGGGGGGK........",
    "........KKKKKKKKKKKKKKKK........", "................................",
    "................................", "................................",
};

// The bin with something in it: no lid, crumpled paper above the top, the
// lid leaning against its side.
constexpr const char *kBinFull32[32] = {
    "................................", "..............KKKKK.............",
    ".............KWWWWWK............", ".............KWDWWDKKKKK........",
    "..........KKKKWWDWWKWWDWK.......", ".........KWDWKWWWDWKDWWWK.......",
    ".........KWWDKWDWWWKWDWWK.......", ".........KWDWKWWWWWKWWDWK.......",
    "........KKKKKKKKKKKKKKKK........", "...KKKK.KGGGGGGGGGGGGGGK........",
    "...KGGK.KGGGGGGGGGGGGGGK........", "...KGGK.KGGGDGGGDGGGDGGK........",
    "...KGGK.KGGGDGGGDGGGDGGK........", "...KGGK.KGGGDGGGDGGGDGGK........",
    "...KGGK.KGGGDGGGDGGGDGGK........", ".KKKGGK.KGGGDGGGDGGGDGGK........",
    ".KGKGGK.KGGGDGGGDGGGDGGK........", ".KGKGGK.KGGGDGGGDGGGDGGK........",
    ".KGKGGK.KGGGDGGGDGGGDGGK........", ".KGKGGK.KGGGDGGGDGGGDGGK........",
    ".KGKGGK.KGGGDGGGDGGGDGGK........", ".KGKGGK.KGGGDGGGDGGGDGGK........",
    ".KKKGGK.KGGGDGGGDGGGDGGK........", "...KGGK.KGGGDGGGDGGGDGGK........",
    "...KGGK.KGGGDGGGDGGGDGGK........", "...KGGK.KGGGDGGGDGGGDGGK........",
    "...KGGK.KGGGGGGGGGGGGGGK........", "...KGGK.KGGGGGGGGGGGGGGK........",
    "...KKKK.KKKKKKKKKKKKKKKK........", "................................",
    "................................", "................................",
};

// The bins at 16x16, drawn for that size (black edges, white body: they
// show on grey toolbars).
constexpr const char *kBin16[16] = {
    "................", "......KKKK......",
    "......KWWK......", "..KKKKKKKKKKKK..",
    "..KWWWWWWWWWWK..", "..KKKKKKKKKKKK..",
    "...KWWWWWWWWK...", "...KWWDWWDWWK...",
    "...KWWDWWDWWK...", "...KWWDWWDWWK...",
    "...KWWDWWDWWK...", "...KWWDWWDWWK...",
    "...KWWDWWDWWK...", "...KWWWWWWWWK...",
    "...KKKKKKKKKK...", "................",
};
constexpr const char *kBinFull16[16] = {
    "................", ".....KKK.KKKK...",
    "....KWWWKWWWWK..", "...KWDWWKWWDWK..",
    "...KWWWDKWDWWK..", "..KKKKKKKKKKKK..",
    "...KWWWWWWWWK...", "...KWWDWWDWWK...",
    "...KWWDWWDWWK...", "...KWWDWWDWWK...",
    "...KWWDWWDWWK...", "...KWWDWWDWWK...",
    "...KWWDWWDWWK...", "...KWWWWWWWWK...",
    "...KKKKKKKKKK...", "................",
};

constexpr Ink kBoardInks[] = {{'K', kBlack}, {'P', rgb(0, 3, 1)}, {'G', kGrey}, {'O', rgb(6, 5, 0)}};
constexpr Ink kBinInks[] = {{'K', kBlack}, {'G', kGrey}, {'D', kDarkGrey}, {'W', kWhite}};

constexpr const char *kDrive32[32] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..",
    "..KWWWWWWWWWWWWWWWWWWWWWWWWWWK..", "..KLLLLLLLLLLLLLLLLLLLLLLLLLLK..",
    "..KLLLLLLLLLLLLLLLLLLLLLLLLLLK..", "..KLLLLLLLLLLLLLLLLLLLLLLLLLLK..",
    "..KLDDDDDDDDDDDDDDLLLLLLLLLLLK..", "..KLLLLLLLLLLLLLLLLLLLLLLLLLLK..",
    "..KLLLLLLLLLLLLLLLLLLLLLNNNLLK..", "..KLLLLLLLLLLLLLLLLLLLLLNNNLLK..",
    "..KLLLLLLLLLLLLLLLLLLLLLNNNLLK..", "..KLLLLLLLLLLLLLLLLLLLLLLLLLLK..",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
};
constexpr const char *kFile32[32] = {
    "................................", "................................",
    "................................", ".......KKKKKKKKKKKKKK...........",
    ".......KWWWWWWWWWWWWKK..........", ".......KWWWWWWWWWWWWKWK.........",
    ".......KWWWWWWWWWWWWKWWK........", ".......KWWWWWWWWWWWWKWWWK.......",
    ".......KWWWWWWWWWWWWKKKKKK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KKKKKKKKKKKKKKKKKKK......", "................................",
    "................................", "................................",
};
constexpr const char *kDrive16[16] = {
    "................", "................",
    "................", "................",
    "................", ".KKKKKKKKKKKKKK.",
    ".KWWWWWWWWWWWWK.", ".KLLLLLLLLLLLLK.",
    ".KLDDDDDDLLLLLK.", ".KLLLLLLLLLNNLK.",
    ".KLLLLLLLLLLLLK.", ".KKKKKKKKKKKKKK.",
    "................", "................",
    "................", "................",
};

constexpr Ink kDriveInks[] = {{'K', kBlack}, {'L', kGrey}, {'W', kWhite}, {'D', kDarkGrey}, {'N', rgb(0, 7, 0)}};
constexpr Ink kFileInks[] = {{'K', kBlack}, {'W', kWhite}};

// Folder toolbar: Back is an arrow pointing left, Up a folder with an arrow
// rising out of it (they shared the folder picture before).
constexpr const char *kBack16[16] = {
    "................", "................", ".......K........", "......KK........",
    ".....KLK........", "....KLBKKKKKKK..", "...KLBBLLLLLLK..", "..KLBBBBBBBBBK..",
    "...KBBBBBBBBBK..", "....KBBKKKKKKK..", ".....KBK........", "......KK........",
    ".......K........", "................", "................", "................",
};
constexpr Ink kBackInks[] = {{'K', kBlack}, {'B', rgb(1, 3, 3)}, {'L', rgb(4, 6, 3)}};

constexpr const char *kUp16[16] = {
    "................", ".......K........", "......KGK.......", ".....KGGGK......",
    "....KGGGGGK.....", "...KKKKGKKKK....", "..KKKKKGK.......", ".KKKKKKGKKKKKKK.",
    ".KHHHHKGKHHHHHK.", ".KYYYYKGKYYYYYK.", ".KYYYYKKKYYYYYK.", ".KYYYYYYYYYYYYK.",
    ".KYYYYYYYYYYYYK.", ".KKKKKKKKKKKKKK.", "................", "................",
};
constexpr Ink kUpInks[] = {{'K', kBlack}, {'H', rgb(7, 7, 2)}, {'Y', rgb(7, 6, 1)}, {'G', rgb(1, 5, 0)}};

} // namespace

const Bitmap &drive_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kDrive16, kDriveInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &drive_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kDrive32, kDriveInks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &file_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kFile32, kFileInks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &board_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kBoard32, kBoardInks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &bin_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kBin32, kBinInks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &bin_full_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kBinFull32, kBinInks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &bin_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kBin16, kBinInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &bin_full_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kBinFull16, kBinInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &back_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kBack16, kBackInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &up_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kUp16, kUpInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &mascot_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kMascot, kMascotInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

/* ---- Programs, folders and files ---------------------------------------------
 * Written by tools/mockup/icon_proposals.py --cpp from its drawings (the
 * owner's choice, 2026-11-16). Change the drawing there and paste again. */

const Bitmap &editor_icon_32()
{
    static constexpr const char *rows[32] = {
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
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(5, 5, 2)},
        {'B', rgb(1, 3, 3)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(7, 4, 2)},
        {'Y', rgb(7, 7, 3)},
        {'O', rgb(7, 3, 2)},
        {'P', rgb(3, 5, 3)},
        {'G', rgb(7, 7, 2)},
        {'D', rgb(7, 6, 0)},
        {'L', rgb(6, 5, 0)},
        {'N', rgb(6, 4, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &editor_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "...K.K.K.K......", "...K.K.K.K......", "..KKKKKKKKKK..KK",
        "..KWWWWWWWWK.KKB", "..KSSSSSSSSKKKBB", "..KSSSSSSSSKKRRK", "..KSYYYYYYKKRRKK",
        "..KSSSSSSKKRRKK.", "..KSYYYYKKRRKK..", "..KSSSSKKOOKK...", "..KSYYYKOOKK....",
        "..KSSSSKKKKK....", "..KSSSSSSSSK....", "..KKKKKKKKKK....", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(1, 3, 3)},
        {'B', rgb(7, 4, 2)},
        {'S', rgb(7, 7, 3)},
        {'R', rgb(7, 6, 0)},
        {'Y', rgb(3, 5, 3)},
        {'O', rgb(6, 4, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &folder_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "................................",
        "................................", "................................",
        ".....KKKKKKKK...................", "....KWWWWWWWWK..................",
        "...KWWWWWWWWWWK.................", "...KWWWWWWWWWWK.................",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKK...", "..KBBBBBBBBBBBBBBBBBBBBBBBBBK...",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..",
        "..KBSSSSSSSSSSSSSSSSSSSSSSSWKR..", "..KBWWWWWWWWWWWWWWWWWWWWWWWWKR..",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKR..", "....RRRRRRRRRRRRRRRRRRRRRRRRRR..",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(6, 5, 0)},
        {'B', rgb(7, 7, 2)},
        {'S', rgb(7, 6, 1)},
        {'R', rgb(2, 2, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &folder_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "................", "................", "..KKKKK.........",
        ".KWWWWWK........", ".KKKKKKKKKKKKKK.", ".KBBBBBBBBBBBBK.", ".KSSSSSSSSSSSWK.",
        ".KSSSSSSSSSSSWK.", ".KSSSSSSSSSSSWK.", ".KSSSSSSSSSSSWK.", ".KSSSSSSSSSSSWK.",
        ".KWWWWWWWWWWWWK.", ".KKKKKKKKKKKKKK.", "................", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(6, 5, 0)},
        {'B', rgb(7, 7, 2)},
        {'S', rgb(7, 6, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &files_icon_32()
{
    static constexpr const char *rows[32] = {
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
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(6, 5, 0)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(3, 5, 3)},
        {'Y', rgb(7, 7, 2)},
        {'O', rgb(7, 6, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &files_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "................", "....KKKKKKKK....", "..KKKWWWWWWK....",
        ".KBBKWSSSSWK....", ".KKKKWWWWWWKK...", ".KBBKWSSSSWKK...", ".KBBKWWWWWWKK...",
        ".KBKKKKKKKKKKKKK", ".KBKRRRRRRRRRRRK", ".KKRRRRRRRRRRRK.", ".KKRRRRRRRRRRRK.",
        ".KRRRRRRRRRRRK..", ".KKKKKKKKKKKKK..", "................", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(6, 5, 0)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(7, 6, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &text_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", ".......KKKKKKKKKKKKK............",
        ".......KWWWWWWWWWWWKK...........", ".......KWWWWWWWWWWWKBK..........",
        ".......KWWWWWWWWWWWKBBK.........", ".......KWWWWWWWWWWWKBBBK........",
        ".......KWWWWWWWWWWWKKKKKK.......", ".......KWWSSSSSSSWWWWWWWKS......",
        ".......KWWWWWWWWWWWWWWWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KWWSSSSSSSSSSSSWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KWWWWWWWWWWWWWWWWKS......", ".......KWWSSSSSSSSSSSSWWKS......",
        ".......KWWWWWWWWWWWWWWWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KWWSSSSSSSSSSSSWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KWWWWWWWWWWWWWWWWKS......", ".......KWWSSSSSSSSSSSSWWKS......",
        ".......KWWWWWWWWWWWWWWWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KWWSSSSSSWWWWWWWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KWWWWWWWWWWWWWWWWKS......", ".......KWWWWWWWWWWWWWWWWKS......",
        ".......KKKKKKKKKKKKKKKKKKS......", "........SSSSSSSSSSSSSSSSSS......",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(6, 6, 3)},
        {'S', rgb(2, 2, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &text_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "...KKKKKKK......", "...KWWWWWKK.....", "...KWWWWWKBK....",
        "...KWWWWWKKKK...", "...KWSSSWWWWK...", "...KWWWWWWWWK...", "...KWSSSSSSWK...",
        "...KWWWWWWWWK...", "...KWSSSSSSWK...", "...KWWWWWWWWK...", "...KWSSSSWWWK...",
        "...KWWWWWWWWK...", "...KWWWWWWWWK...", "...KKKKKKKKKK...", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(6, 6, 3)},
        {'S', rgb(2, 2, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &picture_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "................................",
        "................................", "...KKKKKKKKKKKKKKKKKKKKKKKKKK...",
        "...KWWWWWWWWWWWWWWWWWWWWWWWWK...", "...KWWWWWWWWWWWWWWWWWWWWWWWWKB..",
        "...KWWSSSSSSSSSSSSSSSSSSSSWWKB..", "...KWWSSSSSSSSSSSSSSRRSSSSWWKB..",
        "...KWWSSSSSSSSSSSSSRRRRSSSWWKB..", "...KWWSSSSSSSSSSSSRRRRRRSSWWKB..",
        "...KWWSSSSSSSSSSSSRRRRRRSSWWKB..", "...KWWSSSSSSSSSSSSSRRRRSSSWWKB..",
        "...KWWSSSSSSSYSSSSSSRRSSSSWWKB..", "...KWWSSSSSSYYYSSSSSSSSSSSWWKB..",
        "...KWWSSSSSYYYYYSSSSSSSSSSWWKB..", "...KWWSSSSSYYYYYYSSOSSSSSSWWKB..",
        "...KWWSSSSYYYYYYYYOOOSSSSSWWKB..", "...KWWSSSYYYYYYYYOOOOOSSSSWWKB..",
        "...KWWSSYYYYYYYYOOOOOOOOSSWWKB..", "...KWWSSYYYYYYOOOOOOOOOOOSWWKB..",
        "...KWWSYYYYYYOOOOOOOOOOOOOWWKB..", "...KWWYYYYYYOOOOOOOOOOOOOOWWKB..",
        "...KWWWWWWWWWWWWWWWWWWWWWWWWKB..", "...KWWWWWWWWWWWWWWWWWWWWWWWWKB..",
        "...KKKKKKKKKKKKKKKKKKKKKKKKKKB..", ".....BBBBBBBBBBBBBBBBBBBBBBBBB..",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(2, 2, 1)},
        {'S', rgb(3, 5, 3)},
        {'R', rgb(7, 6, 0)},
        {'Y', rgb(0, 3, 0)},
        {'O', rgb(1, 5, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &picture_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.",
        ".KWBBBBBBBBBBWK.", ".KWBBBBBBSBBBWK.", ".KWBBBRBSSSBBWK.", ".KWBBRRRBSBBBWK.",
        ".KWBBRRRRYBBBWK.", ".KWBRRRRYYYBBWK.", ".KWBRRRYYYYYYWK.", ".KWRRRYYYYYYYWK.",
        ".KWWWWWWWWWWWWK.", ".KKKKKKKKKKKKKK.", "................", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(3, 5, 3)},
        {'S', rgb(7, 6, 0)},
        {'R', rgb(0, 3, 0)},
        {'Y', rgb(1, 5, 0)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &viewer_icon_32()
{
    static constexpr const char *rows[32] = {
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
    static constexpr Ink inks[] = {
        {'K', rgb(4, 2, 0)},
        {'W', rgb(0, 0, 0)},
        {'B', rgb(7, 7, 2)},
        {'S', rgb(6, 4, 1)},
        {'R', rgb(3, 5, 3)},
        {'Y', rgb(7, 6, 0)},
        {'O', rgb(0, 3, 0)},
        {'P', rgb(1, 5, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &viewer_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWBBBBBBBBBBWK.",
        ".KWBBBBBBSBBBWK.", ".KWBBBRBSSSBBWK.", ".KWBBRRRBSBBBWK.", ".KWBBRRRRYBBBWK.",
        ".KWBRRRRYYYBBWK.", ".KWBRRRYYYYYYWK.", ".KWRRRYYYYYYYWK.", ".KWWWWWWWWWWWWK.",
        ".KKKKKKKKKKKKKK.", "...K........K...", "...K........K...", "..K..........K..",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(6, 4, 1)},
        {'B', rgb(3, 5, 3)},
        {'S', rgb(7, 6, 0)},
        {'R', rgb(0, 3, 0)},
        {'Y', rgb(1, 5, 0)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &tasks_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "................................",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKK...", "..KWWWWWWWWWWWWBBBBBBBBBBBBBK...",
        "..KWWWWWWWWWWWWBBBBBBBBBSSSBKR..", "..KWWWWWWWWWWWWBBBBBBBBBSSSBKR..",
        "..KWWWWWWWWWWWWBBBBBBBBBBBBBKR..", "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..",
        "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..",
        "..KSKKKKYKKKYKKKYKKKYKKKYKKSKR..", "..KSKKKKYKKKYKKOYKKKYKKKYKKSKR..",
        "..KSKKKKYKKKYKKOYKKKYKKKYKKSKR..", "..KSKYYYYYYYYYOYOYYYYYYYYYKSKR..",
        "..KSKKKKYKKKYKOKOKKKOKKKYKKSKR..", "..KSKKKKYKOKYOKKOKKOOKKKYKKSKR..",
        "..KSKKKKYKOOYOKKYOOKYOKKYOKSKR..", "..KSKYYYYOYOOYYYYOYYYOYYOYKSKR..",
        "..KSKKKKYOKKOKKKYKKKYKOOYKKSKR..", "..KSKOOOOKKKYKKKYKKKYKOKYKKSKR..",
        "..KSKKKKYKKKYKKKYKKKYKKKYKKSKR..", "..KSKKKKYKKKYKKKYKKKYKKKYKKSKR..",
        "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..", "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKR..", "....RRRRRRRRRRRRRRRRRRRRRRRRRR..",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(0, 0, 2)},
        {'B', rgb(1, 3, 3)},
        {'S', rgb(5, 5, 2)},
        {'R', rgb(2, 2, 1)},
        {'Y', rgb(0, 3, 0)},
        {'O', rgb(4, 7, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &tasks_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.",
        ".KWWWWWWWWWWWWK.", ".KBBBBBBBBBBBBK.", ".KKKKKKKKKKKKKK.", ".KKKKKKSKKKKKKK.",
        ".KKKKKSKSKSKKKK.", ".KKKKKSKSKSSKKK.", ".KKSSSKKKSKKSKK.", ".KKKKKKKKKKKKKK.",
        ".KKKKKKKKKKKKKK.", ".KKKKKKKKKKKKKK.", "................", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(0, 0, 2)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(4, 7, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

// The Terminal: a small window with a dark screen and a prompt, ">_".
const Bitmap &terminal_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", ".DDDDDDDDDDDDDD.", ".DBBBBBBBBBGGGD.", ".DDDDDDDDDDDDDD.",
        ".DKKKKKKKKKKKKD.", ".DKKKKKKKKKKKKD.", ".DKWWKKKKKKKKKD.", ".DKKKWWKKKKKKKD.",
        ".DKKKKKWWKKKKKD.", ".DKKKWWKKKKKKKD.", ".DKWWKKKWWWWKKD.", ".DKKKKKKKKKKKKD.",
        ".DKKKKKKKKKKKKD.", ".DDDDDDDDDDDDDD.", "................", "................",
    };
    static constexpr Ink inks[] = {
        {'D', rgb(2, 2, 1)},
        {'B', rgb(0, 0, 2)},
        {'G', rgb(5, 5, 2)},
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &defrag_icon_32()
{
    static constexpr const char *rows[32] = {
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
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(1, 3, 3)},
        {'B', rgb(7, 0, 0)},
        {'S', rgb(7, 7, 3)},
        {'R', rgb(5, 5, 2)},
        {'Y', rgb(2, 2, 1)},
        {'O', rgb(4, 7, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &defrag_icon_16()
{
    static constexpr const char *rows[16] = {
        "..........KKKK..", "...KKKK...KWWK..", "...KBBK...KWWK..", "...KBBK...KKKK..",
        "...KKKK.........", "................", ".KKKKKKKKKKKKK..", ".KWWKWWKBBKBBK..",
        ".KWWKWWKBBKBBK..", ".KKKKKKKKKKKKK..", ".KKKKKKKKKKKKKK.", ".KSSSSSSSSSSSSK.",
        ".KRRRRRRRRRRYRK.", ".KRRRRRRRRRRRRK.", ".KKKKKKKKKKKKKK.", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(1, 3, 3)},
        {'B', rgb(7, 0, 0)},
        {'S', rgb(7, 7, 3)},
        {'R', rgb(5, 5, 2)},
        {'Y', rgb(4, 7, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &snake_icon_32()
{
    static constexpr const char *rows[32] = {
        "..........................K.....", "..........................WW....",
        ".........................BSSW...", "........................WSSSSW..",
        "........................WSSSSW..", ".........................WSSW...",
        "..........................WW....", "..................KKKKKKKK......",
        "......KKKKKKKKKKKKKRRRRRRK......", "......KYRRRRRRRRRRKYYYOOYK......",
        "......KYYKKYYKKYYKKYYYOPYK...S..", "......KYYKKYYKKYYKKYYYYYYKSSS...",
        "......KRYYKKKKKKKKKYYYYYYK...S..", "......KRYYK.......KKKKKKKK......",
        "......KRYYK.....................", "......KRYYK.....................",
        "......KRYYKKKKKKKKKKKKKKKKKKK...", "......KRYYRRRRRRRRRRRRRRRRRYK...",
        "......KRYYKYYKKYYKKYYKKYYKKYK...", "......KYYYKYYKKYYKKYYKKYYKKYK...",
        "......KKKKKKKKKKKKKKKKKKKRYYK...", "........................KRYYK...",
        "........................KRYYK...", "........................KRYYK...",
        "....KKKKKKKKKKKKKKKKKKKKKRYYK...", "....KYRRRRRRRRRRRRRRRRRRRRYYK...",
        "....KYYKKYYKKYYKKYYKKYYKKRYYK...", "....KYYKKYYKKYYKKYYKKYYKKYYYK...",
        "....KKKKKKKKKKKKKKKKKKKKKKKKK...", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 3, 0)},
        {'W', rgb(4, 0, 0)},
        {'B', rgb(7, 4, 2)},
        {'S', rgb(7, 0, 0)},
        {'R', rgb(4, 7, 1)},
        {'Y', rgb(1, 5, 0)},
        {'O', rgb(7, 7, 3)},
        {'P', rgb(0, 0, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &snake_icon_16()
{
    static constexpr const char *rows[16] = {
        "............KKKK", "............KWWK", "............KWWK", "............KKKK",
        "...BBBBBBBB.....", "...BSSSSSRBWW...", "...BSBBBBBB.....", "...BSB..........",
        "...BSBBBBBBBBB..", "...BSSSSSSSSSB..", "...BBBBBBBBBSB..", "...........BSB..",
        "..BBBBBBBBBBSB..", "..BSSSSSSSSSSB..", "..BBBBBBBBBBBB..", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(4, 0, 0)},
        {'W', rgb(7, 0, 0)},
        {'B', rgb(0, 3, 0)},
        {'S', rgb(1, 5, 0)},
        {'R', rgb(7, 7, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &cactus_icon_32()
{
    static constexpr const char *rows[32] = {
        "..............KKK...............", ".............KWWWK..............",
        ".............KWWWK..............", ".............KWWWKB.............",
        "............BSKKKRYB............", "............BSRRRRYB............",
        "............BSRRORYB............", "............BSRRRRYB....BBB.....",
        "....BBB.....BSRRRRYB...BSRYB....", "...BSRYB....BSRORRYB...BSRYB....",
        "...BSRYB....BSRRRRYB...BSRYB....", "...BSRYB....BSRRRRYB...BSRYB....",
        "...BSRYB....BSRRRRYBBBBBRRYB....", "...BSRYB....BSRRORRRRRRRRRYB....",
        "...BSRRBBBBBBSRRRRRRRRRRRYB.....", "...BSRRRRRRRRRRRRRYBBBBBBB......",
        "....BSRRRRRRRRORRRYB............", ".....BBBBBBBBSRRRRYB............",
        "............BSRRRRYB............", "............BSRRRRYB............",
        "............BSRORRYB............", "............BSRRRRYB............",
        ".......BBBBBBBBBBBBBBBBBB.......", ".......BPPPPPPPPPPPPPPPPB.......",
        ".......BBBBBBBBBBBBBBBBBB.......", "........BGPPPPPPPPPPPPDB........",
        "........BGPPPPPPPPPPPPDB........", ".........BGPPPPPPPPPPDB.........",
        ".........BGPPPPPPPPPPDB.........", "..........BGPPPPPPPPDB..........",
        "..........BBBBBBBBBBBB..........", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(4, 0, 0)},
        {'W', rgb(7, 3, 2)},
        {'B', rgb(0, 0, 0)},
        {'S', rgb(4, 7, 1)},
        {'R', rgb(1, 5, 0)},
        {'Y', rgb(0, 3, 0)},
        {'O', rgb(7, 7, 3)},
        {'P', rgb(7, 4, 0)},
        {'G', rgb(6, 4, 1)},
        {'D', rgb(4, 2, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &cactus_icon_16()
{
    static constexpr const char *rows[16] = {
        "......KKK.......", ".....KWWWK......", "......KKKK......", "......KBSK......",
        "......KBSK.KK...", "..KK..KBSK.KSK..", ".KBSK.KBSK.KSK..", ".KBSK.KBSKKKSK..",
        ".KBSKKKBSSSSK...", ".KBSSSSBSKKK....", "..KKKKKBSK......", "....KKKKKKKK....",
        "....KRRRRRRK....", ".....KRRRRK.....", ".....KRRRRK.....", ".....KKKKKK.....",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 3, 2)},
        {'B', rgb(4, 7, 1)},
        {'S', rgb(1, 5, 0)},
        {'R', rgb(7, 4, 0)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &network_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        ".KKKKKKKKKKKKKKKKK..............", ".KWWWWWWWWWWWWWWWK..............",
        ".KWBBBBBBBBBBBBBSK..............", ".KWBKKKKKKKKKKKBSK..............",
        ".KWBKWWWWWRRRRKBSK..............", ".KWBKRRRRRRRRRKBSK..............",
        ".KWBKRRRRRRRRRKBSK..............", ".KWBKRRRRRRRRRKBSK..............",
        ".KWBKRRRRRRRRRKBSK..............", ".KWBKKKKKKKKKKKBSK..............",
        ".KWBBBBBBBBBBBKKKKKKKKKKKKKKKKK.", ".KWBBBBBBBBBBBKWWWWWWWWWWWWWWWK.",
        ".KWSSSSSSSSSSSKWBBBBBBBBBBBBBSK.", ".KKKKKKKKKKKKKKWBKKKKKKKKKKKBSK.",
        ".......KBBBK..KWBKWWWWWRRRRKBSK.", ".....KKKKKKKKKKWBKRRRRRRRRRKBSK.",
        ".....KWWWWWWWKKWBKRRRRRRRRRKBSK.", ".....KKKKKKKKKKWBKRRRRRRRRRKBSK.",
        "..............KWBKRRRRRRRRRKBSK.", "...KY.........KWBKKKKKKKKKKKBSK.",
        "...KY.........KWBBBBBBBBBBBBBSK.", "...KY.........KWBBBBBBBBBBBBBSK.",
        "...KY.........KWSSSSSSSSSSSSSSK.", "...KY.........KKKKKKKKKKKKKKKKK.",
        "...KY...............KBBBK.......", "...KY.............KKKKKKKKK.....",
        "...KYYYYYYYYY.....KWWWWWWWK.....", "...KKKKKKKKKK.....KKKKKKKKK.....",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(1, 5, 3)},
        {'Y', rgb(7, 6, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &network_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "KKKKKKKKK.......", "KWWWWWWWK.......", "KWBBBBBWK.......",
        "KWBBBBBWK.......", "KWBBBBBWK.......", "KWWWWWWWK.......", "KKKKKKKKKKKKKKKK",
        "...KKK.KWWWWWWWK", "..K....KWBBBBBWK", "..K....KWBBBBBWK", "..K....KWBBBBBWK",
        "..KKKKKKWWWWWWWK", ".......KKKKKKKKK", ".........KKKKK..", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(5, 5, 2)},
        {'B', rgb(1, 5, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &saver_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", "..KWWWWWWWWWWWWWWWWWWWWWWWWWWK..",
        "..KWBBBBBBBBBBBBBBBBBBBBBBBBSK..", "..KWBKKKKKKKKKKKKKKKKKKKKKKBSK..",
        "..KWBKKKKKKKKKKKKKKKKKKKKKKBSK..", "..KWBKKKKKRKKKKKKKKKWKKKKKKBSK..",
        "..KWBKKKWRKKKKKKKKKKKKKKKKKBSK..", "..KWBKKKRRKKKKKKKKKKKKKKKKKBSK..",
        "..KWBKKKRRRKKKKKWKKKKKKKKKKBSK..", "..KWBKKKKRRRKKKKKKKKKKKKKKKBSK..",
        "..KWBKKKKKRRKKKKKKYKKKKKKKKBSK..", "..KWBKKWKKKKKKKKKYYYKKKKKKKBSK..",
        "..KWBKKKKKKKKKKKKKYKKKKWKKKBSK..", "..KWBKKKKKKKWKKKKKKKKKKKKKKBSK..",
        "..KWBKKKKKKKKKKKKKKKKKKKKKKBSK..", "..KWBKKKKKKKKKKKKKKKKKKKKKKBSK..",
        "..KWBKKKKKKKKKKKKKKKKKKKKKKBSK..", "..KWBBBBBBBBBBBBBBBBBBBBBBBBSK..",
        "..KWBBBBBBBBBBBBBBBBBBBBBBBBSK..", "..KWSSSSSSSSSSSSSSSSSSSSSSSSSK..",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", ".............KBBBK..............",
        "........KKKKKKKKKKKKKKK.........", "........KWWWWWWWWWWWWWK.........",
        "........KKKKKKKKKKKKKKK.........", "................................",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(7, 7, 2)},
        {'Y', rgb(7, 6, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &saver_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWKKKKKKKKKKWK.",
        ".KWKKBKKSKKKKWK.", ".KWKKKKSSSBKKWK.", ".KWKKKKKSKKKKWK.", ".KWKKKKKKKKKKWK.",
        ".KWKKKKBKKKBKWK.", ".KWKKKKKKKKKKWK.", ".KWWWWWWWWWWWWK.", ".KKKKKKKKKKKKKK.",
        "......KKKK......", "....KKKKKKKK....", "....KKKKKKKK....", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(5, 5, 2)},
        {'B', rgb(7, 7, 3)},
        {'S', rgb(7, 6, 0)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &display_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", "..KWWWWWWWWWWWWWWWWWWWWWWWWWWK..",
        "..KWBBBBBBBBBBBBBBBBBBBBBBBBSK..", "..KWBKKKKKKKKKKKKKKKKKKKKKKBSK..",
        "..KWBKRRYRRRYRRRYRRRYRRRYRKBSK..", "..KWBKRRRRRRRRRRRRRRRRRRRRKBSK..",
        "..KWBKYRRKKKKKKKKKKKRRYRRRKBSK..", "..KWBKRRRKOOOOOOOOOKRRRRRRKBSK..",
        "..KWBKRRYKOOOOOOOOOKYRRRYRKBSK..", "..KWBKRRRKWWWWWWWWWKRRRRRRKBSK..",
        "..KWBKYRRKWWWWWWWWWKRRYRRRKBSK..", "..KWBKRRRKWWWWWWWWWKRRRRRRKBSK..",
        "..KWBKRRYKWWWWWWWWWKYRRRYRKBSK..", "..KWBKRRRKKKKKKKKKKKRRRRRRKBSK..",
        "..KWBKYRRRYRRRYRRRYRRRYRRRKBSK..", "..KWBKBBBBBBBBBBBBBBBBBBBBKBSK..",
        "..KWBKBBBBBBBBBBBBBBBBBBBBKBSK..", "..KWBBBBBBBBBBBBBBBBBBBBBBBBSK..",
        "..KWBBBBBBBBBBBBBBBBBBBBBBBBSK..", "..KWSSSSSSSSSSSSSSSSSSSSSSSSSK..",
        "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", ".............KBBBK..............",
        "........KKKKKKKKKKKKKKK.........", "........KWWWWWWWWWWWWWK.........",
        "........KKKKKKKKKKKKKKK.........", "................................",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(1, 3, 3)},
        {'Y', rgb(5, 6, 3)},
        {'O', rgb(0, 0, 2)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &display_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWBBBBBBBBBBWK.",
        ".KWBSBBBSBBBBWK.", ".KWBBBBBBBBSBWK.", ".KWBBBSBBBBBBWK.", ".KWBBBBBBBSBBWK.",
        ".KWBSBBBSBBBBWK.", ".KWBBBBBBBBBBWK.", ".KWWWWWWWWWWWWK.", ".KKKKKKKKKKKKKK.",
        "......KKKK......", "....KKKKKKKK....", "....KKKKKKKK....", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(5, 5, 2)},
        {'B', rgb(1, 3, 3)},
        {'S', rgb(5, 6, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &settings_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "...KKKKKKKKKKKKKKKKKKKKKKKKK....",
        "...KWWWWWWWWWWWWWWWWWWWWWWWK....", "...KWBBBBBBBBBBBBBBBBBBBBBSKS...",
        "...KWBBBBBBBBBBBBBBBBBBBBBSKS...", "...KWBBBBKWBBBBKWBBBBKWBBBSKS...",
        "...KWBBBBKKBBBBKKBBBBKKBBBSKS...", "...KWBBKKKKKKBBKKBBBBKKBBBSKS...",
        "...KWBBKWWWWKBBKKBBBBKKBBBSKS...", "...KWBBKRRRRKBBKKBBBBKKBBBSKS...",
        "...KWBBKRRRRKBBKKBBKKKKKKBSKS...", "...KWBBKKKKKKBBKKBBKWWWWKBSKS...",
        "...KWBBBBKKBBBBKKBBKYYYYKBSKS...", "...KWBBBBKKBBBBKKBBKYYYYKBSKS...",
        "...KWBBBBKKBBBBKKBBKKKKKKBSKS...", "...KWBBBBKKBBKKKKKKBBKKBBBSKS...",
        "...KWBBBBKKBBKWWWWKBBKKBBBSKS...", "...KWBBBBKKBBKOOOOKBBKKBBBSKS...",
        "...KWBBBBKKBBKOOOOKBBKKBBBSKS...", "...KWBBBBKKBBKKKKKKBBKKBBBSKS...",
        "...KWBBBBKKBBBBKKBBBBKKBBBSKS...", "...KWBBBBKKBBBBKKBBBBKKBBBSKS...",
        "...KWBBBBBBBBBBBBBBBBBBBBBSKS...", "...KWBBBBBBBBBBBBBBBBBBBBBSKS...",
        "...KWSSSSSSSSSSSSSSSSSSSSSSKS...", "...KKKKKKKKKKKKKKKKKKKKKKKKKS...",
        ".....SSSSSSSSSSSSSSSSSSSSSSSS...", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(2, 2, 1)},
        {'R', rgb(7, 0, 0)},
        {'Y', rgb(1, 3, 3)},
        {'O', rgb(1, 5, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &settings_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KBBKBBBKBBKBBK.",
        ".KBSSSBBKBBKBBK.", ".KBSSSBBKBBKBBK.", ".KBSSSBBKBRRRBK.", ".KBBKBBBKBRRRBK.",
        ".KBBKBBBKBRRRBK.", ".KBBKBBYYYBKBBK.", ".KBBKBBYYYBKBBK.", ".KBBKBBYYYBKBBK.",
        ".KBBKBBBKBBKBBK.", ".KBBBBBBBBBBBBK.", ".KKKKKKKKKKKKKK.", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(7, 0, 0)},
        {'R', rgb(1, 3, 3)},
        {'Y', rgb(1, 5, 0)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &welcome_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "....................KKK.........",
        "....................KKK.........", "....................KKK.........",
        "...WW...............KKK....WW...", "...WBWWWW...........KKKWWWWBW...",
        ".WWWBBBBBWWWW......WKKKBBBBBWWW.", ".WSWBBBBBBBBBWWWWWWBKKKBBBBBWSW.",
        ".WSWBRRBBBBBBBBWWBBBKKKBBRRBWSW.", ".WSWBBBRRRRBBBBWWBBBKKKRRBBBWSW.",
        ".WSWBBBBBBBRRRBWWBRRKBKBBBBBWSW.", ".WSWBRRBBBBBBBBWWBBBBBBBBRRBWSW.",
        ".WSWBBBRRRRBBBBWWBBBBRRRRBBBWSW.", ".WSWBBBBBBBRRRBWWBRRRBBBBBBBWSW.",
        ".WSWBRRBBBBBBBBWWBBBBBBBBRRBWSW.", ".WSWBBBRRRRBBBBWWBBBBRRRRBBBWSW.",
        ".WSWBBBBBBBRRRBWWBRRRBBBBBBBWSW.", ".WSWBRRBBBBBBBBWWBBBBBBBBRRBWSW.",
        ".WSWBBBRRRRBBBBWWBBBBRRRRBBBWSW.", ".WSWBBBBBBBRRRBWWBRRRBBBBBBBWSW.",
        ".WSWWWBBBBBBBBBWWBBBBBBBBBWWWSW.", ".WSSSSWWWWBBBBBWWBBBBBWWWWSSSSW.",
        ".WSSSSSSSSWWWWBWWBWWWWSSSSSSSSW.", ".WWWWSSSSSSSSSWWWWSSSSSSSSSWWWW.",
        ".....WWWWWWWSSSWWSSSWWWWWWW.....", "............WWWWWWWW............",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(7, 0, 0)},
        {'W', rgb(0, 0, 0)},
        {'B', rgb(7, 7, 3)},
        {'S', rgb(1, 3, 3)},
        {'R', rgb(2, 2, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &welcome_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "................", "................", ".KKK........KKK.",
        "KKWWKKKKKKKKWWKK", "KKWWWWWKKWWWWWKK", "KKWBBBWKKWBBBWKK", "KKWWWWWKKWWWWWKK",
        "KKWBBBWKKWBBBWKK", "KKWWWWWKKWWWWWKK", "KKWWWWWKKWWWWWKK", "KKKKKWWKKWWKKKKK",
        "KSSSSKKKKKKSSSSK", "KKKKSSSKKSSSKKKK", "....KKKKKKKK....", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(2, 2, 1)},
        {'S', rgb(1, 3, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &gallery_icon_32()
{
    static constexpr const char *rows[32] = {
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
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(0, 0, 2)},
        {'B', rgb(1, 3, 3)},
        {'S', rgb(5, 5, 2)},
        {'R', rgb(2, 2, 1)},
        {'Y', rgb(7, 7, 3)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &gallery_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.", ".KWWWWWWWWWWWWK.",
        ".KBBBBBBBBBBBBK.", ".KBKKKKKBBBBBBK.", ".KBKSSKKBRRRRBK.", ".KBKKSKKBBBBBBK.",
        ".KBKSKSKBRRRRBK.", ".KBKKKKKBBBBBBK.", ".KBKKKKKKKKKKBK.", ".KBKSSSSSSSSKBK.",
        ".KBKBBBBBBBBKBK.", ".KBKKKKKKKKKKBK.", ".KKKKKKKKKKKKKK.", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(0, 0, 2)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(7, 7, 3)},
        {'R', rgb(2, 2, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &power_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", ".............KKKKKK.............",
        "..........KKKWWWWWWKKK..........", ".........KWWWBBBBBBWWWK.........",
        ".......KKWWBBWWWWWWBBWWKK.......", "......KWWBBWWWWSSWWWWWWWWK......",
        "......KWBWWWWWWSSWWWWWWWWK......", ".....KWBWWWWWWWSSWWWWWWWWWK.....",
        "....KWWBWWWWWWWSSWWWWWWWWWWK....", "....KWBWWWWSWWWSSWWSSWWWWWWK....",
        "....KWBWWWSSWWWSSWWWSSWWWWWK....", "...KWBWWWWSSWWWSSWWWSSWWWWWWK...",
        "...KWBWWWSSWWWWSSWWWWSSWWWWWK...", "...KWBWWWSSWWWWSSWWWWSSWWWWWK...",
        "...KWWWWWSSWWWWWWWWWWSSWWWRWK...", "...KWWWWWSSWWWWWWWWWWSSWWWRWK...",
        "...KWWWWWWSSWWWWWWWWSSWWWWRWK...", "....KWWWWWSSWWWWWWWWSSWWWRWK....",
        "....KWWWWWWSSSWWWWSSSWWWWRWK....", "....KWWWWWWWSSSSSSSSWWWWRWWK....",
        ".....KWWWWWWWWSSSSWWWWWWRWK.....", "......KWWWWWWWWWWWWWWWWRWK......",
        "......KWWWWWWWWWWWWWWRRWWK......", ".......KKWWWRWWWWWWRRWWKK.......",
        ".........KWWWRRRRRRWWWK.........", "..........KKKWWWWWWKKK..........",
        ".............KKKKKK.............", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 0, 0)},
        {'B', rgb(7, 4, 2)},
        {'S', rgb(7, 7, 3)},
        {'R', rgb(4, 0, 0)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &power_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "......KKKK......", "....KKWWWWKK....", "...KWWWBBWWWK...",
        "..KWWWWBBWWWWK..", "..KWWBWBBWBWWK..", ".KWWBWWBBWWBWWK.", ".KWWBWWBBWWBWWK.",
        ".KWWBWWWWWWBWWK.", ".KWWBWWWWWWBWWK.", "..KWWBWWWWBWWK..", "..KWWWBBBBWWWK..",
        "...KWWWWWWWWK...", "....KKWWWWKK....", "......KKKK......", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 0, 0)},
        {'B', rgb(7, 7, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &run_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "................................",
        "................................", "..KKKKKKKKKKKKKKKKKKKKKKKKKKK...",
        "..KWWWWWWWWWWWWBBBBBBBBBBBBBK...", "..KWWWWWWWWWWWWBBBBBBBBBSSSBKR..",
        "..KWWWWWWWWWWWWBBBBBBBBBSSSBKR..", "..KWWWWWWWWWWWWBBBBBBBBBBBBBKR..",
        "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..",
        "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..", "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..",
        "..KSKKYKKKKKKKKKKKKKKKKKKKKSKR..", "..KSKKKYKKKKKKKKKKKKKKKKKKKSKR..",
        "..KSKKKKYKKKKKKKKKKKKKKKKKKSKR..", "..KSKKKYKKKKKKKKKKKKKKKKKKKSKR..",
        "..KSKKYKKKKKKKKKKKKKKKKKKKKSKR..", "..KSKKKKKKKKKYYYYYYKKKKKKKKSKR..",
        "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..", "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..",
        "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..", "..KSKKKKKKKKKKKKKKKKKKKKKKKSKR..",
        "..KSSSSSSSSSSSSSSSSSSSSSSSSSKR..", "..KKKKKKKKKKKKKKKKKKKKKKKKKKKR..",
        "....RRRRRRRRRRRRRRRRRRRRRRRRRR..", "................................",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(0, 0, 2)},
        {'B', rgb(1, 3, 3)},
        {'S', rgb(5, 5, 2)},
        {'R', rgb(2, 2, 1)},
        {'Y', rgb(7, 7, 3)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &run_icon_16()
{
    static constexpr const char *rows[16] = {
        "................", "................", ".KKKKKKKKKKKKKK.", ".KWWWWWWWWWWWWK.",
        ".KWWWWWWWWWWWWK.", ".KBBBBBBBBBBBBK.", ".KKKKKKKKKKKKKK.", ".KKKSKKKKKKKKKK.",
        ".KKKKSKKKKKKKKK.", ".KKKSKKKKKKKKKK.", ".KKKKKKKSSSSKKK.", ".KKKKKKKKKKKKKK.",
        ".KKKKKKKKKKKKKK.", ".KKKKKKKKKKKKKK.", "................", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(0, 0, 2)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(7, 7, 3)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &browser_icon_16()
{
    static constexpr const char *rows[16] = {
        "....KKKKKKKK....", "..KKBBGGBBBBKK..", ".KBBGGGGBBBBBBK.", ".KBGGGGGGBBBBBK.",
        "KBBGGGGGBBBBGGBK", "KBBBGGGBBBBGGGBK", "KBBBBGBBBBBGGGBK", "KBBBBBBBBBBBGGBK",
        "KBBBBBBGGBBBBBBK", "KBBBBBGGGGBBBBBK", "KBBBBBGGGGGBBBBK", ".KBBBBBGGGBBBBK.",
        ".KBBBBBBGGBBBBK.", "..KKBBBBBBBBKK..", "....KKKKKKKK....", "................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'B', rgb(1, 3, 3)},
        {'G', rgb(1, 5, 1)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &service_icon_32()
{
    static constexpr const char *rows[32] = {
        "................................", ".............KKKKKK.............",
        ".............KWBBSK.............", "......KKK....KWBBSK....KKK......",
        ".....KWBBK..KKWBBSKK..KBBSK.....", "....KWBBBBKKWWBBBBSSKKBBBBSK....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....", ".....KBBBBBBBBBBBBBBBBBBBBK.....",
        "......KBBBBBBBKKKKKKBBBBBK......", ".....KWBBBBBKK......KKBBBSK.....",
        "....KWBBBBBK..........KBBBSK....", "....KWBBBBK............KBBSK....",
        ".KKKKWBBBK..............KBSKKKK.", ".KWWWBBBBK..............KBBSSSK.",
        ".KBBBBBBK................KBBBBK.", ".KBBBBBBK................KBBBBK.",
        ".KBBBBBBK................KBBBBK.", ".KSSSBBBBK..............KBBSSSK.",
        ".KKKKWBBBK..............KBSKKKK.", "....KWBBBBK............KBBSK....",
        "....KWBBBBBK..........KBBBSK....", ".....KWBBBBBKK......KKBBBSK.....",
        "......KBBBBBBBKKKKKKBBBBBK......", ".....KBBBBBBBBBBBBBBBBBBBBK.....",
        "....KBBBBBBBBBBBBBBBBBBBBBBK....", "....KSBBBBKKSSBBBBSSKKBBBBSK....",
        ".....KSBBK..KKWBBSKK..KBSSK.....", "......KKK....KWBBSK....KKK......",
        ".............KSSSSK.............", ".............KKKKKK.............",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(7, 7, 3)},
        {'B', rgb(5, 5, 2)},
        {'S', rgb(2, 2, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &service_icon_16()
{
    static constexpr const char *rows[16] = {
        "......KKKK......", "..KK..KWWK..KK..", ".KWWKKKWWKKKWWK.", ".KWWWWWWWWWWWWK.",
        "..KWWWKKKKWWWK..", "..KWWK....KWWK..", "KKKWK......KWKKK", "KWWWK......KWWWK",
        "KWWWK......KWWWK", "KKKWK......KWKKK", "..KWWK....KWWK..", "..KWWWKKKKWWWK..",
        ".KWWWWWWWWWWWWK.", ".KWWKKKWWKKKWWK.", "..KK..KWWK..KK..", "......KKKK......",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(5, 5, 2)},
    };
    static constexpr Pixels<16> pixels = paint<16>(rows, inks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &mascot_32()
{
    static constexpr const char *rows[32] = {
        "................................", "................................",
        "................................", "................................",
        "...KKKKK................KKKKK...", "..KWWWWWK..............KWWWWWK..",
        ".KWBBBBBWK............KWBBBBBWK.", ".KWBBBBBBWK..KKKKKK..KWBBBBBBWK.",
        ".KWBBBBBBWKKKSSSSSSKKKWBBBBBBWK.", ".KWBBBBBWKSSSSSSSSSSSSKWBBBBBWK.",
        ".KWWBBBWKSSSSSSSSSSSSSSKWBBBWWK.", "..KWWWWKSSSSSSSSSSSSSSSSKWWWWK..",
        "...KKKKSSSSSSSSSSSSSSSSSSKKKK...", "....KRSSSSSSSSSSSSSSSSSSSSRK....",
        "....KRSSSSSSSSSSSSSSSSSSSSRK....", "....KRSSKKKSSSSSSSSSSKKKSSRK....",
        "....KRSKKSKKSSSSSSSSKKSKKSRK....", "....KRSKKKKKSSSSSSSSKKKKKSRK....",
        "....KRSSKKKSSSSSSSSSSKKKSSRK....", "....KRSSSSSSSSSYYSSSSSSSSSRK....",
        "OO..KRSSSSSSSSYYYYSSSSSSSSRK..OO", "..OOKRRSSSSSSSSYYSSSSSSSSRRKOO..",
        "....OKRSSSSSSSSKKSSSSSSSSRKO....", "..OOOKRRSSSSSKKSSKKSSSSSRRKOOO..",
        "OO....KRRSSSSSSSSSSSSSSRRK....OO", ".......KRRRSSSSSSSSSSRRRK.......",
        "........KKRRRRRRRRRRRRKK........", "..........KKKKKKKKKKKK..........",
        "................................", "................................",
        "................................", "................................",
    };
    static constexpr Ink inks[] = {
        {'K', rgb(0, 0, 0)},
        {'W', rgb(5, 5, 2)},
        {'B', rgb(7, 4, 2)},
        {'S', rgb(7, 7, 3)},
        {'R', rgb(6, 6, 3)},
        {'Y', rgb(7, 3, 2)},
        {'O', rgb(2, 2, 1)},
    };
    static constexpr Pixels<32> pixels = paint<32>(rows, inks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

/* ---- Signal bars ------------------------------------------------------------- */

namespace {

constexpr const char *kPadlock[7] = {".KKKK.", ".K..K.", "KKKKKK", "KYYYYK", "KYKKYK", "KYYYYK", "KKKKKK"};
constexpr const char *kCross[7] = {"R...R.", "RR.RR.", ".RRR..", ".RRR..", "RR.RR.", "R...R.", "......"};

// Four bars 2 pixels wide, 3 to 12 high, standing on row 14; a filled bar
// is black, an empty one only a dark grey foot.
constexpr Pixels<16> signal_pixels(int bars, bool locked)
{
    Pixels<16> out;
    for (int i = 0; i < 256; i++) {
        out.p[i] = kClear;
    }
    for (int b = 0; b < 4; b++) {
        const int x = 5 + b * 3, top = 12 - b * 3;
        const bool filled = b < bars;
        for (int y = filled ? top : 13; y <= 14; y++) {
            out.p[y * 16 + x] = filled ? kBlack : kDarkGrey;
            out.p[y * 16 + x + 1] = filled ? kBlack : kDarkGrey;
        }
    }
    const char *const *mark = bars < 0 ? kCross : (locked ? kPadlock : nullptr);
    if (mark != nullptr) {
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 6; x++) {
                const char ch = mark[y][x];
                if (ch != '.') {
                    out.p[y * 16 + x] = ch == 'K' ? kBlack : ch == 'Y' ? rgb(7, 6, 1) : rgb(7, 0, 0);
                }
            }
        }
    }
    return out;
}

} // namespace

const Bitmap &signal_icon_16(int bars, bool locked)
{
    static constexpr Pixels<16> pixels[11] = {
        signal_pixels(-1, false), signal_pixels(0, false), signal_pixels(1, false), signal_pixels(2, false),
        signal_pixels(3, false),  signal_pixels(4, false), signal_pixels(0, true),  signal_pixels(1, true),
        signal_pixels(2, true),   signal_pixels(3, true),  signal_pixels(4, true),
    };
    static constexpr Bitmap bitmaps[11] = {
        {16, 16, pixels[0].p, kClear}, {16, 16, pixels[1].p, kClear}, {16, 16, pixels[2].p, kClear},
        {16, 16, pixels[3].p, kClear}, {16, 16, pixels[4].p, kClear}, {16, 16, pixels[5].p, kClear},
        {16, 16, pixels[6].p, kClear}, {16, 16, pixels[7].p, kClear}, {16, 16, pixels[8].p, kClear},
        {16, 16, pixels[9].p, kClear}, {16, 16, pixels[10].p, kClear},
    };
    if (bars < 0) {
        return bitmaps[0];
    }
    bars = bars > 4 ? 4 : bars;
    return bitmaps[(locked ? 6 : 1) + bars];
}

} // namespace gfx
