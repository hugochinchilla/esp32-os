#include "core/net/rfb.hpp"

#include <cstring>

#include "core/gfx/canvas.hpp"
#include "core/wm/window.hpp"

namespace rfb {

namespace {

// The lock for a block, if there is one.
class Hold {
public:
    explicit Hold(ports::Lock *lock) : lock_(lock)
    {
        if (lock_ != nullptr) {
            lock_->lock();
        }
    }
    ~Hold()
    {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }
    Hold(const Hold &) = delete;
    Hold &operator=(const Hold &) = delete;

private:
    ports::Lock *lock_;
};

uint32_t stamp_of(const video::PointerShape &shape)
{
    uint32_t h = 2166136261u;
    const auto mix = [&h](uint8_t v) { h = (h ^ v) * 16777619u; };
    mix(shape.width);
    mix(shape.height);
    mix(shape.hot_x);
    mix(shape.hot_y);
    const int count = shape.width * shape.height;
    for (int i = 0; i < count && i < static_cast<int>(sizeof(shape.pixels)); i++) {
        mix(shape.pixels[i]);
    }
    return h | 2;
}

constexpr uint32_t kEncodingRaw = 0, kEncodingZrle = 16, kEncodingCursor = 0xFFFFFF11; // -239
const char kName[] = "Chinchilla";

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t get32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 | p[3];
}

// Keys that are not characters: X11 key symbol to USB usage code.
uint16_t special_key(uint32_t keysym)
{
    switch (keysym) {
    case 0xFF08: return 0x2A; // BackSpace
    case 0xFF09: return 0x2B; // Tab
    case 0xFE20: return 0x2B; // Shift+Tab as some viewers send it
    case 0xFF0D: return 0x28; // Return
    case 0xFF8D: return 0x28; // keypad Enter
    case 0xFF1B: return 0x29; // Escape
    case 0xFF63: return 0x49; // Insert
    case 0xFFFF: return 0x4C; // Delete
    case 0xFF50: return 0x4A; // Home
    case 0xFF57: return 0x4D; // End
    case 0xFF55: return 0x4B; // Page Up
    case 0xFF56: return 0x4E; // Page Down
    case 0xFF51: return 0x50; // Left
    case 0xFF52: return 0x52; // Up
    case 0xFF53: return 0x4F; // Right
    case 0xFF54: return 0x51; // Down
    case 0xFF67: return 0x65; // Menu
    case 0xFFE1: return 0xE1; // Shift
    case 0xFFE2: return 0xE5;
    case 0xFFE3: return 0xE0; // Control
    case 0xFFE4: return 0xE4;
    case 0xFFE9: return 0xE2; // Alt
    case 0xFFE7: return 0xE2; // Meta, where a viewer sends it for Alt
    case 0xFFEA: return 0xE6;
    case 0xFFEB: return 0xE3; // Super (the logo key)
    case 0xFFEC: return 0xE7;
    default: break;
    }
    if (keysym >= 0xFFBE && keysym <= 0xFFC9) {
        return static_cast<uint16_t>(0x3A + (keysym - 0xFFBE)); // F1 to F12
    }
    return 0;
}

// The character a key symbol types, 0 if it is not a Latin-1 character.
uint8_t character_of(uint32_t keysym)
{
    if (keysym >= 0x20 && keysym <= 0xFF && keysym != 0x7F) {
        return static_cast<uint8_t>(keysym);
    }
    if (keysym >= 0x01000020 && keysym <= 0x010000FF) {
        return static_cast<uint8_t>(keysym); // given as Unicode
    }
    if (keysym >= 0xFFB0 && keysym <= 0xFFB9) {
        return static_cast<uint8_t>('0' + (keysym - 0xFFB0)); // keypad digits
    }
    switch (keysym) {
    case 0xFFAA: return '*';
    case 0xFFAB: return '+';
    case 0xFFAD: return '-';
    case 0xFFAE: return '.';
    case 0xFFAF: return '/';
    case 0xFF80: return ' ';
    default: return 0;
    }
}

// The US keyboard key that types `ch`: with the shift state given if there
// is one (exact), otherwise with the other (any).
void us_key(uint8_t ch, bool shift, uint16_t &exact, uint16_t &any)
{
    exact = any = 0;
    for (uint16_t usage = 0x04; usage <= 0x38; usage++) {
        for (int s = 0; s < 2; s++) {
            if (static_cast<uint8_t>(wm::key::us_char(usage, s != 0)) == ch) {
                (((s != 0) == shift) ? exact : any) = usage;
            }
        }
    }
}

} // namespace

