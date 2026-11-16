// Buffered file reading and a streaming inflate (RFC 1951), written for this
// project after the structure of Mark Adler's "puff" reference decoder:
// canonical Huffman codes decoded a bit at a time, a 32 KB window, and
// output produced on demand so a PNG can be decompressed a row at a time.
#include <cstring>

#include "internal.hpp"

namespace image {

/* ---- Reader ------------------------------------------------------------------ */

void Reader::attach(const fs::Storage &storage, fs::File &file)
{
    storage_ = &storage;
    file_ = &file;
    filled_ = at_ = 0;
    pos_ = 0;
    ended_ = false;
}

bool Reader::fill()
{
    size_t got = 0;
    if (storage_->read(*file_, buf_, sizeof(buf_), got) != ports::FsError::Ok) {
        got = 0;
    }
    filled_ = got;
    at_ = 0;
    pos_ += static_cast<uint32_t>(got);
    if (got == 0) {
        ended_ = true;
        return false;
    }
    return true;
}

int Reader::byte()
{
    if (at_ == filled_ && !fill()) {
        return -1;
    }
    return buf_[at_++];
}

uint32_t Reader::be16()
{
    const int a = byte(), b = byte();
    return (a < 0 || b < 0) ? 0 : static_cast<uint32_t>((a << 8) | b);
}

uint32_t Reader::be32()
{
    const uint32_t a = be16(), b = be16();
    return (a << 16) | b;
}

uint32_t Reader::le16()
{
    const int a = byte(), b = byte();
    return (a < 0 || b < 0) ? 0 : static_cast<uint32_t>(a | (b << 8));
}

uint32_t Reader::le32()
{
    const uint32_t a = le16(), b = le16();
    return a | (b << 16);
}

bool Reader::read(uint8_t *out, size_t n)
{
    while (n > 0) {
        if (at_ == filled_ && !fill()) {
            return false;
        }
        size_t take = filled_ - at_ < n ? filled_ - at_ : n;
        memcpy(out, buf_ + at_, take);
        at_ += take;
        out += take;
        n -= take;
    }
    return true;
}

bool Reader::skip(uint32_t n)
{
    if (n <= filled_ - at_) {
        at_ += n;
        return true;
    }
    return seek(tell() + n);
}

bool Reader::seek(uint32_t offset)
{
    filled_ = at_ = 0;
    pos_ = offset;
    if (storage_->seek(*file_, offset) != ports::FsError::Ok) {
        ended_ = true;
        return false;
    }
    return true;
}

/* ---- Inflate ------------------------------------------------------------------ */

namespace {

const uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

constexpr uint32_t kWindow = 32768;

} // namespace

void Inflate::init(uint8_t *window, Source source, void *context)
{
    window_ = window;
    source_ = source;
    context_ = context;
    wpos_ = 0;
    bitbuf_ = 0;
    bitcnt_ = 0;
    state_ = State::Header;
    last_ = false;
    failed_ = false;
    stored_left_ = copy_left_ = copy_dist_ = 0;
    total_ = 0;
}

int Inflate::bit()
{
    if (bitcnt_ == 0) {
        const int b = source_(context_);
        if (b < 0) {
            failed_ = true; // ran out in the middle of the data
            return 0;
        }
        bitbuf_ = static_cast<uint32_t>(b);
        bitcnt_ = 8;
    }
    const int v = static_cast<int>(bitbuf_ & 1);
    bitbuf_ >>= 1;
    bitcnt_--;
    return v;
}

int Inflate::bits(int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) {
        v |= bit() << i;
    }
    return v;
}

int Inflate::decode(const Huffman &h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= bit();
        const int count = h.count[len];
        if (code - count < first) {
            return h.symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (failed_) {
            return -1;
        }
    }
    return -1; // no such code
}

bool Inflate::build(Huffman &h, const uint8_t *lengths, int n)
{
    memset(h.count, 0, sizeof(h.count));
    for (int i = 0; i < n; i++) {
        h.count[lengths[i]]++;
    }
    if (h.count[0] == n) {
        return true; // no codes at all (a block with no distances)
    }
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) {
            return false; // more codes than lengths allow
        }
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) {
        offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
        }
    }
    return true;
}

bool Inflate::fixed_tables()
{
    uint8_t lengths[288];
    int i = 0;
    for (; i < 144; i++) {
        lengths[i] = 8;
    }
    for (; i < 256; i++) {
        lengths[i] = 9;
    }
    for (; i < 280; i++) {
        lengths[i] = 7;
    }
    for (; i < 288; i++) {
        lengths[i] = 8;
    }
    if (!build(lit_, lengths, 288)) {
        return false;
    }
    for (i = 0; i < 30; i++) {
        lengths[i] = 5;
    }
    return build(dist_, lengths, 30);
}

