// Outgoing connections, as the core sees them: the port a machine fills in
// so that a program can reach a server (the browser, a web page).
//
// The listening side is net::Listener (core/net/stream.hpp). This is the
// other way round, and one thing about it is different: finding a host by
// its name and connecting to it can take seconds, so connect() may wait.
// The system therefore calls it from a background job, never from the UI
// task (core/os/tcp.hpp). Reading and writing never wait, as with a Stream.
//
// Adapters: net::PosixDialer (core/net/posix_dialer.hpp), the sockets of the
// PC and of the board; and in the web page a relay, since a page cannot open
// a connection of its own (web/README.md).
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

enum class DialFailure : uint8_t {
    None,
    NoLines,   // every line is in use
    NoNetwork, // the machine has no way out
    NotFound,  // no host has that name (or the name service did not answer)
    Refused,   // the host is there and nothing listens on that port
    TimedOut,  // no answer
};

// In words that end "Could not reach <host>: ".
inline const char *dial_failure_text(DialFailure failure)
{
    switch (failure) {
    case DialFailure::NoLines: return "too many connections are open";
    case DialFailure::NoNetwork: return "this machine is not connected to a network";
    case DialFailure::NotFound: return "no site with that name was found (is the network connected?)";
    case DialFailure::Refused: return "it refused the connection";
    case DialFailure::TimedOut: return "it did not answer";
    case DialFailure::None: break;
    }
    return "the connection failed";
}

class Dialer {
public:
    // Finds the host and connects to it. May wait for seconds: called off
    // the UI task where there are tasks. Returns the line's number (0 or
    // more), or minus a DialFailure.
    virtual int connect(const char *host, int port) = 0;
    // As net::Stream: bytes done, 0 for none now, -1 when the line has ended.
    virtual int read(int line, uint8_t *out, size_t capacity) = 0;
    virtual int write(int line, const uint8_t *data, size_t size) = 0;
    virtual void hang_up(int line) = 0;

protected:
    ~Dialer() = default;
};

} // namespace net