Server::Server(ports::Display &display, ports::Pointer &pointer, ports::InputSink &input, ports::Clock &clock,
               ports::Memory &memory)
    : display_(display), pointer_(pointer), input_(input), clock_(clock), memory_(memory)
{
    width_ = display.width() > kMaxWidth ? kMaxWidth : display.width();
    height_ = display.height() > kMaxHeight ? kMaxHeight : display.height();
    cells_x_ = (width_ + kCell - 1) / kCell;
    cells_y_ = (height_ + kCell - 1) / kCell;
    blocks_x_ = (width_ + kBlock - 1) / kBlock;
    blocks_y_ = (height_ + kBlock - 1) / kBlock;
}

/* ---- Connections ---------------------------------------------------------------- */

void Server::start(net::Stream *stream)
{
    if (out_ == nullptr) {
        out_ = static_cast<uint8_t *>(memory_.allocate(kOutMax));
        tile_ = static_cast<uint8_t *>(memory_.allocate(kTileMax));
        work_ = memory_.allocate(net::Deflater::kMemory);
        if (out_ == nullptr || tile_ == nullptr || work_ == nullptr) {
            free_buffers(); // no memory: no remote screen
            stream->close();
            stream_ = nullptr;
            state_ = State::Idle;
            return;
        }
        deflater_.init(work_);
    }
    collect(false);
    stream_ = stream;
    state_ = State::Version;
    stats_.connections++;
    stats_.connected = true;
    stats_.compressed = stats_.draws_pointer = false;
    out_size_ = out_sent_ = in_size_ = 0;
    skip_ = encodings_left_ = 0;
    zrle_ = cursor_ = requested_ = updating_ = shape_due_ = false;
    sent_shape_ = 0;
    buttons_ = mods_ = 0;
    memset(held_, 0, sizeof(held_));
    deflater_.reset();
    // The screen's own format, until the viewer asks for another.
    format_ = {8, 8, false, true, {7, 7, 3}, {5, 2, 0}};
    build_table();
    memcpy(out_, "RFB 003.008\n", 12);
    out_size_ = 12;
}

void Server::drop()
{
    if (stream_ != nullptr) {
        stream_->close();
    }
    stream_ = nullptr;
    state_ = State::Idle;
    stats_.connected = false;
    collect(false);
    // Keys and buttons the viewer was holding are let go.
    for (Held &h : held_) {
        if (h.usage != 0) {
            input_.key(h.usage, false);
        }
        h = {};
    }
    if (buttons_ != 0) {
        buttons_ = 0;
        input_.mouse({true, last_x_, last_y_, 0, 0});
    }
}

// Sends what is waiting. True when nothing is left.
bool Server::flush()
{
    while (out_sent_ < out_size_) {
        const int done = stream_->write(out_ + out_sent_, out_size_ - out_sent_);
        if (done < 0) {
            drop();
            return false;
        }
        if (done == 0) {
            return false;
        }
        out_sent_ += static_cast<size_t>(done);
        stats_.bytes_sent += static_cast<uint32_t>(done);
    }
    out_size_ = out_sent_ = 0;
    return true;
}

void Server::put16(uint16_t v)
{
    put8(static_cast<uint8_t>(v >> 8));
    put8(static_cast<uint8_t>(v));
}

