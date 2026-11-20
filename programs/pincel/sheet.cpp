// Pincel's sheet: what the tools do to the picture (sheet.hpp).
#include "sheet.hpp"

#include <cstring>

namespace pincel {

namespace {

void grow(gfx::Rect &changed, const gfx::Rect &r) { changed = gfx::bounding(changed, r); }

// A run of one row, cut to the sheet. x1 is one past the end.
void span(Sheet &s, int y, int x0, int x1, gfx::Colour c)
{
    if (y < 0 || y >= s.height) {
        return;
    }
    x0 = x0 < 0 ? 0 : x0;
    x1 = x1 > s.width ? s.width : x1;
    if (x0 < x1) {
        memset(s.pixels + y * s.width + x0, c, static_cast<size_t>(x1 - x0));
    }
}

void fill(Sheet &s, const gfx::Rect &r, gfx::Colour c)
{
    for (int y = r.y; y < r.bottom(); y++) {
        span(s, y, r.x, r.right(), c);
    }
}

gfx::Rect cut(const Sheet &s, const gfx::Rect &r) { return r.intersect({0, 0, s.width, s.height}); }

// How many pixels of row `dy` lie outside an ellipse that fills a box of
// w by h, counted from the box's left edge (the right side is the same).
// A pixel is inside when its middle is.
int outside(int w, int h, int dy)
{
    if (dy < 0 || dy >= h) {
        return w; // no such row: all of it
    }
    const int64_t Y = 2 * dy + 1 - h;
    if (Y >= -1 && Y <= 1) {
        return 0; // the middle row, or the middle two: the whole width, so that it reaches its box
    }
    const int64_t limit = static_cast<int64_t>(w) * w * h * h - Y * Y * w * w;
    int lo = 0, hi = (w - 1) / 2; // the middle column is always inside
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        const int64_t X = 2 * mid + 1 - w;
        if (X * X * h * h <= limit) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

void order(int &a, int &b)
{
    if (a > b) {
        const int t = a;
        a = b;
        b = t;
    }
}

} // namespace

void stamp(Sheet &sheet, int x, int y, Tip tip, gfx::Colour colour, gfx::Rect &changed)
{
    const int n = tip.size < 1 ? 1 : tip.size;
    const int left = x - n / 2, top = y - n / 2;
    if (tip.square || n < 3) {
        fill(sheet, {left, top, n, n}, colour);
    } else {
        for (int row = 0; row < n; row++) {
            const int out = outside(n, n, row);
            span(sheet, top + row, left + out, left + n - out, colour);
        }
    }
    grow(changed, cut(sheet, {left, top, n, n}));
}

void line(Sheet &sheet, int x0, int y0, int x1, int y1, Tip tip, gfx::Colour colour, gfx::Rect &changed)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx - dy;
    gfx::Rect touched;
    for (;;) {
        stamp(sheet, x0, y0, tip, colour, touched);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = 2 * error;
        if (twice > -dy) {
            error -= dy;
            x0 += sx;
        }
        if (twice < dx) {
            error += dx;
            y0 += sy;
        }
    }
    grow(changed, touched);
}

void box(Sheet &sheet, int x0, int y0, int x1, int y1, int edge, Style style, gfx::Colour ink, gfx::Colour paper,
         gfx::Rect &changed)
{
    order(x0, x1);
    order(y0, y1);
    const gfx::Rect r = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    const gfx::Rect inside = r.inset(edge);
    if (style == Style::Solid || inside.empty()) {
        fill(sheet, r, ink);
    } else {
        fill(sheet, {r.x, r.y, r.w, edge}, ink);
        fill(sheet, {r.x, r.bottom() - edge, r.w, edge}, ink);
        fill(sheet, {r.x, inside.y, edge, inside.h}, ink);
        fill(sheet, {r.right() - edge, inside.y, edge, inside.h}, ink);
        if (style == Style::Both) {
            fill(sheet, inside, paper);
        }
    }
    grow(changed, cut(sheet, r));
}

void ellipse(Sheet &sheet, int x0, int y0, int x1, int y1, int edge, Style style, gfx::Colour ink,
             gfx::Colour paper, gfx::Rect &changed)
{
    order(x0, x1);
    order(y0, y1);
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    // The inside: the ellipse of the box drawn in by the edge.
    const int iw = w - 2 * edge, ih = h - 2 * edge;
    for (int row = 0; row < h; row++) {
        const int y = y0 + row;
        if (y < 0 || y >= sheet.height) {
            continue;
        }
        const int out = outside(w, h, row);
        const int left = x0 + out, right = x0 + w - out; // one past
        if (style == Style::Solid) {
            span(sheet, y, left, right, ink);
            continue;
        }
        // The edge is what is not in the inside, and at least every pixel
        // with the outside above or below it, so that it has no gaps.
        int above = outside(w, h, row - 1), below = outside(w, h, row + 1);
        int run = (above > below ? above : below) - out; // pixels with the outside above or below
        run = run < 1 ? 1 : run;
        int hole_left = right, hole_right = right; // the inside's part of this row: none
        if (iw > 0 && ih > 0 && row >= edge && row < edge + ih) {
            const int inner = outside(iw, ih, row - edge);
            hole_left = x0 + edge + inner;
            hole_right = x0 + edge + iw - inner;
        }
        if (hole_left < left + run) {
            hole_left = left + run;
        }
        if (hole_right > right - run) {
            hole_right = right - run;
        }
        if (hole_left >= hole_right) {
            span(sheet, y, left, right, ink);
            continue;
        }
        span(sheet, y, left, hole_left, ink);
        span(sheet, y, hole_right, right, ink);
        if (style == Style::Both) {
            span(sheet, y, hole_left, hole_right, paper);
        }
    }
    grow(changed, cut(sheet, {x0, y0, w, h}));
}

void flood(Sheet &sheet, int x, int y, gfx::Colour colour, const uint8_t *before, Seed *seeds, int capacity,
           gfx::Rect &changed)
{
    if (x < 0 || y < 0 || x >= sheet.width || y >= sheet.height || capacity < 1) {
        return;
    }
    const int w = sheet.width, h = sheet.height;
    uint8_t *p = sheet.pixels;
    const gfx::Colour target = p[y * w + x];
    if (target == colour) {
        return;
    }
    int count = 0;
    bool lost = false; // a seed did not fit: the sheet is looked over afterwards
    int x_min = x, x_max = x, y_min = y, y_max = y;
    auto push = [&](int sx, int sy) {
        if (count < capacity) {
            seeds[count++] = {static_cast<int16_t>(sx), static_cast<int16_t>(sy)};
        } else {
            lost = true;
        }
    };
    // Fills the run of the target colour that (sx, sy) is in and leaves a
    // seed for each run touching it in the rows above and below.
    auto run = [&](int sx, int sy) {
        uint8_t *row = p + sy * w;
        if (row[sx] != target) {
            return;
        }
        int a = sx, b = sx + 1;
        while (a > 0 && row[a - 1] == target) {
            a--;
        }
        while (b < w && row[b] == target) {
            b++;
        }
        memset(row + a, colour, static_cast<size_t>(b - a));
        x_min = a < x_min ? a : x_min;
        x_max = b - 1 > x_max ? b - 1 : x_max;
        y_min = sy < y_min ? sy : y_min;
        y_max = sy > y_max ? sy : y_max;
        for (int side = -1; side <= 1; side += 2) {
            const int ny = sy + side;
            if (ny < 0 || ny >= h) {
                continue;
            }
            const uint8_t *next = p + ny * w;
            for (int i = a; i < b; i++) {
                if (next[i] == target && (i == a || next[i - 1] != target)) {
                    push(i, ny);
                }
            }
        }
    };
    push(x, y);
    for (;;) {
        while (count > 0) {
            const Seed s = seeds[--count];
            run(s.x, s.y);
        }
        if (!lost) {
            break;
        }
        // Seeds were dropped. What this flood has filled is told from the
        // copy: there it has the target colour, here the new one. Any pixel
        // still to fill that touches one of those starts it again.
        lost = false;
        for (int sy = 0; sy < h && count < capacity; sy++) {
            for (int sx = 0; sx < w && count < capacity; sx++) {
                const int i = sy * w + sx;
                if (p[i] != target) {
                    continue;
                }
                auto filled = [&](int j) { return p[j] == colour && before[j] == target; };
                if ((sx > 0 && filled(i - 1)) || (sx + 1 < w && filled(i + 1)) || (sy > 0 && filled(i - w)) ||
                    (sy + 1 < h && filled(i + w))) {
                    push(sx, sy);
                    lost = true; // look once more after these
                }
            }
        }
        if (count == 0) {
            break;
        }
    }
    grow(changed, {x_min, y_min, x_max - x_min + 1, y_max - y_min + 1});
}

void flip_horizontal(Sheet &sheet)
{
    for (int y = 0; y < sheet.height; y++) {
        uint8_t *row = sheet.pixels + y * sheet.width;
        for (int a = 0, b = sheet.width - 1; a < b; a++, b--) {
            const uint8_t t = row[a];
            row[a] = row[b];
            row[b] = t;
        }
    }
}

void flip_vertical(Sheet &sheet)
{
    for (int a = 0, b = sheet.height - 1; a < b; a++, b--) {
        uint8_t *top = sheet.pixels + a * sheet.width, *bottom = sheet.pixels + b * sheet.width;
        for (int x = 0; x < sheet.width; x++) {
            const uint8_t t = top[x];
            top[x] = bottom[x];
            bottom[x] = t;
        }
    }
}

void invert(Sheet &sheet)
{
    const size_t n = static_cast<size_t>(sheet.width) * sheet.height;
    for (size_t i = 0; i < n; i++) {
        sheet.pixels[i] = static_cast<uint8_t>(~sheet.pixels[i]);
    }
}

void bmp_header(int width, int height, uint8_t out[kBmpHeader])
{
    memset(out, 0, kBmpHeader);
    auto put32 = [&](int at, uint32_t v) {
        out[at] = static_cast<uint8_t>(v);
        out[at + 1] = static_cast<uint8_t>(v >> 8);
        out[at + 2] = static_cast<uint8_t>(v >> 16);
        out[at + 3] = static_cast<uint8_t>(v >> 24);
    };
    const uint32_t data = static_cast<uint32_t>(bmp_row(width)) * static_cast<uint32_t>(height);
    out[0] = 'B';
    out[1] = 'M';
    put32(2, static_cast<uint32_t>(kBmpHeader) + data);
    put32(10, kBmpHeader); // where the pixels start
    put32(14, 40);         // the size of the second header
    put32(18, static_cast<uint32_t>(width));
    put32(22, static_cast<uint32_t>(height)); // positive: the last row first
    out[26] = 1;                              // planes
    out[28] = 8;                              // bits a pixel
    put32(34, data);
    put32(38, 2835); // 72 dots an inch, both ways
    put32(42, 2835);
    put32(46, 256);
    for (int c = 0; c < 256; c++) {
        const gfx::Rgb rgb = gfx::shown(static_cast<gfx::Colour>(c));
        uint8_t *e = out + 54 + c * 4;
        e[0] = rgb.b;
        e[1] = rgb.g;
        e[2] = rgb.r;
    }
}

} // namespace pincel
