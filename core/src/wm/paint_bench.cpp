// How fast the board draws: each kind of drawing over the same 144,000
// pixels of the real screen, memory copied to the screen, and every open
// window painted alone. Asked for by the debug link (dbg::kPaintBench,
// `tools/dbg.py paintbench`); the figures say where a slow repaint's time
// goes and what a change to the painting could gain
// (docs/journal/2026-11-18-slow-repaints.md).
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/gfx/font.hpp"
#include "core/wm/wm.hpp"

namespace wm {

using gfx::Canvas;

namespace {

constexpr Rect kArea = {80, 60, 480, 300}; // 80 by 25 characters
constexpr int kRuns = 3;

struct Report {
    char *out;
    size_t capacity, used;
    void line(const char *format, ...) __attribute__((format(printf, 2, 3)))
    {
        if (used + 1 >= capacity) {
            return;
        }
        va_list args;
        va_start(args, format);
        const int n = vsnprintf(out + used, capacity - used, format, args);
        va_end(args);
        if (n > 0) {
            used = used + static_cast<size_t>(n) < capacity ? used + static_cast<size_t>(n) : capacity - 1;
        }
    }
};

// A screen of text as the Terminal has it: 80 columns, every row different.
void text_row(int row, char *text)
{
    for (int i = 0; i < 80; i++) {
        text[i] = static_cast<char>(i % 7 == 6 ? ' ' : 'A' + (i * 5 + row * 3) % 58);
    }
    text[80] = 0;
}

// What text_opaque writes, with nothing worked out for each pixel but its
// colour: no clip list, the glyph's row read once for its six pixels. A
// sketch, to measure what the Terminal's text could cost; it is not used.
void text_straight(uint8_t *pixels, int stride, int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg)
{
    const gfx::Font &f = gfx::kSpleen6x12;
    const gfx::Colour two[2] = {bg, fg};
    for (int row = 0; row < f.height; row++) {
        uint8_t *dst = pixels + (y + row) * stride + x;
        for (const char *p = s; *p != 0; p++, dst += 6) {
            const unsigned bits = f.glyph(static_cast<uint8_t>(*p))[row];
            dst[0] = two[bits >> 7];
            dst[1] = two[(bits >> 6) & 1];
            dst[2] = two[(bits >> 5) & 1];
            dst[3] = two[(bits >> 4) & 1];
            dst[4] = two[(bits >> 3) & 1];
            dst[5] = two[(bits >> 2) & 1];
        }
    }
}

} // namespace

size_t Wm::paint_bench(char *out, size_t capacity)
{
    if (capacity == 0 || saver_on_ || screen_fn_ != nullptr) {
        return 0;
    }
    Report report = {out, capacity, 0};
    uint8_t *const pixels = display_.pixels();
    static gfx::RectList clip; // 2 KB: not on the stack
    clip.set(kArea);
    Canvas c(pixels, width_, height_, clip, kArea, kArea.x, kArea.y);
    const Rect all = {0, 0, kArea.w, kArea.h};
    const gfx::Colour ink = gfx::ink(), paper = gfx::paper();

    // The shortest of kRuns, in µs.
    const auto time = [this](auto work) {
        uint32_t best = UINT32_MAX;
        for (int i = 0; i < kRuns; i++) {
            const uint32_t start = clock_.micros();
            work();
            const uint32_t took = clock_.micros() - start;
            best = took < best ? took : best;
        }
        return best;
    };

    // A picture of one chess square's size, half of it clear.
    static uint8_t tile[40 * 40];
    constexpr int kClear = 0xE3;
    for (int i = 0; i < 40 * 40; i++) {
        tile[i] = static_cast<uint8_t>(((i % 40) / 4 + (i / 40) / 4) % 2 != 0 ? kClear : (i * 7) & 0x1F);
    }
    const gfx::Bitmap picture = {40, 40, tile, kClear};
    const auto tiles = [&](auto one) {
        for (int y = 0; y + 40 <= kArea.h; y += 40) {
            for (int x = 0; x < kArea.w; x += 40) {
                one(x, y);
            }
        }
    };
    char text[81];

    report.line("paint bench: %d x %d = %d pixels, shortest of %d, in us\n", kArea.w, kArea.h, kArea.w * kArea.h, kRuns);
    report.line("fill %u\n", static_cast<unsigned>(time([&] { c.fill(all, paper); })));
    report.line("fill_rgb %u\n", static_cast<unsigned>(time([&] { c.fill_rgb(all, {0, 110, 120}); })));
    report.line("fill_fade %u\n", static_cast<unsigned>(time([&] { c.fill_fade(all, {0, 0, 128}, {16, 132, 208}); })));
    // A fade nobody asked for before: its rows are worked out each time (a
    // title bar of another width), then one line of it is painted.
    int width = kArea.w;
    report.line("fade worked out %u\n", static_cast<unsigned>(time([&] {
                    width -= 7;
                    c.fill_fade({0, 0, width, 1}, {0, 0, 128}, {16, 132, 208});
                })));
    // Lines from top to bottom, each pixel in another line of the screen's
    // memory: four side by side, 1,200 pixels, drawn one after the other
    // and then through the band, where each row is written in one piece.
    const auto lines_down = [&](Canvas &on) {
        for (int i = 0; i < 4; i++) {
            on.vline(i, 0, kArea.h, i % 2 != 0 ? ink : paper);
        }
    };
    report.line("4 lines down %u\n", static_cast<unsigned>(time([&] { lines_down(c); })));
    report.line("4 lines down in the band %u\n",
                static_cast<unsigned>(time([&] { c.layered({0, 0, 4, kArea.h}, lines_down); })));
    report.line("fill in the band %u\n",
                static_cast<unsigned>(time([&] { c.layered(all, [&](Canvas &l) { l.fill(all, paper); }); })));
    report.line("text_opaque %u\n", static_cast<unsigned>(time([&] {
                    for (int row = 0; row < 25; row++) {
                        text_row(row, text);
                        c.text_opaque(0, row * 12, text, ink, paper);
                    }
                })));
    report.line("text_opaque straight %u\n", static_cast<unsigned>(time([&] {
                    for (int row = 0; row < 25; row++) {
                        text_row(row, text);
                        text_straight(pixels, width_, kArea.x, kArea.y + row * 12, text, ink, paper);
                    }
                })));
    report.line("text %u\n", static_cast<unsigned>(time([&] {
                    for (int row = 0; row < 25; row++) {
                        text_row(row, text);
                        c.text(0, row * 12, text, ink);
                    }
                })));
    report.line("blit_opaque 84 x 40x40 %u\n",
                static_cast<unsigned>(time([&] { tiles([&](int x, int y) { c.blit_opaque(picture, x, y, paper); }); })));
    report.line("blit 84 x 40x40 %u\n",
                static_cast<unsigned>(time([&] { tiles([&](int x, int y) { c.blit(picture, x, y); }); })));
    // Memory to the screen: from the screen's own memory (what a kept
    // picture of a window would be copied from), and from the stack.
    report.line("copy from external %u\n", static_cast<unsigned>(time([&] {
                    for (int y = 0; y < kArea.h; y++) {
                        memcpy(pixels + (kArea.y + y) * width_ + kArea.x, pixels + ((kArea.y + y + 240) % height_) * width_ + 40,
                               kArea.w);
                    }
                })));
    uint8_t row_of_stack[kArea.w];
    memset(row_of_stack, paper, sizeof(row_of_stack));
    report.line("copy from internal %u\n", static_cast<unsigned>(time([&] {
                    for (int y = 0; y < kArea.h; y++) {
                        memcpy(pixels + (kArea.y + y) * width_ + kArea.x, row_of_stack, kArea.w);
                    }
                })));

    // Every window alone, all of it, whatever covers it: its frame (title
    // and menu bar), then what the program paints.
    for (int i = 0; i < count_; i++) {
        Window &w = *stack_[i];
        if (!visible(w)) {
            continue;
        }
        clip.set(w.frame_);
        uint32_t start = clock_.micros();
        Canvas frame(pixels, width_, height_, clip, w.frame_);
        paint_frame(frame, w);
        const uint32_t frame_us = clock_.micros() - start;
        start = clock_.micros();
        if (w.menu_bar_ != nullptr) {
            paint_menu_bar(frame, w);
        }
        const uint32_t menu_us = clock_.micros() - start;
        const Rect in = w.client();
        Canvas client(pixels, width_, height_, clip, in, in.x, in.y);
        const proc::Scope as(procs_, w.owner_);
        start = clock_.micros();
        if (w.layered()) {
            client.layered({0, 0, in.w, in.h}, [&](Canvas &l) { w.paint(l); });
        } else {
            w.paint(client);
        }
        report.line("window %.20s: %dx%d frame %u menu %u inside %u\n", w.title_, w.frame_.w, w.frame_.h,
                    static_cast<unsigned>(frame_us), static_cast<unsigned>(menu_us),
                    static_cast<unsigned>(clock_.micros() - start));
    }
    report.line("everything again %u\n", static_cast<unsigned>(repaint_all()));
    return report.used;
}

} // namespace wm
