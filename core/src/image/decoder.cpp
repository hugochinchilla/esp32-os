#include <cstring>
#include <new>

#include "internal.hpp"

namespace image {

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
    if (pool_ == nullptr || bytes > pool_bytes_ || used_ > pool_bytes_ - bytes) {
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

void Decoder::use_pool(ports::Memory &memory, size_t bytes)
{
    if (bytes != pool_bytes_) {
        release(memory);
        pool_bytes_ = bytes;
    }
}

int Decoder::frame_ms() const
{
    return format_ != nullptr ? format_->frame_ms() : 0;
}

bool Decoder::more_frames() const
{
    return format_ != nullptr && done_ && next_row_ == info_.height && format_->more();
}

Error Decoder::next_frame()
{
    if (!more_frames()) {
        return Error::Malformed;
    }
    const Error e = format_->next();
    if (e == Error::None) {
        next_row_ = 0;
        done_ = false;
    }
    return e;
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
        pool_ = static_cast<uint8_t *>(memory.allocate(pool_bytes_));
        if (pool_ == nullptr) {
            close();
            return Error::NoMemory;
        }
    }
    used_ = 0;
    void *place = allocate(sizeof(Reader));
    if (place == nullptr) {
        close();
        return Error::NoMemory;
    }
    reader_ = new (place) Reader;
    Reader &r = *reader_;
    r.attach(source);
    r.seek(0); // the source may have been read before
    const int a = r.byte(), b = r.byte();
    r.seek(0);
    static const uint8_t white[3] = {255, 255, 255};
    if (a == 'B' && b == 'M') {
        format_ = make_bmp(*this, r);
    } else if (a == 0x89 && b == 'P') {
        format_ = make_png(*this, r, background != nullptr ? background : white);
    } else if (a == 0xFF && b == 0xD8) {
        format_ = make_jpeg(*this, r);
    } else if (a == 'G' && b == 'I') {
        format_ = make_gif(*this, r, background != nullptr ? background : white);
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
