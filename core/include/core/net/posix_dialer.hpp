// Outgoing connections through the POSIX socket calls: the PC's, and lwIP's
// on the board (ESP-IDF offers the same calls). Only POSIX headers.
#pragma once

#include <atomic>
#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/net/dialer.hpp"

namespace net {

class PosixDialer : public Dialer {
public:
    static constexpr int kLines = 4;
    static constexpr int kConnectSeconds = 10;

    ~PosixDialer()
    {
        for (int i = 0; i < kLines; i++) {
            hang_up(i);
        }
    }

    // Waits: for the name service, then for the host, kConnectSeconds at most.
    int connect(const char *host, int port) override
    {
        // A line is claimed by its flag alone, so two jobs can each take one.
        int line = -1;
        for (int i = 0; i < kLines && line < 0; i++) {
            bool free = false;
            if (lines_[i].used.compare_exchange_strong(free, true)) {
                line = i;
            }
        }
        if (line < 0) {
            return -static_cast<int>(DialFailure::NoLines);
        }
        DialFailure failure = DialFailure::NotFound;
        const int fd = open(host, port, failure);
        if (fd < 0) {
            lines_[line].used = false;
            return -static_cast<int>(failure);
        }
        lines_[line].fd = fd;
        return line;
    }

    int read(int line, uint8_t *out, size_t capacity) override
    {
        const int fd = fd_of(line);
        if (fd < 0) {
            return -1;
        }
        const int got = static_cast<int>(::recv(fd, out, capacity, 0));
        if (got > 0) {
            return got;
        }
        return got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    }

    int write(int line, const uint8_t *data, size_t size) override
    {
        const int fd = fd_of(line);
        if (fd < 0) {
            return -1;
        }
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL; // a server that left is an error, not a signal
#else
        const int flags = 0;
#endif
        const int done = static_cast<int>(::send(fd, data, size, flags));
        if (done >= 0) {
            return done;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
    }

    void hang_up(int line) override
    {
        if (line < 0 || line >= kLines || !lines_[line].used) {
            return;
        }
        if (lines_[line].fd >= 0) {
            ::close(lines_[line].fd);
            lines_[line].fd = -1;
        }
        lines_[line].used = false;
    }

    int lines_in_use() const
    {
        int count = 0;
        for (int i = 0; i < kLines; i++) {
            count += lines_[i].used ? 1 : 0;
        }
        return count;
    }

private:
    struct Line {
        std::atomic<bool> used{false};
        int fd = -1;
    };

    int fd_of(int line) const { return line >= 0 && line < kLines && lines_[line].used ? lines_[line].fd : -1; }

    // A connected socket that never waits, or -1 and why.
    static int open(const char *host, int port, DialFailure &failure)
    {
        char service[8];
        snprintf(service, sizeof(service), "%d", port);
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *found = nullptr;
        if (getaddrinfo(host, service, &hints, &found) != 0 || found == nullptr) {
            failure = DialFailure::NotFound;
            return -1;
        }
        failure = DialFailure::TimedOut;
        int fd = -1;
        for (const addrinfo *a = found; a != nullptr && fd < 0; a = a->ai_next) {
            fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0) {
                continue;
            }
            // Without waiting from the start, so that connecting has a limit.
            const int flags = fcntl(fd, F_GETFL, 0);
            bool ok = flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
            if (ok && ::connect(fd, a->ai_addr, a->ai_addrlen) != 0) {
                ok = false;
                if (errno == EINPROGRESS) {
                    fd_set writable;
                    FD_ZERO(&writable);
                    FD_SET(fd, &writable);
                    timeval limit = {kConnectSeconds, 0};
                    if (select(fd + 1, nullptr, &writable, nullptr, &limit) > 0) {
                        int error = 0;
                        socklen_t size = sizeof(error);
                        ok = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0;
                        if (!ok && error == ECONNREFUSED) {
                            failure = DialFailure::Refused;
                        }
                    }
                } else if (errno == ECONNREFUSED) {
                    failure = DialFailure::Refused;
                }
            }
            if (!ok) {
                ::close(fd);
                fd = -1;
            }
        }
        freeaddrinfo(found);
        return fd;
    }

    Line lines_[kLines];
};

} // namespace net
