// Network connections as the core sees them: a stream of bytes that never
// waits, and something that hands out new ones (a listening TCP port).
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

class Stream {
public:
    // Both return at once. read(): bytes read, 0 if none are waiting, -1 if
    // the other side has closed or the connection broke. write(): bytes
    // taken (maybe fewer than asked, 0 if none fit now), -1 if broken.
    virtual int read(uint8_t *out, size_t capacity) = 0;
    virtual int write(const uint8_t *data, size_t size) = 0;
    virtual void close() = 0;

protected:
    ~Stream() = default;
};

class Listener {
public:
    // A new connection, or nullptr when nobody is waiting. The stream
    // belongs to the listener and is valid until its close(), or until
    // accept() gives the next one, which ends it: one connection at a time.
    virtual Stream *accept() = 0;

protected:
    ~Listener() = default;
};

} // namespace net
