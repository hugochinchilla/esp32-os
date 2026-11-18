// The fast ways of drawing give the pixels of the plain ones
// (docs/journal/2026-11-18-faster-drawing.md): the left-to-right fade, text
// with its background, and a picture painted through the scratch band,
// each against a pixel-by-pixel version written here, over random places
// and random clips. Every pixel outside the clip must stay as it was.
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "core/gfx/canvas.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

constexpr int kW = 640, kH = 480;
using gfx::Colour;
using gfx::Rect;
using gfx::Rgb;

std::mt19937 rng(20261118);
int pick(int low, int high) { return low + static_cast<int>(rng() % static_cast<unsigned>(high - low + 1)); }

struct Screen {
    std::vector<uint8_t> px = std::vector<uint8_t>(kW * kH, 0x55);
};

// A clip of one to four rectangles somewhere near `around`.
void random_clip(gfx::RectList &clip, const Rect &around)
{
    clip.clear();
    const int count = pick(1, 4);
    for (int i = 0; i < count; i++) {
        clip.add(Rect{around.x + pick(-20, around.w), around.y + pick(-10, around.h), pick(1, around.w + 30),
                      pick(1, around.h + 10)}
                     .intersect({0, 0, kW, kH}));
    }
}

bool in_clip(const gfx::RectList &clip, int x, int y)
{
    for (int i = 0; i < clip.size(); i++) {
        if (clip[i].contains(x, y)) {
            return true;
        }
    }
    return false;
}

// What the canvas drew against `want(x, y)` (screen coordinates; -1 where
// nothing is to be drawn), inside `limit` and the clip only.
template <typename Want> bool same(const Screen &s, const gfx::RectList &clip, const Rect &limit, Want want)
{
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x++) {
            const int w = limit.contains(x, y) && in_clip(clip, x, y) ? want(x, y) : -1;
            if (s.px[y * kW + x] != (w < 0 ? 0x55 : w)) {
                printf("  at %d,%d: %02x, wanted %02x\n", x, y, s.px[y * kW + x], w < 0 ? 0x55 : w);
                return false;
            }
        }
    }
    return true;
}

void test_fade()
{
    for (int round = 0; round < 300; round++) {
        Screen s;
        const Rect whole = {pick(-30, 500), pick(0, 440), pick(1, 600), pick(1, 30)};
        // A part of the fade, as a title bar painted in pieces.
        const Rect part = round % 3 == 0 ? whole : Rect{whole.x + pick(0, whole.w - 1), whole.y, pick(1, whole.w), whole.h};
        const Rgb from = {static_cast<uint8_t>(pick(0, 255)), static_cast<uint8_t>(pick(0, 255)),
                          static_cast<uint8_t>(pick(0, 255))};
        const Rgb to = {static_cast<uint8_t>(pick(0, 255)), static_cast<uint8_t>(pick(0, 255)),
                        static_cast<uint8_t>(pick(0, 255))};
        gfx::RectList clip;
        random_clip(clip, whole);
        const Rect limit = {0, 0, kW, kH};
        gfx::Canvas c(s.px.data(), kW, kH, clip, limit);
        c.fill_fade(part, from, to, false, &whole);
        const int span = whole.w > 1 ? whole.w - 1 : 1;
        CHECK(same(s, clip, part.intersect(limit), [&](int x, int y) {
            const int i = x - whole.x;
            const Rgb at = {static_cast<uint8_t>(from.r + (to.r - from.r) * i / span),
                            static_cast<uint8_t>(from.g + (to.g - from.g) * i / span),
                            static_cast<uint8_t>(from.b + (to.b - from.b) * i / span)};
            return static_cast<int>(gfx::dither(at, x, y));
        }));
    }
}

void test_text()
{
    const gfx::Font &f = gfx::kSpleen6x12;
    const char *const kTexts[] = {"C:\\> dir", "The quick brown fox jumps over the lazy dog", "a", "",
                                  "a\xC3\xB1o, caf\xC3\xA9 y \xC2\xBFqu\xC3\xA9?", "0123456789 !\"#$%&'()*+,-./"};
    for (int round = 0; round < 600; round++) {
        Screen s;
        const char *text = kTexts[round % 6];
        // The Latin-1 characters the text decodes to.
        std::vector<uint8_t> codes;
        for (const char *p = text; uint8_t code = gfx::next_latin1(p);) {
            codes.push_back(code);
        }
        const int len = static_cast<int>(codes.size());
        const int ox = pick(0, 300), oy = pick(0, 400), x = pick(-40, 200), y = pick(-6, 50);
        const int caret = round % 4 == 0 ? x + pick(0, len * f.width + 3) : -1;
        const bool tile = round % 5 == 0;
        const Colour fg = static_cast<Colour>(pick(0, 255)), bg = static_cast<Colour>(pick(0, 255));
        const Rgb soft = {static_cast<uint8_t>(pick(0, 255)), static_cast<uint8_t>(pick(0, 255)),
                          static_cast<uint8_t>(pick(0, 255))};
        const Rect box = {ox + x, oy + y, len * f.width, f.height};
        gfx::RectList clip;
        random_clip(clip, box.w > 0 ? box : Rect{ox, oy, 20, 12});
        const Rect limit = {pick(0, 100), pick(0, 100), pick(200, 540), pick(200, 380)};
        gfx::Canvas c(s.px.data(), kW, kH, clip, limit, ox, oy);
        const int end = tile ? c.text_opaque(x, y, text, fg, soft, caret) : c.text_opaque(x, y, text, fg, bg, caret);
        CHECK(end == x + len * f.width);
        CHECK(same(s, clip, box.intersect(limit), [&](int px, int py) {
            if (caret >= 0 && px == caret + ox) {
                return static_cast<int>(gfx::ink());
            }
            const int index = (px - box.x) / f.width, col = (px - box.x) % f.width, row = py - box.y;
            if ((f.glyph(codes[static_cast<size_t>(index)])[row] & (0x80u >> col)) != 0) {
                return static_cast<int>(fg);
            }
            return static_cast<int>(tile ? gfx::dither(soft, px, py) : bg);
        }));
    }
}

// A picture of several layers painted through the band shows, in any part
// of it, what the whole picture shows there.
void test_band()
{
    const Rect picture = {37, 51, 400, 130};
    const auto paint = [&](gfx::Canvas &l) {
        l.fill(picture, 0x10);
        for (int i = 0; i < 12; i++) {
            l.fill({picture.x + i * 33, picture.y + i * 9, 40, 40}, static_cast<Colour>(0x20 + i));
            l.text(picture.x + i * 33 + 2, picture.y + i * 9 + 3, "Qg", 0xFF, true);
        }
    };
    Screen whole;
    gfx::RectList all;
    all.set({0, 0, kW, kH});
    gfx::Canvas(whole.px.data(), kW, kH, all, {0, 0, kW, kH}).layered(picture, paint);
    for (int round = 0; round < 200; round++) {
        Screen s;
        gfx::RectList clip;
        random_clip(clip, picture);
        gfx::Canvas(s.px.data(), kW, kH, clip, {0, 0, kW, kH}).layered(picture, paint);
        CHECK(same(s, clip, picture, [&](int x, int y) { return static_cast<int>(whole.px[y * kW + x]); }));
    }
}

} // namespace

int main()
{
    test_fade();
    test_text();
    test_band();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("canvas: all checks passed\n");
    return 0;
}
