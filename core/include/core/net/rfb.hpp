// The remote screen: a VNC server (the RFB protocol, versions 3.3 to 3.8)
// for one viewer at a time. An ordinary VNC program sees the screen and
// sends keys and mouse.
//
// It runs on the UI task, a little at each poll(), and never waits for the
// network. Only what changed is sent: the window manager reports every
// rectangle it paints (damage()), kept here as a grid of 16x16 cells. The
// pointer is not in the picture; its shape goes to the viewer, which draws
// it itself, so moving the mouse costs no screen updates.
//
// No password: anyone on the network can connect (the owner's choice,
// docs/DECISIONS.md).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/rect.hpp"
#include "core/net/deflate.hpp"
#include "core/net/stream.hpp"
#include "core/ports.hpp"

namespace rfb {

constexpr int kPort = 5900;

class Server {
public:
    static constexpr int kCell = 16;   // damage is kept per 16x16 pixels
    static constexpr int kBlock = 64;  // sent as rectangles of up to 64x64
    static constexpr int kMaxWidth = 1024, kMaxHeight = 768;
    static constexpr uint32_t kBudgetUs = 4000; // encoding time per poll()

    struct Stats {
        uint32_t connections;
        uint32_t updates;    // screen updates sent
        uint32_t rectangles;
        uint32_t bytes_sent;
        uint32_t keys, pointer_events;
        bool connected;
        bool compressed;     // the viewer takes ZRLE
        bool draws_pointer;  // the viewer draws the pointer itself
        uint8_t bits_per_pixel;
    };

    // `memory` gives the buffers (about 190 KB) at the first connection.
    Server(ports::Display &display, ports::Pointer &pointer, ports::InputSink &input, ports::Clock &clock,
           ports::Memory &memory);

    // Where viewers connect (two places: the VNC port and the web server's
    // WebSocket); nullptr while there is none.
    static constexpr int kListeners = 2;
    void set_listener(net::Listener *listener, int which = 0) { listeners_[which] = listener; }
    // A part of the screen was painted.
    void damage(const gfx::Rect &r);
    // Takes new viewers, reads what the viewer sent, sends what fits.
    void poll();
    const Stats &stats() const { return stats_; }

private:
    enum class State : uint8_t { Idle, Version, Security, Init, Running };

    struct Format {
        uint8_t bits_per_pixel; // 8, 16 or 32
        uint8_t depth;
        bool big_endian;
        bool true_colour;
        uint16_t max[3];  // red, green, blue
        uint8_t shift[3];
    };

    void start(net::Stream *stream);
    void drop();
    bool flush();
    void put8(uint8_t v) { out_[out_size_++] = v; }
    void put16(uint16_t v);
    void put32(uint32_t v);
    void read_input();
    size_t message(const uint8_t *m, size_t size); // bytes used, 0: wait for more
    void set_format(const uint8_t *f);
    void build_table();
    void send_colour_map();
    void key_event(uint32_t keysym, bool pressed);
    void pointer_event(uint8_t mask, int x, int y);
    void begin_update();
    void send_block(int index);
    void send_pointer_shape();
    int put_pixel(uint8_t *out, uint32_t value) const;  // bytes written
    int put_cpixel(uint8_t *out, uint32_t value) const; // the short form ZRLE uses
    size_t encode_tile(const gfx::Rect &r, uint8_t *out) const;
    gfx::Rect block_damage(const uint8_t *grid, int index) const;
    uint32_t shape_stamp() const;

    ports::Display &display_;
    ports::Pointer &pointer_;
    ports::InputSink &input_;
    ports::Clock &clock_;
    ports::Memory &memory_;
    net::Listener *listeners_[kListeners] = {};
    net::Stream *stream_ = nullptr;
    State state_ = State::Idle;
    int minor_ = 8; // protocol version 3.x agreed
    Stats stats_ = {};

    int width_, height_;
    int cells_x_, cells_y_, blocks_x_, blocks_y_;
    static constexpr int kCells = (kMaxWidth / kCell) * (kMaxHeight / kCell);
    uint8_t dirty_[kCells] = {};   // changed since the last update began
    uint8_t sending_[kCells] = {}; // the update being sent
    bool any_dirty_ = false;

    // The viewer's wishes.
    Format format_ = {};
    uint32_t table_[256] = {}; // screen colour to the viewer's pixel value
    bool zrle_ = false, cursor_ = false;
    bool cpixel3_ = false, cpixel_high_ = false;
    bool requested_ = false; // the viewer has asked for an update
    uint32_t sent_shape_ = 0;
    bool shape_due_ = false;
    bool map_due_ = false; // a colour map to send before the next update

    // The update under way.
    bool updating_ = false;
    int next_block_ = 0;

    // Input from the viewer.
    uint8_t in_[64];
    size_t in_size_ = 0;
    uint32_t skip_ = 0;          // bytes of a long message still to pass over
    uint32_t encodings_left_ = 0;
    uint8_t buttons_ = 0;
    uint8_t wheel_ = 0; // wheel bits of the last pointer event
    uint8_t mods_ = 0; // bit 0 shift, bit 1 ctrl, bit 2 alt
    struct Held {
        uint32_t keysym;
        uint16_t usage;
    };
    Held held_[8] = {};

    // Buffers, from the Memory port at the first connection.
    static constexpr size_t kTileMax = 1 + kBlock * kBlock * 4;
    static constexpr size_t kOutMax = 16 + net::Deflater::bound(kTileMax) + 64;
    uint8_t *out_ = nullptr;
    size_t out_size_ = 0, out_sent_ = 0;
    uint8_t *tile_ = nullptr;
    net::Deflater deflater_;
};

} // namespace rfb