void Server::put32(uint32_t v)
{
    put16(static_cast<uint16_t>(v >> 16));
    put16(static_cast<uint16_t>(v));
}

void Server::damage(const gfx::Rect &r)
{
    const Hold hold(lock_);
    if (!collecting_) {
        return; // a new viewer starts with the whole screen
    }
    mark(r);
}

bool Server::anything_dirty()
{
    const Hold hold(lock_);
    return any_dirty_;
}

void Server::collect(bool on)
{
    const Hold hold(lock_);
    memset(dirty_, 0, sizeof(dirty_));
    any_dirty_ = false;
    collecting_ = on;
}

void Server::mark(const gfx::Rect &r)
{
    const gfx::Rect c = r.intersect({0, 0, width_, height_});
    if (c.empty()) {
        return;
    }
    for (int cy = c.y / kCell; cy <= (c.bottom() - 1) / kCell; cy++) {
        memset(dirty_ + cy * cells_x_ + c.x / kCell, 1, static_cast<size_t>((c.right() - 1) / kCell - c.x / kCell + 1));
    }
    any_dirty_ = true;
}

// Gives the buffers back to the Memory port. Only between viewers: nothing
// reads them while state_ is Idle, and start() takes them again.
void Server::free_buffers()
{
    if (out_ != nullptr) {
        memory_.release(out_);
    }
    if (tile_ != nullptr) {
        memory_.release(tile_);
    }
    if (work_ != nullptr) {
        memory_.release(work_);
    }
    out_ = tile_ = nullptr;
    work_ = nullptr;
    out_size_ = out_sent_ = 0;
}

void Server::poll()
{
    turn();
    // The last viewer has gone: its memory goes back. Here and not in
    // drop(), which is called from the middle of reading and sending, and
    // so that a viewer that takes over from another keeps the buffers.
    if (state_ == State::Idle && out_ != nullptr) {
        free_buffers();
    }
}

void Server::turn()
{
    for (net::Listener *listener : listeners_) {
        net::Stream *s = listener != nullptr ? listener->accept() : nullptr;
        if (s == nullptr) {
            continue;
        }
        if (state_ != State::Idle) {
            if (s == stream_) {
                stream_ = nullptr; // its listener has already ended the old one
            }
            drop(); // one viewer at a time: the new one takes over
        }
        start(s);
    }
    if (state_ == State::Idle) {
        return;
    }
    read_input();
    if (state_ == State::Idle) {
        return;
    }
    const uint32_t began = clock_.micros();
    while (true) {
        if (out_size_ > 0 && !flush()) {
            return; // the network is full for now, or gone
        }
        if (state_ != State::Running) {
            return;
        }
        if (!updating_) {
            if (cursor_ && requested_) {
                shape_due_ = shape_stamp() != sent_shape_;
            }
            if (!requested_ || !(anything_dirty() || shape_due_)) {
                return;
            }
            begin_update();
            continue;
        }
        while (next_block_ < blocks_x_ * blocks_y_ && block_damage(sending_, next_block_).empty()) {
            next_block_++;
        }
        if (next_block_ >= blocks_x_ * blocks_y_) {
            updating_ = false;
            continue;
        }
        send_block(next_block_++);
        if (clock_.micros() - began > kBudgetUs) {
            flush();
            return; // the rest at the next poll: the desktop stays quick
        }
    }
}

