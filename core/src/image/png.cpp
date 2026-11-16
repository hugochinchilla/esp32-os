// PNG decoding, a row at a time: all colour types and bit depths,
// transparency mixed over a background colour. Interlaced pictures are
// reported as unsupported.
#include <cstdio>
#include <cstring>
#include <new>

#include "internal.hpp"

namespace image {

namespace {

class Png final : public FormatDecoder {
public:
    Png(Decoder &d, Reader &r, const uint8_t background[3]) : d_(d), r_(r)
    {
        memcpy(bg_, background, 3);
    }

    Error header(Info &info) override
    {
        static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        uint8_t sig[8];
        if (!r_.read(sig, 8) || memcmp(sig, signature, 8) != 0) {
            return Error::Malformed;
        }
        bool have_header = false;
        for (;;) {
            const uint32_t len = r_.be32();
            const uint32_t type = r_.be32();
            if (r_.ended() || len > 0x7FFFFFFF) {
                return Error::Malformed;
            }
            if (type == tag("IHDR")) {
                if (len != 13) {
                    return Error::Malformed;
                }
                w_ = static_cast<int>(r_.be32());
                h_ = static_cast<int>(r_.be32());
                depth_ = r_.byte();
                ctype_ = r_.byte();
                const int compression = r_.byte(), filter = r_.byte(), interlace = r_.byte();
                if (r_.ended() || compression != 0 || filter != 0 || w_ <= 0 || h_ <= 0) {
                    return Error::Malformed;
                }
                if (!valid_depth()) {
                    return Error::Malformed;
                }
                if (interlace == 1) {
                    return Error::Unsupported;
                }
                if (interlace != 0) {
                    return Error::Malformed;
                }
                if (w_ > kMaxWidth || h_ > kMaxHeight) {
                    return Error::TooLarge;
                }
                have_header = true;
            } else if (type == tag("PLTE")) {
                if (len % 3 != 0 || len > 768) {
                    return Error::Malformed;
                }
                palette_n_ = static_cast<int>(len / 3);
                if (!r_.read(palette_, len)) {
                    return Error::Malformed;
                }
            } else if (type == tag("tRNS")) {
                if (!read_trns(len)) {
                    return Error::Malformed;
                }
            } else if (type == tag("IDAT")) {
                if (!have_header || (ctype_ == 3 && palette_n_ == 0)) {
                    return Error::Malformed;
                }
                chunk_left_ = len;
                break;
            } else if (type == tag("IEND")) {
                return Error::Malformed; // no picture data
            } else if (!r_.skip(len)) {
                return Error::Malformed;
            }
            r_.skip(4); // the chunk's CRC
        }

        channels_ = ctype_ == 2 ? 3 : ctype_ == 4 ? 2 : ctype_ == 6 ? 4 : 1;
        const int bits_per_pixel = channels_ * depth_;
        rowbytes_ = (static_cast<size_t>(w_) * bits_per_pixel + 7) / 8;
        bpp_ = bits_per_pixel < 8 ? 1 : bits_per_pixel / 8;
        window_ = static_cast<uint8_t *>(d_.allocate(32768));
        cur_ = static_cast<uint8_t *>(d_.allocate(rowbytes_ + 1));
        prev_ = static_cast<uint8_t *>(d_.allocate(rowbytes_ + 1));
        if (window_ == nullptr || cur_ == nullptr || prev_ == nullptr) {
            return Error::TooLarge;
        }
        memset(prev_, 0, rowbytes_ + 1);
        z_.init(window_, &Png::source, this);
        // The zlib header: deflate, no preset dictionary, valid check bits.
        const int cmf = source(this), flg = source(this);
        if (cmf < 0 || flg < 0 || (cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 ||
            (flg & 0x20)) {
            return Error::Malformed;
        }

        info.width = w_;
        info.height = h_;
        info.format = Format::Png;
        static const char *const kinds[] = {"grey", "?", "RGB", "palette", "grey + alpha", "?", "RGBA"};
        snprintf(info.detail, sizeof(info.detail), "PNG, %d-bit %s", depth_, kinds[ctype_]);
        return Error::None;
    }

    Error next_row(uint8_t *rgb) override
    {
        if (z_.read(cur_, rowbytes_ + 1) != rowbytes_ + 1) {
            return Error::Malformed;
        }
        if (!unfilter()) {
            return Error::Malformed;
        }
        convert(rgb);
        uint8_t *t = prev_;
        prev_ = cur_;
        cur_ = t;
        return Error::None;
    }

private:
    static constexpr uint32_t tag(const char (&s)[5])
    {
        return (static_cast<uint32_t>(s[0]) << 24) | (static_cast<uint32_t>(s[1]) << 16) |
               (static_cast<uint32_t>(s[2]) << 8) | static_cast<uint32_t>(s[3]);
    }

    bool valid_depth() const
    {
        switch (ctype_) {
        case 0:
            return depth_ == 1 || depth_ == 2 || depth_ == 4 || depth_ == 8 || depth_ == 16;
        case 3:
            return depth_ == 1 || depth_ == 2 || depth_ == 4 || depth_ == 8;
        case 2:
        case 4:
        case 6:
            return depth_ == 8 || depth_ == 16;
        default:
            return false;
        }
    }

    bool read_trns(uint32_t len)
    {
        if (ctype_ == 3) {
            if (len > 256) {
                return false;
            }
            memset(alpha_, 255, sizeof(alpha_));
            has_alpha_table_ = true;
            return r_.read(alpha_, len);
        }
        if (ctype_ == 0 && len == 2) {
            key_[0] = static_cast<uint16_t>(r_.be16());
            has_key_ = true;
            return !r_.ended();
        }
        if (ctype_ == 2 && len == 6) {
            for (int i = 0; i < 3; i++) {
                key_[i] = static_cast<uint16_t>(r_.be16());
            }
            has_key_ = true;
            return !r_.ended();
        }
        return r_.skip(len); // tRNS where it does not belong: ignored
    }

    // The zlib stream, across as many IDAT chunks as it takes.
    static int source(void *self)
    {
        Png &p = *static_cast<Png *>(self);
        while (p.chunk_left_ == 0) {
            p.r_.skip(4); // CRC
            const uint32_t len = p.r_.be32();
            const uint32_t type = p.r_.be32();
            if (p.r_.ended() || type != tag("IDAT") || len > 0x7FFFFFFF) {
                return -1;
            }
            p.chunk_left_ = len;
        }
        p.chunk_left_--;
        return p.r_.byte();
    }

    bool unfilter()
    {
        uint8_t *row = cur_ + 1;
        const uint8_t *up = prev_ + 1;
        const size_t n = rowbytes_;
        const size_t b = static_cast<size_t>(bpp_);
        switch (cur_[0]) {
        case 0:
            break;
        case 1:
            for (size_t i = b; i < n; i++) {
                row[i] = static_cast<uint8_t>(row[i] + row[i - b]);
            }
            break;
        case 2:
            for (size_t i = 0; i < n; i++) {
                row[i] = static_cast<uint8_t>(row[i] + up[i]);
            }
            break;
        case 3:
            for (size_t i = 0; i < n; i++) {
                const int left = i >= b ? row[i - b] : 0;
                row[i] = static_cast<uint8_t>(row[i] + ((left + up[i]) >> 1));
            }
            break;
        case 4:
            for (size_t i = 0; i < n; i++) {
                const int a = i >= b ? row[i - b] : 0, c = i >= b ? up[i - b] : 0, u = up[i];
                const int p = a + u - c;
                const int pa = p > a ? p - a : a - p, pb = p > u ? p - u : u - p, pc = p > c ? p - c : c - p;
                const int pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? u : c);
                row[i] = static_cast<uint8_t>(row[i] + pred);
            }
            break;
        default:
            return false;
        }
        return true;
    }

