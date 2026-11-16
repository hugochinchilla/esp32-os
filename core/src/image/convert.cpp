#include "core/image/convert.hpp"

#include <cstring>

#include "core/gfx/canvas.hpp"

namespace image {

bool Converter::ready(ports::Memory &memory)
{
    if (sums_ == nullptr) {
        sums_ = static_cast<uint32_t *>(memory.allocate(sizeof(uint32_t) * kMaxWidth * 3));
        errors_ = static_cast<int16_t *>(memory.allocate(sizeof(int16_t) * (kMaxWidth + 2) * 3 * 2));
        col_start_ = static_cast<uint16_t *>(memory.allocate(sizeof(uint16_t) * (kMaxWidth + 1)));
    }
    return sums_ != nullptr && errors_ != nullptr && col_start_ != nullptr;
}

void Converter::begin(int src_w, int src_h, int dst_w, int dst_h, uint8_t *out, Dither dither)
{
    src_w_ = src_w;
    src_h_ = src_h;
    dst_w_ = dst_w < 1 ? 1 : (dst_w > src_w ? src_w : (dst_w > kMaxWidth ? kMaxWidth : dst_w));
    dst_h_ = dst_h < 1 ? 1 : (dst_h > src_h ? src_h : dst_h);
    out_ = out;
    dither_ = dither;
    dy_ = 0;
    rows_in_ = 0;
    for (int x = 0; x <= dst_w_; x++) {
        col_start_[x] = static_cast<uint16_t>(static_cast<int64_t>(x) * src_w_ / dst_w_);
    }
    memset(sums_, 0, sizeof(uint32_t) * dst_w_ * 3);
    memset(errors_, 0, sizeof(int16_t) * (dst_w_ + 2) * 3 * 2);
}

void Converter::row(int y, const uint8_t *rgb)
{
    if (dy_ >= dst_h_) {
        return;
    }
    for (int x = 0; x < dst_w_; x++) {
        uint32_t r = 0, g = 0, b = 0;
        for (int sx = col_start_[x]; sx < col_start_[x + 1]; sx++) {
            r += rgb[sx * 3];
            g += rgb[sx * 3 + 1];
            b += rgb[sx * 3 + 2];
        }
        sums_[x * 3] += r;
        sums_[x * 3 + 1] += g;
        sums_[x * 3 + 2] += b;
    }
    rows_in_++;
    // The last source row of this result row?
    const int next_start = static_cast<int>(static_cast<int64_t>(dy_ + 1) * src_h_ / dst_h_);
    if (y + 1 >= next_start) {
        emit();
    }
}

void Converter::emit()
{
    uint8_t *o = out_ + static_cast<size_t>(dy_) * dst_w_;
    int16_t *cur = errors_ + ((dy_ & 1) ? (dst_w_ + 2) * 3 : 0);
    int16_t *next = errors_ + ((dy_ & 1) ? 0 : (dst_w_ + 2) * 3);
    if (dither_ == Dither::Diffusion) {
        memset(next, 0, sizeof(int16_t) * (dst_w_ + 2) * 3);
    }
    for (int x = 0; x < dst_w_; x++) {
        const uint32_t area = static_cast<uint32_t>(col_start_[x + 1] - col_start_[x]) * rows_in_;
        const int avg[3] = {static_cast<int>((sums_[x * 3] + area / 2) / area),
                            static_cast<int>((sums_[x * 3 + 1] + area / 2) / area),
                            static_cast<int>((sums_[x * 3 + 2] + area / 2) / area)};
        if (dither_ == Dither::Ordered) {
            o[x] = gfx::dither({static_cast<uint8_t>(avg[0]), static_cast<uint8_t>(avg[1]),
                                static_cast<uint8_t>(avg[2])},
                               x, dy_);
        } else if (dither_ == Dither::None) {
            o[x] = gfx::nearest({static_cast<uint8_t>(avg[0]), static_cast<uint8_t>(avg[1]),
                                 static_cast<uint8_t>(avg[2])});
        } else {
            // Floyd-Steinberg: the error goes 7/16 right, 3/16, 5/16, 1/16 below.
            int v[3];
            for (int c = 0; c < 3; c++) {
                const int t = avg[c] + cur[(x + 1) * 3 + c];
                v[c] = t < 0 ? 0 : (t > 255 ? 255 : t);
            }
            gfx::Rgb got;
            o[x] = gfx::nearest({static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2])},
                                &got);
            const int real[3] = {got.r, got.g, got.b};
            for (int c = 0; c < 3; c++) {
                const int e = v[c] - real[c];
                cur[(x + 2) * 3 + c] = static_cast<int16_t>(cur[(x + 2) * 3 + c] + e * 7 / 16);
                next[x * 3 + c] = static_cast<int16_t>(next[x * 3 + c] + e * 3 / 16);
                next[(x + 1) * 3 + c] = static_cast<int16_t>(next[(x + 1) * 3 + c] + e * 5 / 16);
                next[(x + 2) * 3 + c] = static_cast<int16_t>(next[(x + 2) * 3 + c] + e / 16);
            }
        }
    }
    if (dither_ == Dither::Diffusion) {
        memset(cur, 0, sizeof(int16_t) * (dst_w_ + 2) * 3); // becomes the row after next
    }
    memset(sums_, 0, sizeof(uint32_t) * dst_w_ * 3);
    rows_in_ = 0;
    dy_++;
}

} // namespace image
