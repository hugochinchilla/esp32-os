#include "core/www/http.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace www {

namespace {

// The value of a header line "Name: value" if it is that header's, else nullptr.
const char *header_value(const char *line, const char *name)
{
    const size_t length = strlen(name);
    if (strncasecmp(line, name, length) != 0 || line[length] != ':') {
        return nullptr;
    }
    const char *value = line + length + 1;
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    return value;
}

} // namespace

void Fetch::drop_connection()
{
    if (connection_ != 0) {
        wire_.close(connection_);
        connection_ = 0;
    }
}

void Fetch::stop()
{
    drop_connection();
    if (body_ != nullptr) {
        memory_.release(body_);
        body_ = nullptr;
    }
    size_ = capacity_ = 0;
    state_ = State::Idle;
}

void Fetch::fail(const char *format, const char *detail)
{
    stop();
    snprintf(error_, sizeof(error_), format, detail);
    state_ = State::Failed;
}

void Fetch::finish()
{
    drop_connection();
    state_ = State::Done;
}

bool Fetch::start(const char *url, uint32_t now_us)
{
    stop();
    error_[0] = 0;
    moves_ = 0;
    if (strlen(url) >= sizeof(url_)) {
        fail("The address is too long.");
        return false;
    }
    snprintf(url_, sizeof(url_), "%s", url);
    return begin(now_us);
}

bool Fetch::begin(uint32_t now_us)
{
    drop_connection();
    if (body_ != nullptr) {
        memory_.release(body_);
        body_ = nullptr;
    }
    size_ = capacity_ = 0;
    cut_ = false;
    line_size_ = 0;
    status_seen_ = false;
    status_ = 0;
    type_[0] = 0;
    location_[0] = 0;
    length_known_ = false;
    length_ = 0;
    chunked_ = false;
    chunk_ = Chunk::Size;
    chunk_left_ = 0;

    if (scheme_of(url_) == Scheme::Https) {
        fail("That is a secure (https) address, and this browser cannot encrypt.");
        return false;
    }
    int port = 80;
    const char *path = nullptr;
    if (!split_http(url_, host_, sizeof(host_), port, path)) {
        fail("That is not an address this browser can open.");
        return false;
    }
    // The request. Bytes that cannot be in an address go as %XX.
    size_t n = static_cast<size_t>(snprintf(request_, sizeof(request_), "GET "));
    if (*path != '/') {
        request_[n++] = '/';
    }
    static const char kHex[] = "0123456789ABCDEF";
    for (const char *p = path; *p != 0 && n + 4 < kMaxUrl + 8; p++) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c <= ' ' || c >= 0x7F) {
            request_[n++] = '%';
            request_[n++] = kHex[c >> 4];
            request_[n++] = kHex[c & 15];
        } else {
            request_[n++] = static_cast<char>(c);
        }
    }
    char host_line[100];
    if (port == 80) {
        snprintf(host_line, sizeof(host_line), "%s", host_);
    } else {
        snprintf(host_line, sizeof(host_line), "%s:%d", host_, port);
    }
    n += static_cast<size_t>(snprintf(request_ + n, sizeof(request_) - n,
                                      " HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n"
                                      "Accept: text/html, text/plain, */*\r\nConnection: close\r\n\r\n",
                                      host_line, kAgent));
    request_size_ = n;
    request_sent_ = 0;

    connection_ = wire_.connect(host_, port);
    if (connection_ == 0) {
        fail("There is no connection free to open %s with. Try again in a moment.", host_);
        return false;
    }
    heard_us_ = now_us;
    state_ = State::Connecting;
    return true;
}

Fetch::State Fetch::poll(uint32_t now_us)
{
    if (!busy()) {
        return state_;
    }
    if (state_ == State::Connecting) {
        const Wire::State s = wire_.state(connection_);
        if (s == Wire::State::Failed) {
            char text[160];
            snprintf(text, sizeof(text), "Could not reach %.60s: %.70s.", host_, wire_.why(connection_));
            fail("%s", text);
            return state_;
        }
        if (s == Wire::State::Open) {
            state_ = State::Sending;
            heard_us_ = now_us;
        }
    }
    if (state_ == State::Sending) {
        const int done = wire_.write(connection_, reinterpret_cast<const uint8_t *>(request_) + request_sent_,
                                     request_size_ - request_sent_);
        if (done < 0) {
            fail("The connection to %s was lost.", host_);
            return state_;
        }
        if (done > 0) {
            request_sent_ += static_cast<size_t>(done);
            heard_us_ = now_us;
        }
        if (request_sent_ == request_size_) {
            state_ = State::Waiting;
        }
    }
    // A turn reads a few buffers and leaves the rest for the next: the
    // desktop is waiting too.
    for (int round = 0; round < 8 && (state_ == State::Waiting || state_ == State::Receiving); round++) {
        uint8_t buffer[1024];
        const int got = wire_.read(connection_, buffer, sizeof(buffer));
        if (got == 0) {
            break;
        }
        if (got < 0) {
            if (state_ == State::Waiting) {
                fail("%s closed the connection without an answer.", host_);
            } else {
                finish(); // the server closing is how a page without a length ends
            }
            return state_;
        }
        heard_us_ = now_us;
        const uint8_t *data = buffer;
        size_t left = static_cast<size_t>(got);
        // The first lines, a byte at a time; then the page.
        while (left > 0 && state_ == State::Waiting) {
            const char c = static_cast<char>(*data++);
            left--;
            if (c != '\n') {
                if (line_size_ + 1 < sizeof(line_)) {
                    line_[line_size_++] = c;
                }
                continue;
            }
            while (line_size_ > 0 && line_[line_size_ - 1] == '\r') {
                line_size_--;
            }
            line_[line_size_] = 0;
            const size_t length = line_size_;
            line_size_ = 0;
            if (!status_seen_ && strncmp(line_, "HTTP/", 5) != 0) {
                // No status line: an answer of the very first kind, all page.
                status_seen_ = true;
                status_ = 200;
                snprintf(type_, sizeof(type_), "text/html");
                state_ = State::Receiving;
                line_[length] = '\n';
                body_bytes(reinterpret_cast<const uint8_t *>(line_), length + 1);
                break;
            }
            if (length == 0) {
                if (!headers_done(now_us)) {
                    return state_; // moved, failed, or nothing follows
                }
            } else {
                header_line(line_);
            }
        }
        if (left > 0 && state_ == State::Receiving) {
            body_bytes(data, left);
        }
    }
    if (busy() && now_us - heard_us_ > kQuietUs) {
        fail("%s did not answer in time.", host_);
    }
    return state_;
}

