#include <cstring>

#include "internal.hpp"

namespace image {

namespace {

// The reader lives here, next to the pool, so a Decoder needs nothing else.
Reader g_reader; // ponytail: one picture is decoded at a time (one UI task)

} // namespace

const char *error_text(Error e)
{
    switch (e) {
    case Error::None:
        return "no error";
    case Error::NotFound:
        return "the file could not be opened";
    case Error::Unsupported:
        return "this kind of picture is not supported yet";
    case Error::TooLarge:
        return "the picture is too large";
    case Error::Malformed:
        return "the picture is damaged or is not a picture";
    default:
        return "there is not enough memory";
    }
}

void *Decoder::allocate(size_t bytes)
{
    bytes = (bytes + 7) & ~static_cast<size_t>(7);
    if (pool_ == nullptr || used_ + bytes > kPoolBytes) {
        return nullptr;
    }
    void *p = pool_ + used_;
    used_ += bytes;
    return p;
}

void Decoder::release(ports::Memory &memory)
{
    close();
    if (pool_ != nullptr) {
        memory.release(pool_);
        pool_ = nullptr;
    }
}

Error Decoder::open(const fs::Storage &storage, const char *path, ports::Memory &memory, Info &info,
                    const uint8_t background[3])
{
    close();
    own_.storage = &storage;
    if (storage.open(path, ports::OpenMode::Read, own_.file) != ports::FsError::Ok) {
        return Error::NotFound;
    }
    return open(own_, memory, info, background);
}

Error Decoder::open(Source &source, ports::Memory &memory, Info &info, const uint8_t background[3])
{
    if (&source != &own_) {
        close();
    }
    if (pool_ == nullptr) {
        pool_ = static_cast<uint8_t *>(memory.allocate(kPoolBytes));
        if (pool_ == nullptr) {
            close();
            return Error::NoMemory;
        }
    }
    used_ = 0;
    g_reader.attach(source);
    const int a = g_reader.byte(), b = g_reader.byte();
    g_reader.seek(0);
    static const uint8_t white[3] = {255, 255, 255};
    if (a == 'B' && b == 'M') {
        format_ = make_bmp(*this, g_reader);
    } else if (a == 0x89 && b == 'P') {
        format_ = make_png(*this, g_reader, background != nullptr ? background : white);
    } else if (a == 0xFF && b == 0xD8) {
        format_ = make_jpeg(*this, g_reader);
    } else if (a == 'G' && b == 'I') {
        format_ = make_gif(*this, g_reader, background != nullptr ? background : white);
    } else {
        close();
        return a < 0 ? Error::Malformed : Error::Unsupported;
    }
    if (format_ == nullptr) {
        close();
        return Error::NoMemory;
    }
    info_ = Info();
    Error e = format_->header(info_);
    if (e == Error::None) {
        rgb_ = static_cast<uint8_t *>(allocate(static_cast<size_t>(info_.width) * 3));
        if (rgb_ == nullptr) {
            e = Error::TooLarge;
        }
    }
    if (e != Error::None) {
        close();
        return e;
    }
    info = info_;
    next_row_ = 0;
    done_ = false;
    return Error::None;
}

Error Decoder::step(RowSink &sink, int rows)
{
    for (int i = 0; i < rows && !done_; i++) {
        if (!format_->prepare()) {
            continue; // not a row yet, but a turn of work all the same
        }
        const Error e = format_->next_row(rgb_);
        if (e != Error::None) {
            done_ = true;
            return e;
        }
        sink.row(next_row_, rgb_);
        if (++next_row_ == info_.height) {
            done_ = true;
        }
    }
    return Error::None;
}

void Decoder::close()
{
    if (own_.file.open() && own_.storage != nullptr) {
        own_.storage->close(own_.file);
    }
    format_ = nullptr; // in the pool: nothing to free
    done_ = true;
}

} // namespace image
