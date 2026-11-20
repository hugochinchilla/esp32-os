#include "core/gfx/canvas.hpp"

#include <cstring>

namespace gfx {

namespace {

// Channel levels of the board: red and green 3 bits, blue 2 bits (blue's
// third resistor repeats its top bit, so its levels are 0, 2, 5, 7 of 7).
const uint8_t kLevels3[8] = {0, 36, 73, 109, 146, 182, 219, 255};
const uint8_t kLevelsB[4] = {0, 73, 182, 255};
const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// The level index for value `v` at threshold `t` (0..n-1): the nearest level
// below, or the one above when v is far enough past it. Same rule as
// tools/mockup/desktop.py: (v - lo) / (hi - lo) > (t + 0.5) / n.
unsigned channel(unsigned v, const uint8_t *levels, unsigned count, unsigned t, unsigned n = 16)
{
    unsigned lo = 0;
    while (lo + 1 < count && levels[lo + 1] <= v) {
        lo++;
    }
    if (levels[lo] == v || lo + 1 == count) {
        return lo;
    }
    unsigned span = levels[lo + 1] - levels[lo];
    return 2 * n * (v - levels[lo]) > (2 * t + 1) * span ? lo + 1 : lo;
}

// channel() for the 16 thresholds of the 4x4 dither, as two tables worked
// out the first time a colour is dithered (8 KB): the level of every value
// at every threshold, already in its place in the byte for red and blue.
struct Levels {
    uint8_t three[16][256]; // red and green: 0..7
    uint8_t blue[16][256];  // 0..3
    bool ready;
};
Levels levels;

const Levels &dither_levels()
{
    if (!levels.ready) {
        for (unsigned t = 0; t < 16; t++) {
            for (unsigned v = 0; v < 256; v++) {
                levels.three[t][v] = static_cast<uint8_t>(channel(v, kLevels3, 8, t));
                levels.blue[t][v] = static_cast<uint8_t>(channel(v, kLevelsB, 4, t));
            }
        }
        levels.ready = true;
    }
    return levels;
}

Colour dither_t(Rgb c, unsigned t)
{
    const Levels &l = dither_levels();
    return rgb(l.three[t][c.r], l.three[t][c.g], l.blue[t][c.b]);
}

} // namespace

uint8_t *Canvas::write_counts_ = nullptr;
uint8_t Canvas::band_[kBandRows * kMaxWidth];

void Canvas::copy_band(const Rect &band)
{
    for (int i = 0; i < clip_.size(); i++) {
        const Rect part = clip_[i].intersect(band);
        for (int y = part.y; y < part.bottom(); y++) {
            memcpy(pixels_ + y * width_ + part.x, band_ + (y - band.y) * width_ + part.x, part.w);
            mark(part.x, y, part.w);
        }
    }
}

namespace {

unsigned nearest_level(unsigned v, const uint8_t *levels, unsigned count)
{
    unsigned best = 0;
    for (unsigned i = 1; i < count; i++) {
        const int d = static_cast<int>(v) - levels[i], b = static_cast<int>(v) - levels[best];
        if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) {
            best = i;
        }
    }
    return best;
}

} // namespace

Colour nearest(Rgb c, Rgb *shown_as)
{
    const unsigned r = nearest_level(c.r, kLevels3, 8), g = nearest_level(c.g, kLevels3, 8),
                   b = nearest_level(c.b, kLevelsB, 4);
    if (shown_as != nullptr) {
        *shown_as = {kLevels3[r], kLevels3[g], kLevelsB[b]};
    }
    return rgb(r, g, b);
}

Rgb shown(Colour c)
{
    return {kLevels3[c >> 5], kLevels3[(c >> 2) & 7], kLevelsB[c & 3]};
}

