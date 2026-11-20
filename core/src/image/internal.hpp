// Shared by the decoders: buffered file reading, the inflate (zlib)
// decompressor, and the interface each format implements.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/image/image.hpp"

namespace image {

// Reads a file through a small buffer. Past the end it gives -1 (and 0s for
// read_u8 and friends) and remembers that it ran out, so damaged files fail
// cleanly instead of reading garbage.
class Reader {
public:
    void attach(Source &source);
    int byte();                // 0..255, or -1 at the end
    uint32_t be16();
    uint32_t be32();
    uint32_t le16();
    uint32_t le32();
    bool read(uint8_t *out, size_t n); // false if the file ended first
    bool skip(uint32_t n);
    bool seek(uint32_t offset);
    uint32_t tell() const { return pos_ - (filled_ - at_); }
    bool ended() const { return ended_; }

private:
    bool fill();

    Source *source_ = nullptr;
    uint8_t buf_[1024];
    size_t filled_ = 0, at_ = 0;
    uint32_t pos_ = 0; // file position of the end of what is in buf_
    bool ended_ = false;
};

// Streaming inflate (RFC 1951), pulling compressed bytes from a function
// and giving out decompressed bytes on demand. Uses a 32 KB window from the
// caller.
class Inflate {
public:
    using Source = int (*)(void *context); // next byte, or -1 at the end

    void init(uint8_t *window, Source source, void *context);
    // Up to `n` bytes; fewer only at the end of the stream or on an error.
    size_t read(uint8_t *out, size_t n);
    bool failed() const { return failed_; }
    bool finished() const { return state_ == State::Done; }

private:
    struct Huffman {
        uint16_t count[16];
        uint16_t symbol[288];
    };
    enum class State : uint8_t { Header, Stored, Codes, Done };

    int bit();
    int bits(int n);
    int decode(const Huffman &h);
    bool build(Huffman &h, const uint8_t *lengths, int n);
    bool fixed_tables();
    bool dynamic_tables();
    void put(uint8_t b, uint8_t *out, size_t &done);

    Source source_ = nullptr;
    void *context_ = nullptr;
    uint8_t *window_ = nullptr;
    uint32_t wpos_ = 0;
    uint32_t bitbuf_ = 0;
    int bitcnt_ = 0;
    State state_ = State::Header;
    bool last_ = false;
    bool failed_ = false;
    uint32_t stored_left_ = 0;
    uint32_t copy_left_ = 0, copy_dist_ = 0;
    uint32_t total_ = 0; // bytes given out so far
    Huffman lit_, dist_;
};

// What each format implements.
class FormatDecoder {
public:
    virtual Error header(Info &info) = 0;
    // The next row into `rgb` (width x 3 bytes).
    virtual Error next_row(uint8_t *rgb) = 0;
    // A bounded piece of whatever has to be done before the next row can
    // be given; true once next_row() may be called. Most formats need none.
    virtual bool prepare() { return true; }
    // An animation: how long the picture just given stays, whether another
    // follows it, and on to that one (its rows then come from the top).
    virtual int frame_ms() { return 0; }
    virtual bool more() { return false; }
    virtual Error next() { return Error::Unsupported; }

protected:
    ~FormatDecoder() = default;
};

FormatDecoder *make_bmp(Decoder &d, Reader &r);
FormatDecoder *make_png(Decoder &d, Reader &r, const uint8_t background[3]);
FormatDecoder *make_jpeg(Decoder &d, Reader &r);
FormatDecoder *make_gif(Decoder &d, Reader &r, const uint8_t background[3]);

} // namespace image
