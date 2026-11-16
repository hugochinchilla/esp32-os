#include "core/video/pointer.hpp"

namespace video {

namespace {

// 'X' outline, '.' fill, ' ' see-through. Hotspot at the tip, top left.
const char *const kArrow[19] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X......XXXXX",
    "X...X..X    ",
    "X..X X..X   ",
    "X.X  X..X   ",
    "XX    X..X  ",
    "X     X..X  ",
    "       X..X ",
    "       XXX  ",
};

const char *const kWestEast[9] = {
    "    X     X    ",
    "   XX     XX   ",
    "  X.X     X.X  ",
    " X..XXXXXXX..X ",
    "X.............X",
    " X..XXXXXXX..X ",
    "  X.X     X.X  ",
    "   XX     XX   ",
    "    X     X    ",
};

const char *const kNorthSouth[15] = {
    "    X    ", "   X.X   ", "  X...X  ", " X.....X ", "XXXX.XXXX",
    "   X.X   ", "   X.X   ", "   X.X   ", "   X.X   ", "   X.X   ",
    "XXXX.XXXX", " X.....X ", "  X...X  ", "   X.X   ", "    X    ",
};

const char *const kNwSe[11] = {
    "XXXXXX     ",
    "X....X     ",
    "X...X      ",
    "X..X.X     ",
    "X.X X.X    ",
    "XX   X.X   ",
    "    X.X XX ",
    "     X.X.X ",
    "      X..X ",
    "     X...X ",
    "    XXXXXX ",
};

PointerShape make(const char *const *rows, int width, int height, int hot_x, int hot_y,
                  bool mirror = false)
{
    PointerShape shape = {};
    shape.width = static_cast<uint8_t>(width);
    shape.height = static_cast<uint8_t>(height);
    shape.hot_x = static_cast<uint8_t>(hot_x);
    shape.hot_y = static_cast<uint8_t>(hot_y);
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            char c = rows[row][mirror ? width - 1 - col : col];
            shape.pixels[row * width + col] =
                c == 'X' ? kPointerBlack : c == '.' ? kPointerWhite : kPointerClear;
        }
    }
    return shape;
}

} // namespace

const PointerShape &arrow_pointer()
{
    static const PointerShape shape = make(kArrow, 12, 19, 0, 0);
    return shape;
}

const PointerShape &resize_we_pointer()
{
    static const PointerShape shape = make(kWestEast, 15, 9, 7, 4);
    return shape;
}

const PointerShape &resize_ns_pointer()
{
    static const PointerShape shape = make(kNorthSouth, 9, 15, 4, 7);
    return shape;
}

const PointerShape &resize_nwse_pointer()
{
    static const PointerShape shape = make(kNwSe, 11, 11, 5, 5);
    return shape;
}

const PointerShape &resize_nesw_pointer()
{
    static const PointerShape shape = make(kNwSe, 11, 11, 5, 5, true);
    return shape;
}

} // namespace video
