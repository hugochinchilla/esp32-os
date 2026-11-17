#include "core/net/web.hpp"

#include <cstdio>
#include <cstring>

namespace net {

namespace {

/* ---- SHA-1 and base64, only for the WebSocket greeting ------------------------- */

uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

void sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = static_cast<uint32_t>(p[i * 4]) << 24 | static_cast<uint32_t>(p[i * 4 + 1]) << 16 |
               static_cast<uint32_t>(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        const uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

// For short texts (under 120 bytes, enough for a key and the GUID).
void sha1(const uint8_t *data, size_t size, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t padded[128] = {};
    memcpy(padded, data, size);
    padded[size] = 0x80;
    const size_t total = size + 9 <= 64 ? 64 : 128;
    const uint64_t bits = static_cast<uint64_t>(size) * 8;
    for (int i = 0; i < 8; i++) {
        padded[total - 1 - static_cast<size_t>(i)] = static_cast<uint8_t>(bits >> (8 * i));
    }
    for (size_t at = 0; at < total; at += 64) {
        sha1_block(h, padded + at);
    }
    for (int i = 0; i < 5; i++) {
        out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
}

void base64(const uint8_t *data, size_t size, char *out)
{
    static const char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const uint32_t v = a << 16 | b << 8 | c;
        out[o++] = kDigits[v >> 18];
        out[o++] = kDigits[(v >> 12) & 63];
        out[o++] = i + 1 < size ? kDigits[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < size ? kDigits[v & 63] : '=';
    }
    out[o] = 0;
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

// Does `line` start with `name` (given in small letters), whatever the case?
bool starts(const char *line, const char *name)
{
    for (; *name != 0; line++, name++) {
        if (lower(*line) != *name) {
            return false;
        }
    }
    return true;
}

bool contains(const char *text, const char *word)
{
    for (; *text != 0; text++) {
        if (starts(text, word)) {
            return true;
        }
    }
    return false;
}

const char kNotFound[] = "Not here. The remote screen is at /\n";
const char kNoPage[] = "This board has no viewer page.\n";

} // namespace

void websocket_accept_key(const char *key, char out[29])
{
    static const char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t text[120];
    size_t size = strlen(key);
    if (size > sizeof(text) - (sizeof(kGuid) - 1)) {
        size = sizeof(text) - (sizeof(kGuid) - 1);
    }
    memcpy(text, key, size);
    memcpy(text + size, kGuid, sizeof(kGuid) - 1);
    uint8_t digest[20];
    sha1(text, size + sizeof(kGuid) - 1, digest);
    base64(digest, sizeof(digest), out);
}

/* ---- Requests -------------------------------------------------------------------- */

Stream *WebGate::accept()
{
    if (port_ == nullptr) {
        return nullptr;
    }
    take_new();
    bool opened = false;
    for (Request &r : requests_) {
        if (r.stream == nullptr) {
            continue;
        }
        if (!r.answering) {
            if (!read_request(r)) {
                end(r);
                continue;
            }
            if (!r.answering) {
                continue; // the request has not all arrived
            }
            if (answer(r)) {
                opened = true;
                continue;
            }
        }
        send_answer(r);
    }
    return opened ? this : nullptr;
}

bool WebGate::watch(Watch &watch) const
{
    if (port_ == nullptr) {
        return true; // nothing can come
    }
    if (!port_->watch(watch)) {
        return false;
    }
    for (const Request &r : requests_) {
        if (r.stream == nullptr) {
            continue;
        }
        const int handle = r.stream->handle();
        if (handle < 0) {
            return false;
        }
        watch.add(handle, !r.answering, r.answering);
    }
    return true;
}

void WebGate::take_new()
{
    while (Stream *s = port_->accept()) {
        Request *slot = nullptr;
        for (Request &r : requests_) {
            if (r.stream == nullptr) {
                slot = &r;
                break;
            }
            // All in use: the oldest goes (a browser's spare connection that
            // never asked for anything, most of the time).
            if (slot == nullptr || r.age < slot->age) {
                slot = &r;
            }
        }
        if (slot->stream != nullptr) {
            end(*slot);
        }
        *slot = {};
        slot->stream = s;
        slot->age = next_age_++;
    }
}

void WebGate::end(Request &r)
{
    if (r.stream != nullptr) {
        r.stream->close();
    }
    r = {};
}

// Reads the request a byte at a time, so nothing after its last line is
// taken: on a WebSocket those bytes belong to the frames. Sets `answering`
// when the empty line that ends the request has come.
bool WebGate::read_request(Request &r)
{
    for (int budget = 0; budget < 2048; budget++) {
        uint8_t byte;
        const int got = r.stream->read(&byte, 1);
        if (got < 0) {
            return false;
        }
        if (got == 0) {
            return true;
        }
        if (byte != '\n') {
            if (r.line_size < sizeof(r.line) - 1) {
                r.line[r.line_size++] = static_cast<char>(byte);
            } else {
                r.line_cut = true;
            }
            continue;
        }
        if (r.line_size > 0 && r.line[r.line_size - 1] == '\r') {
            r.line_size--;
        }
        r.line[r.line_size] = 0;
        if (r.line_size == 0 && !r.line_cut) {
            if (!r.first_line_seen) {
                return false; // an empty request
            }
            r.answering = true;
            return true;
        }
        header_line(r);
        r.line_size = 0;
        r.line_cut = false;
    }
    return true;
}

void WebGate::header_line(Request &r)
{
    if (!r.first_line_seen) {
        r.first_line_seen = true;
        r.is_get = strncmp(r.line, "GET ", 4) == 0;
        const char *path = r.line + 4;
        r.root = r.is_get && path[0] == '/' && (path[1] == ' ' || path[1] == '?' || strncmp(path, "/index.html", 11) == 0);
        if (r.is_get && second_path_ != nullptr) {
            const size_t size = strlen(second_path_);
            const char after = strncmp(path, second_path_, size) == 0 ? path[size] : 'x';
            r.second = after == ' ' || after == '?' || after == '/';
        }
        return;
    }
    if (starts(r.line, "upgrade:")) {
        r.upgrade = contains(r.line + 8, "websocket");
    } else if (starts(r.line, "sec-websocket-protocol:")) {
        r.binary = contains(r.line + 23, "binary");
    } else if (starts(r.line, "sec-websocket-key:") && !r.line_cut) {
        const char *value = r.line + 18;
        while (*value == ' ' || *value == '\t') {
            value++;
        }
        size_t size = strlen(value);
        while (size > 0 && (value[size - 1] == ' ' || value[size - 1] == '\t')) {
            size--;
        }
        if (size < sizeof(r.key)) {
            memcpy(r.key, value, size);
            r.key[size] = 0;
        }
    }
}

// The whole request is here: decide the answer. True if it became the
// WebSocket (the request's place is then free again).
bool WebGate::answer(Request &r)
{
    if (r.is_get && r.upgrade && r.key[0] != 0) {
        if (socket_ != nullptr) {
            socket_->close(); // one viewer at a time: the new one takes over
        }
        socket_ = r.stream;
        char reply[29];
        websocket_accept_key(r.key, reply);
        const int size = snprintf(reinterpret_cast<char *>(pending_), sizeof(pending_),
                                  "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                  "Sec-WebSocket-Accept: %s\r\n%s\r\n",
                                  reply, r.binary ? "Sec-WebSocket-Protocol: binary\r\n" : "");
        pending_size_ = static_cast<uint8_t>(size);
        pending_sent_ = 0;
        out_left_ = in_left_ = in_done_ = 0;
        header_size_ = 0;
        in_frame_ = pong_due_ = false;
        r = {};
        return true;
    }
    const char *status = "404 Not Found";
    const char *type = "text/plain";
    r.body = kNotFound;
    r.body_size = sizeof(kNotFound) - 1;
    if (!r.is_get) {
        status = "405 Method Not Allowed";
    } else if (r.root && page_ != nullptr) {
        status = "200 OK";
        type = "text/html; charset=utf-8";
        r.body = page_;
        r.body_size = page_size_;
        pages_served_++;
    } else if (r.second && second_ != nullptr) {
        status = "200 OK";
        type = "text/html; charset=utf-8";
        r.body = second_;
        r.body_size = second_size_;
        pages_served_++;
    } else if (r.root) {
        r.body = kNoPage;
        r.body_size = sizeof(kNoPage) - 1;
    }
    const int size = snprintf(r.head, sizeof(r.head),
                              "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
                              "Cache-Control: no-cache\r\nConnection: close\r\n\r\n",
                              status, type, static_cast<unsigned>(r.body_size));
    r.head_size = static_cast<uint8_t>(size);
    return false;
}

bool WebGate::send_answer(Request &r)
{
    while (r.head_sent < r.head_size || r.body_sent < r.body_size) {
        const bool head = r.head_sent < r.head_size;
        const int done = head ? r.stream->write(reinterpret_cast<const uint8_t *>(r.head) + r.head_sent,
                                                static_cast<size_t>(r.head_size - r.head_sent))
                              : r.stream->write(reinterpret_cast<const uint8_t *>(r.body) + r.body_sent,
                                                r.body_size - r.body_sent);
        if (done < 0) {
            end(r);
            return false;
        }
        if (done == 0) {
            return true; // the rest at the next round
        }
        if (head) {
            r.head_sent = static_cast<uint8_t>(r.head_sent + done);
        } else {
            r.body_sent += static_cast<size_t>(done);
        }
    }
    end(r);
    return false;
}

/* ---- The WebSocket as a stream ------------------------------------------------------ */

void WebGate::close()
{
    if (socket_ != nullptr) {
        socket_->close();
        socket_ = nullptr;
    }
}

int WebGate::flush_pending()
{
    while (pending_sent_ < pending_size_) {
        const int done = socket_->write(pending_ + pending_sent_, static_cast<size_t>(pending_size_ - pending_sent_));
        if (done < 0) {
            close();
            return -1;
        }
        if (done == 0) {
            return 0;
        }
        pending_sent_ = static_cast<uint8_t>(pending_sent_ + done);
    }
    pending_size_ = pending_sent_ = 0;
    if (pong_due_ && out_left_ == 0) {
        // Between two frames of ours: the answer to a ping.
        pong_due_ = false;
        pending_[0] = 0x8A;
        pending_[1] = pong_size_;
        memcpy(pending_ + 2, pong_, pong_size_);
        pending_size_ = static_cast<uint8_t>(2 + pong_size_);
        return flush_pending();
    }
    return 1;
}

int WebGate::write(const uint8_t *data, size_t size)
{
    if (socket_ == nullptr) {
        return -1;
    }
    int ready = flush_pending();
    if (ready <= 0) {
        return ready;
    }
    if (size == 0) {
        return 0;
    }
    if (out_left_ == 0) {
        // A new frame for what is offered now. The caller sends a stream of
        // bytes, so where the frames are cut does not matter; once a frame
        // is announced, the next bytes written fill it.
        const uint32_t n = size > 65535 ? 65535 : static_cast<uint32_t>(size);
        pending_[0] = 0x82; // a whole binary message
        if (n < 126) {
            pending_[1] = static_cast<uint8_t>(n);
            pending_size_ = 2;
        } else {
            pending_[1] = 126;
            pending_[2] = static_cast<uint8_t>(n >> 8);
            pending_[3] = static_cast<uint8_t>(n);
            pending_size_ = 4;
        }
        out_left_ = n;
        ready = flush_pending();
        if (ready <= 0) {
            return ready;
        }
    }
    const int done = socket_->write(data, size < out_left_ ? size : out_left_);
    if (done < 0) {
        close();
        return -1;
    }
    out_left_ -= static_cast<uint32_t>(done);
    return done;
}

int WebGate::read(uint8_t *out, size_t capacity)
{
    if (socket_ == nullptr) {
        return -1;
    }
    size_t total = 0;
    bool broken = false;
    while (total < capacity) {
        if (!in_frame_) {
            size_t need = 2;
            if (header_size_ >= 2) {
                const int short_size = header_[1] & 0x7F;
                // An unmasked frame is refused below as soon as it is seen.
                need = 2 + (short_size == 126 ? 2 : short_size == 127 ? 8 : 0) + ((header_[1] & 0x80) != 0 ? 4 : 0);
            }
            if (header_size_ < need) {
                const int got = socket_->read(header_ + header_size_, need - header_size_);
                if (got < 0) {
                    broken = true;
                    break;
                }
                if (got == 0) {
                    break;
                }
                header_size_ = static_cast<uint8_t>(header_size_ + got);
                continue;
            }
            // A whole header. Browsers always mask what they send.
            opcode_ = header_[0] & 0x0F;
            const int short_size = header_[1] & 0x7F;
            uint32_t size = static_cast<uint32_t>(short_size);
            bool fine = (header_[0] & 0x70) == 0 && (header_[1] & 0x80) != 0;
            if (short_size == 126) {
                size = static_cast<uint32_t>(header_[2]) << 8 | header_[3];
            } else if (short_size == 127) {
                fine = fine && header_[2] == 0 && header_[3] == 0 && header_[4] == 0 && header_[5] == 0 &&
                       header_[6] < 0x80;
                size = static_cast<uint32_t>(header_[6]) << 24 | static_cast<uint32_t>(header_[7]) << 16 |
                       static_cast<uint32_t>(header_[8]) << 8 | header_[9];
            }
            const bool data = opcode_ <= 2;
            const bool control = opcode_ == 9 || opcode_ == 10; // ping, pong
            if (!fine || !(data || control) || (control && size > sizeof(pong_))) {
                broken = true; // a close frame (8) ends here too
                break;
            }
            memcpy(mask_, header_ + need - 4, 4);
            header_size_ = 0;
            in_left_ = size;
            in_done_ = 0;
            in_frame_ = size > 0;
            if (opcode_ == 9) {
                pong_size_ = 0;
                pong_due_ = size == 0;
            }
            continue;
        }
        uint8_t aside[sizeof(pong_)];
        const bool data = opcode_ <= 2;
        uint8_t *to = data ? out + total : aside;
        const size_t room = data ? capacity - total : sizeof(aside);
        const int got = socket_->read(to, room < in_left_ ? room : in_left_);
        if (got < 0) {
            broken = true;
            break;
        }
        if (got == 0) {
            break;
        }
        for (int i = 0; i < got; i++) {
            to[i] ^= mask_[(in_done_ + static_cast<uint32_t>(i)) & 3];
        }
        if (data) {
            total += static_cast<size_t>(got);
        } else if (opcode_ == 9) {
            memcpy(pong_ + pong_size_, aside, static_cast<size_t>(got));
            pong_size_ = static_cast<uint8_t>(pong_size_ + got);
        }
        in_done_ += static_cast<uint32_t>(got);
        in_left_ -= static_cast<uint32_t>(got);
        if (in_left_ == 0) {
            in_frame_ = false;
            pong_due_ = pong_due_ || opcode_ == 9;
        }
    }
    if (broken) {
        close();
        return total > 0 ? static_cast<int>(total) : -1;
    }
    if (pong_due_ && flush_pending() < 0) {
        return total > 0 ? static_cast<int>(total) : -1;
    }
    return static_cast<int>(total);
}

} // namespace net
