// Drawing on the 8-bit framebuffer (RRRGGGBB), clipped to a rectangle list.
// The edges, buttons and scroll tracks are drawn as the theme in use says
// (core/gfx/theme.hpp); the classic one follows tools/mockup/desktop.py,
// which drew docs/look/target-98.png.
#pragma once

#include <cstdint>

#include "core/gfx/font.hpp"
#include "core/gfx/rect.hpp"

namespace gfx {

using Colour = uint8_t;

// A board colour from levels: red and green 0..7, blue 0..3.
constexpr Colour rgb(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Colour>((r << 5) | (g << 2) | b);
}

constexpr Colour kBlack = rgb(0, 0, 0);
constexpr Colour kWhite = rgb(7, 7, 3);
// Fixed colours, for drawings. What is part of the user interface asks the
// theme instead: gfx::face(), ink(), paper(), dim().
constexpr Colour kGrey = rgb(5, 5, 2);     // the classic face of windows and buttons
constexpr Colour kDarkGrey = rgb(2, 2, 1); // and their shadow

// A colour in full 8-bit-per-channel RGB, shown with the 4x4 ordered dither
// where the board has no exact match.
struct Rgb {
    uint8_t r, g, b;
};

// The board colour used at screen position (x, y) for `c`.
Colour dither(Rgb c, int x, int y);
// Each channel: the board level below, or the one above when the value is
// more than (t + 0.5) / n of the way to it. dither() is this with the 4x4
// Bayer matrix, n = 16.
Colour threshold(Rgb c, unsigned t, unsigned n);
// The nearest board colour, and (if asked) what it really shows as.
Colour nearest(Rgb c, Rgb *shown = nullptr);
// What a board colour really shows as (blue has levels 0, 2, 5, 7 of 7).
Rgb shown(Colour c);

// A picture with one board colour per pixel. Pixels equal to `transparent`
// are not drawn (use a value outside the picture's colours).
struct Bitmap {
    int width;
    int height;
    const uint8_t *pixels;
    int transparent; // -1: none
};

enum class ButtonLook {
    Raised,  // normal button
    Pressed, // held down
    Checked, // toggled on (taskbar button of the active window)
};

class Canvas {
public:
    // Draws into `pixels` (a screen of `width` x `height`). Only the parts of
    // `clip` inside `limit` are touched; both are in screen coordinates.
    // Drawing coordinates are relative to (origin_x, origin_y).
    Canvas(uint8_t *pixels, int width, int height, const RectList &clip, const Rect &limit,
           int origin_x = 0, int origin_y = 0);

    // A canvas for a part of this one: origin moved to `r`'s corner and
    // drawing limited to `r` (in this canvas's coordinates).
    Canvas sub(const Rect &r) const;

    void fill(const Rect &r, Colour c);
    void hline(int x, int y, int w, Colour c);
    void vline(int x, int y, int h, Colour c);
    // Exact where the board has the colour, dithered otherwise.
    void fill_rgb(const Rect &r, Rgb c);
    // The same with an 8x8 pattern over it: a set bit (the top bit of a row
    // is its left pixel) is drawn in `ink`. The pattern is fixed to the
    // screen, like the dither, so parts painted apart join up.
    void fill_pattern(const Rect &r, Rgb paper, Rgb ink, const uint8_t rows[8]);
    // A left-to-right fade across `r`, dithered (the 98 title bar); with
    // `vertical`, top to bottom.
    void fill_fade(const Rect &r, Rgb from, Rgb to, bool vertical = false, const Rect *span = nullptr);
    // A dotted rectangle (every other pixel), as round focused widgets.
    void focus_rect(const Rect &r);
    // A scroll bar's track, and the inside of a button that is switched on:
    // the white and grey weave of the classic look, one colour in a flat one.
    void checker(const Rect &r);

    // One-pixel frame: top and left in `tl`, bottom and right in `br`.
    void edge(const Rect &r, Colour tl, Colour br);
    void raised(const Rect &r); // window edge, two pixels
    void sunken(const Rect &r); // text fields, lists
    // A thin edge pressed in, one pixel: an open menu-bar entry, the tray.
    void dent(const Rect &r);
    void button(const Rect &r, ButtonLook look);
    // Only the edges of a button: for buttons that fill their own inside.
    void button_frame(const Rect &r, ButtonLook look);

    void blit(const Bitmap &b, int x, int y);
    // Same, with every other visible pixel in `tint` (a selected icon).
    void blit_selected(const Bitmap &b, int x, int y, Colour tint);
    // Same, every pixel drawn as a 2x2 block.
    void blit2x(const Bitmap &b, int x, int y);
    // Small shapes given as strings, 'X' for a set pixel (button glyphs).
    void pattern(int x, int y, const char *const *rows, int count, Colour c);
    // The same with the blanks in `bg`: every pixel of the shape's box once.
    void pattern_opaque(int x, int y, const char *const *rows, int count, Colour fg, Colour bg);

