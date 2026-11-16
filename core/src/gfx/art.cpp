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

constexpr const char *kFolder[16] = {
    "................", "................", "................", "................",
    "..KKKKKK........", ".KKKKKKKKKKKKKK.", ".KHHHHHHHHHHHHH.", ".KYYYYYYYYYYYYK.",
    ".KYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYK.", ".KYYYYYYYYYYYYK.",
    ".KYYYYYYYYYYYYK.", ".KKKKKKKKKKKKKK.", "................", "................",
};
constexpr Ink kFolderInks[] = {{'K', kBlack}, {'H', rgb(7, 7, 2)}, {'Y', rgb(7, 6, 1)}};

constexpr const char *kSnake[16] = {
    "................", "................", "..........KKKK..", ".........KGGWKK.", ".........KGGGGKR",
    ".........KLLKK..", "..KKKKKKKKGGK...", ".KGGLLGGLLGGK...", ".KLLKKKKKKKK....", ".KGGK...........",
    ".KLLKKKKKKKKK...", ".KGGLLGGLLGGLK..", "..KKKKKKKKKKLK..", "..........KKK...", "................",
    "................",
};
constexpr Ink kSnakeInks[] = {{'K', rgb(0, 2, 0)}, {'G', rgb(1, 4, 0)}, {'L', rgb(3, 6, 1)},
                              {'W', kWhite},       {'R', rgb(7, 0, 0)}};

constexpr const char *kText[16] = {
    "................", "................", "....KKKKKKKKK...", "....KWWWWWWWK...",
    "....KBBBBBBBK...", "....KWWWWWWWK...", "....KBBBBBBBK...", "....KWWWWWWWK...",
    "....KWWWWWWWK...", "....KBBBBBBBK...", "....KWWWWWWWK...", "....KWWWWWWWK...",
    "....KBBBBBBBK...", "....KWWWWWWWK...", "....KKKKKKKKK...", "................",
};
constexpr Ink kTextInks[] = {{'K', kBlack}, {'W', kWhite}, {'B', rgb(0, 0, 2)}};

constexpr const char *kNetwork[16] = {
    "................", "................", "................", "..KKKKKKKK......",
    "..KSSSSSSK......", "..KSSSSSSK......", "..KSSSSSSK......", "..KSSSSKKKKKKKK.",
    "..KKKKKKSSSSSSK.", "....KKKKSSSSSSK.", "....KKKKSSSSSSK.", ".......KSSSSSSK.",
    ".......KKKKKKKK.", ".........KKKK...", ".........KKKK...", "................",
};
constexpr Ink kNetworkInks[] = {{'K', kBlack}, {'S', rgb(1, 5, 3)}};

constexpr const char *kSettings[16] = {
    "................", "..KKKKKKKKKKKK..", "..KWWWWWWWWWWK..", "..KWKWWWKWWWWK..",
    "..KWKWWWKWWKWK..", "..KRRRWWKWWKWK..", "..KWKWWRRRWKWK..", "..KWKWWWKWWKWK..",
    "..KWKWWWKWRRRK..", "..KWKWWWKWWKWK..", "..KWWWWWWWWWWK..", "..KKKKKKKKKKKK..",
    "................", "................", "................", "................",
};
constexpr Ink kSettingsInks[] = {{'K', kBlack}, {'W', kGrey}, {'R', rgb(7, 0, 0)}};

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
constexpr const char *kFolder32[32] = {
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
constexpr const char *kNet32[32] = {
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

constexpr Ink kBoardInks[] = {{'K', kBlack}, {'P', rgb(0, 3, 1)}, {'G', kGrey}, {'O', rgb(6, 5, 0)}};
constexpr Ink kFolderInks32[] = {{'K', kBlack}, {'T', rgb(6, 5, 0)}, {'Y', rgb(7, 6, 1)}, {'H', rgb(7, 7, 2)}};
constexpr Ink kNetInks32[] = {{'K', kBlack}, {'S', rgb(1, 5, 3)}, {'G', kGrey}};
constexpr Ink kBinInks[] = {{'K', kBlack}, {'G', kGrey}, {'D', kDarkGrey}, {'W', kWhite}};

constexpr const char *kText32[32] = {
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
constexpr const char *kPicture32[32] = {
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

constexpr Ink kTextInks32[] = {{'K', kBlack}, {'W', kWhite}, {'B', rgb(0, 0, 2)}};
constexpr Ink kPictureInks[] = {{'K', kBlack}, {'W', kWhite}, {'S', rgb(3, 5, 3)}, {'G', rgb(0, 4, 0)}, {'Y', rgb(7, 6, 0)}};
constexpr Ink kDriveInks[] = {{'K', kBlack}, {'L', kGrey}, {'W', kWhite}, {'D', kDarkGrey}, {'N', rgb(0, 7, 0)}};
constexpr Ink kFileInks[] = {{'K', kBlack}, {'W', kWhite}};



} // namespace

const Bitmap &settings_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kSettings, kSettingsInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

// A 16x16 picture from a 32x32 one: every other pixel.
constexpr Pixels<16> half(const Pixels<32> &big)
{
    Pixels<16> out;
    for (int r = 0; r < 16; r++) {
        for (int q = 0; q < 16; q++) {
            out.p[r * 16 + q] = big.p[(r * 2 + 1) * 32 + q * 2 + 1];
        }
    }
    return out;
}

const Bitmap &picture_icon_16()
{
    static constexpr Pixels<32> big = paint<32>(kPicture32, kPictureInks);
    static constexpr Pixels<16> pixels = half(big);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &drive_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kDrive16, kDriveInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &text_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kText32, kTextInks32);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &picture_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kPicture32, kPictureInks);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
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

const Bitmap &folder_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kFolder32, kFolderInks32);
    static constexpr Bitmap bitmap = {32, 32, pixels.p, kClear};
    return bitmap;
}

const Bitmap &network_icon_32()
{
    static constexpr Pixels<32> pixels = paint<32>(kNet32, kNetInks32);
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
    static constexpr Pixels<32> big = paint<32>(kBin32, kBinInks);
    static constexpr Pixels<16> pixels = half(big);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &bin_full_icon_16()
{
    static constexpr Pixels<32> big = paint<32>(kBinFull32, kBinInks);
    static constexpr Pixels<16> pixels = half(big);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &mascot_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kMascot, kMascotInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &snake_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kSnake, kSnakeInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &folder_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kFolder, kFolderInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &text_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kText, kTextInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
    return bitmap;
}

const Bitmap &network_icon_16()
{
    static constexpr Pixels<16> pixels = paint<16>(kNetwork, kNetworkInks);
    static constexpr Bitmap bitmap = {16, 16, pixels.p, kClear};
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
