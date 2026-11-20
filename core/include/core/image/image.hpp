// Picture decoding: BMP, PNG, baseline JPEG and GIF, written for this project.
//
// A picture is decoded a few rows at a time and handed over row by row, top
// to bottom, as 8-bit RGB, so the whole decoded picture never has to be in
// memory. All working memory comes from one pool taken once from the Memory
// port (external RAM on the board). Integer arithmetic only, so the board
// and the PC decode to exactly the same pixels. Bad files give an error,
// never a crash.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/ports.hpp"

namespace image {

enum class Format : uint8_t { Unknown, Bmp, Png, Jpeg, Gif };

enum class Error : uint8_t {
    None,
    NotFound,    // the file could not be opened
    Unsupported, // a kind of picture not handled (interlaced PNG, progressive JPEG...)
    TooLarge,    // too wide for the working memory
    Malformed,   // damaged or not a picture
    NoMemory,    // the working memory could not be had
};

const char *error_text(Error e);

struct Info {
    int width = 0;
    int height = 0;
    Format format = Format::Unknown;
    char detail[40] = ""; // "PNG, 8-bit RGB"
};

// Receives the decoded rows.
class RowSink {
public:
    // `rgb` holds width x 3 bytes; rows come in order from 0.
    virtual void row(int y, const uint8_t *rgb) = 0;

protected:
    ~RowSink() = default;
};

// Limits, set by the size of the working memory.
constexpr int kMaxWidth = 4096;
constexpr int kMaxJpegWidth = 2048;
constexpr int kMaxHeight = 16384;
constexpr size_t kPoolBytes = 384 * 1024;

class FormatDecoder; // one per format, inside

// Where a picture's bytes come from: something that is read in order and
// can be put at a place. A program hands in one over a file it has opened
// through the OS calls; open() with a path makes one over the drives.
class Source {
public:
    // Up to `size` bytes; `got` 0 at the end. False if it could not be read.
    virtual bool read(void *data, size_t size, size_t &got) = 0;
    virtual bool seek(uint32_t offset) = 0;

protected:
    ~Source() = default;
};

class Decoder {
public:
    // Opens a picture and reads its header. Transparent parts of PNGs and
    // GIFs are shown over `background` (RGB).
    Error open(const fs::Storage &storage, const char *path, ports::Memory &memory, Info &info,
               const uint8_t background[3] = nullptr);
    // The same from a source the caller keeps until close(): the decoder
    // reads from it and does not close it.
    Error open(Source &source, ports::Memory &memory, Info &info, const uint8_t background[3] = nullptr);
    // Decodes up to `rows` more rows into `sink`. Error::None until done or
    // failed; check done(). It may give fewer rows, or none, while work is
    // done that has to come before them (an interlaced GIF).
    Error step(RowSink &sink, int rows);
    bool done() const { return done_; }
    int rows_done() const { return next_row_; }
    void close();
    // Closes, and gives the working memory back to where open() got it.
    void release(ports::Memory &memory);
    ~Decoder() { close(); }

    // Working memory, for the format decoders: a bump allocator over the pool.
    void *allocate(size_t bytes);

private:
    // A file on the drives, for open() with a path.
    class OwnFile : public Source {
    public:
        bool read(void *data, size_t size, size_t &got) override
        {
            return storage->read(file, data, size, got) == ports::FsError::Ok;
        }
        bool seek(uint32_t offset) override { return storage->seek(file, offset) == ports::FsError::Ok; }
        fs::File file;
        const fs::Storage *storage = nullptr;
    };
    OwnFile own_;
    uint8_t *pool_ = nullptr;
    size_t used_ = 0;
    FormatDecoder *format_ = nullptr;
    uint8_t *rgb_ = nullptr; // one decoded row
    Info info_;
    int next_row_ = 0;
    bool done_ = true;
};

} // namespace image
