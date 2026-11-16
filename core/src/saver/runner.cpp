#include <cstring>

#include "internal.hpp"

namespace saver {

void line(const Screen &s, int x0, int y0, int x1, int y1, uint8_t c)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        put(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void clear(const Screen &s)
{
    memset(s.pixels, 0, static_cast<size_t>(s.width) * s.height);
}

void Runner::begin(uint8_t *pixels, int width, int height, uint32_t seed)
{
    screen_ = {pixels, width, height};
    clear(screen_);
    animation(index_).start(screen_, seed);
    first_ = true;
    count_ = 0;
    slowest_ = 0;
}

void Runner::tick(uint64_t now_us)
{
    if (first_) {
        first_ = false;
        began_us_ = now_us;
        next_us_ = now_us;
    }
    if (now_us < next_us_) {
        return;
    }
    // Keep the pace; after a long gap start counting again, with no burst
    // of frames to catch up.
    next_us_ += kFrameUs;
    if (next_us_ <= now_us) {
        next_us_ = now_us + kFrameUs;
    }
    const uint64_t before = wm_.now_us();
    animation(index_).step(screen_);
    const uint32_t took = static_cast<uint32_t>(wm_.now_us() - before);
    slowest_ = took > slowest_ ? took : slowest_;
    count_++;
}

void Runner::end(uint64_t now_us)
{
    frames_ = count_;
    shown_us_ = first_ ? 0 : now_us - began_us_;
    slowest_us_ = slowest_;
}

} // namespace saver

namespace saver {

Animation &starfield_animation();
Animation &chinchillas_animation();
Animation &curves_animation();

Animation &animation(int index)
{
    switch (index) {
    case kChinchillas: return chinchillas_animation();
    case kCurves: return curves_animation();
    default: return starfield_animation();
    }
}

} // namespace saver
