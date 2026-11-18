// Chinchilla as a desktop program: the parts that do not need SDL, so the PC
// tests cover them with no window. How the board's 256 colours become the
// PC's, and where the 640x480 picture sits in a window of any size.
#pragma once

#include <cstdint>

#include "core/video/pointer.hpp"

namespace desktop {

// One screen byte (RRRGGGBB) as the monitor shows it through the board's
// resistors, as 0xAARRGGBB. Blue has two bits; its third resistor repeats
// the top bit, giving the levels 0, 2, 5, 7 (the same table as tools/dbg.py).
constexpr uint32_t level3(unsigned n) { return (n * 255 + 3) / 7; }
constexpr uint32_t colour(uint8_t v)
{
    constexpr unsigned kBlue[4] = {0, 2, 5, 7};
    return 0xFF000000u | (level3(v >> 5) << 16) | (level3((v >> 2) & 7) << 8) | level3(kBlue[v & 3]);
}

// The screen as the monitor would show it: the picture in memory with the
// pointer mixed in on the way out, row by row, by the same function the
// board's video interrupt uses. `out` gets width * height pixels.
inline void to_rgb(const uint8_t *pixels, int width, int height, const video::PointerState &pointer,
                   uint32_t *out)
{
    uint8_t row[2048];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            row[x] = pixels[y * width + x];
        }
        video::compose_pointer(row, y * width, width, width, pointer);
        for (int x = 0; x < width; x++) {
            out[y * width + x] = colour(row[x]);
        }
    }
}

// Where the picture sits in the window: as large as fits with its shape
// kept (4:3), centred, the rest black.
struct Fit {
    int x, y, w, h;
};
inline Fit fit(int window_w, int window_h, int width, int height)
{
    if (window_w <= 0 || window_h <= 0) {
        return {0, 0, 0, 0};
    }
    int w = window_w, h = static_cast<int>(static_cast<int64_t>(window_w) * height / width);
    if (h > window_h) {
        h = window_h;
        w = static_cast<int>(static_cast<int64_t>(window_h) * width / height);
    }
    return {(window_w - w) / 2, (window_h - h) / 2, w, h};
}

// A place in the window as a place on Chinchilla's screen. Outside the
// picture it is the nearest pixel of its edge, so a drag that leaves the
// picture keeps following the mouse.
inline void to_screen(const Fit &f, int window_x, int window_y, int width, int height, int &x, int &y)
{
    if (f.w <= 0 || f.h <= 0) {
        x = y = 0;
        return;
    }
    x = static_cast<int>(static_cast<int64_t>(window_x - f.x) * width / f.w);
    y = static_cast<int>(static_cast<int64_t>(window_y - f.y) * height / f.h);
    x = window_x < f.x ? 0 : (x >= width ? width - 1 : x);
    y = window_y < f.y ? 0 : (y >= height ? height - 1 : y);
}

// The wheel as whole notches. A mouse wheel sends whole notches; two fingers
// on a trackpad send many small parts of one, which are added up here until
// they make a notch, so a slow movement scrolls slowly and none is lost.
// Turning back starts from nothing, so the first notch back is whole.
// One of these for each direction.
struct WheelNotches {
    float part = 0;
    int take(float amount)
    {
        if (amount * part < 0) {
            part = 0;
        }
        part += amount;
        const int whole = static_cast<int>(part); // towards zero
        part -= static_cast<float>(whole);
        return whole > 127 ? 127 : (whole < -127 ? -127 : whole);
    }
};

// How many times larger the picture is first drawn with hard pixel edges,
// before it is smoothed down to the window: the next whole number up. At a
// whole multiple of 640x480 nothing is smoothed at all.
inline int whole_scale(const Fit &f, int width, int height)
{
    int k = 1;
    while (k < 8 && (width * k < f.w || height * k < f.h)) {
        k++;
    }
    return k;
}

} // namespace desktop