    // Sample `i` of a row of `depth_`-bit samples (one channel per sample).
    unsigned sample(const uint8_t *row, int i) const
    {
        switch (depth_) {
        case 16:
            return static_cast<unsigned>((row[i * 2] << 8) | row[i * 2 + 1]);
        case 8:
            return row[i];
        default: {
            const int bit = i * depth_;
            return (row[bit >> 3] >> (8 - depth_ - (bit & 7))) & ((1u << depth_) - 1);
        }
        }
    }

    uint8_t to8(unsigned v) const
    {
        if (depth_ == 16) {
            return static_cast<uint8_t>(v >> 8);
        }
        return static_cast<uint8_t>(v * 255 / ((1u << depth_) - 1));
    }

    void blend(uint8_t *out, uint8_t r, uint8_t g, uint8_t b, unsigned a) const
    {
        out[0] = static_cast<uint8_t>((r * a + bg_[0] * (255 - a) + 127) / 255);
        out[1] = static_cast<uint8_t>((g * a + bg_[1] * (255 - a) + 127) / 255);
        out[2] = static_cast<uint8_t>((b * a + bg_[2] * (255 - a) + 127) / 255);
    }

    void convert(uint8_t *rgb) const
    {
        const uint8_t *row = cur_ + 1;
        for (int x = 0; x < w_; x++) {
            uint8_t *o = rgb + x * 3;
            switch (ctype_) {
            case 0: {
                const unsigned s = sample(row, x);
                const uint8_t v = to8(s);
                blend(o, v, v, v, (has_key_ && s == key_[0]) ? 0 : 255);
                break;
            }
            case 2: {
                const unsigned r = sample(row, x * 3), g = sample(row, x * 3 + 1), b = sample(row, x * 3 + 2);
                const bool clear = has_key_ && r == key_[0] && g == key_[1] && b == key_[2];
                blend(o, to8(r), to8(g), to8(b), clear ? 0 : 255);
                break;
            }
            case 3: {
                const unsigned i = sample(row, x);
                if (static_cast<int>(i) >= palette_n_) {
                    o[0] = o[1] = o[2] = 0; // an index past the palette: black
                    break;
                }
                const uint8_t *p = palette_ + i * 3;
                blend(o, p[0], p[1], p[2], has_alpha_table_ ? alpha_[i] : 255);
                break;
            }
            case 4: {
                const uint8_t v = to8(sample(row, x * 2));
                blend(o, v, v, v, to8(sample(row, x * 2 + 1)));
                break;
            }
            default: { // 6
                blend(o, to8(sample(row, x * 4)), to8(sample(row, x * 4 + 1)), to8(sample(row, x * 4 + 2)),
                      to8(sample(row, x * 4 + 3)));
                break;
            }
            }
        }
    }

    Decoder &d_;
    Reader &r_;
    uint8_t bg_[3];
    int w_ = 0, h_ = 0, depth_ = 0, ctype_ = 0, channels_ = 1, bpp_ = 1;
    size_t rowbytes_ = 0;
    uint8_t palette_[768] = {};
    int palette_n_ = 0;
    uint8_t alpha_[256] = {};
    bool has_alpha_table_ = false;
    uint16_t key_[3] = {};
    bool has_key_ = false;
    uint32_t chunk_left_ = 0;
    uint8_t *window_ = nullptr, *cur_ = nullptr, *prev_ = nullptr;
    Inflate z_;
};

} // namespace

FormatDecoder *make_png(Decoder &d, Reader &r, const uint8_t background[3])
{
    void *place = d.allocate(sizeof(Png));
    return place != nullptr ? new (place) Png(d, r, background) : nullptr;
}

} // namespace image
