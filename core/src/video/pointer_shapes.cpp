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

PointerShape make_arrow()
{
    PointerShape shape = {};
    shape.width = 12;
    shape.height = 19;
    for (int row = 0; row < shape.height; row++) {
        for (int col = 0; col < shape.width; col++) {
            char c = kArrow[row][col];
            shape.pixels[row * shape.width + col] =
                c == 'X' ? kPointerBlack : c == '.' ? kPointerWhite : kPointerClear;
        }
    }
    return shape;
}

} // namespace

const PointerShape &arrow_pointer()
{
    static const PointerShape arrow = make_arrow();
    return arrow;
}

} // namespace video
