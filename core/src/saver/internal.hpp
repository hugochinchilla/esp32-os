// Shared by the screensavers: random numbers, a sine and simple drawing.
#pragma once

#include <cstdint>

#include "core/saver/saver.hpp"

namespace saver {

// xorshift32: small, and the same on every machine.
struct Random {
    uint32_t state = 1;
    void seed(uint32_t s) { state = s != 0 ? s : 0x9E3779B9u; }
    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    // 0 .. n-1
    int below(int n) { return static_cast<int>((next() >> 8) % static_cast<uint32_t>(n)); }
    // lo .. hi
    int range(int lo, int hi) { return lo + below(hi - lo + 1); }
};

// Sine of angle/1024 of a turn, times 1024 (Bhaskara's approximation: whole
// numbers only, within 0.2 % of the real thing).
inline int sine(int angle)
{
    angle &= 1023;
    const bool negative = angle >= 512;
    const int64_t x = negative ? angle - 512 : angle;
    const int64_t p = x * (512 - x);
    const int v = static_cast<int>(4096 * p / (327680 - p));
    return negative ? -v : v;
}

inline void put(const Screen &s, int x, int y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < s.width && y < s.height) {
        s.pixels[y * s.width + x] = c;
    }
}

void line(const Screen &s, int x0, int y0, int x1, int y1, uint8_t c);
void clear(const Screen &s);

} // namespace saver
