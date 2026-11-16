// Kept on its own: on the board this file is placed in internal RAM
// (core/linker.lf) because the video interrupt calls it.
#include "core/video/pointer.hpp"

namespace video {

void compose_pointer(uint8_t *out, int first_pixel_index, int pixel_count, int screen_width,
                     const PointerState &state)
{
    if (!state.visible || state.shape == nullptr || pixel_count <= 0) {
        return;
    }
    const PointerShape &shape = *state.shape;
    const int top = state.y - shape.hot_y;
    const int left = state.x - shape.hot_x;
    const int end = first_pixel_index + pixel_count;

    // Rows the run touches, narrowed to the pointer's rows.
    int row0 = first_pixel_index / screen_width;
    int row1 = (end - 1) / screen_width + 1;
    if (row0 < top) {
        row0 = top;
    }
    if (row1 > top + shape.height) {
        row1 = top + shape.height;
    }
    if (row0 < 0) {
        row0 = 0;
    }

    const int col0 = left < 0 ? 0 : left;
    const int col1 = left + shape.width > screen_width ? screen_width : left + shape.width;
    for (int row = row0; row < row1; row++) {
        const int row_start = row * screen_width;
        int i0 = row_start + col0;
        int i1 = row_start + col1;
        if (i0 < first_pixel_index) {
            i0 = first_pixel_index;
        }
        if (i1 > end) {
            i1 = end;
        }
        const uint8_t *src = shape.pixels + (row - top) * shape.width + (i0 - row_start - left);
        uint8_t *dst = out + (i0 - first_pixel_index);
        for (int i = i0; i < i1; i++, src++, dst++) {
            if (*src == kPointerBlack) {
                *dst = 0x00;
            } else if (*src == kPointerWhite) {
                *dst = 0xFF;
            }
        }
    }
}

} // namespace video
