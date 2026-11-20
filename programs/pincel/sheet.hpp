// Pincel's sheet: the picture being drawn, one board colour (RRRGGGBB) a
// pixel, and what the tools do to it. Nothing here asks the system for
// anything but a colour's red, green and blue (gfx::shown), so the PC's
// tests draw on a sheet of their own.
//
// Every call cuts what it draws to the sheet and adds the part it changed
// to `changed`, which the window then paints again.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"

namespace pincel {

struct Sheet {
    uint8_t *pixels = nullptr;
    int width = 0, height = 0;
};

constexpr int kMaxWidth = 800, kMaxHeight = 600;

// The tip a tool draws with: `size` pixels across, round or square (a size
// of 1 or 2 is the same either way).
struct Tip {
    int size = 1;
    bool square = false;
};

// How a rectangle or an ellipse is drawn.
enum class Style : uint8_t {
    Outline, // its edge in the ink
    Both,    // its edge in the ink, the inside in the paper
    Solid,   // all of it in the ink
};

// One touch of the tip, its middle at (x, y).
void stamp(Sheet &sheet, int x, int y, Tip tip, gfx::Colour colour, gfx::Rect &changed);
// The tip dragged from one point to the other.
void line(Sheet &sheet, int x0, int y0, int x1, int y1, Tip tip, gfx::Colour colour, gfx::Rect &changed);
// A rectangle or an ellipse between two corners, in any order; `edge` is
// how thick its outline is.
void box(Sheet &sheet, int x0, int y0, int x1, int y1, int edge, Style style, gfx::Colour ink, gfx::Colour paper,
         gfx::Rect &changed);
void ellipse(Sheet &sheet, int x0, int y0, int x1, int y1, int edge, Style style, gfx::Colour ink,
             gfx::Colour paper, gfx::Rect &changed);

// A place the flood still has to look at.
struct Seed {
    int16_t x, y;
};
// Fills the area of one colour that (x, y) is in, as far as it goes up,
// down, left and right. `before` is a copy of the sheet as it is now, which
// the caller has anyway (it is what Undo goes back to). `seeds` is working
// memory: more makes a tangled area faster, and any amount is enough.
void flood(Sheet &sheet, int x, int y, gfx::Colour colour, const uint8_t *before, Seed *seeds, int capacity,
           gfx::Rect &changed);

void flip_horizontal(Sheet &sheet);
void flip_vertical(Sheet &sheet);
void invert(Sheet &sheet);

// The sheet as a BMP file of 8 bits a pixel, whose 256 colours are the
// board's own, so the file holds exactly what was drawn. The file is the
// header, then the rows from the last to the first, each of bmp_row()
// bytes.
constexpr size_t kBmpHeader = 14 + 40 + 256 * 4;
void bmp_header(int width, int height, uint8_t out[kBmpHeader]);
constexpr int bmp_row(int width) { return (width + 3) & ~3; }

} // namespace pincel
