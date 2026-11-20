// GIF decoding (87a and 89a), a row at a time: the first picture of the
// file, laid on the file's "screen". Transparent pixels, and whatever of
// the screen the picture does not cover, show the background colour handed
// in, as web browsers do (the file's own background colour is ignored).
// An animation shows its first picture.
//
// A plain GIF is decoded as its rows are asked for. An interlaced one
// stores its rows out of order (every 8th, then the ones between), so all
// of it is decoded first, into working memory, a piece at a time.
#include <cstdio>
#include <cstring>
#include <new>

#include "internal.hpp"

namespace image {

namespace {

constexpr int kCodes = 4096;       // LZW codes are at most 12 bits
constexpr int kPrepareSlice = 4096; // pixels of an interlaced picture per prepare()

class Gif final : public FormatDecoder {
public:
    Gif(Decoder &d, Reader &r, const uint8_t background[3]) : d_(d), r_(r)
    {
        memcpy(bg_, background, 3);
    }

    Error header(Info &info) override
    {
        uint8_t sig[6];
        if (!r_.read(sig, 6) || memcmp(sig, "GIF8", 4) != 0 || (sig[4] != '7' && sig[4] != '9') || sig[5] != 'a') {
            return Error::Malformed;
        }
        const int screen_w = static_cast<int>(r_.le16());
        const int screen_h = static_cast<int>(r_.le16());
        const int flags = r_.byte();
        r_.skip(2); // background colour (not used), aspect ratio
        if (r_.ended()) {
            return Error::Malformed;
        }
        int global_n = 0;
        if (flags & 0x80) {
            global_n = 2 << (flags & 7);
            if (!r_.read(palette_, static_cast<size_t>(global_n) * 3)) {
                return Error::Malformed;
            }
        }
        const Error e = picture_header(global_n);
        if (e != Error::None) {
            return e;
        }
        // The screen, grown to hold the picture if the file's figures are
        // too small (or 0, as some old files have them).
        w_ = screen_w > x_ + fw_ ? screen_w : x_ + fw_;
        h_ = screen_h > y_ + fh_ ? screen_h : y_ + fh_;
        if (w_ > kMaxWidth || h_ > kMaxHeight) {
            return Error::TooLarge;
        }
        if (interlaced_) {
            whole_ = static_cast<uint8_t *>(d_.allocate(static_cast<size_t>(fw_) * static_cast<size_t>(fh_)));
        } else {
            row_ = static_cast<uint8_t *>(d_.allocate(static_cast<size_t>(fw_)));
        }
        if (whole_ == nullptr && row_ == nullptr) {
            return Error::TooLarge;
        }
        info.width = w_;
        info.height = h_;
        info.format = Format::Gif;
        snprintf(info.detail, sizeof(info.detail), "GIF, %d colours%s", colours_, interlaced_ ? ", interlaced" : "");
        return Error::None;
    }

