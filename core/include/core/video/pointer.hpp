// The mouse pointer. It is never drawn into the picture in memory: it is
// mixed into the pixels on their way out, by compose_pointer(), both when the
// board sends lines to the monitor and when a screenshot is taken, so the two
// always show the same thing.
#pragma once

#include <cstdint>

namespace video {

constexpr int kMaxPointerSize = 32;

enum PointerPixel : uint8_t {
    kPointerClear = 0, // see-through
    kPointerBlack = 1,
    kPointerWhite = 2,
};

struct PointerShape {
    uint8_t width;  // 1..kMaxPointerSize
    uint8_t height; // 1..kMaxPointerSize
    uint8_t hot_x;  // the pixel that sits at the pointer's position
    uint8_t hot_y;
    uint8_t pixels[kMaxPointerSize * kMaxPointerSize]; // PointerPixel, rows of `width`
};

struct PointerState {
    int16_t x; // screen position of the hotspot
    int16_t y;
    bool visible;
    const PointerShape *shape; // nullptr: nothing to draw
};

// The default arrow, and the double arrows shown over window edges while
// resizing (original art).
const PointerShape &arrow_pointer();
const PointerShape &resize_we_pointer();   // left-right
const PointerShape &resize_ns_pointer();   // up-down
const PointerShape &resize_nwse_pointer(); // top left to bottom right
const PointerShape &resize_nesw_pointer(); // top right to bottom left

// Mixes the pointer into a run of pixels on their way out. `out` holds
// `pixel_count` pixels starting at pixel number `first_pixel_index` of the
// screen (counted row by row from the top left). Returns at once when the run
// does not touch the pointer's rows.
//
// On the board this runs in the video interrupt, so it must stay short and
// must not call anything outside internal RAM (see core/linker.lf).
void compose_pointer(uint8_t *out, int first_pixel_index, int pixel_count, int screen_width,
                     const PointerState &state);

} // namespace video