Colour threshold(Rgb c, unsigned t, unsigned n)
{
    return rgb(channel(c.r, kLevels3, 8, t, n), channel(c.g, kLevels3, 8, t, n),
               channel(c.b, kLevelsB, 4, t, n));
}

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
    Canvas c(pixels_, width_, height_, clip_, screen, ox_ + r.x, oy_ + r.y);
    c.counting_ = counting_;
    return c;
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
    each({x, y, 1, 1}, [&](const Rect &s) {
        pixels_[s.y * width_ + s.x] = c;
        mark(s.x, s.y, 1);
    });
}

void Canvas::fill(const Rect &r, Colour c)
{
    each(r, [&](const Rect &s) {
        uint8_t *row = pixels_ + s.y * width_ + s.x;
        for (int y = 0; y < s.h; y++, row += width_) {
            memset(row, c, s.w);
            mark(s.x, s.y + y, s.w);
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
            mark(s.x, y, s.w);
        }
    });
}

void Canvas::fill_pattern(const Rect &r, Rgb paper, Rgb ink, const uint8_t rows[8])
{
    // 8x8 holds both the pattern and two turns of the 4x4 dither.
    Colour tile[8][8];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            tile[y][x] = dither_t((rows[y] & (0x80 >> x)) != 0 ? ink : paper, kBayer[y & 3][x & 3]);
        }
    }
    each(r, [&](const Rect &s) {
        for (int y = s.y; y < s.bottom(); y++) {
            const Colour *t = tile[y & 7];
            uint8_t *row = pixels_ + y * width_;
            for (int x = s.x; x < s.right(); x++) {
                row[x] = t[x & 7];
            }
            mark(s.x, y, s.w);
        }
    });
}

void Canvas::fill_fade(const Rect &r, Rgb from, Rgb to, bool vertical, const Rect *span_rect)
{
    // The fade runs across `span_rect` (r itself by default): a part of a
    // title bar is filled with its share of the whole fade.
    const Rect &whole = span_rect != nullptr ? *span_rect : r;
    if (vertical) {
        const int vspan = whole.h > 1 ? whole.h - 1 : 1;
        const int sy0 = whole.y + oy_;
        each(r, [&](const Rect &s) {
            for (int y = s.y; y < s.bottom(); y++) {
                const int i = y - sy0;
                const Rgb c = {
                    static_cast<uint8_t>(from.r + (to.r - from.r) * i / vspan),
                    static_cast<uint8_t>(from.g + (to.g - from.g) * i / vspan),
                    static_cast<uint8_t>(from.b + (to.b - from.b) * i / vspan),
                };
                Colour row4[4];
                for (int k = 0; k < 4; k++) {
                    row4[k] = dither_t(c, kBayer[y & 3][k]);
                }
                uint8_t *p = pixels_ + y * width_;
                for (int x = s.x; x < s.right(); x++) {
                    p[x] = row4[x & 3];
                }
                mark(s.x, y, s.w);
            }
        });
        return;
    }
    // Left to right. The dither repeats every four lines, so the whole fade
    // is four rows of pixels: they are worked out once and copied, row by
    // row, and kept for the next call that asks for the same fade (a title
    // bar is painted in two bands, and again whenever its window is).
    const int span = whole.w > 1 ? whole.w - 1 : 1;
    const int sx0 = whole.x + ox_; // screen x of the fade's first column
    const FadeRows &rows = fade_rows(from, to, span, sx0);
    each(r, [&](const Rect &s) {
        for (int y = s.y; y < s.bottom(); y++) {
            memcpy(pixels_ + y * width_ + s.x, rows.row[y & 3] + (s.x - rows.x), static_cast<size_t>(s.w));
            mark(s.x, y, s.w);
        }
    });
}

