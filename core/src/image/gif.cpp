// GIF decoding (87a and 89a), a row at a time: the file's pictures, laid on
// the file's "screen". Transparent pixels, and whatever of the screen a
// picture does not cover, show the background colour handed in, as web
// browsers do (the file's own background colour is ignored).
//
// A plain GIF is decoded as its rows are asked for. An interlaced one
// stores its rows out of order (every 8th, then the ones between), so all
// of it is decoded first, into working memory, a piece at a time.
//
// An animation is several pictures, each a patch laid on what the ones
// before left. For that the whole screen is kept in working memory as RGB
// (the "canvas"), each picture is decoded onto it a piece at a time, and
// the rows given out are the canvas's. A screen too large for the working
// memory is not played: it shows its first picture, as a still GIF does.
#include <cstdio>
#include <cstring>
#include <new>

#include "internal.hpp"

namespace image {

namespace {

constexpr int kCodes = 4096;        // LZW codes are at most 12 bits
constexpr int kPrepareSlice = 1024; // pixels decoded ahead of the rows per prepare()

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
        if (flags & 0x80) {
            global_n_ = 2 << (flags & 7);
            if (!r_.read(global_, static_cast<size_t>(global_n_) * 3)) {
                return Error::Malformed;
            }
        }
        first_at_ = r_.tell();
        const Error e = picture_header();
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
        // Is there another picture after this one? Found by passing over
        // this one's data, and coming back.
        const uint32_t data_at = r_.tell();
        const bool several = skip_blocks() && another_follows();
        if (!r_.seek(data_at)) {
            return Error::Malformed;
        }
        if (several) {
            canvas_ = static_cast<uint8_t *>(d_.allocate(pixel_count() * 3));
        }
        if (canvas_ != nullptr) {
            clear_canvas();
            row_ = static_cast<uint8_t *>(d_.allocate(static_cast<size_t>(w_)));
        } else if (interlaced_) {
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
        info.several = several;
        info.animated = canvas_ != nullptr;
        snprintf(info.detail, sizeof(info.detail), "GIF, %d colours%s", colours_,
                 canvas_ != nullptr ? ", animated" : several ? ", too large to play" : interlaced_ ? ", interlaced" : "");
        return Error::None;
    }

    // An interlaced picture is decoded whole before its first row can be
    // given, and so is each picture of an animation, onto the canvas: a
    // piece per call, so that nobody waits long.
    bool prepare() override
    {
        if ((canvas_ == nullptr && !interlaced_) || stream_row_ >= fh_ || short_) {
            return true;
        }
        for (int done = 0; done < kPrepareSlice && stream_row_ < fh_; done += fw_) {
            const int y = interlaced_ ? interlaced_row() : stream_row_;
            const bool whole_row = canvas_ != nullptr
                                       ? row_to_canvas(y)
                                       : pixels(whole_ + static_cast<size_t>(y) * static_cast<size_t>(fw_), fw_) == fw_;
            if (!whole_row) {
                short_ = true;
                break;
            }
            stream_row_++;
        }
        const bool ready = short_ || stream_row_ >= fh_;
        if (ready && canvas_ != nullptr) {
            look_ahead();
        }
        return ready;
    }

