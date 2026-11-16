#include "core/net/deflate.hpp"

#include <cstring>

namespace net {

namespace {

constexpr int kMinMatch = 3, kMaxMatch = 258;
constexpr int kChain = 12; // earlier places tried for each position

constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                    33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

inline uint32_t hash3(const uint8_t *p)
{
    return ((static_cast<uint32_t>(p[0]) << 16 | static_cast<uint32_t>(p[1]) << 8 | p[2]) * 0x9E3779B1u) >> 19;
}

} // namespace

void Deflater::init(void *memory)
{
    window_ = static_cast<uint8_t *>(memory);
    head_ = reinterpret_cast<uint16_t *>(window_ + 2 * kWindow);
    prev_ = head_ + kHashSize;
    reset();
}

void Deflater::reset()
{
    pos_ = 0;
    started_ = false;
    memset(head_, 0, (kHashSize + kWindow) * sizeof(uint16_t));
}

void Deflater::put_bits(uint32_t value, int count)
{
    bits_ |= value << bit_count_;
    bit_count_ += count;
    while (bit_count_ >= 8) {
        out_[out_size_++] = static_cast<uint8_t>(bits_);
        bits_ >>= 8;
        bit_count_ -= 8;
    }
}

void Deflater::put_code(uint32_t code, int length)
{
    uint32_t reversed = 0;
    for (int i = 0; i < length; i++) {
        reversed = reversed << 1 | ((code >> i) & 1);
    }
    put_bits(reversed, length);
}

void Deflater::literal(uint8_t value)
{
    if (value < 144) {
        put_code(0x30 + value, 8);
    } else {
        put_code(0x190 + (value - 144), 9);
    }
}

void Deflater::match(int length, int distance)
{
    int l = 28;
    while (kLengthBase[l] > length) {
        l--;
    }
    const int symbol = 257 + l;
    if (symbol < 280) {
        put_code(static_cast<uint32_t>(symbol - 256), 7);
    } else {
        put_code(static_cast<uint32_t>(0xC0 + symbol - 280), 8);
    }
    put_bits(static_cast<uint32_t>(length - kLengthBase[l]), kLengthExtra[l]);
    int d = 29;
    while (kDistBase[d] > distance) {
        d--;
    }
    put_code(static_cast<uint32_t>(d), 5);
    put_bits(static_cast<uint32_t>(distance - kDistBase[d]), kDistExtra[d]);
}

size_t Deflater::compress(const uint8_t *in, size_t size, uint8_t *out)
{
    out_ = out;
    out_size_ = 0;
    bits_ = 0;
    bit_count_ = 0;
    if (!started_) {
        started_ = true;
        out_[out_size_++] = 0x78; // zlib: deflate, 32 KB window
        out_[out_size_++] = 0x01;
    }
    if (size > kMaxPiece) {
        size = kMaxPiece; // the caller never sends more
    }
    // Room in the window: keep the last 32 KB at the start. Positions in
    // the tables move with it; the older ones become 0, which is only ever
    // a place to compare with, never trusted.
    if (pos_ + size > 2 * kWindow) {
        memmove(window_, window_ + pos_ - kWindow, kWindow);
        const size_t moved = pos_ - kWindow;
        for (size_t i = 0; i < kHashSize + kWindow; i++) {
            head_[i] = head_[i] > moved ? static_cast<uint16_t>(head_[i] - moved) : 0;
        }
        // prev_ is indexed by position modulo 32 KB: turn it by the move.
        if (moved % kWindow != 0) {
            static_assert(kWindow <= 65536, "positions fit 16 bits");
            // ponytail: an uneven move loses the chains (heads still work).
            memset(prev_, 0, kWindow * sizeof(uint16_t));
        }
        pos_ = kWindow;
    }
    memcpy(window_ + pos_, in, size);
    const size_t end = pos_ + size;

    put_bits(2, 3); // not the last block; fixed codes
    size_t p = pos_;
    while (p < end) {
        int best = 0, best_dist = 0;
        if (p + kMinMatch <= end) {
            const uint32_t h = hash3(window_ + p);
            size_t candidate = head_[h];
            const int most = end - p < static_cast<size_t>(kMaxMatch) ? static_cast<int>(end - p) : kMaxMatch;
            for (int tries = 0; tries < kChain; tries++) {
                if (candidate >= p || p - candidate > kWindow) {
                    break;
                }
                const uint8_t *a = window_ + candidate, *b = window_ + p;
                if (a[best] == b[best]) {
                    int n = 0;
                    while (n < most && a[n] == b[n]) {
                        n++;
                    }
                    if (n > best) {
                        best = n;
                        best_dist = static_cast<int>(p - candidate);
                        if (n == most) {
                            break;
                        }
                    }
                }
                const size_t next = prev_[candidate & (kWindow - 1)];
                if (next >= candidate) {
                    break;
                }
                candidate = next;
            }
            prev_[p & (kWindow - 1)] = head_[h];
            head_[h] = static_cast<uint16_t>(p);
        }
        if (best >= kMinMatch) {
            match(best, best_dist);
            // The places inside the repeat can be found later too.
            for (size_t q = p + 1; q < p + static_cast<size_t>(best) && q + kMinMatch <= end; q++) {
                const uint32_t h = hash3(window_ + q);
                prev_[q & (kWindow - 1)] = head_[h];
                head_[h] = static_cast<uint16_t>(q);
            }
            p += static_cast<size_t>(best);
        } else {
            literal(window_[p]);
            p++;
        }
    }
    put_code(0, 7); // end of block
    // Sync flush: an empty stored block, which ends on a byte boundary.
    put_bits(0, 3);
    if (bit_count_ > 0) {
        put_bits(0, 8 - bit_count_);
    }
    out_[out_size_++] = 0;
    out_[out_size_++] = 0;
    out_[out_size_++] = 0xFF;
    out_[out_size_++] = 0xFF;
    pos_ = end;
    return out_size_;
}

} // namespace net
