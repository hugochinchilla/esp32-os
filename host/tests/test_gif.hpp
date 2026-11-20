// A GIF made for the tests: a frog's face on a transparent background, the
// first picture interlaced. Animated, the frog blinks: a second picture,
// a patch over the eyes that stays 150 ms after the first's 800 ms, for
// ever. The LZW is the plainest there is: a clear code often enough that
// every code stays 9 bits wide and stands for one pixel.
#pragma once

#include <cstdint>
#include <string>

namespace test_gif {

inline std::string frog(int width, int height, bool animated)
{
    std::string out = "GIF89a";
    const auto put16 = [&out](int v) {
        out += static_cast<char>(v & 255);
        out += static_cast<char>(v >> 8);
    };
    put16(width);
    put16(height);
    out += std::string("\xF2\x00\x00", 3); // the file's colours, 8 of them
    const uint8_t colours[8][3] = {{255, 0, 255}, {40, 150, 60}, {20, 90, 40}, {255, 255, 255},
                                   {0, 0, 0},     {230, 90, 90}, {0, 0, 0},    {0, 0, 0}};
    out.append(reinterpret_cast<const char *>(colours), sizeof(colours));
    if (animated) {
        out += std::string("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);
    }
    const int eye = height / 5, eye_y = height / 4;
    // The colour of a pixel; 0 is see-through. With the eyes shut, only
    // the eyes are drawn (the rest of the patch leaves what is there).
    const auto pixel = [width, height, eye, eye_y](int x, int y, bool shut) -> int {
        const auto inside = [x, y](int cx, int cy, int rx, int ry) {
            const long dx = x - cx, dy = y - cy;
            return dx * dx * ry * ry + dy * dy * rx * rx < static_cast<long>(rx) * rx * ry * ry;
        };
        const int cx = width / 2, cy = height * 5 / 8;
        for (int side = -1; side <= 1; side += 2) {
            const int ex = cx + side * width / 6;
            if (shut) {
                if (inside(ex, eye_y, eye, eye)) {
                    return y == eye_y || inside(ex, eye_y, eye * 3 / 4, eye * 3 / 4) ? (y == eye_y ? 4 : 1) : 2;
                }
                continue;
            }
            if (inside(ex, eye_y, eye / 3, eye / 3)) {
                return 4;
            }
            if (inside(ex, eye_y, eye * 3 / 4, eye * 3 / 4)) {
                return 3;
            }
            if (inside(ex, eye_y, eye, eye)) {
                return 2;
            }
        }
        if (shut) {
            return 0;
        }
        if (inside(cx, cy + height / 10, width / 5, height / 16)) {
            return 5;
        }
        if (inside(cx, cy, width * 3 / 10 - 3, height * 3 / 8 - 3)) {
            return 1;
        }
        return inside(cx, cy, width * 3 / 10, height * 3 / 8) ? 2 : 0;
    };
    // One picture: the patch x, y, w, h of the frog, for `hundredths`.
    const auto picture = [&](int px, int py, int pw, int ph, bool shut, bool interlaced, int hundredths) {
        out += std::string("\x21\xF9\x04\x01", 4); // colour 0 is transparent
        put16(hundredths);
        out += std::string("\x00\x00", 2);
        out += ',';
        put16(px);
        put16(py);
        put16(pw);
        put16(ph);
        out += interlaced ? '\x40' : '\x00';
        out += '\x08';
        std::string data;
        uint32_t bits = 0;
        int nbits = 0, since_clear = 0;
        const auto code = [&](uint32_t c) {
            bits |= c << nbits;
            for (nbits += 9; nbits >= 8; nbits -= 8, bits >>= 8) {
                data += static_cast<char>(bits & 255);
            }
        };
        code(256);
        static const int first[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
        for (int pass = interlaced ? 0 : 3; pass < 4; pass++) {
            for (int y = interlaced ? first[pass] : 0; y < ph; y += interlaced ? step[pass] : 1) {
                for (int x = 0; x < pw; x++) {
                    if (++since_clear == 250) {
                        code(256);
                        since_clear = 1;
                    }
                    code(static_cast<uint32_t>(pixel(px + x, py + y, shut)));
                }
            }
        }
        code(257);
        if (nbits > 0) {
            data += static_cast<char>(bits & 255);
        }
        for (size_t at = 0; at < data.size(); at += 255) {
            const size_t n = data.size() - at < 255 ? data.size() - at : 255;
            out += static_cast<char>(n);
            out.append(data, at, n);
        }
        out += '\0';
    };
    picture(0, 0, width, height, false, true, 80);
    if (animated) {
        picture(width / 2 - width / 6 - eye, eye_y - eye, width / 3 + 2 * eye, 2 * eye, true, false, 15);
    }
    out += ';';
    return out;
}

} // namespace test_gif
