#include "core/sys/log.hpp"

#include <cstring>

namespace sys {

Log &log()
{
    static Log the_log;
    return the_log;
}

void Log::set_room(char *room, size_t size, ports::Lock *lock)
{
    room_ = room;
    size_ = room != nullptr ? size : 0;
    lock_ = lock;
    total_ = 0;
}

void Log::write(const char *text, size_t size)
{
    if (size_ == 0 || size == 0) {
        return;
    }
    if (lock_ != nullptr && !lock_->try_lock()) {
        dropped_++;
        return;
    }
    if (size > size_) { // only its end fits
        text += size - size_;
        total_ += size - size_;
        size = size_;
    }
    const size_t at = static_cast<size_t>(total_ % size_);
    const size_t first = size < size_ - at ? size : size_ - at;
    memcpy(room_ + at, text, first);
    memcpy(room_, text + first, size - first);
    total_ += size;
    if (lock_ != nullptr) {
        lock_->unlock();
    }
}

uint64_t Log::oldest()
{
    if (lock_ != nullptr) {
        lock_->lock();
    }
    uint64_t from = total_ > size_ ? total_ - size_ : 0;
    if (from > 0) {
        // The ring has gone round: the oldest text starts in the middle of
        // a line. Begin at the first whole one.
        while (from < total_ && room_[from % size_] != '\n') {
            from++;
        }
        if (from < total_) {
            from++;
        }
    }
    if (lock_ != nullptr) {
        lock_->unlock();
    }
    return from;
}

size_t Log::read(uint64_t &from, char *out, size_t capacity)
{
    if (size_ == 0 || capacity == 0) {
        return 0;
    }
    if (lock_ != nullptr) {
        lock_->lock();
    }
    const uint64_t first = total_ > size_ ? total_ - size_ : 0;
    if (from < first) {
        from = first;
    }
    size_t n = 0;
    while (n < capacity && from < total_) {
        out[n++] = room_[from % size_];
        from++;
    }
    if (lock_ != nullptr) {
        lock_->unlock();
    }
    return n;
}

} // namespace sys
