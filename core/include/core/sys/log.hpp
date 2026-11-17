// The system log: the last lines the machine wrote about itself, kept in
// memory for `dmesg` (backlog item 16; docs/journal/2026-11-17-dmesg.md).
//
// A ring of bytes the machine lends. Any task may write; a writer that
// finds the log busy drops its text rather than wait, as the debug link does
// with log lines, because whoever logs must never be held up by it. Not for
// interrupt handlers.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace sys {

class Log {
public:
    // Where the text is kept, and the lock for a machine with more than one
    // task. Without room nothing is kept.
    void set_room(char *room, size_t size, ports::Lock *lock = nullptr);
    bool kept() const { return size_ > 0; }

    void write(const char *text, size_t size);

    // Reading, a piece at a time. Positions count bytes since the machine
    // started: start at oldest() and call read() until it gives nothing.
    // Text that was pushed out meanwhile is skipped.
    uint64_t oldest();
    size_t read(uint64_t &from, char *out, size_t capacity);

    uint64_t written() const { return total_; }
    uint32_t dropped() const { return dropped_; } // writes that found it busy

private:
    char *room_ = nullptr;
    size_t size_ = 0;
    ports::Lock *lock_ = nullptr;
    uint64_t total_ = 0;
    uint32_t dropped_ = 0;
};

// The one log of the machine.
Log &log();

} // namespace sys
