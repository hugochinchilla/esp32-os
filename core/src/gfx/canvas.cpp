#include "core/gfx/canvas.hpp"

#include <cstring>

namespace gfx {

namespace {

// Channel levels of the board: red and green 3 bits, blue 2 bits (blue's
// third resistor repeats its top bit, so its levels are 0, 2, 5, 7 of 7).
const uint8_t kLevels3[8] = {0, 36, 73, 109, 146, 182, 219, 255};
const uint8_t kLevelsB[4] = {0, 73, 182, 255};
const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// The level index for value `v` at threshold `t` (0..15): the nearest level
// below, or the one above when v is far enough past it. Same rule as
// tools/mockup/desktop.py: (v - lo) / (hi - lo) > (t + 0.5) / 16.
unsigned channel(unsigned v, const uint8_t *levels, unsigned count, unsigned t)
{
    unsigned lo = 0;
    while (lo + 1 < count && levels[lo + 1] <= v) {
        lo++;
    }
    if (levels[lo] == v || lo + 1 == count) {
        return lo;
    }
    unsigned span = levels[lo + 1] - levels[lo];
    return 32 * (v - levels[lo]) > (2 * t + 1) * span ? lo + 1 : lo;
}

Colour dither_t(Rgb c, unsigned t)
{
    return rgb(channel(c.r, kLevels3, 8, t), channel(c.g, kLevels3, 8, t),
               channel(c.b, kLevelsB, 4, t));
}

} // namespace

Colour dither(Rgb c, int x, int y)
{
    return dither_t(c, kBayer[y & 3][x & 3]);
}

Canvas::Canvas(uint8_t *pixels, int width, int height, const RectList &clip, const Rect &limit,
               int origin_x, int origin_y)
    : pixels_(pixels), width_(width), height_(height), clip_(clip),
      limit_(limit.intersect({0, 0, width, height})), ox_(origin_x), oy_(origin_y)
{
}

Canvas Canvas::sub(const Rect &r) const
{
    Rect screen = r.offset(ox_, oy_).intersect(limit_);
    return Canvas(pixels_, width_, height_, clip_, screen, ox_ + r.x, oy_ + r.y);
}

template <typename Fn> void Canvas::each(const Rect &r, Fn fn)
{
    const Rect s = r.offset(ox_, oy_).intersect(limit_);
    if (s.empty()) {
        return;
    }
    for (int i = 0; i < clip_.size(); i++) {
        const Rect part = clip_[i].intersect(s);
        if (!part.empty()) {
            fn(part);
        }
    }
}

void Canvas::put(int x, int y, Colour c)
{
    each({x, y, 1, 1}, [&](const Rect &s) { pixels_[s.y * width_ + s.x] = c; });
}

void Canvas::fill(const Rect &r, Colour c)
{
    each(r, [&](const Rect &s) {
        uint8_t *row = pixels_ + s.y * width_ + s.x;
        for (int y = 0; y < s.h; y++, row += width_) {
            memset(row, c, s.w);
        }
    });
}

void Canvas::hline(int x, int y, int w, Colour c)
{
    fill({x, y, w, 1}, c);
}

void Canvas::vline(int x, int y, int h, Colour c)
{
    fill({x, y, 1, h}, c);
}

void Canvas::fill_rgb(const Rect &r, Rgb c)
{
    // The dither repeats every 4 pixels both ways: work out the 16 colours
    // once, in screen position terms.
    Colour tile[4][4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            tile[y][x] = dither_t(c, kBayer[y][x]);
        }
    }
    each(r, [&](const Rect &s) {
        for (int y = s.y; y < s.bottom(); y++) {
            const Colour *t = tile[y & 3];
            uint8_t *row = pixels_ + y * width_;
            for (int x = s.x; x < s.right(); x++) {
                row[x] = t[x & 3];
            }
        }
    });
}

void Canvas::fill_fade(const Rect &r, Rgb from, Rgb to)
{
    const int span = r.w > 1 ? r.w - 1 : 1;
    const int sx0 = r.x + ox_; // screen x of the fade's first column
    each(r, [&](const Rect &s) {
        for (int x = s.x; x < s.right(); x++) {
            const int i = x - sx0;
            const Rgb c = {
                static_cast<uint8_t>(from.r + (to.r - from.r) * i / span),
                static_cast<uint8_t>(from.g + (to.g - from.g) * i / span),
                static_cast<uint8_t>(from.b + (to.b - from.b) * i / span),
            };
            Colour column[4];
            for (int k = 0; k < 4; k++) {
                column[k] = dither_t(c, kBayer[k][x & 3]);
            }
            uint8_t *p = pixels_ + s.y * width_ + x;
            for (int y = s.y; y < s.bottom(); y++, p += width_) {
                *p = column[y & 3];
            }
        }
    });
}

void Canvas::checker(const Rect &r)
{
    each(r, [&](const Rect &s) {
        for (int y = s.y; y < s.bottom(); y++) {
            uint8_t *row = pixels_ + y * width_;
            for (int x = s.x; x < s.right(); x++) {
                row[x] = ((x + y) & 1) ? kWhite : kGrey;
            }
        }
    });
}