const Canvas::FadeRows &Canvas::fade_rows(Rgb from, Rgb to, int span, int sx0)
{
    static FadeRows kept; // ponytail: one fade kept, the last one asked for
    FadeRows &f = kept;
    if (f.valid && f.sx0 == sx0 && f.span == span && f.from.r == from.r && f.from.g == from.g &&
        f.from.b == from.b && f.to.r == to.r && f.to.g == to.g && f.to.b == to.b) {
        return f;
    }
    f.valid = true;
    f.sx0 = sx0;
    f.span = span;
    f.from = from;
    f.to = to;
    f.x = 0;
    Rgb back[4] = {}; // the colours of the four columns before this one
    for (int x = 0; x < kFadeWidth; x++) {
        const int i = x - sx0;
        const Rgb c = {
            static_cast<uint8_t>(from.r + (to.r - from.r) * i / span),
            static_cast<uint8_t>(from.g + (to.g - from.g) * i / span),
            static_cast<uint8_t>(from.b + (to.b - from.b) * i / span),
        };
        // The colour changes every few columns, and four columns back the
        // dither is at the same place: the same colour there gives the
        // same four pixels.
        Rgb &was = back[x & 3];
        if (x >= 4 && c.r == was.r && c.g == was.g && c.b == was.b) {
            for (int k = 0; k < 4; k++) {
                f.row[k][x] = f.row[k][x - 4];
            }
        } else {
            for (int k = 0; k < 4; k++) {
                f.row[k][x] = dither_t(c, kBayer[k][x & 3]);
            }
        }
        was = c;
    }
    return f;
}

void Canvas::focus_rect(const Rect &r)
{
    const Colour c = ink();
    for (int x = r.x; x < r.right(); x += 2) {
        put(x, r.y, c);
        put(x + ((r.h - 1) & 1), r.bottom() - 1, c);
    }
    for (int y = r.y + 2; y < r.bottom() - 1; y += 2) {
        put(r.x, y, c);
        put(r.right() - 1, y - ((r.w - 1) & 1), c);
    }
}

void Canvas::checker(const Rect &r)
{
    if (theme().flat()) {
        fill(r, theme().track);
        return;
    }
    each(r, [&](const Rect &s) {
        for (int y = s.y; y < s.bottom(); y++) {
            uint8_t *row = pixels_ + y * width_;
            for (int x = s.x; x < s.right(); x++) {
                row[x] = ((x + y) & 1) ? kWhite : kGrey;
            }
            mark(s.x, y, s.w);
        }
    });
}

void Canvas::edge(const Rect &r, Colour tl, Colour br)
{
    // Four lines that never share a pixel.
    hline(r.x, r.y, r.w - 1, tl);
    vline(r.x, r.y + 1, r.h - 2, tl);
    hline(r.x, r.bottom() - 1, r.w, br);
    vline(r.right() - 1, r.y, r.h - 1, br);
}

// In a flat theme every edge is the outline with a ring of `inside` in it.
// Two pixels, as the bevels are, so nothing that is laid out moves.

void Canvas::raised(const Rect &r)
{
    const Theme &t = theme();
    if (t.flat()) {
        edge(r, t.outline, t.outline);
        edge(r.inset(1), t.face, t.face);
        return;
    }
    edge(r, kGrey, kBlack);
    edge(r.inset(1), kWhite, kDarkGrey);
}

void Canvas::sunken(const Rect &r)
{
    const Theme &t = theme();
    if (t.flat()) {
        edge(r, t.outline, t.outline);
        edge(r.inset(1), t.paper, t.paper);
        return;
    }
    edge(r, kDarkGrey, kWhite);
    edge(r.inset(1), kBlack, kGrey);
}

void Canvas::dent(const Rect &r)
{
    const Theme &t = theme();
    if (t.flat()) {
        edge(r, t.outline, t.outline);
        return;
    }
    edge(r, kDarkGrey, kWhite);
}

