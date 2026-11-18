// Fetching a page over HTTP (core/www): one request at a time, never
// waiting. The browser calls poll() from a timer and the desktop goes on
// meanwhile.
//
// It speaks HTTP/1.0 with a Host line, which every server answers, reads
// answers of 1.0 and 1.1 (a length, chunks, or until the server closes),
// and follows a site that has moved. Plain HTTP only: there is no TLS, so
// an https address is refused with words that say so.
//
// It talks through a Wire: outgoing connections that never wait. In the
// system that is os::tcp (core/os/tcp.hpp); in the tests, a script.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"
#include "core/www/url.hpp"

namespace www {

class Wire {
public:
    enum class State : uint8_t { Connecting, Open, Failed };
    // A connection being made, by its number; 0 if there can be no more.
    virtual int connect(const char *host, int port) = 0;
    virtual State state(int connection) = 0;
    // Why it failed, in words that end a sentence ("the site was not found").
    virtual const char *why(int connection) = 0;
    // As net::Stream: bytes done, 0 for none now, -1 when it has ended.
    virtual int read(int connection, uint8_t *out, size_t capacity) = 0;
    virtual int write(int connection, const uint8_t *data, size_t size) = 0;
    virtual void close(int connection) = 0;

protected:
    ~Wire() = default;
};

class Fetch {
public:
    // ponytail: the most of a page that is kept; the rest is not read, and
    // the page says it was cut. A quarter of a megabyte of HTML is a long
    // page of the kind this browser shows.
    static constexpr size_t kMaxBody = 256 * 1024;
    static constexpr int kMaxMoves = 5;
    static constexpr uint32_t kQuietUs = 30 * 1000000u; // nothing heard for this long: given up
    static constexpr const char *kAgent = "Mozilla/4.0 (compatible; Vizcacha 1.0; Chinchilla; ESP32-S3)";

    enum class State : uint8_t {
        Idle,
        Connecting,
        Sending,
        Waiting,   // for the answer's first lines
        Receiving, // the page itself
        Done,      // body() holds what came, whatever status() says
        Failed,    // error() says why
    };

    Fetch(Wire &wire, ports::Memory &memory) : wire_(wire), memory_(memory) {}
    ~Fetch() { stop(); }
    Fetch(const Fetch &) = delete;
    Fetch &operator=(const Fetch &) = delete;

    // Starts fetching an http address. False, with error(), if it cannot
    // even start.
    bool start(const char *url, uint32_t now_us);
    // Its turn: call often. Returns the state it is in now.
    State poll(uint32_t now_us);
    // Gives up, and gives back what it holds.
    void stop();

    State state() const { return state_; }
    bool busy() const { return state_ != State::Idle && state_ != State::Done && state_ != State::Failed; }
    const char *error() const { return error_; }
    // The address the answer came from: where a moved site sent us.
    const char *url() const { return url_; }
    int status() const { return status_; }
    const char *content_type() const { return type_; }
    const uint8_t *body() const { return body_; }
    size_t size() const { return size_; }
    bool cut() const { return cut_; } // the page was longer than kMaxBody
    size_t expected() const { return length_known_ ? length_ : 0; }

private:
    bool begin(uint32_t now_us);
    void fail(const char *format, const char *detail = "");
    void finish();
    void drop_connection();
    bool take(const uint8_t *data, size_t size);
    void header_line(char *line);
    bool headers_done(uint32_t now_us);
    void body_bytes(const uint8_t *data, size_t size);

    Wire &wire_;
    ports::Memory &memory_;
    State state_ = State::Idle;
    int connection_ = 0;
    char url_[kMaxUrl] = "";
    char host_[80] = "";
    char error_[160] = "";
    char request_[kMaxUrl + 256];
    size_t request_size_ = 0, request_sent_ = 0;
    char line_[1024];
    size_t line_size_ = 0;
    bool status_seen_ = false;
    int status_ = 0;
    char type_[64] = "";
    char location_[kMaxUrl] = "";
    bool length_known_ = false;
    size_t length_ = 0;
    bool chunked_ = false;
    enum class Chunk : uint8_t { Size, Data, End, Trailer } chunk_ = Chunk::Size;
    size_t chunk_left_ = 0;
    uint8_t *body_ = nullptr;
    size_t size_ = 0, capacity_ = 0;
    bool cut_ = false;
    int moves_ = 0;
    uint32_t heard_us_ = 0;
};

} // namespace www
