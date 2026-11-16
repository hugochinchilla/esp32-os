#include "core/gfx/art.hpp"

namespace gfx {

namespace {

constexpr int kClear = rgb(7, 0, 3); // magenta: never used in the pictures

struct Ink {
    char key;
    Colour colour;
};

// Builds a 16x16 bitmap from text rows; characters not in `inks` are clear.
template <int N> struct Picture16 {
    uint8_t pixels[256];
    Bitmap bitmap;

    Picture16(const char *const (&rows)[16], const Ink (&inks)[N])
        : bitmap{16, 16, pixels, kClear}
    {
        for (int r = 0; r < 16; r++) {
            for (int q = 0; q < 16; q++) {
                uint8_t p = kClear;
                for (const Ink &ink : inks) {
                    if (rows[r][q] == ink.key) {
                        p = ink.colour;
                    }
                }
                pixels[r * 16 + q] = p;
            }
        }
    }
};

const char *const kMascot[16] = {
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................",
};
const Ink kMascotInks[] = {{'G', kDarkGrey}, {'L', kWhite}, {'P', rgb(7, 4, 2)},
                           {'K', kBlack},    {'N', rgb(7, 3, 2)}, {'D', kDarkGrey}};

const char *const kFolder[16] = {
    "................", "................", "................", "................",
    "..KKKKKK........", ".KKKKKKKKKKKKKK.", ".KHHHHHHHHHHHHH.", ".KYYYYYYYYYYYYK.",
    ".KYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYK.",
    ".KYYYYYYYYYYYYK.", ".KKKKKKKKKKKKKK.", "................", "................",
};
const Ink kFolderInks[] = {{'K', kBlack}, {'H', rgb(7, 7, 2)}, {'Y', rgb(7, 6, 1)}};

const char *const kText[16] = {
    "................", "................", "....KKKKKKKKK...", "....KWWWWWWWK...",
    "....KBBBBBBBK...", "....KWWWWWWWK...", "....KBBBBBBBK...", "....KWWWWWWWK...",
    "....KWWWWWWWK...", "....KBBBBBBBK...", "....KWWWWWWWK...", "....KWWWWWWWK...",
    "....KBBBBBBBK...", "....KWWWWWWWK...", "....KKKKKKKKK...", "................",
};
const Ink kTextInks[] = {{'K', kBlack}, {'W', kWhite}, {'B', rgb(0, 0, 2)}};

const char *const kNetwork[16] = {
    "................", "................", "................", "..KKKKKKKK......",
    "..KSSSSSSK......", "..KSSSSSSK......", "..KSSSSSSK......", "..KSSSSKKKKKKKK.",
    "..KKKKKKSSSSSSK.", "....KKKKSSSSSSK.", "....KKKKSSSSSSK.", ".......KSSSSSSK.",
    ".......KKKKKKKK.", ".........KKKK...", ".........KKKK...", "................",
};
const Ink kNetworkInks[] = {{'K', kBlack}, {'S', rgb(1, 5, 3)}};

const char *const kSettings[16] = {
    "................", "..KKKKKKKKKKKK..", "..KWWWWWWWWWWK..", "..KWKWWWKWWWWK..",
    "..KWKWWWKWWKWK..", "..KRRRWWKWWKWK..", "..KWKWWRRRWKWK..", "..KWKWWWKWWKWK..",
    "..KWKWWWKWRRRK..", "..KWKWWWKWWKWK..", "..KWWWWWWWWWWK..", "..KKKKKKKKKKKK..",
    "................", "................", "................", "................",
};
const Ink kSettingsInks[] = {{'K', kBlack}, {'W', kGrey}, {'R', rgb(7, 0, 0)}};

const char *const kBoard32[32] = {
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
const char *const kFolder32[32] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "................................", "...KKKKKKKKKKKK.................",
    "...KTTTTTTTTTTK.................", "...KTTTTTTTTTTK.................",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", "..KHHHHHHHHHHHHHHHHHHHHHHHHHHK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..", "..KYYYYYYYYYYYYYYYYYYYYYYYYYYK..",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..", "................................",
    "................................", "................................",
    "................................", "................................",
};
const char *const kNet32[32] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "................................",
    "....KKKKKKKKKKKKKKK.............", "....KSSSSSSSSSSSSSK.............",
    "....KSSSSSSSSSSSSSK.............", "....KSSSSSSSSSSSSSK.............",
    "....KSSSSSSSSSSSSSK.............", "....KSSSSSSSSSSSSSK.............",
    "....KSSSSSSSSSSSSSK.............", "....KSSSSSSSSSSSSSK.............",
    "....KSSSSSSSSSKKKKKKKKKKKKKKK...", "....KSSSSSSSSSKSSSSSSSSSSSSSK...",
    "....KSSSSSSSSSKSSSSSSSSSSSSSK...", "....KKKKKKKKKKKSSSSSSSSSSSSSK...",
    "........KKKKKKKSSSSSSSSSSSSSK...", "........KGGGGGKSSSSSSSSSSSSSK...",
    "........KKKKKKKSSSSSSSSSSSSSK...", "..............KSSSSSSSSSSSSSK...",
    "..............KSSSSSSSSSSSSSK...", "..............KSSSSSSSSSSSSSK...",
    "..............KSSSSSSSSSSSSSK...", "..............KKKKKKKKKKKKKKK...",
    "..................KKKKKKK.......", "..................KGGGGGK.......",
    "..................KKKKKKK.......", "................................",
    "................................", "................................",
};
const char *const kBin32[32] = {
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

const Ink kBoardInks[] = {{'K', kBlack}, {'P', rgb(0, 3, 1)}, {'G', kGrey}, {'O', rgb(6, 5, 0)}};
const Ink kFolderInks32[] = {{'K', kBlack}, {'T', rgb(6, 5, 0)}, {'Y', rgb(7, 6, 1)}, {'H', rgb(7, 7, 2)}};
const Ink kNetInks32[] = {{'K', kBlack}, {'S', rgb(1, 5, 3)}, {'G', kGrey}};
const Ink kBinInks[] = {{'K', kBlack}, {'G', kGrey}, {'D', kDarkGrey}};

const char *const kText32[32] = {
    "................................", "................................",
    "................................", ".......KKKKKKKKKKKKKKKKKKK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWBBBBBBBBBBBBBWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWBBBBBBBBBBBBBWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWBBBBBBBBBBBBBWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWBBBBBBBBBBBBBWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWBBBBBBBBBBBBBWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWBBBBBBBBBBBBBWWK......",
    ".......KWWWWWWWWWWWWWWWWWK......", ".......KWWWWWWWWWWWWWWWWWK......",
    ".......KKKKKKKKKKKKKKKKKKK......", "................................",
    "................................", "................................",
};
const char *const kPicture32[32] = {
    "................................", "................................",
    "................................", "................................",
    "................................", "....KKKKKKKKKKKKKKKKKKKKKKKKK...",
    "....KWWWWWWWWWWWWWWWWWWWWWWWK...", "....KWSSSSSSSSSSSSSSSSSSSSSWK...",
    "....KWSSSSSSSSSSSSSSSSSSSSSWK...", "....KWSSSSSSSSSSSSSSYYYSSSSWK...",
    "....KWSSSSSSSSSSSSSYYYYYSSSWK...", "....KWSSSSSSSSSSSSSYYYYYSSSWK...",
    "....KWSSSSSSSSSSSSSYYYYYSSSWK...", "....KWSSSSSSSSSSSSSSYYYSSSSWK...",
    "....KWSSSSSSSGSSSSSSSSSSSSSWK...", "....KWSSSSSSSGSSSSSSSSSSSSSWK...",
    "....KWSSSSSSGGGSSSSSSSSSSSSWK...", "....KWSSSSSGGGGGSSSSSSSSSSSWK...",
    "....KWSSSSSGGGGGSSSSSSSSSSSWK...", "....KWSSSSGGGGGGGSSSSSSSSSSWK...",
    "....KWSSSGGGGGGGGGSSSSSSSSSWK...", "....KWSSSGGGGGGGGGSSSSSSSSSWK...",
    "....KWSSGGGGGGGGGGGSSSSSSSSWK...", "....KWSGGGGGGGGGGGGGSSSSSSSWK...",
    "....KWGGGGGGGGGGGGGGGSSSSSSWK...", "....KWWWWWWWWWWWWWWWWWWWWWWWK...",
    "....KKKKKKKKKKKKKKKKKKKKKKKKK...", "................................",
    "................................", "................................",
    "................................", "................................",
};
const char *const kDrive32[32] = {
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
const char *const kFile32[32] = {
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
const char *const kDrive16[16] = {
    "................", "................",
    "................", "................",
    "................", ".KKKKKKKKKKKKKK.",
    ".KWWWWWWWWWWWWK.", ".KLLLLLLLLLLLLK.",
    ".KLDDDDDDLLLLLK.", ".KLLLLLLLLLNNLK.",
    ".KLLLLLLLLLLLLK.", ".KKKKKKKKKKKKKK.",
    "................", "................",
    "................", "................",
};

const Ink kTextInks32[] = {{'K', kBlack}, {'W', kWhite}, {'B', rgb(0, 0, 2)}};
const Ink kPictureInks[] = {{'K', kBlack}, {'W', kWhite}, {'S', rgb(3, 5, 3)}, {'G', rgb(0, 4, 0)}, {'Y', rgb(7, 6, 0)}};
const Ink kDriveInks[] = {{'K', kBlack}, {'L', kGrey}, {'W', kWhite}, {'D', kDarkGrey}, {'N', rgb(0, 7, 0)}};
const Ink kFileInks[] = {{'K', kBlack}, {'W', kWhite}};

template <int N> struct Picture32 {
    uint8_t pixels[1024];
    Bitmap bitmap;

    Picture32(const char *const (&rows)[32], const Ink (&inks)[N]) : bitmap{32, 32, pixels, kClear}
    {
        for (int r = 0; r < 32; r++) {
            for (int q = 0; q < 32; q++) {
                uint8_t p = kClear;
                for (const Ink &ink : inks) {
                    if (rows[r][q] == ink.key) {
                        p = ink.colour;
                    }
                }
                pixels[r * 32 + q] = p;
            }
        }
    }
};

} // namespace

const Bitmap &settings_icon_16()
{
    static const Picture16<3> picture(kSettings, kSettingsInks);
    return picture.bitmap;
}

const Bitmap &drive_icon_16()
{
    static const Picture16<5> picture(kDrive16, kDriveInks);
    return picture.bitmap;
}

const Bitmap &text_icon_32()
{
    static const Picture32<3> picture(kText32, kTextInks32);
    return picture.bitmap;
}

const Bitmap &picture_icon_32()
{
    static const Picture32<5> picture(kPicture32, kPictureInks);
    return picture.bitmap;
}

const Bitmap &drive_icon_32()
{
    static const Picture32<5> picture(kDrive32, kDriveInks);
    return picture.bitmap;
}

const Bitmap &file_icon_32()
{
    static const Picture32<2> picture(kFile32, kFileInks);
    return picture.bitmap;
}

const Bitmap &board_icon_32()
{
    static const Picture32<4> picture(kBoard32, kBoardInks);
    return picture.bitmap;
}

const Bitmap &folder_icon_32()
{
    static const Picture32<4> picture(kFolder32, kFolderInks32);
    return picture.bitmap;
}

const Bitmap &network_icon_32()
{
    static const Picture32<3> picture(kNet32, kNetInks32);
    return picture.bitmap;
}

const Bitmap &bin_icon_32()
{
    static const Picture32<3> picture(kBin32, kBinInks);
    return picture.bitmap;
}

const Bitmap &mascot_16()
{
    static const Picture16<6> picture(kMascot, kMascotInks);
    return picture.bitmap;
}

const Bitmap &folder_icon_16()
{
    static const Picture16<3> picture(kFolder, kFolderInks);
    return picture.bitmap;
}

const Bitmap &text_icon_16()
{
    static const Picture16<3> picture(kText, kTextInks);
    return picture.bitmap;
}

const Bitmap &network_icon_16()
{
    static const Picture16<2> picture(kNetwork, kNetworkInks);
    return picture.bitmap;
}

} // namespace gfx