void Canvas::button_frame(const Rect &r, ButtonLook look)
{
    const Theme &t = theme();
    if (t.flat()) {
        // The outline with its four corners left to the face colour.
        const Colour in = look == ButtonLook::Raised ? t.button : t.button_down;
        hline(r.x + 1, r.y, r.w - 2, t.outline);
        hline(r.x + 1, r.bottom() - 1, r.w - 2, t.outline);
        vline(r.x, r.y + 1, r.h - 2, t.outline);
        vline(r.right() - 1, r.y + 1, r.h - 2, t.outline);
        put(r.x, r.y, t.face);
        put(r.right() - 1, r.y, t.face);
        put(r.x, r.bottom() - 1, t.face);
        put(r.right() - 1, r.bottom() - 1, t.face);
        edge(r.inset(1), in, in);
        return;
    }
    if (look == ButtonLook::Raised) {
        edge(r, kWhite, kBlack);
        edge(r.inset(1), kGrey, kDarkGrey);
    } else {
        edge(r, kBlack, kWhite);
        edge(r.inset(1), kDarkGrey, kGrey);
    }
}

void Canvas::button(const Rect &r, ButtonLook look)
{
    if (theme().flat()) {
        fill(r.inset(2), button_face(look != ButtonLook::Raised));
        button_frame(r, look);
        return;
    }
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
                    mark(s.x + col, s.y + row, 1);
                }
            }
        }
    });
}

void Canvas::copy(const Bitmap &b, int x, int y, const Rect &part)
{
    each(part.intersect({x, y, b.width, b.height}), [&](const Rect &s) {
        const int bx = s.x - (x + ox_), by = s.y - (y + oy_);
        for (int row = 0; row < s.h; row++) {
            memcpy(pixels_ + (s.y + row) * width_ + s.x, b.pixels + (by + row) * b.width + bx,
                   static_cast<size_t>(s.w));
            mark(s.x, s.y + row, s.w);
        }
    });
}

void Canvas::blit_selected(const Bitmap &b, int x, int y, Colour tint)
{
    each({x, y, b.width, b.height}, [&](const Rect &s) {
        const int bx = s.x - (x + ox_), by = s.y - (y + oy_);
        for (int row = 0; row < s.h; row++) {
            const uint8_t *src = b.pixels + (by + row) * b.width + bx;
            uint8_t *dst = pixels_ + (s.y + row) * width_ + s.x;
            for (int col = 0; col < s.w; col++) {
                if (src[col] != b.transparent) {
                    dst[col] = ((s.x + col + s.y + row) & 1) ? tint : src[col];
                    mark(s.x + col, s.y + row, 1);
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
                    mark(s.x + col, s.y + row, 1);
                }
            }
        }
    });
}

void Canvas::pattern_opaque(int x, int y, const char *const *rows, int count, Colour fg, Colour bg)
{
    for (int r = 0; r < count; r++) {
        for (int q = 0; rows[r][q] != 0; q++) {
            put(x + q, y + r, rows[r][q] == 'X' ? fg : bg);
        }
    }
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
                        mark(px, py, 1);
                    }
                }
            }
        }
    });
    return x + len * advance;
}