    Error next_row(uint8_t *rgb) override
    {
        const int y = out_y_++;
        if (canvas_ != nullptr) {
            // A first picture cut short is damage, as in a still GIF; a
            // later one only ends the animation (see look_ahead()).
            if (short_ && frame_ == 0) {
                return Error::Malformed;
            }
            memcpy(rgb, canvas_ + static_cast<size_t>(y) * static_cast<size_t>(w_) * 3, static_cast<size_t>(w_) * 3);
            return Error::None;
        }
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

    int frame_ms() override { return delay_ms_; }
    bool more() override { return next_at_ != 0; }

    // On to the next picture of the animation: what the last one asked to
    // have done with itself, then the new one's header. Its pixels come in
    // prepare().
    Error next() override
    {
        if (next_at_ == 0) {
            return Error::Malformed;
        }
        const uint32_t at = next_at_;
        next_at_ = 0;
        const bool round = at == first_at_;
        int was[4] = {0, 0, 0, 0}; // what the last picture gives back, clipped to the screen
        if (round) {
            clear_canvas();
            frame_ = 0;
        } else {
            if (disposal_ == 2) { // "restore to background": its patch is see-through again
                clip(x_, y_, fw_, fh_, was);
                for (int y = was[1]; y < was[1] + was[3]; y++) {
                    uint8_t *o = canvas_ + (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(was[0])) * 3;
                    for (int x = 0; x < was[2]; x++, o += 3) {
                        o[0] = bg_[0];
                        o[1] = bg_[1];
                        o[2] = bg_[2];
                    }
                }
            }
            frame_++;
        }
        if (!r_.seek(at)) {
            return Error::Malformed;
        }
        const Error e = picture_header();
        if (e != Error::None) {
            return e;
        }
        out_y_ = 0;
        // What may now differ from the picture before: the new patch and
        // what the last one gave back; going round again, everything.
        clip(x_, y_, fw_, fh_, changed_);
        if (round) {
            clip(0, 0, w_, h_, changed_);
        } else if (was[2] > 0 && was[3] > 0) {
            const int x0 = was[0] < changed_[0] || changed_[2] == 0 ? was[0] : changed_[0];
            const int y0 = was[1] < changed_[1] || changed_[3] == 0 ? was[1] : changed_[1];
            const int x1 = was[0] + was[2] > changed_[0] + changed_[2] ? was[0] + was[2] : changed_[0] + changed_[2];
            const int y1 = was[1] + was[3] > changed_[1] + changed_[3] ? was[1] + was[3] : changed_[1] + changed_[3];
            changed_[0] = x0;
            changed_[1] = y0;
            changed_[2] = x1 - x0;
            changed_[3] = y1 - y0;
        }
        return Error::None;
    }

    void changed(int part[4]) override { memcpy(part, changed_, sizeof(changed_)); }
    void from_row(int first) override { out_y_ = first; }

private:
    // x, y, w, h cut to the screen; w and h 0 if none of it is on it.
    void clip(int x, int y, int w, int h, int out[4]) const
    {
        const int x1 = x + w < w_ ? x + w : w_, y1 = y + h < h_ ? y + h : h_;
        out[0] = x < w_ ? x : w_;
        out[1] = y < h_ ? y : h_;
        out[2] = x1 > out[0] ? x1 - out[0] : 0;
        out[3] = y1 > out[1] ? y1 - out[1] : 0;
    }

    size_t pixel_count() const { return static_cast<size_t>(w_) * static_cast<size_t>(h_); }

    // Reads on to the next picture in the file, past the extensions before
    // it, and then that picture's own header, up to where its pixels begin.
    Error picture_header()
    {
        transparent_ = -1;
        disposal_ = 0;
        delay_ms_ = 100;
        if (!to_picture()) {
            return Error::Malformed;
        }
        x_ = static_cast<int>(r_.le16());
        y_ = static_cast<int>(r_.le16());
        fw_ = static_cast<int>(r_.le16());
        fh_ = static_cast<int>(r_.le16());
        const int flags = r_.byte();
        if (r_.ended() || fw_ == 0 || fh_ == 0) {
            return Error::Malformed;
        }
        colours_ = global_n_;
        memset(palette_, 0, sizeof(palette_));
        if (flags & 0x80) { // the picture's own colours instead of the file's
            colours_ = 2 << (flags & 7);
            if (!r_.read(palette_, static_cast<size_t>(colours_) * 3)) {
                return Error::Malformed;
            }
        } else {
            memcpy(palette_, global_, static_cast<size_t>(global_n_) * 3);
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
        prev_ = -1;
        sp_ = 0;
        bits_ = 0;
        nbits_ = block_left_ = 0;
        blocks_ended_ = ended_ = short_ = false;
        stream_row_ = pass_ = pass_y_ = 0;
        return Error::None;
    }

    // Reads on to where a picture's header begins, past the extensions
    // before it, keeping what they say about that picture. False when
    // there is no picture: the end of the file, or not a GIF's block.
    bool to_picture()
    {
        for (;;) {
            const int b = r_.byte();
            if (b == 0x2C) {
                return true;
            }
            if (b == 0) {
                continue;
            }
            if (b != 0x21) {
                return false;
            }
            const int label = r_.byte();
            const int size = r_.byte();
            if (size < 0) {
                return false;
            }
            if (size == 0) {
                continue; // an extension with nothing in it
            }
            uint8_t head[11] = {};
            if (label == 0xF9 && size >= 4) {
                // Graphic control: the transparent colour, how long the
                // picture stays, what happens to it then.
                if (!r_.read(head, 4) || !r_.skip(static_cast<uint32_t>(size - 4))) {
                    return false;
                }
                transparent_ = (head[0] & 1) ? head[3] : -1;
                disposal_ = (head[0] >> 2) & 7;
                // Hundredths of a second. Under 20 ms meant "as fast as you
                // can" on slow machines; web browsers show those at 10 a second.
                const int ms = (head[1] | (head[2] << 8)) * 10;
                delay_ms_ = ms < 20 ? 100 : ms;
            } else if (label == 0xFF && size == 11) {
                // An application's. Netscape's says how many more times round.
                if (!r_.read(head, 11)) {
                    return false;
                }
                if (memcmp(head, "NETSCAPE2.0", 11) == 0) {
                    const int n = r_.byte();
                    if (n < 0) {
                        return false;
                    }
                    if (n == 0) {
                        continue;
                    }
                    uint8_t loop[3] = {};
                    if (n != 3) {
                        if (!r_.skip(static_cast<uint32_t>(n))) {
                            return false;
                        }
                    } else if (!r_.read(loop, 3)) {
                        return false;
                    } else if (loop[0] == 1 && !loops_known_) {
                        loops_known_ = true;
                        rounds_left_ = loop[1] | (loop[2] << 8);
                        forever_ = rounds_left_ == 0;
                    }
                }
            } else if (!r_.skip(static_cast<uint32_t>(size))) {
                return false;
            }
            if (!skip_blocks()) {
                return false;
            }
        }
    }

    // Whether a picture follows from here in the file. What the extensions
    // on the way say is about that picture, not the one in hand, whose
    // figures are kept.
    bool another_follows()
    {
        const int transparent = transparent_, disposal = disposal_, delay = delay_ms_;
        const bool another = to_picture();
        transparent_ = transparent;
        disposal_ = disposal;
        delay_ms_ = delay;
        return another;
    }

    // The first row pixel by pixel, the others copied from it.
    void clear_canvas()
    {
        const size_t row = static_cast<size_t>(w_) * 3;
        for (int x = 0; x < w_; x++) {
            memcpy(canvas_ + x * 3, bg_, 3);
        }
        for (int y = 1; y < h_; y++) {
            memcpy(canvas_ + static_cast<size_t>(y) * row, canvas_, row);
        }
    }

    // Which row of the picture the next row of an interlaced one's data is.
    int interlaced_row()
    {
        static const uint8_t first[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
        while (pass_y_ >= fh_) { // a short picture has no rows in some passes
            pass_++;
            pass_y_ = first[pass_];
        }
        const int y = pass_y_;
        pass_y_ += step[pass_];
        return y;
    }

    // The next row of the picture's data onto the canvas, as the picture's
    // row `y`. A later picture of an animation may stick out of the screen:
    // that part is decoded and dropped. False if the data ended early.
    bool row_to_canvas(int y)
    {
        const int sy = y_ + y;
        for (int x = 0; x < fw_;) {
            const int n = fw_ - x < w_ ? fw_ - x : w_;
            if (pixels(row_, n) < n) {
                return false;
            }
            for (int i = 0; i < n; i++) {
                const int sx = x_ + x + i;
                if (sx < w_ && sy < h_ && row_[i] != transparent_) {
                    memcpy(canvas_ + (static_cast<size_t>(sy) * static_cast<size_t>(w_) + static_cast<size_t>(sx)) * 3,
                           palette_ + row_[i] * 3, 3);
                }
            }
            x += n;
        }
        return true;
    }

    // A picture of the animation is on the canvas: where is the next one?
    // After this one's data if the file has another; at the first again if
    // the file asks for another round; nowhere (0) at the end, or if this
    // picture was cut short.
    void look_ahead()
    {
        next_at_ = 0;
        if (short_) {
            return;
        }
        if (!blocks_ended_) { // what is left of the data: pixels beyond the picture, the end code
            if (!r_.skip(static_cast<uint32_t>(block_left_)) || !skip_blocks()) {
                return;
            }
        }
        const uint32_t at = r_.tell();
        if (another_follows()) {
            next_at_ = at;
        } else if (frame_ > 0 && (forever_ || rounds_left_ > 0)) {
            rounds_left_ -= forever_ ? 0 : 1;
            next_at_ = first_at_;
        }
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
    int w_ = 0, h_ = 0;                   // the screen: the size of what is given out
    int x_ = 0, y_ = 0, fw_ = 0, fh_ = 0; // where the picture in hand lies on it
    int colours_ = 0, transparent_ = -1, out_y_ = 0;
    bool interlaced_ = false;
    uint8_t global_[768] = {}; // the file's colours
    int global_n_ = 0;
    uint8_t palette_[768] = {}; // this picture's: the file's, or its own
    uint8_t *row_ = nullptr;    // one row of colour numbers
    uint8_t *whole_ = nullptr;  // an interlaced picture, all of it
    int stream_row_ = 0, pass_ = 0, pass_y_ = 0;
    bool short_ = false; // the data ended before the whole picture was decoded

    // An animation
    uint8_t *canvas_ = nullptr; // the screen, RGB
    uint32_t first_at_ = 0;     // where the first picture's blocks begin in the file
    uint32_t next_at_ = 0;      // where the next picture's begin; 0: there is none
    int frame_ = 0, delay_ms_ = 100, disposal_ = 0;
    int changed_[4] = {}; // of the screen, by the picture in hand: x, y, w, h
    bool loops_known_ = false, forever_ = false;
    int rounds_left_ = 0; // more times round, after this one

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

size_t animation_pool(int width, int height)
{
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    // The reader, the decoder, the canvas, a row of colour numbers, a row
    // of RGB, and the rounding of five allocations.
    return sizeof(Reader) + sizeof(Gif) + pixels * 3 + static_cast<size_t>(width) * 4 + 5 * 8;
}

FormatDecoder *make_gif(Decoder &d, Reader &r, const uint8_t background[3])
{
    void *place = d.allocate(sizeof(Gif));
    return place != nullptr ? new (place) Gif(d, r, background) : nullptr;
}

} // namespace image
