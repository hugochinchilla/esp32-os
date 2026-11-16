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

} // namespace

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