int Canvas::text_scaled(int x, int y, const char *s, Colour c, bool bold, bool slanted, int scale)
{
    const Font &f = kSpleen6x12;
    scale = scale < 1 ? 1 : scale > 4 ? 4 : scale;
    const int glyph_w = f.width + (bold ? 1 : 0);
    const int advance = glyph_w * scale;
    int len = 0;
    for (const char *p = s; next_latin1(p) != 0;) {
        len++;
    }
    // A leaning letter reaches a little past its cell.
    const int lean = slanted ? (f.height - 1) / 4 * scale : 0;
    each({x, y, len * advance + lean, f.height * scale}, [&](const Rect &part) {
        const char *p = s;
        int gx = x + ox_;
        const int gy = y + oy_;
        for (uint8_t code; (code = next_latin1(p)) != 0; gx += advance) {
            if (gx + advance + lean <= part.x) {
                continue;
            }
            if (gx >= part.right()) {
                break;
            }
            const uint8_t *g = f.glyph(code);
            for (int row = 0; row < f.height; row++) {
                unsigned bits = g[row];
                if (bold) {
                    bits |= bits >> 1;
                }
                if (bits == 0) {
                    continue;
                }
                const int shift = slanted ? (f.height - 1 - row) / 4 * scale : 0;
                for (int sy = 0; sy < scale; sy++) {
                    const int py = gy + row * scale + sy;
                    if (py < part.y || py >= part.bottom()) {
                        continue;
                    }
                    uint8_t *dst = pixels_ + py * width_;
                    for (int col = 0; col < glyph_w; col++) {
                        if (!(bits & (0x80u >> col))) {
                            continue;
                        }
                        for (int sx = 0; sx < scale; sx++) {
                            const int px = gx + shift + col * scale + sx;
                            if (px >= part.x && px < part.right()) {
                                dst[px] = c;
                                mark(px, py, 1);
                            }
                        }
                    }
                }
            }
        }
    });
    return x + len * advance;
}

template <typename BgFn>
int Canvas::text_cells(int x, int y, const char *s, Colour fg, BgFn bg, int caret_x)
{
    const Font &f = kSpleen6x12;
    int len = 0;
    for (const char *p = s; next_latin1(p) != 0;) {
        len++;
    }
    const int sx0 = x + ox_, sy0 = y + oy_;
    const int scaret = caret_x >= 0 ? caret_x + ox_ : -1;
    const Colour caret = ink();
    // A character at a time: its glyph's row is read once and its pixels
    // written one after the other. Only a character cut by the edge of the
    // part, or with the caret in it, is checked pixel by pixel. The
    // characters of a part are decoded once, a stretch at a time, not once
    // for each of their rows.
    const int cell = f.width;
    const int caret_cell = scaret >= sx0 ? (scaret - sx0) / cell : -1;
    each({x, y, len * cell, f.height}, [&](const Rect &part) {
        const int first = (part.x - sx0) / cell, last = (part.right() - 1 - sx0) / cell;
        const char *p = s;
        for (int i = 0; i < first; i++) {
            next_latin1(p);
        }
        constexpr int kStretch = 96;
        uint8_t codes[kStretch];
        for (int from = first; from <= last; from += kStretch) {
            const int count = last - from + 1 < kStretch ? last - from + 1 : kStretch;
            for (int i = 0; i < count; i++) {
                codes[i] = next_latin1(p);
            }
            const int x0 = sx0 + from * cell; // the stretch's first pixel
            // The cells of the stretch that are whole inside the part.
            const int whole_from = x0 < part.x ? 1 : 0;
            const int whole_to = x0 + count * cell > part.right() ? count - 1 : count;
            const int caret_at = caret_cell - from; // in the stretch, if 0 <= caret_at < count
            for (int py = part.y; py < part.bottom(); py++) {
                const int row = py - sy0;
                uint8_t *const dst = pixels_ + py * width_;
                // One cell, each pixel checked.
                const auto careful = [&](int i) {
                    const unsigned bits = f.glyph(codes[i])[row];
                    for (int col = 0; col < cell; col++) {
                        const int px = x0 + i * cell + col;
                        if (px >= part.x && px < part.right()) {
                            // The caret is always the text colour, even on a selection.
                            dst[px] = px == scaret ? caret : ((bits & (0x80u >> col)) ? fg : bg(px, py));
                        }
                    }
                };
                // Cells [a, b), all of them whole and without the caret.
                const auto straight = [&](int a, int b) {
                    int px = x0 + a * cell;
                    uint8_t *out = dst + px;
                    if (cell == 6) { // the font there is: no loop to count
                        for (int i = a; i < b; i++, out += 6, px += 6) {
                            const unsigned bits = f.glyph(codes[i])[row];
                            out[0] = (bits & 0x80u) ? fg : bg(px, py);
                            out[1] = (bits & 0x40u) ? fg : bg(px + 1, py);
                            out[2] = (bits & 0x20u) ? fg : bg(px + 2, py);
                            out[3] = (bits & 0x10u) ? fg : bg(px + 3, py);
                            out[4] = (bits & 0x08u) ? fg : bg(px + 4, py);
                            out[5] = (bits & 0x04u) ? fg : bg(px + 5, py);
                        }
                        return;
                    }
                    for (int i = a; i < b; i++) {
                        const unsigned bits = f.glyph(codes[i])[row];
                        for (int col = 0; col < cell; col++, px++) {
                            *out++ = (bits & (0x80u >> col)) ? fg : bg(px, py);
                        }
                    }
                };
                for (int i = 0; i < whole_from && i < count; i++) {
                    careful(i);
                }
                if (caret_at >= whole_from && caret_at < whole_to) {
                    straight(whole_from, caret_at);
                    careful(caret_at);
                    straight(caret_at + 1, whole_to);
                } else {
                    straight(whole_from, whole_to);
                }
                for (int i = whole_to > whole_from ? whole_to : whole_from; i < count; i++) {
                    careful(i);
                }
            }
        }
        for (int py = part.y; py < part.bottom(); py++) {
            mark(part.x, py, part.w);
        }
    });
    return x + len * f.width;
}