uint32_t Server::watch(net::Watch &watch)
{
    uint32_t sleep_us = kNever;
    for (const net::Listener *listener : listeners_) {
        if (listener != nullptr && !listener->watch(watch)) {
            const uint32_t by_clock = listener->busy() ? kBusyPollUs : kListenPollUs;
            sleep_us = by_clock < sleep_us ? by_clock : sleep_us;
        }
    }
    if (state_ == State::Idle) {
        return sleep_us;
    }
    const int handle = stream_->handle();
    if (handle < 0) {
        return kBusyPollUs;
    }
    const bool unsent = out_sent_ < out_size_;
    watch.add(handle, true, unsent);
    if (unsent || state_ != State::Running) {
        return sleep_us; // until the network takes more, or the viewer speaks
    }
    if (updating_) {
        return 0; // the budget ran out in the middle of an update
    }
    if (!requested_) {
        return sleep_us; // the viewer has what it asked for; it will ask again
    }
    // The viewer waits for the screen to change, and that comes from the UI
    // task, which has no way to end this sleep.
    return kBusyPollUs;
}

/* ---- What the viewer sends -------------------------------------------------------- */

void Server::read_input()
{
    for (int rounds = 0; rounds < 64; rounds++) {
        while (in_size_ > 0) {
            size_t used = 0;
            if (skip_ > 0) {
                used = skip_ < in_size_ ? skip_ : in_size_;
                skip_ -= static_cast<uint32_t>(used);
            } else if (encodings_left_ > 0) {
                if (in_size_ >= 4) {
                    const uint32_t e = get32(in_);
                    zrle_ = zrle_ || e == kEncodingZrle;
                    cursor_ = cursor_ || e == kEncodingCursor;
                    stats_.compressed = zrle_;
                    stats_.draws_pointer = cursor_;
                    encodings_left_--;
                    used = 4;
                }
            } else {
                used = message(in_, in_size_);
            }
            if (state_ == State::Idle) {
                return;
            }
            if (used == 0) {
                break;
            }
            memmove(in_, in_ + used, in_size_ - used);
            in_size_ -= used;
        }
        const int got = stream_->read(in_ + in_size_, sizeof(in_) - in_size_);
        if (got < 0) {
            drop();
            return;
        }
        if (got == 0) {
            return;
        }
        in_size_ += static_cast<size_t>(got);
    }
}

size_t Server::message(const uint8_t *m, size_t size)
{
    switch (state_) {
    case State::Version: {
        if (size < 12) {
            return 0;
        }
        if (memcmp(m, "RFB 003.", 8) != 0 || m[11] != '\n') {
            drop();
            return 0;
        }
        const int minor = (m[8] - '0') * 100 + (m[9] - '0') * 10 + (m[10] - '0');
        minor_ = minor >= 8 ? 8 : (minor == 7 ? 7 : 3);
        if (minor_ == 3) {
            put32(1); // the server decides: no password
            state_ = State::Init;
        } else {
            put8(1); // one way in:
            put8(1); // no password
            state_ = State::Security;
        }
        return 12;
    }
    case State::Security:
        if (m[0] != 1) {
            drop();
            return 0;
        }
        if (minor_ == 8) {
            put32(0); // accepted
        }
        state_ = State::Init;
        return 1;
    case State::Init: {
        // (The "shared" flag is not looked at: there is one viewer.)
        put16(static_cast<uint16_t>(width_));
        put16(static_cast<uint16_t>(height_));
        put8(format_.bits_per_pixel);
        put8(format_.depth);
        put8(format_.big_endian);
        put8(format_.true_colour);
        for (int i = 0; i < 3; i++) {
            put16(format_.max[i]);
        }
        for (int i = 0; i < 3; i++) {
            put8(format_.shift[i]);
        }
        put8(0);
        put8(0);
        put8(0);
        put32(sizeof(kName) - 1);
        memcpy(out_ + out_size_, kName, sizeof(kName) - 1);
        out_size_ += sizeof(kName) - 1;
        collect(true);
        state_ = State::Running;
        return 1;
    }
    case State::Running:
        break;
    case State::Idle:
        return 0;
    }

    switch (m[0]) {
    case 0: // SetPixelFormat
        if (size < 20) {
            return 0;
        }
        set_format(m + 4);
        return 20;
    case 2: // SetEncodings: the list follows, four bytes each
        if (size < 4) {
            return 0;
        }
        encodings_left_ = get16(m + 2);
        zrle_ = cursor_ = false;
        stats_.compressed = stats_.draws_pointer = false;
        return 4;
    case 3: { // FramebufferUpdateRequest
        if (size < 10) {
            return 0;
        }
        requested_ = true;
        if (m[1] == 0) { // not incremental: all of that part again
            const Hold hold(lock_);
            mark({get16(m + 2), get16(m + 4), get16(m + 6), get16(m + 8)});
        }
        return 10;
    }
    case 4: // KeyEvent
        if (size < 8) {
            return 0;
        }
        stats_.keys++;
        key_event(get32(m + 4), m[1] != 0);
        return 8;
    case 5: // PointerEvent
        if (size < 6) {
            return 0;
        }
        stats_.pointer_events++;
        pointer_event(m[1], get16(m + 2), get16(m + 4));
        return 6;
    case 6: // ClientCutText: the viewer's clipboard, not used
        if (size < 8) {
            return 0;
        }
        skip_ = get32(m + 4);
        return 8;
    default:
        drop(); // something this server does not know: the stream is lost
        return 0;
    }
}