void Fetch::header_line(char *line)
{
    if (!status_seen_) {
        status_seen_ = true;
        const char *space = strchr(line, ' ');
        status_ = space != nullptr ? atoi(space + 1) : 0;
        return;
    }
    if (const char *value = header_value(line, "Content-Type")) {
        snprintf(type_, sizeof(type_), "%s", value);
    } else if (const char *value = header_value(line, "Content-Length")) {
        length_known_ = isdigit(static_cast<unsigned char>(*value)) != 0;
        length_ = static_cast<size_t>(strtoul(value, nullptr, 10));
    } else if (const char *value = header_value(line, "Location")) {
        snprintf(location_, sizeof(location_), "%s", value);
    } else if (const char *value = header_value(line, "Transfer-Encoding")) {
        chunked_ = strncasecmp(value, "chunked", 7) == 0;
    }
}

// The blank line after the headers. True: the page follows.
bool Fetch::headers_done(uint32_t now_us)
{
    if (status_ >= 100 && status_ < 200) {
        status_seen_ = false; // "100 Continue": the real answer comes next
        return true;
    }
    if (status_ >= 300 && status_ < 400 && status_ != 304 && location_[0] != 0) {
        char to[kMaxUrl];
        if (!resolve(url_, location_, to, sizeof(to))) {
            fail("%s sent the browser to an address it cannot use.", host_);
            return false;
        }
        if (++moves_ > kMaxMoves) {
            fail("%s keeps sending the browser somewhere else.", host_);
            return false;
        }
        snprintf(url_, sizeof(url_), "%s", to);
        if (scheme_of(url_) == Scheme::Https) {
            fail("%s only answers at a secure (https) address, and this browser cannot encrypt.", host_);
            return false;
        }
        begin(now_us);
        return false;
    }
    state_ = State::Receiving;
    if (status_ == 204 || status_ == 304 || (length_known_ && length_ == 0 && !chunked_)) {
        finish();
        return false;
    }
    return true;
}

// Keeps bytes of the page. False: no more are wanted.
bool Fetch::take(const uint8_t *data, size_t size)
{
    if (size_ + size > kMaxBody) {
        size = kMaxBody - size_;
        cut_ = true;
    }
    if (size_ + size > capacity_) {
        size_t wanted = capacity_ == 0 ? 8192 : capacity_ * 2;
        if (capacity_ == 0 && length_known_ && !chunked_ && length_ > wanted) {
            wanted = length_;
        }
        while (wanted < size_ + size) {
            wanted *= 2;
        }
        if (wanted > kMaxBody) {
            wanted = kMaxBody;
        }
        uint8_t *bigger = static_cast<uint8_t *>(memory_.allocate(wanted));
        if (bigger == nullptr) {
            fail("There is not enough memory for this page.");
            return false;
        }
        if (body_ != nullptr) {
            memcpy(bigger, body_, size_);
            memory_.release(body_);
        }
        body_ = bigger;
        capacity_ = wanted;
    }
    memcpy(body_ + size_, data, size);
    size_ += size;
    if (cut_) {
        finish();
        return false;
    }
    return true;
}

void Fetch::body_bytes(const uint8_t *data, size_t size)
{
    if (!chunked_) {
        if (length_known_ && size_ + size >= length_) {
            if (take(data, length_ - size_) && state_ == State::Receiving) {
                finish();
            }
            return;
        }
        take(data, size);
        return;
    }
    // Chunks: a length in hex on a line, that many bytes, a line end; a
    // length of zero ends the page.
    while (size > 0 && state_ == State::Receiving) {
        switch (chunk_) {
        case Chunk::Size:
        case Chunk::Trailer:
            {
                const char c = static_cast<char>(*data++);
                size--;
                if (c != '\n') {
                    if (line_size_ + 1 < sizeof(line_)) {
                        line_[line_size_++] = c;
                    }
                    break;
                }
                while (line_size_ > 0 && line_[line_size_ - 1] == '\r') {
                    line_size_--;
                }
                line_[line_size_] = 0;
                const bool blank = line_size_ == 0;
                line_size_ = 0;
                if (chunk_ == Chunk::Trailer) {
                    if (blank) {
                        finish();
                    }
                    break;
                }
                if (blank) {
                    break;
                }
                chunk_left_ = static_cast<size_t>(strtoul(line_, nullptr, 16));
                chunk_ = chunk_left_ == 0 ? Chunk::Trailer : Chunk::Data;
            }
            break;
        case Chunk::Data:
            {
                const size_t part = size < chunk_left_ ? size : chunk_left_;
                if (!take(data, part)) {
                    return;
                }
                data += part;
                size -= part;
                chunk_left_ -= part;
                if (chunk_left_ == 0) {
                    chunk_ = Chunk::Size; // the line end after the data reads as a blank line
                }
            }
            break;
        case Chunk::End: break;
        }
    }
}

} // namespace www