int Canvas::text_opaque(int x, int y, const char *s, Colour fg, Colour bg, int caret_x)
{
    return text_cells(x, y, s, fg, [bg](int, int) { return bg; }, caret_x);
}

int Canvas::text_opaque(int x, int y, const char *s, Colour fg, Rgb bg, int caret_x)
{
    Colour tile[4][4];
    for (int ty = 0; ty < 4; ty++) {
        for (int tx = 0; tx < 4; tx++) {
            tile[ty][tx] = dither_t(bg, kBayer[ty][tx]);
        }
    }
    return text_cells(x, y, s, fg, [&tile](int px, int py) { return tile[py & 3][px & 3]; }, caret_x);
}

void Canvas::blit_opaque(const Bitmap &b, int x, int y, Colour bg)
{
    each({x, y, b.width, b.height}, [&](const Rect &s) {
        const int bx = s.x - (x + ox_), by = s.y - (y + oy_);
        for (int row = 0; row < s.h; row++) {
            const uint8_t *src = b.pixels + (by + row) * b.width + bx;
            uint8_t *dst = pixels_ + (s.y + row) * width_ + s.x;
            for (int col = 0; col < s.w; col++) {
                dst[col] = src[col] != b.transparent ? src[col] : bg;
            }
            mark(s.x, s.y + row, s.w);
        }
    });
}

void Canvas::fill_around(const Rect &area, const Rect &hole, Colour c)
{
    const Rect h = hole.intersect(area);
    if (h.empty()) {
        fill(area, c);
        return;
    }
    fill({area.x, area.y, area.w, h.y - area.y}, c);                  // above
    fill({area.x, h.bottom(), area.w, area.bottom() - h.bottom()}, c); // below
    fill({area.x, h.y, h.x - area.x, h.h}, c);                         // left
    fill({h.right(), h.y, area.right() - h.right(), h.h}, c);          // right
}

void Canvas::text_up(int x, int y, const char *s, Colour c, bool bold)
{
    const Font &f = kSpleen6x12;
    const int advance = f.width + (bold ? 1 : 0);
    int along = 0;
    for (uint8_t code; (code = next_latin1(s)) != 0; along += advance) {
        const uint8_t *g = f.glyph(code);
        for (int row = 0; row < f.height; row++) {
            unsigned bits = g[row];
            if (bold) {
                bits |= bits >> 1;
            }
            for (int col = 0; col < advance; col++) {
                if (bits & (0x80u >> col)) {
                    // Glyph row goes right, glyph column goes up.
                    put(x + row, y - along - col, c);
                }
            }
        }
    }
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
