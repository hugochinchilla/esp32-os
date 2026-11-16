// Baseline JPEG decoding (and extended Huffman, 8-bit), one row of MCUs at
// a time: grey or YCbCr with sampling factors 1 or 2, restart markers.
// Progressive and arithmetic-coded JPEGs are reported as unsupported.
// Integer arithmetic only, so every machine decodes the same pixels.
#include <cstdio>
#include <cstring>
#include <new>

#include "internal.hpp"

namespace image {

namespace {

const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// 4096 x C(k) x cos((2n + 1) k pi / 16), C(0) = sqrt(1/8), C(k) = 1/2.
const int16_t kIdct[8][8] = {
    {1448, 1448, 1448, 1448, 1448, 1448, 1448, 1448},
    {2009, 1703, 1138, 400, -400, -1138, -1703, -2009},
    {1892, 784, -784, -1892, -1892, -784, 784, 1892},
    {1703, -400, -2009, -1138, 1138, 2009, 400, -1703},
    {1448, -1448, -1448, 1448, 1448, -1448, -1448, 1448},
    {1138, -2009, 400, 1703, -1703, -400, 2009, -1138},
    {784, -1892, 1892, -784, -784, 1892, -1892, 784},
    {400, -1138, 1703, -2009, 2009, -1703, 1138, -400},
};

uint8_t clamp8(int v)
{
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// The 8x8 inverse DCT of `coef` (natural order) into `out` (stride).
void idct(const int32_t *coef, uint8_t *out, int stride)
{
    int32_t tmp[64];
    for (int u = 0; u < 8; u++) {
        // A column with only its first value is flat: the common case.
        bool flat = true;
        for (int v = 1; v < 8 && flat; v++) {
            flat = coef[v * 8 + u] == 0;
        }
        for (int y = 0; y < 8; y++) {
            int32_t s;
            if (flat) {
                s = kIdct[0][y] * coef[u];
            } else {
                s = 0;
                for (int v = 0; v < 8; v++) {
                    s += kIdct[v][y] * coef[v * 8 + u];
                }
            }
            tmp[y * 8 + u] = (s + 512) >> 10; // keeps 2 extra bits
        }
    }
    for (int y = 0; y < 8; y++) {
        const int32_t *t = tmp + y * 8;
        uint8_t *o = out + y * stride;
        for (int x = 0; x < 8; x++) {
            int32_t s = 0;
            for (int u = 0; u < 8; u++) {
                s += kIdct[u][x] * t[u];
            }
            o[x] = clamp8(((s + (1 << 13)) >> 14) + 128);
        }
    }
}

class Jpeg final : public FormatDecoder {
public:
    Jpeg(Decoder &d, Reader &r) : d_(d), r_(r) {}

    Error header(Info &info) override
    {
        if (r_.byte() != 0xFF || r_.byte() != 0xD8) {
            return Error::Malformed;
        }
        bool have_frame = false;
        for (;;) {
            const int m = marker();
            if (m < 0) {
                return Error::Malformed;
            }
            if (m == 0xD9) {
                return Error::Malformed; // ended before any picture data
            }
            if (m >= 0xD0 && m <= 0xD7) {
                continue; // a stray restart marker
            }
            const uint32_t len = r_.be16();
            if (len < 2 || r_.ended()) {
                return Error::Malformed;
            }
            const uint32_t body = len - 2;
            if (m == 0xC0 || m == 0xC1) {
                const Error e = frame(body);
                if (e != Error::None) {
                    return e;
                }
                have_frame = true;
            } else if (m == 0xC2 || m == 0xC6 || m == 0xCA || m == 0xCE) {
                return Error::Unsupported; // progressive
            } else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                return Error::Unsupported; // lossless or arithmetic coding
            } else if (m == 0xC4) {
                if (!huffman_tables(body)) {
                    return Error::Malformed;
                }
            } else if (m == 0xDB) {
                if (!quant_tables(body)) {
                    return Error::Malformed;
                }
            } else if (m == 0xDD) {
                if (body != 2) {
                    return Error::Malformed;
                }
                restart_interval_ = static_cast<int>(r_.be16());
            } else if (m == 0xDA) {
                if (!have_frame) {
                    return Error::Malformed;
                }
                const Error e = scan(body);
                if (e != Error::None) {
                    return e;
                }
                break;
            } else if (!r_.skip(body)) {
                return Error::Malformed;
            }
        }
        info.width = w_;
        info.height = h_;
        info.format = Format::Jpeg;
        if (ncomp_ == 1) {
            snprintf(info.detail, sizeof(info.detail), "JPEG, grey");
        } else {
            snprintf(info.detail, sizeof(info.detail), "JPEG, %s", hmax_ == 1 && vmax_ == 1 ? "4:4:4"
                                                                   : hmax_ == 2 && vmax_ == 2 ? "4:2:0"
                                                                   : hmax_ == 2 ? "4:2:2" : "4:4:0");
        }
        restarts_left_ = restart_interval_;
        return Error::None;
    }

    Error next_row(uint8_t *rgb) override
    {
        if (row_in_mcu_ == 0) {
            const Error e = decode_mcu_row();
            if (e != Error::None) {
                return e;
            }
        }
        const int y = row_in_mcu_;
        for (int x = 0; x < w_; x++) {
            uint8_t *o = rgb + x * 3;
            if (ncomp_ == 1) {
                o[0] = o[1] = o[2] = comp_[0].plane[y * comp_[0].stride + x];
                continue;
            }
            const int Y = sample(0, x, y), cb = sample(1, x, y) - 128, cr = sample(2, x, y) - 128;
            // ITU-R BT.601, 16-bit fixed point.
            o[0] = clamp8(Y + ((91881 * cr + 32768) >> 16));
            o[1] = clamp8(Y - ((22554 * cb + 46802 * cr - 32768) >> 16));
            o[2] = clamp8(Y + ((116130 * cb + 32768) >> 16));
        }
        row_in_mcu_ = (row_in_mcu_ + 1) % mcu_h_;
        return Error::None;
    }

private:
    struct Huffman {
        uint8_t values[256];
        int32_t maxcode[18];
        int32_t valptr[17];
        int32_t mincode[17];
        bool set = false;
    };
    struct Component {
        int id, h, v, tq, td = 0, ta = 0;
        int dc = 0;
        uint8_t *plane = nullptr;
        int stride = 0; // pixels per row of its plane
    };

    // The next marker code (after 0xFF and any fill bytes), -1 at the end.
    int marker()
    {
        int b = r_.byte();
        while (b >= 0 && b != 0xFF) {
            b = r_.byte(); // junk between segments: skipped
        }
        while (b == 0xFF) {
            b = r_.byte();
        }
        return b;
    }

    Error frame(uint32_t len)
    {
        const int precision = r_.byte();
        h_ = static_cast<int>(r_.be16());
        w_ = static_cast<int>(r_.be16());
        ncomp_ = r_.byte();
        if (r_.ended() || precision != 8 || w_ <= 0 || h_ <= 0 || len != 6u + 3u * static_cast<uint32_t>(ncomp_)) {
            return precision == 12 ? Error::Unsupported : Error::Malformed;
        }
        if (ncomp_ != 1 && ncomp_ != 3) {
            return Error::Unsupported; // CMYK and others
        }
        if (w_ > kMaxJpegWidth || h_ > kMaxHeight) {
            return Error::TooLarge;
        }
        hmax_ = vmax_ = 1;
        for (int i = 0; i < ncomp_; i++) {
            Component &c = comp_[i];
            c.id = r_.byte();
            const int s = r_.byte();
            c.h = s >> 4;
            c.v = s & 15;
            c.tq = r_.byte();
            if (c.h < 1 || c.h > 2 || c.v < 1 || c.v > 2 || c.tq > 3) {
                return (c.h > 2 || c.v > 2) ? Error::Unsupported : Error::Malformed;
            }
            hmax_ = c.h > hmax_ ? c.h : hmax_;
            vmax_ = c.v > vmax_ ? c.v : vmax_;
        }
        if (ncomp_ == 1) {
            comp_[0].h = comp_[0].v = hmax_ = vmax_ = 1; // one component: blocks are the MCUs
        }
        mcu_w_ = 8 * hmax_;
        mcu_h_ = 8 * vmax_;
        mcus_x_ = (w_ + mcu_w_ - 1) / mcu_w_;
        mcus_y_ = (h_ + mcu_h_ - 1) / mcu_h_;
        for (int i = 0; i < ncomp_; i++) {
            Component &c = comp_[i];
            c.stride = mcus_x_ * c.h * 8;
            c.plane = static_cast<uint8_t *>(d_.allocate(static_cast<size_t>(c.stride) * c.v * 8));
            if (c.plane == nullptr) {
                return Error::TooLarge;
            }
        }
        return r_.ended() ? Error::Malformed : Error::None;
    }

    bool quant_tables(uint32_t len)
    {
        while (len > 0) {
            const int pq = r_.byte();
            const int id = pq & 15, precision = pq >> 4;
            if (id > 3 || precision > 1 || len < 1u + 64u * (precision + 1)) {
                return false;
            }
            for (int k = 0; k < 64; k++) {
                q_[id][k] = static_cast<uint16_t>(precision ? r_.be16() : static_cast<uint32_t>(r_.byte()));
            }
            len -= 1 + 64 * (precision + 1);
        }
        return !r_.ended();
    }

    bool huffman_tables(uint32_t len)
    {
        while (len > 0) {
            const int tc = r_.byte();
            const int cls = tc >> 4, id = tc & 15;
            if (cls > 1 || id > 3 || len < 17) {
                return false;
            }
            uint8_t counts[17] = {};
            int total = 0;
            for (int i = 1; i <= 16; i++) {
                counts[i] = static_cast<uint8_t>(r_.byte());
                total += counts[i];
            }
            if (total > 256 || len < 17u + static_cast<uint32_t>(total)) {
                return false;
            }
            Huffman &h = cls == 0 ? dc_[id] : ac_[id];
            if (!r_.read(h.values, static_cast<size_t>(total))) {
                return false;
            }
            // Canonical codes (JPEG F.15 and F.16).
            int code = 0, k = 0;
            for (int l = 1; l <= 16; l++) {
                h.valptr[l] = k;
                h.mincode[l] = code;
                code += counts[l];
                k += counts[l];
                h.maxcode[l] = counts[l] ? code - 1 : -1;
                code <<= 1;
                if (code > (2 << l)) {
                    return false; // more codes than the length allows
                }
            }
            h.maxcode[17] = 0x7FFFFFFF;
            h.set = true;
            len -= 17 + static_cast<uint32_t>(total);
        }
        return !r_.ended();
    }

    Error scan(uint32_t len)
    {
        const int n = r_.byte();
        if (n != ncomp_ || len != 4u + 2u * static_cast<uint32_t>(n)) {
            // Baseline files may split components over several scans; only
            // the usual single interleaved scan is handled.
            return n > 0 && n < ncomp_ ? Error::Unsupported : Error::Malformed;
        }
        for (int i = 0; i < n; i++) {
            const int id = r_.byte(), t = r_.byte();
            Component *c = nullptr;
            for (int j = 0; j < ncomp_; j++) {
                if (comp_[j].id == id) {
                    c = &comp_[j];
                }
            }
            if (c == nullptr || (t >> 4) > 3 || (t & 15) > 3) {
                return Error::Malformed;
            }
            c->td = t >> 4;
            c->ta = t & 15;
            if (!dc_[c->td].set || !ac_[c->ta].set) {
                return Error::Malformed;
            }
        }
        const int ss = r_.byte(), se = r_.byte(), a = r_.byte();
        if (r_.ended() || ss != 0 || se != 63 || a != 0) {
            return Error::Malformed;
        }
        return Error::None;
    }

    // Entropy-coded data, with 0xFF 0x00 stuffing; a marker stops it.
    int bit()
    {
        if (bitcnt_ == 0) {
            if (marker_ != 0) {
                bad_ = true; // reading past a marker: damaged data
                return 0;
            }
            int b = r_.byte();
            if (b < 0) {
                bad_ = true;
                return 0;
            }
            if (b == 0xFF) {
                int b2 = r_.byte();
                while (b2 == 0xFF) {
                    b2 = r_.byte();
                }
                if (b2 != 0) {
                    marker_ = b2 < 0 ? 0xD9 : b2;
                    bad_ = bad_ || b2 < 0;
                    b = 0;
                }
            }
            bitbuf_ = static_cast<uint32_t>(b);
            bitcnt_ = 8;
        }
        bitcnt_--;
        return static_cast<int>((bitbuf_ >> bitcnt_) & 1);
    }

    int bits(int n)
    {
        int v = 0;
        for (int i = 0; i < n; i++) {
            v = (v << 1) | bit();
        }
        return v;
    }

    int decode(const Huffman &h)
    {
        int code = bit();
        int l = 1;
        while (code > h.maxcode[l]) {
            if (++l > 16 || bad_) {
                return -1;
            }
            code = (code << 1) | bit();
        }
        const int i = h.valptr[l] + code - h.mincode[l];
        return (i >= 0 && i < 256) ? h.values[i] : -1;
    }

    static int extend(int v, int s)
    {
        return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
    }

    bool block(Component &c, int32_t *coef)
    {
        memset(coef, 0, 64 * sizeof(int32_t));
        const uint16_t *q = q_[c.tq];
        const int s = decode(dc_[c.td]);
        if (s < 0 || s > 11) {
            return false;
        }
        c.dc += s ? extend(bits(s), s) : 0;
        coef[0] = clamp_coef(c.dc * q[0]);
        for (int k = 1; k < 64;) {
            const int rs = decode(ac_[c.ta]);
            if (rs < 0) {
                return false;
            }
            const int run = rs >> 4, size = rs & 15;
            if (size == 0) {
                if (run != 15) {
                    break; // end of block
                }
                k += 16;
                continue;
            }
            k += run;
            if (k > 63 || size > 10) {
                return false;
            }
            coef[kZigzag[k]] = clamp_coef(extend(bits(size), size) * q[k]);
            k++;
        }
        return !bad_;
    }

    // Out of range only in damaged files; kept in range so the inverse DCT
    // cannot overflow.
    static int32_t clamp_coef(int32_t v)
    {
        return v < -2047 ? -2047 : (v > 2047 ? 2047 : v);
    }

    bool restart()
    {
        bitcnt_ = 0; // restart markers sit on a byte boundary
        int m = marker_;
        marker_ = 0;
        if (m == 0) {
            m = marker();
        }
        if (m < 0xD0 || m > 0xD7) {
            return false;
        }
        for (int i = 0; i < ncomp_; i++) {
            comp_[i].dc = 0;
        }
        restarts_left_ = restart_interval_;
        return true;
    }

    Error decode_mcu_row()
    {
        if (mcu_row_ >= mcus_y_) {
            return Error::Malformed;
        }
        int32_t coef[64];
        for (int mx = 0; mx < mcus_x_; mx++) {
            if (restart_interval_ > 0) {
                if (restarts_left_ == 0 && !restart()) {
                    return Error::Malformed;
                }
                restarts_left_--;
            }
            for (int i = 0; i < ncomp_; i++) {
                Component &c = comp_[i];
                for (int by = 0; by < c.v; by++) {
                    for (int bx = 0; bx < c.h; bx++) {
                        if (!block(c, coef)) {
                            return Error::Malformed;
                        }
                        idct(coef, c.plane + by * 8 * c.stride + (mx * c.h + bx) * 8, c.stride);
                    }
                }
            }
        }
        mcu_row_++;
        return Error::None;
    }

    // Component i at full-size position (x, y within the MCU row).
    int sample(int i, int x, int y) const
    {
        const Component &c = comp_[i];
        return c.plane[(y * c.v / vmax_) * c.stride + x * c.h / hmax_];
    }

    Decoder &d_;
    Reader &r_;
    uint16_t q_[4][64] = {};
    Huffman dc_[4], ac_[4];
    Component comp_[3] = {};
    int ncomp_ = 0, w_ = 0, h_ = 0, hmax_ = 1, vmax_ = 1;
    int mcu_w_ = 8, mcu_h_ = 8, mcus_x_ = 0, mcus_y_ = 0, mcu_row_ = 0, row_in_mcu_ = 0;
    int restart_interval_ = 0, restarts_left_ = 0;
    uint32_t bitbuf_ = 0;
    int bitcnt_ = 0;
    int marker_ = 0;
    bool bad_ = false;
};

} // namespace

FormatDecoder *make_jpeg(Decoder &d, Reader &r)
{
    void *place = d.allocate(sizeof(Jpeg));
    return place != nullptr ? new (place) Jpeg(d, r) : nullptr;
}

} // namespace image
