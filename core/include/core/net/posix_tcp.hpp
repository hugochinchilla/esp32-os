// A listening TCP port through the POSIX socket calls: the PC's in the
// simulator, lwIP's on the board (ESP-IDF offers the same calls). Only POSIX
// headers, no ESP-IDF ones. One connection at a time: a new one replaces the
// old. Nothing here waits.
#pragma once

#include <cerrno>

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/net/stream.hpp"

namespace net {

class PosixTcpListener : public Listener, private Stream {
public:
    ~PosixTcpListener() { stop(); }

    // Starts listening (on every address of the machine, or only on this
    // machine itself with `local_only`). False if the port cannot be had.
    bool listen(int port, bool local_only = false)
    {
        stop();
        listening_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listening_ < 0) {
            return false;
        }
        int yes = 1;
        setsockopt(listening_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        address.sin_addr.s_addr = htonl(local_only ? INADDR_LOOPBACK : INADDR_ANY);
        if (::bind(listening_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
            ::listen(listening_, 1) != 0 || !no_waiting(listening_)) {
            stop();
            return false;
        }
        return true;
    }

    void stop()
    {
        close();
        if (listening_ >= 0) {
            ::close(listening_);
            listening_ = -1;
        }
    }

    bool listening() const { return listening_ >= 0; }

    Stream *accept() override
    {
        if (listening_ < 0) {
            return nullptr;
        }
        const int fd = ::accept(listening_, nullptr, nullptr);
        if (fd < 0) {
            return nullptr;
        }
        close();
        int yes = 1;
        // Small packets (a key, a small update) go out at once.
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        if (!no_waiting(fd)) {
            ::close(fd);
            return nullptr;
        }
        connection_ = fd;
        return this;
    }

private:
    static bool no_waiting(int fd)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    int read(uint8_t *out, size_t capacity) override
    {
        if (connection_ < 0) {
            return -1;
        }
        const int got = static_cast<int>(::recv(connection_, out, capacity, 0));
        if (got > 0) {
            return got;
        }
        return got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    }

    int write(const uint8_t *data, size_t size) override
    {
        if (connection_ < 0) {
            return -1;
        }
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL; // a viewer that left is an error, not a signal
#else
        const int flags = 0;
#endif
        const int done = static_cast<int>(::send(connection_, data, size, flags));
        if (done >= 0) {
            return done;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    }

    void close() override
    {
        if (connection_ >= 0) {
            ::close(connection_);
            connection_ = -1;
        }
    }

    int listening_ = -1;
    int connection_ = -1;
};

} // namespace net