void Server::set_format(const uint8_t *f)
{
    Format n;
    n.bits_per_pixel = f[0];
    n.depth = f[1];
    n.big_endian = f[2] != 0;
    n.true_colour = f[3] != 0;
    for (int i = 0; i < 3; i++) {
        n.max[i] = get16(f + 4 + 2 * i);
        n.shift[i] = f[10 + i];
    }
    const bool sizes_ok = n.bits_per_pixel == 8 || n.bits_per_pixel == 16 || n.bits_per_pixel == 32;
    if (!sizes_ok || n.shift[0] > 31 || n.shift[1] > 31 || n.shift[2] > 31) {
        drop();
        return;
    }
    if (!n.true_colour && n.bits_per_pixel != 8) {
        drop(); // a colour map only makes sense with 8 bits
        return;
    }
    format_ = n;
    build_table();
    stats_.bits_per_pixel = n.bits_per_pixel;
    map_due_ = !n.true_colour;
    // Everything the viewer has is in the old format.
    damage({0, 0, width_, height_});
}

void Server::build_table()
{
    uint32_t mask = 0;
    for (int i = 0; i < 3; i++) {
        mask |= static_cast<uint32_t>(format_.max[i]) << format_.shift[i];
    }
    for (int c = 0; c < 256; c++) {
        // What the board really shows for this colour (blue's four levels
        // are not evenly spaced), scaled to the viewer's range.
        const gfx::Rgb s = gfx::shown(static_cast<gfx::Colour>(c));
        const uint8_t level[3] = {s.r, s.g, s.b};
        uint32_t v = 0;
        for (int i = 0; i < 3; i++) {
            v |= ((static_cast<uint32_t>(level[i]) * format_.max[i] + 127) / 255) << format_.shift[i];
        }
        table_[c] = format_.true_colour ? v : static_cast<uint32_t>(c);
    }
    // ZRLE sends 32-bit pixels as three bytes when the colours leave one
    // end byte unused.
    const bool low = mask < (1u << 24), high = (mask & 0xFF) == 0;
    cpixel3_ = format_.true_colour && format_.bits_per_pixel == 32 && format_.depth <= 24 && (low || high);
    cpixel_high_ = !low;
    stats_.bits_per_pixel = format_.bits_per_pixel;
}

// For a viewer that wants a colour map: the 256 colours of the screen.
void Server::send_colour_map()
{
    put8(1);
    put8(0);
    put16(0);
    put16(256);
    for (int c = 0; c < 256; c++) {
        const gfx::Rgb s = gfx::shown(static_cast<gfx::Colour>(c));
        put16(static_cast<uint16_t>(s.r * 257));
        put16(static_cast<uint16_t>(s.g * 257));
        put16(static_cast<uint16_t>(s.b * 257));
    }
}