    // Text in Spleen 6x12, top left at (x, y). UTF-8, Latin-1 characters.
    // Bold doubles each pixel sideways. Returns the x after the text.
    int text(int x, int y, const char *s, Colour c, bool bold = false);
    // The same font `scale` times as large (1 to 4), and leaning if
    // `slanted` (the top of each letter pushed right): a web page's
    // headings and its italics. Returns the x after the text.
    int text_scaled(int x, int y, const char *s, Colour c, bool bold, bool slanted, int scale);
    // Text with its background in one pass: every pixel of each character
    // cell is written once, in `fg` or `bg` (no erase before drawing, so
    // nothing flashes). `caret_x`, if not -1, is a column drawn in the
    // theme's text colour (a text caret). Returns the x after the text.
    int text_opaque(int x, int y, const char *s, Colour fg, Colour bg, int caret_x = -1);
    // The same over a dithered background colour (a selection).
    int text_opaque(int x, int y, const char *s, Colour fg, Rgb bg, int caret_x = -1);
    // A bitmap with its clear pixels in `bg`, each pixel written once.
    void blit_opaque(const Bitmap &b, int x, int y, Colour bg);
    // `area` without `hole`: up to four rectangles.
    void fill_around(const Rect &area, const Rect &hole, Colour c);

    // Layers in one pass: `paint(Canvas &)` draws `r` (drawing coordinates)
    // with the usual calls, background first, covering every pixel of `r`;
    // it runs into a scratch band of kBandRows lines, band by band, and each
    // band is copied to the screen once. So text and icons over a background
    // never show half drawn (one screen buffer, no flashes).
    static constexpr int kBandRows = 12;
    // The widest screen there is (ports::Display::kMaxWidth, which a test
    // holds equal): the band and the fade table are sized for it.
    static constexpr int kMaxWidth = 800;
    template <typename Fn> void layered(const Rect &r, Fn paint)
    {
        if (!counting_) {
            paint(*this); // already in a scratch band
            return;
        }
        const Rect s = r.offset(ox_, oy_).intersect(limit_);
        for (int y = s.y; y < s.bottom(); y += kBandRows) {
            // Only as much of the band as is asked for: a small part of a
            // wide picture is not painted across its whole width.
            const Rect band = asked({s.x, y, s.w, s.bottom() - y < kBandRows ? s.bottom() - y : kBandRows});
            if (band.empty()) {
                continue;
            }
            RectList whole;
            whole.set(band);
            Canvas scratch(band_ - band.y * width_, width_, height_, whole, band, ox_, oy_);
            scratch.counting_ = false;
            paint(scratch);
            copy_band(band);
        }
    }

    // Tests only: when set, every pixel written adds one to its counter
    // (one byte per screen pixel), to check that paints do not draw twice.
    static void count_writes(uint8_t *counts) { write_counts_ = counts; }

    // Text turned a quarter left, reading upwards; (x, y) is the bottom left
    // corner of the first character.
    void text_up(int x, int y, const char *s, Colour c, bool bold = false);
    static int text_width(const char *s, bool bold = false);
    static int text_height() { return kSpleen6x12.height; }

    const Rect &limit() const { return limit_; }
    int origin_x() const { return ox_; }
    int origin_y() const { return oy_; }

private:
    // Calls fn(rect) for each part of `r` (drawing coordinates) that may be
    // drawn, in screen coordinates.
    template <typename Fn> void each(const Rect &r, Fn fn);
    void put(int x, int y, Colour c); // one pixel, drawing coordinates
    void mark(int x, int y, int w) const // the write counter, if on
    {
        if (write_counts_ != nullptr && counting_) {
            uint8_t *p = write_counts_ + y * width_ + x;
            for (int i = 0; i < w; i++) {
                p[i]++;
            }
        }
    }
    template <typename BgFn> int text_cells(int x, int y, const char *s, Colour fg, BgFn bg, int caret_x);
    void copy_band(const Rect &band); // scratch band to the screen, through the clip
    // The rectangle around the parts of `r` that the clip lets through;
    // empty when there are none.
    Rect asked(const Rect &r) const
    {
        Rect all = {};
        for (int i = 0; i < clip_.size(); i++) {
            const Rect part = clip_[i].intersect(r);
            if (!part.empty()) {
                all = all.empty() ? part : bounding(all, part);
            }
        }
        return all;
    }
    // A left-to-right fade as the four rows of pixels its dither repeats
    // in, for every column of the screen (fill_fade).
    static constexpr int kFadeWidth = kMaxWidth;
    struct FadeRows {
        bool valid;
        int sx0, span;
        Rgb from, to;
        int x; // the screen column of row[k][0]
        Colour row[4][kFadeWidth];
    };
    static const FadeRows &fade_rows(Rgb from, Rgb to, int span, int sx0);
    static uint8_t *write_counts_;
    static uint8_t band_[kBandRows * kMaxWidth];
    bool counting_ = true; // false for the scratch band

    uint8_t *pixels_;
    int width_, height_;
    const RectList &clip_;
    Rect limit_;
    int ox_, oy_;
};

} // namespace gfx

#include "core/gfx/theme.hpp"
