// The board's small web server, for the remote screen in a browser. It
// answers two things on one port:
//
//   GET /            one page (the viewer), given with set_page()
//   a WebSocket      the bytes of a VNC connection inside WebSocket frames
//
// The WebSocket comes out as an ordinary net::Stream, so the VNC server
// (core/net/rfb.hpp) uses the gate as one more listener and knows nothing of
// browsers. One WebSocket at a time: a new one ends the earlier one.
//
// Nothing here waits. Everything happens inside accept(), which the VNC
// server calls at each of its polls; watch() says what should bring the
// next one.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/net/stream.hpp"

namespace net {

// base64(SHA-1(key + the WebSocket GUID)): the answer to Sec-WebSocket-Key.
// `out` needs 29 bytes. For the tests.
void websocket_accept_key(const char *key, char out[29]);

class WebGate : public Listener, private Stream {
public:
    static constexpr int kConnections = 4; // browsers open several at once

    // Where the connections come from: a listener that hands out several
    // streams at a time (net::PosixTcpPool), not one that replaces them.
    void set_port(Listener *port) { port_ = port; }
    // The page for GET /. The text must stay valid.
    void set_page(const char *html, size_t size)
    {
        page_ = html;
        page_size_ = size;
    }

    // Serves the page to whoever asks, and returns the stream of a new
    // WebSocket connection (always this same object) or nullptr.
    Stream *accept() override;
    // A request is being read or answered.
    bool busy() const override
    {
        for (const Request &r : requests_) {
            if (r.stream != nullptr) {
                return true;
            }
        }
        return false;
    }

    // New connections, requests still arriving, answers still leaving.
    bool watch(Watch &watch) const override;

    uint32_t pages_served() const { return pages_served_; }

private:
    struct Request {
        Stream *stream;
        uint32_t age;        // which came first: the oldest goes when full
        char line[200];      // the header line being read
        uint8_t line_size;
        bool line_cut;       // the line was longer than the buffer
        bool first_line_seen, is_get, root, upgrade, binary;
        char key[64];        // Sec-WebSocket-Key
        // The answer being sent.
        char head[192];
        uint8_t head_size, head_sent;
        const char *body;
        size_t body_size, body_sent;
        bool answering;
    };

    void take_new();
    bool read_request(Request &r); // false: the connection is over
    void header_line(Request &r);
    bool answer(Request &r);       // true: a WebSocket was opened on it
    bool send_answer(Request &r);  // false: the connection is over
    void end(Request &r);

    // The WebSocket, as a Stream.
    int read(uint8_t *out, size_t capacity) override;
    int write(const uint8_t *data, size_t size) override;
    void close() override;
    int handle() const override { return socket_ != nullptr ? socket_->handle() : -1; }
    int flush_pending(); // 1 nothing left, 0 not all sent yet, -1 broken

    Listener *port_ = nullptr;
    const char *page_ = nullptr;
    size_t page_size_ = 0;
    Request requests_[kConnections] = {};
    uint32_t next_age_ = 1;
    uint32_t pages_served_ = 0;

    Stream *socket_ = nullptr;  // the WebSocket's connection
    uint8_t pending_[192];      // the handshake answer, a frame header, a pong
    uint8_t pending_size_ = 0, pending_sent_ = 0;
    uint32_t out_left_ = 0;     // bytes still owed to the frame being sent
    uint8_t header_[14];        // the frame header being read
    uint8_t header_size_ = 0;
    uint8_t opcode_ = 0;
    uint8_t mask_[4] = {};
    uint32_t in_left_ = 0;      // payload bytes of the frame being read
    uint32_t in_done_ = 0;      // payload bytes read (for the mask)
    bool in_frame_ = false;
    uint8_t pong_[125];         // a ping's payload, to send back
    uint8_t pong_size_ = 0;
    bool pong_due_ = false;
};

// The viewer page the board serves (core/src/net/viewer_page.cpp).
const char *viewer_page();
size_t viewer_page_size();

} // namespace net