void Server::key_event(uint32_t keysym, bool pressed)
{
    uint16_t usage = special_key(keysym);
    uint16_t other = 0;
    if (usage >= 0xE0) {
        const uint8_t bit = (usage & 3) == 1 ? 1 : ((usage & 3) == 0 ? 2 : ((usage & 3) == 2 ? 4 : 0));
        mods_ = pressed ? (mods_ | bit) : (mods_ & static_cast<uint8_t>(~bit));
    }
    const uint8_t ch = usage == 0 ? character_of(keysym) : 0;
    if (ch != 0) {
        // A key of the US keyboard when the viewer's shift state agrees (so
        // menus and shortcuts see a key); otherwise the character itself,
        // as typed text: other keyboard layouts, ñ, accents.
        us_key(ch, (mods_ & 1) != 0, usage, other);
        if (usage == 0 && (mods_ & 6) != 0) {
            usage = other; // with Ctrl or Alt: the key, whatever shift says
        }
        if (usage == 0 && pressed) {
            const char text[3] = {ch < 0x80 ? static_cast<char>(ch) : static_cast<char>(0xC0 | (ch >> 6)),
                                  ch < 0x80 ? '\0' : static_cast<char>(0x80 | (ch & 0x3F)), '\0'};
            input_.text(text);
            return;
        }
    }
    if (pressed) {
        if (usage == 0) {
            return;
        }
        Held *slot = nullptr;
        for (Held &h : held_) {
            if (h.usage == usage) {
                slot = &h; // repeating: the viewer sends it again
            } else if (h.usage == 0 && slot == nullptr) {
                slot = &h;
            }
        }
        if (slot != nullptr) {
            *slot = {keysym, usage};
        }
        input_.key(usage, true);
        return;
    }
    // Released: the key that went down for it, even if shift changed since.
    for (Held &h : held_) {
        if (h.usage != 0 && (h.keysym == keysym || h.usage == usage || (other != 0 && h.usage == other))) {
            const uint16_t was = h.usage;
            h = {};
            input_.key(was, false);
            return;
        }
    }
    if (usage != 0 && ch == 0) {
        input_.key(usage, false);
    }
}

void Server::pointer_event(uint8_t mask, int x, int y)
{
    // RFB: bit 0 left, 1 middle, 2 right, 3 wheel up, 4 wheel down, 5 wheel
    // left, 6 wheel right. Each press of a wheel bit is one notch.
    const uint8_t buttons = static_cast<uint8_t>((mask & 1) | ((mask & 4) >> 1) | ((mask & 2) << 1));
    int8_t wheel = 0;
    if ((mask & 8) && !(wheel_ & 8)) {
        wheel = 1;
    } else if ((mask & 16) && !(wheel_ & 16)) {
        wheel = -1;
    }
    int8_t wheel_x = 0;
    if ((mask & 32) && !(wheel_ & 32)) {
        wheel_x = -1;
    } else if ((mask & 64) && !(wheel_ & 64)) {
        wheel_x = 1;
    }
    wheel_ = mask & 120;
    buttons_ = buttons;
    x = x >= width_ ? width_ - 1 : x;
    y = y >= height_ ? height_ - 1 : y;
    last_x_ = static_cast<int16_t>(x);
    last_y_ = static_cast<int16_t>(y);
    input_.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, wheel, wheel_x});
}

/* ---- What the viewer gets --------------------------------------------------------- */