void Canvas::edge(const Rect &r, Colour tl, Colour br)
{
    hline(r.x, r.y, r.w - 1, tl);
    vline(r.x, r.y, r.h - 1, tl);
    hline(r.x, r.bottom() - 1, r.w, br);
    vline(r.right() - 1, r.y, r.h, br);
}

void Canvas::raised(const Rect &r)
{
    edge(r, kGrey, kBlack);
    edge(r.inset(1), kWhite, kDarkGrey);
}

void Canvas::sunken(const Rect &r)
{
    edge(r, kDarkGrey, kWhite);
    edge(r.inset(1), kBlack, kGrey);
}

void Canvas::button(const Rect &r, ButtonLook look)
{
    if (look == ButtonLook::Raised) {
        fill(r.inset(2), kGrey);
        edge(r, kWhite, kBlack);
        edge(r.inset(1), kGrey, kDarkGrey);
        return;
    }
    edge(r, kBlack, kWhite);
    edge(r.inset(1), kDarkGrey, kGrey);
    if (look == ButtonLook::Checked) {
        checker(r.inset(2));
    } else {
        fill(r.inset(2), kGrey);
    }
}

void Canvas::blit(const Bitmap &b, int x, int y)
{
    each({x, y, b.width, b.height}, [&](const Rect &s) {
        const int bx = s.x - (x + ox_), by = s.y - (y + oy_);
        for (int row = 0; row < s.h; row++) {
            const uint8_t *src = b.pixels + (by + row) * b.width + bx;
            uint8_t *dst = pixels_ + (s.y + row) * width_ + s.x;
            for (int col = 0; col < s.w; col++) {
                if (src[col] != b.transparent) {
                    dst[col] = src[col];
                }
            }
        }
    });
}

void Canvas::blit2x(const Bitmap &b, int x, int y)
{
    each({x, y, b.width * 2, b.height * 2}, [&](const Rect &s) {
        const int bx = s.x - (x + ox_), by = s.y - (y + oy_);
        for (int row = 0; row < s.h; row++) {
            const uint8_t *src = b.pixels + ((by + row) / 2) * b.width;
            uint8_t *dst = pixels_ + (s.y + row) * width_ + s.x;
            for (int col = 0; col < s.w; col++) {
                uint8_t p = src[(bx + col) / 2];
                if (p != b.transparent) {
                    dst[col] = p;
                }
            }
        }
    });
}

void Canvas::pattern(int x, int y, const char *const *rows, int count, Colour c)
{
    for (int r = 0; r < count; r++) {
        for (int q = 0; rows[r][q] != 0; q++) {
            if (rows[r][q] == 'X') {
                put(x + q, y + r, c);
            }
        }
    }
}

int Canvas::text(int x, int y, const char *s, Colour c, bool bold)
{
    const Font &f = kSpleen6x12;
    const int advance = f.width + (bold ? 1 : 0);
    int len = 0;
    for (const char *p = s; next_latin1(p) != 0;) {
        len++;
    }
    const int glyph_w = f.width + (bold ? 1 : 0);
    // Only the characters that can show are drawn.
    each({x, y, len * advance, f.height}, [&](const Rect &part) {
        const char *p = s;
        int gx = x + ox_;
        const int gy = y + oy_;
        for (uint8_t code; (code = next_latin1(p)) != 0; gx += advance) {
            if (gx + glyph_w <= part.x) {
                continue;
            }
            if (gx >= part.right()) {
                break;
            }
            const uint8_t *g = f.glyph(code);
            for (int row = 0; row < f.height; row++) {
                const int py = gy + row;
                if (py < part.y || py >= part.bottom()) {
                    continue;
                }
                unsigned bits = g[row];
                if (bold) {
                    bits |= bits >> 1;
                }
                if (bits == 0) {
                    continue;
                }
                uint8_t *dst = pixels_ + py * width_;
                for (int col = 0; col < glyph_w; col++) {
                    const int px = gx + col;
                    if ((bits & (0x80u >> col)) && px >= part.x && px < part.right()) {
                        dst[px] = c;
                    }
                }
            }
        }
    });
    return x + len * advance;
}

int Canvas::text_width(const char *s, bool bold)
{
    int len = 0;
    while (next_latin1(s) != 0) {
        len++;
    }
    return len * (kSpleen6x12.width + (bold ? 1 : 0));
}

uint8_t next_latin1(const char *&text)
{
    const uint8_t b = static_cast<uint8_t>(*text);
    if (b == 0) {
        return 0;
    }
    text++;
    if (b < 0x80) {
        return b;
    }
    if ((b == 0xC2 || b == 0xC3) && (static_cast<uint8_t>(*text) & 0xC0) == 0x80) {
        return static_cast<uint8_t>(((b & 0x03) << 6) | (static_cast<uint8_t>(*text++) & 0x3F));
    }
    // Something outside Latin-1: skip its continuation bytes.
    while ((static_cast<uint8_t>(*text) & 0xC0) == 0x80) {
        text++;
    }
    return '?';
}

} // namespace gfx
