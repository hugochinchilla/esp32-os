// Network connections as the core sees them: a stream of bytes that never
// waits, and something that hands out new ones (a listening TCP port).
//
// Nothing here waits, so somebody has to know when to come back. The
// services task (core/net/services.hpp) asks each stream and listener what
// it is waiting for (handle(), watch()) and sleeps on all of it at once.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

// Collects what a sleep should end on: connections, by the number the
// machine knows each by (a socket), and whether to wake when bytes have
// arrived on it (readable) or when it can take more (writable).
class Watch {
public:
    virtual void add(int handle, bool readable, bool writable) = 0;

protected:
    ~Watch() = default;
};

class Stream {
public:
    // Both return at once. read(): bytes read, 0 if none are waiting, -1 if
    // the other side has closed or the connection broke. write(): bytes
    // taken (maybe fewer than asked, 0 if none fit now), -1 if broken.
    virtual int read(uint8_t *out, size_t capacity) = 0;
    virtual int write(const uint8_t *data, size_t size) = 0;
    virtual void close() = 0;
    // The connection's number for a Watch, -1 if it has none (a stream that
    // is not a socket, as in the tests): then it can only be looked at by
    // the clock.
    virtual int handle() const { return -1; }

protected:
    ~Stream() = default;
};

class Listener {
public:
    // A new connection, or nullptr when nobody is waiting. The stream
    // belongs to the listener and is valid until its close(). A listener
    // with one connection at a time gives the same object again for the
    // next one, which ends the earlier connection.
    virtual Stream *accept() = 0;
    // True while it has work of its own under way that needs accept() called
    // often (a web page half sent).
    virtual bool busy() const { return false; }
    // Adds everything that should bring accept() back: someone knocking, a
    // request half read, an answer half sent. False if it cannot say (not
    // sockets): then accept() has to be called by the clock, as busy() asks.
    virtual bool watch(Watch & /*watch*/) const { return false; }

protected:
    ~Listener() = default;
};

} // namespace net