bool Inflate::dynamic_tables()
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t lengths[320];
    const int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (nlen > 286 || ndist > 30) {
        return false;
    }
    int i = 0;
    for (; i < ncode; i++) {
        lengths[order[i]] = static_cast<uint8_t>(bits(3));
    }
    for (; i < 19; i++) {
        lengths[order[i]] = 0;
    }
    Huffman lencode;
    if (!build(lencode, lengths, 19) || failed_) {
        return false;
    }
    i = 0;
    while (i < nlen + ndist) {
        int sym = decode(lencode);
        if (sym < 0 || failed_) {
            return false;
        }
        if (sym < 16) {
            lengths[i++] = static_cast<uint8_t>(sym);
            continue;
        }
        int len = 0, repeat;
        if (sym == 16) {
            if (i == 0) {
                return false; // nothing to repeat
            }
            len = lengths[i - 1];
            repeat = 3 + bits(2);
        } else if (sym == 17) {
            repeat = 3 + bits(3);
        } else {
            repeat = 11 + bits(7);
        }
        if (i + repeat > nlen + ndist) {
            return false;
        }
        while (repeat-- > 0) {
            lengths[i++] = static_cast<uint8_t>(len);
        }
    }
    if (lengths[256] == 0) {
        return false; // no end-of-block code
    }
    return build(lit_, lengths, nlen) && build(dist_, lengths + nlen, ndist) && !failed_;
}

void Inflate::put(uint8_t b, uint8_t *out, size_t &done)
{
    out[done++] = b;
    window_[wpos_] = b;
    wpos_ = (wpos_ + 1) & (kWindow - 1);
    total_++;
}

size_t Inflate::read(uint8_t *out, size_t n)
{
    size_t done = 0;
    while (done < n && !failed_) {
        if (copy_left_ > 0) {
            put(window_[(wpos_ - copy_dist_) & (kWindow - 1)], out, done);
            copy_left_--;
            continue;
        }
        switch (state_) {
        case State::Done:
            return done;
        case State::Header: {
            last_ = bit() != 0;
            const int type = bits(2);
            if (failed_) {
                break;
            }
            if (type == 0) {
                bitbuf_ = 0; // a stored block starts on a byte boundary
                bitcnt_ = 0;
                const int a = source_(context_), b = source_(context_), c = source_(context_),
                          d = source_(context_);
                if (a < 0 || b < 0 || c < 0 || d < 0 || (a | (b << 8)) != (~(c | (d << 8)) & 0xFFFF)) {
                    failed_ = true;
                    break;
                }
                stored_left_ = static_cast<uint32_t>(a | (b << 8));
                state_ = State::Stored;
            } else if (type == 1) {
                failed_ = !fixed_tables();
                state_ = State::Codes;
            } else if (type == 2) {
                failed_ = !dynamic_tables();
                state_ = State::Codes;
            } else {
                failed_ = true;
            }
            break;
        }
        case State::Stored: {
            if (stored_left_ == 0) {
                state_ = last_ ? State::Done : State::Header;
                break;
            }
            const int b = source_(context_);
            if (b < 0) {
                failed_ = true;
                break;
            }
            put(static_cast<uint8_t>(b), out, done);
            stored_left_--;
            break;
        }
        case State::Codes: {
            int sym = decode(lit_);
            if (sym < 0) {
                failed_ = true;
                break;
            }
            if (sym < 256) {
                put(static_cast<uint8_t>(sym), out, done);
                break;
            }
            if (sym == 256) {
                state_ = last_ ? State::Done : State::Header;
                break;
            }
            sym -= 257;
            if (sym >= 29) {
                failed_ = true;
                break;
            }
            const uint32_t len = kLengthBase[sym] + static_cast<uint32_t>(bits(kLengthExtra[sym]));
            const int dsym = decode(dist_);
            if (dsym < 0 || dsym >= 30) {
                failed_ = true;
                break;
            }
            const uint32_t dist = kDistBase[dsym] + static_cast<uint32_t>(bits(kDistExtra[dsym]));
            if (dist > total_ || dist > kWindow) {
                failed_ = true; // reaches back before the start
                break;
            }
            copy_left_ = len;
            copy_dist_ = dist;
            break;
        }
        }
    }
    return done;
}

} // namespace image