// The damaged part of a block: the box around its dirty cells.
gfx::Rect Server::block_damage(const uint8_t *grid, int index) const
{
    const int per = kBlock / kCell;
    const int bx = index % blocks_x_ * per, by = index / blocks_x_ * per;
    int x0 = per, y0 = per, x1 = -1, y1 = -1;
    for (int cy = 0; cy < per && by + cy < cells_y_; cy++) {
        const uint8_t *row = grid + (by + cy) * cells_x_ + bx;
        for (int cx = 0; cx < per && bx + cx < cells_x_; cx++) {
            if (row[cx] != 0) {
                x0 = cx < x0 ? cx : x0;
                x1 = cx > x1 ? cx : x1;
                y0 = cy < y0 ? cy : y0;
                y1 = cy > y1 ? cy : y1;
            }
        }
    }
    if (x1 < 0) {
        return {};
    }
    const gfx::Rect r = {(bx + x0) * kCell, (by + y0) * kCell, (x1 - x0 + 1) * kCell, (y1 - y0 + 1) * kCell};
    return r.intersect({0, 0, width_, height_});
}

// The pointer's shape as it is now, to compare with what was sent. It may be
// read while the UI task changes it; a wrong answer then only sends the
// shape once more.
uint32_t Server::shape_stamp() const
{
    const video::PointerState p = pointer_.state();
    if (p.shape == nullptr || !p.visible) {
        return 1;
    }
    return stamp_of(*p.shape);
}

void Server::begin_update()
{
    if (map_due_) {
        map_due_ = false;
        send_colour_map();
    }
    {
        // What is marked from here on is for the next update: the blocks of
        // this one are read from the screen after this moment.
        const Hold hold(lock_);
        memcpy(sending_, dirty_, static_cast<size_t>(cells_x_ * cells_y_));
        memset(dirty_, 0, static_cast<size_t>(cells_x_ * cells_y_));
        any_dirty_ = false;
    }
    int count = shape_due_ ? 1 : 0;
    for (int b = 0; b < blocks_x_ * blocks_y_; b++) {
        count += block_damage(sending_, b).empty() ? 0 : 1;
    }
    put8(0); // FramebufferUpdate
    put8(0);
    put16(static_cast<uint16_t>(count));
    if (shape_due_) {
        send_pointer_shape();
    }
    stats_.updates++;
    stats_.rectangles += static_cast<uint32_t>(count);
    updating_ = true;
    requested_ = false;
    next_block_ = 0;
}

int Server::put_pixel(uint8_t *out, uint32_t v) const
{
    switch (format_.bits_per_pixel) {
    case 8:
        out[0] = static_cast<uint8_t>(v);
        return 1;
    case 16:
        out[format_.big_endian ? 0 : 1] = static_cast<uint8_t>(v >> 8);
        out[format_.big_endian ? 1 : 0] = static_cast<uint8_t>(v);
        return 2;
    default:
        for (int i = 0; i < 4; i++) {
            out[format_.big_endian ? 3 - i : i] = static_cast<uint8_t>(v >> (8 * i));
        }
        return 4;
    }
}

int Server::put_cpixel(uint8_t *out, uint32_t v) const
{
    if (!cpixel3_) {
        return put_pixel(out, v);
    }
    if (cpixel_high_) {
        v >>= 8;
    }
    for (int i = 0; i < 3; i++) {
        out[format_.big_endian ? 2 - i : i] = static_cast<uint8_t>(v >> (8 * i));
    }
    return 3;
}