    // An interlaced picture is decoded whole before its first row can be
    // given: a piece per call, so that nobody waits long.
    bool prepare() override
    {
        if (!interlaced_ || stream_row_ >= fh_ || short_) {
            return true;
        }
        static const uint8_t first[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
        for (int done = 0; done < kPrepareSlice && stream_row_ < fh_; done += fw_) {
            while (pass_y_ >= fh_) { // a short picture has no rows in some passes
                pass_++;
                pass_y_ = first[pass_];
            }
            if (pixels(whole_ + static_cast<size_t>(pass_y_) * static_cast<size_t>(fw_), fw_) < fw_) {
                short_ = true;
                return true;
            }
            pass_y_ += step[pass_];
            stream_row_++;
        }
        return stream_row_ >= fh_;
    }

    Error next_row(uint8_t *rgb) override
    {
        const int y = out_y_++;
        for (int x = 0; x < w_; x++) {
            memcpy(rgb + x * 3, bg_, 3);
        }
        if (y < y_ || y >= y_ + fh_) {
            return Error::None;
        }
        const uint8_t *px = row_;
        if (interlaced_) {
            if (short_) {
                return Error::Malformed;
            }
            px = whole_ + static_cast<size_t>(y - y_) * static_cast<size_t>(fw_);
        } else if (pixels(row_, fw_) < fw_) {
            return Error::Malformed;
        }
        uint8_t *o = rgb + x_ * 3;
        for (int x = 0; x < fw_; x++, o += 3) {
            if (px[x] != transparent_) {
                memcpy(o, palette_ + px[x] * 3, 3);
            }
        }
        return Error::None;
    }

private:
    // Reads on to the first picture, past the extensions before it, and
    // then that picture's own header, up to where its pixels begin.
    Error picture_header(int global_n)
    {
        for (;;) {
            const int b = r_.byte();
            if (b == 0x2C) {
                break;
            }
            if (b == 0x21) {
                const int label = r_.byte();
                if (label == 0xF9) { // graphic control: which colour is transparent
                    uint8_t g[4] = {};
                    const int size = r_.byte();
                    if (size < 4 || !r_.read(g, 4) || !r_.skip(static_cast<uint32_t>(size - 4))) {
                        return Error::Malformed;
                    }
                    transparent_ = (g[0] & 1) ? g[3] : -1;
                }
                if (!skip_blocks()) {
                    return Error::Malformed;
                }
            } else if (b != 0) { // the end of the file, or not a GIF's block: no picture
                return Error::Malformed;
            }
        }
        x_ = static_cast<int>(r_.le16());
        y_ = static_cast<int>(r_.le16());
        fw_ = static_cast<int>(r_.le16());
        fh_ = static_cast<int>(r_.le16());
        const int flags = r_.byte();
        if (r_.ended() || fw_ == 0 || fh_ == 0) {
            return Error::Malformed;
        }
        colours_ = global_n;
        if (flags & 0x80) { // the picture's own colours instead of the file's
            colours_ = 2 << (flags & 7);
            memset(palette_, 0, sizeof(palette_));
            if (!r_.read(palette_, static_cast<size_t>(colours_) * 3)) {
                return Error::Malformed;
            }
        }
        if (colours_ == 0) {
            return Error::Malformed;
        }
        interlaced_ = (flags & 0x40) != 0;
        min_ = r_.byte();
        if (min_ < 1 || min_ > 8) {
            return Error::Malformed;
        }
        clear_ = 1 << min_;
        width_ = min_ + 1;
        next_ = clear_ + 2;
        return Error::None;
    }

    // Data comes in blocks of up to 255 bytes, each with its length before
    // it; a length of 0 ends them.
    bool skip_blocks()
    {
        for (;;) {
            const int n = r_.byte();
            if (n < 0) {
                return false;
            }
            if (n == 0) {
                return true;
            }
            if (!r_.skip(static_cast<uint32_t>(n))) {
                return false;
            }
        }
    }

    int data_byte()
    {
        if (block_left_ == 0) {
            const int n = blocks_ended_ ? 0 : r_.byte();
            if (n <= 0) {
                blocks_ended_ = true;
                return -1;
            }
            block_left_ = n;
        }
        block_left_--;
        return r_.byte();
    }

    int code()
    {
        while (nbits_ < width_) {
            const int b = data_byte();
            if (b < 0) {
                return -1;
            }
            bits_ |= static_cast<uint32_t>(b) << nbits_;
            nbits_ += 8;
        }
        const int c = static_cast<int>(bits_ & ((1u << width_) - 1));
        bits_ >>= width_;
        nbits_ -= width_;
        return c;
    }

    // The next `n` pixels of the picture, as colour numbers. Fewer when the
    // data ends early or is damaged. LZW: each code stands for a string of
    // pixels, a known string plus one pixel; a string comes out backwards,
    // so it goes through a stack.
    int pixels(uint8_t *out, int n)
    {
        int done = 0;
        while (done < n) {
            if (sp_ > 0) {
                out[done++] = stack_[--sp_];
                continue;
            }
            if (ended_) {
                break;
            }
            const int in = code();
            if (in < 0 || in == clear_ + 1) {
                ended_ = true;
                break;
            }
            if (in == clear_) {
                width_ = min_ + 1;
                next_ = clear_ + 2;
                prev_ = -1;
                continue;
            }
            int c = in;
            if (prev_ < 0) { // the first code after a clear is a single pixel
                if (c > clear_) {
                    ended_ = true;
                    break;
                }
            } else if (c > next_ || (c == next_ && next_ >= kCodes)) {
                ended_ = true;
                break;
            } else if (c == next_) { // the string being made right now: the last one plus its first pixel
                stack_[sp_++] = first_;
                c = prev_;
            }
            while (c > clear_) {
                stack_[sp_++] = suffix_[c];
                c = prefix_[c];
            }
            first_ = static_cast<uint8_t>(c);
            stack_[sp_++] = first_;
            if (prev_ >= 0 && next_ < kCodes) {
                prefix_[next_] = static_cast<uint16_t>(prev_);
                suffix_[next_] = first_;
                next_++;
                if (next_ == (1 << width_) && width_ < 12) {
                    width_++;
                }
            }
            prev_ = in;
        }
        return done;
    }

    Decoder &d_;
    Reader &r_;
    uint8_t bg_[3];
    int w_ = 0, h_ = 0;                 // the screen: the size of what is given out
    int x_ = 0, y_ = 0, fw_ = 0, fh_ = 0; // where the picture lies on it
    int colours_ = 0, transparent_ = -1, out_y_ = 0;
    bool interlaced_ = false;
    uint8_t palette_[768] = {};
    uint8_t *row_ = nullptr;   // one row of colour numbers
    uint8_t *whole_ = nullptr; // an interlaced picture, all of it
    int stream_row_ = 0, pass_ = 0, pass_y_ = 0;
    bool short_ = false; // the interlaced picture's data ended early

    // LZW
    int min_ = 0, clear_ = 0, width_ = 0, next_ = 0, prev_ = -1, sp_ = 0;
    uint8_t first_ = 0;
    uint32_t bits_ = 0;
    int nbits_ = 0, block_left_ = 0;
    bool blocks_ended_ = false, ended_ = false;
    uint16_t prefix_[kCodes];
    uint8_t suffix_[kCodes];
    uint8_t stack_[kCodes + 2];
};

} // namespace

FormatDecoder *make_gif(Decoder &d, Reader &r, const uint8_t background[3])
{
    void *place = d.allocate(sizeof(Gif));
    return place != nullptr ? new (place) Gif(d, r, background) : nullptr;
}

} // namespace image
