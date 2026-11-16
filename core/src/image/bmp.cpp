// BMP decoding: 1, 4, 8, 16, 24 and 32 bits, uncompressed or with bit
// fields, bottom-up or top-down. Compressed (RLE) BMPs are unsupported.
#include <cstdio>
#include <cstring>
#include <new>

#include "internal.hpp"

namespace image {

namespace {

class Bmp final : public FormatDecoder {
public:
    Bmp(Decoder &d, Reader &r) : d_(d), r_(r) {}

    Error header(Info &info) override
    {
        if (r_.byte() != 'B' || r_.byte() != 'M') {
            return Error::Malformed;
        }
        r_.skip(8);
        data_ = r_.le32();
        const uint32_t dib = r_.le32();
        uint32_t compression = 0, colours = 0;
        if (dib == 12) { // OS/2 core header
            w_ = static_cast<int>(r_.le16());
            h_ = static_cast<int>(static_cast<int16_t>(r_.le16()));
            r_.le16();
            bpp_ = static_cast<int>(r_.le16());
            entry_ = 3;
        } else if (dib >= 40 && dib <= 124) {
            w_ = static_cast<int32_t>(r_.le32());
            h_ = static_cast<int32_t>(r_.le32());
            r_.le16(); // planes
            bpp_ = static_cast<int>(r_.le16());
            compression = r_.le32();
            r_.skip(12); // image size, resolution
            colours = r_.le32();
            r_.skip(4);
            entry_ = 4;
            if (compression == 3 && (bpp_ == 16 || bpp_ == 32)) {
                masks_[0] = r_.le32(); // inside the V2+ header, or right after a 40-byte one
                masks_[1] = r_.le32();
                masks_[2] = r_.le32();
            }
        } else {
            return Error::Malformed;
        }
        if (r_.ended()) {
            return Error::Malformed;
        }
        top_down_ = h_ < 0;
        h_ = h_ < 0 ? -h_ : h_;
        if (w_ <= 0 || h_ <= 0) {
            return Error::Malformed;
        }
        if (compression == 1 || compression == 2 || compression == 4 || compression == 5) {
            return Error::Unsupported; // RLE, JPEG or PNG inside a BMP
        }
        if (compression != 0 && compression != 3) {
            return Error::Malformed;
        }
        if (bpp_ != 1 && bpp_ != 4 && bpp_ != 8 && bpp_ != 16 && bpp_ != 24 && bpp_ != 32) {
            return Error::Malformed;
        }
        if (w_ > kMaxWidth || h_ > kMaxHeight) {
            return Error::TooLarge;
        }
        if (compression == 0 && bpp_ == 16) { // 5-5-5
            masks_[0] = 0x7C00;
            masks_[1] = 0x03E0;
            masks_[2] = 0x001F;
        }
        if (compression == 0 && bpp_ == 32) {
            masks_[0] = 0x00FF0000;
            masks_[1] = 0x0000FF00;
            masks_[2] = 0x000000FF;
        }
        if (bpp_ <= 8) {
            const uint32_t most = 1u << bpp_;
            palette_n_ = static_cast<int>(colours == 0 || colours > most ? most : colours);
            if (!r_.seek(14 + dib)) {
                return Error::Malformed;
            }
            for (int i = 0; i < palette_n_; i++) {
                uint8_t e[4];
                if (!r_.read(e, static_cast<size_t>(entry_))) {
                    return Error::Malformed;
                }
                palette_[i * 3] = e[2];
                palette_[i * 3 + 1] = e[1];
                palette_[i * 3 + 2] = e[0];
            }
        } else if (bpp_ == 16 || bpp_ == 32) {
            for (int c = 0; c < 3; c++) {
                if (masks_[c] == 0) {
                    return Error::Malformed;
                }
                shift_[c] = 0;
                while (!((masks_[c] >> shift_[c]) & 1)) {
                    shift_[c]++;
                }
                max_[c] = masks_[c] >> shift_[c];
            }
        }
        stride_ = ((static_cast<uint32_t>(w_) * static_cast<uint32_t>(bpp_) + 31) / 32) * 4;
        row_ = static_cast<uint8_t *>(d_.allocate(stride_));
        if (row_ == nullptr) {
            return Error::TooLarge;
        }
        info.width = w_;
        info.height = h_;
        info.format = Format::Bmp;
        snprintf(info.detail, sizeof(info.detail), "BMP, %d-bit%s", bpp_, top_down_ ? ", top-down" : "");
        return Error::None;
    }

    Error next_row(uint8_t *rgb) override
    {
        const int file_row = top_down_ ? y_ : h_ - 1 - y_;
        y_++;
        if (!r_.seek(data_ + static_cast<uint32_t>(file_row) * stride_) || !r_.read(row_, stride_)) {
            return Error::Malformed;
        }
        for (int x = 0; x < w_; x++) {
            uint8_t *o = rgb + x * 3;
            if (bpp_ <= 8) {
                const int bit = x * bpp_;
                const int i = (row_[bit >> 3] >> (8 - bpp_ - (bit & 7))) & ((1 << bpp_) - 1);
                if (i >= palette_n_) {
                    o[0] = o[1] = o[2] = 0;
                } else {
                    memcpy(o, palette_ + i * 3, 3);
                }
            } else if (bpp_ == 24) {
                o[0] = row_[x * 3 + 2];
                o[1] = row_[x * 3 + 1];
                o[2] = row_[x * 3];
            } else {
                const uint8_t *p = row_ + x * (bpp_ / 8);
                const uint32_t v = bpp_ == 16 ? static_cast<uint32_t>(p[0] | (p[1] << 8))
                                              : static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16)) |
                                                    (static_cast<uint32_t>(p[3]) << 24);
                for (int c = 0; c < 3; c++) {
                    const uint32_t part = (v & masks_[c]) >> shift_[c];
                    o[c] = static_cast<uint8_t>(max_[c] == 0 ? 0 : (part * 255 + max_[c] / 2) / max_[c]);
                }
            }
        }
        return Error::None;
    }

private:
    Decoder &d_;
    Reader &r_;
    int w_ = 0, h_ = 0, bpp_ = 0, y_ = 0, entry_ = 4;
    bool top_down_ = false;
    uint32_t data_ = 0, stride_ = 0;
    uint8_t palette_[768] = {};
    int palette_n_ = 0;
    uint32_t masks_[3] = {};
    int shift_[3] = {};
    uint32_t max_[3] = {};
    uint8_t *row_ = nullptr;
};

} // namespace

FormatDecoder *make_bmp(Decoder &d, Reader &r)
{
    void *place = d.allocate(sizeof(Bmp));
    return place != nullptr ? new (place) Bmp(d, r) : nullptr;
}

} // namespace image