// One ZRLE tile (before compression): a single colour, a palette of up to
// 16 colours with 1, 2 or 4 bits a pixel, or every pixel as it is.
size_t Server::encode_tile(const gfx::Rect &r, uint8_t *out) const
{
    const uint8_t *screen = display_.pixels();
    const int stride = display_.width();
    uint8_t index_of[256];
    uint8_t palette[17];
    bool seen[256] = {};
    int colours = 0;
    for (int y = 0; y < r.h && colours <= 16; y++) {
        const uint8_t *row = screen + (r.y + y) * stride + r.x;
        for (int x = 0; x < r.w; x++) {
            if (!seen[row[x]]) {
                seen[row[x]] = true;
                index_of[row[x]] = static_cast<uint8_t>(colours);
                palette[colours] = row[x];
                if (++colours > 16) {
                    break;
                }
            }
        }
    }
    size_t n = 0;
    if (colours == 1) {
        out[n++] = 1;
        n += static_cast<size_t>(put_cpixel(out + n, table_[palette[0]]));
        return n;
    }
    if (colours <= 16) {
        out[n++] = static_cast<uint8_t>(colours);
        for (int i = 0; i < colours; i++) {
            n += static_cast<size_t>(put_cpixel(out + n, table_[palette[i]]));
        }
        const int bits = colours == 2 ? 1 : (colours <= 4 ? 2 : 4);
        for (int y = 0; y < r.h; y++) {
            const uint8_t *row = screen + (r.y + y) * stride + r.x;
            uint8_t byte = 0;
            int filled = 0;
            for (int x = 0; x < r.w; x++) {
                byte = static_cast<uint8_t>(byte << bits | index_of[row[x]]);
                filled += bits;
                if (filled == 8) {
                    out[n++] = byte;
                    byte = 0;
                    filled = 0;
                }
            }
            if (filled > 0) {
                out[n++] = static_cast<uint8_t>(byte << (8 - filled)); // rows end on a byte
            }
        }
        return n;
    }
    out[n++] = 0;
    for (int y = 0; y < r.h; y++) {
        const uint8_t *row = screen + (r.y + y) * stride + r.x;
        for (int x = 0; x < r.w; x++) {
            n += static_cast<size_t>(put_cpixel(out + n, table_[row[x]]));
        }
    }
    return n;
}

void Server::send_block(int index)
{
    const gfx::Rect r = block_damage(sending_, index);
    put16(static_cast<uint16_t>(r.x));
    put16(static_cast<uint16_t>(r.y));
    put16(static_cast<uint16_t>(r.w));
    put16(static_cast<uint16_t>(r.h));
    if (zrle_) {
        put32(kEncodingZrle);
        const size_t raw = encode_tile(r, tile_);
        const size_t packed = deflater_.compress(tile_, raw, out_ + out_size_ + 4);
        put32(static_cast<uint32_t>(packed));
        out_size_ += packed;
        return;
    }
    put32(kEncodingRaw);
    const uint8_t *screen = display_.pixels();
    for (int y = 0; y < r.h; y++) {
        const uint8_t *row = screen + (r.y + y) * display_.width() + r.x;
        for (int x = 0; x < r.w; x++) {
            out_size_ += static_cast<size_t>(put_pixel(out_ + out_size_, table_[row[x]]));
        }
    }
}

// The pointer's shape, for the viewer to draw: its pixels, then one bit per
// pixel saying which are part of it.
void Server::send_pointer_shape()
{
    const video::PointerState p = pointer_.state();
    const bool shown = p.shape != nullptr && p.visible;
    if (shown) {
        shape_ = *p.shape;
        shape_.width = shape_.width > video::kMaxPointerSize ? video::kMaxPointerSize : shape_.width;
        shape_.height = shape_.height > video::kMaxPointerSize ? video::kMaxPointerSize : shape_.height;
    }
    sent_shape_ = shown ? stamp_of(shape_) : 1;
    shape_due_ = false;
    const int w = shown ? shape_.width : 0, h = shown ? shape_.height : 0;
    put16(shown ? shape_.hot_x : 0);
    put16(shown ? shape_.hot_y : 0);
    put16(static_cast<uint16_t>(w));
    put16(static_cast<uint16_t>(h));
    put32(kEncodingCursor);
    for (int i = 0; i < w * h; i++) {
        const bool white = shape_.pixels[i] == video::kPointerWhite;
        out_size_ += static_cast<size_t>(put_pixel(out_ + out_size_, table_[white ? gfx::kWhite : gfx::kBlack]));
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 8) {
            uint8_t bits = 0;
            for (int i = 0; i < 8 && x + i < w; i++) {
                if (shape_.pixels[y * w + x + i] != video::kPointerClear) {
                    bits |= static_cast<uint8_t>(0x80 >> i);
                }
            }
            put8(bits);
        }
    }
}

} // namespace rfb
