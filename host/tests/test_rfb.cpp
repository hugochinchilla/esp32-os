// Tests of the remote screen server with a connection kept in memory: the
// greeting, what viewers' keys and mouse become, and bad input. (That the
// picture arrives right is checked with a real zlib by tools/test_vnc_sim.py.)
//
// With an argument, it instead writes the compressor's output for a long,
// varied input to <argument>.z and the input to <argument>.raw, for that
// script to check against zlib.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/net/deflate.hpp"
#include "core/net/rfb.hpp"
#include "core/wm/window.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

class Pipe : public net::Stream, public net::Listener {
public:
    std::string to_server, from_server;
    bool waiting = false, open = false;
    size_t room = 1 << 20; // bytes write() takes at a time

    net::Stream *accept() override
    {
        if (!waiting) {
            return nullptr;
        }
        waiting = false;
        open = true;
        return this;
    }
    int read(uint8_t *out, size_t capacity) override
    {
        if (!open) {
            return -1;
        }
        const size_t n = std::min(capacity, to_server.size());
        memcpy(out, to_server.data(), n);
        to_server.erase(0, n);
        return static_cast<int>(n);
    }
    int write(const uint8_t *data, size_t size) override
    {
        if (!open) {
            return -1;
        }
        const size_t n = std::min(size, room);
        from_server.append(reinterpret_cast<const char *>(data), n);
        return static_cast<int>(n);
    }
    void close() override { open = false; }
};

// Writes down what the desktop would be given.
class Log : public ports::InputSink {
public:
    std::string events;
    void key(uint16_t usage, bool pressed) override
    {
        char text[16];
        snprintf(text, sizeof(text), "%c%02X ", pressed ? '+' : '-', usage);
        events += text;
    }
    void mouse(const ports::MouseEvent &e) override
    {
        char text[40];
        snprintf(text, sizeof(text), "m%d,%d,%d,%d ", e.x, e.y, e.buttons, e.wheel);
        events += text;
    }
    void text(const char *utf8) override { events += std::string("'") + utf8 + "' "; }
    std::string take()
    {
        std::string out = events;
        events.clear();
        return out;
    }
};

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostMemory memory;
Log desktop;
Pipe pipe_;
rfb::Server server(display, pointer, desktop, clock_, memory);

void connect()
{
    pipe_.to_server.clear();
    pipe_.from_server.clear();
    pipe_.waiting = true;
    server.poll();
    pipe_.to_server = std::string("RFB 003.008\n") + '\1' + '\1';
    server.poll();
}

void key(uint32_t keysym, bool down)
{
    const char m[8] = {4, static_cast<char>(down), 0, 0, static_cast<char>(keysym >> 24),
                       static_cast<char>(keysym >> 16), static_cast<char>(keysym >> 8), static_cast<char>(keysym)};
    pipe_.to_server.append(m, 8);
    server.poll();
}

void tap(uint32_t keysym)
{
    key(keysym, true);
    key(keysym, false);
}

void pointer_at(int x, int y, int mask)
{
    const char m[6] = {5, static_cast<char>(mask), static_cast<char>(x >> 8), static_cast<char>(x),
                       static_cast<char>(y >> 8), static_cast<char>(y)};
    pipe_.to_server.append(m, 6);
    server.poll();
}

void test_greeting()
{
    server.set_listener(&pipe_);
    server.poll();
    CHECK(!server.stats().connected);
    connect();
    CHECK(server.stats().connected);
    // Version, the one security type, "accepted", then the screen: 640x480,
    // 8 bits, and the name.
    const std::string &out = pipe_.from_server;
    CHECK(out.substr(0, 12) == "RFB 003.008\n");
    CHECK(out.substr(12, 2) == std::string("\1\1"));
    CHECK(out.substr(14, 4) == std::string(4, '\0'));
    CHECK(out.size() == 18 + 24 + 10 && out.substr(42) == "Chinchilla");
    CHECK(static_cast<uint8_t>(out[18]) == 2 && static_cast<uint8_t>(out[19]) == 0x80); // 640
    CHECK(out[22] == 8);
}

void test_keys()
{
    connect();
    desktop.take();
    // Plain letters and digits are keys of the US keyboard.
    tap('a');
    CHECK(desktop.take() == "+04 -04 ");
    tap('1');
    CHECK(desktop.take() == "+1E -1E ");
    // A capital with Shift held is the same key (the shift went first).
    key(0xFFE1, true);
    tap('A');
    key(0xFFE1, false);
    CHECK(desktop.take() == "+E1 +04 -04 -E1 ");
    // A capital without Shift (Caps Lock), ñ, and a character whose shift
    // differs from the US keyboard's (a Spanish "/" is Shift+7): as text.
    tap('A');
    CHECK(desktop.take() == "'A' ");
    tap(0xF1);
    CHECK(desktop.take() == "'\xC3\xB1' ");
    key(0xFFE1, true);
    tap('/');
    key(0xFFE1, false);
    CHECK(desktop.take() == "+E1 '/' -E1 ");
    tap(0x010000E9); // é given as Unicode
    CHECK(desktop.take() == "'\xC3\xA9' ");
    // With Alt or Ctrl it is always the key: Alt+F, Ctrl+Shift... shortcuts.
    key(0xFFE9, true);
    tap('F');
    key(0xFFE9, false);
    CHECK(desktop.take() == "+E2 +09 -09 -E2 ");
    // Named keys, function keys, the keypad.
    tap(0xFF0D);
    tap(0xFF1B);
    tap(0xFF51);
    tap(0xFFC1); // F4
    tap(0xFFB7); // keypad 7
    tap(0xFF8D);
    CHECK(desktop.take() == "+28 -28 +29 -29 +50 -50 +3D -3D +24 -24 +28 -28 ");
    // Shift let go before the letter: the key that went down comes up.
    key(0xFFE1, true);
    key('B', true);
    key(0xFFE1, false);
    key('b', false);
    CHECK(desktop.take() == "+E1 +05 -E1 -05 ");
    // Keys nobody knows do nothing.
    tap(0xFE50);
    tap(0x20AC);
    CHECK(desktop.take() == "");
}

void test_pointer()
{
    connect();
    desktop.take();
    pointer_at(100, 50, 0);
    pointer_at(100, 50, 1); // left
    pointer_at(100, 50, 4); // right is bit 2 for a viewer, bit 1 for us
    pointer_at(100, 50, 2); // middle
    CHECK(desktop.take() == "m100,50,0,0 m100,50,1,0 m100,50,2,0 m100,50,4,0 ");
    pointer_at(100, 50, 8); // the wheel is a button press and release
    pointer_at(100, 50, 0);
    pointer_at(100, 50, 16);
    pointer_at(100, 50, 0);
    CHECK(desktop.take() == "m100,50,0,1 m100,50,0,0 m100,50,0,-1 m100,50,0,0 ");
    pointer_at(5000, 5000, 0); // outside the screen
    CHECK(desktop.take() == "m639,479,0,0 ");

    // A viewer that goes away with a key and a button held: both are let go.
    key('a', true);
    pointer_at(10, 10, 1);
    desktop.take();
    pipe_.open = false;
    server.poll();
    CHECK(!server.stats().connected);
    CHECK(desktop.take() == "-04 m0,0,0,0 ");
}

void test_bad_input()
{
    // Not a VNC viewer.
    pipe_.to_server.clear();
    pipe_.waiting = true;
    server.poll();
    pipe_.to_server = "GET / HTTP/1.1\r\n\r\n";
    server.poll();
    CHECK(!server.stats().connected && !pipe_.open);
    // A pixel size that does not exist; an unknown message.
    connect();
    pipe_.to_server = std::string("\0\0\0\0\x18\x18\0\1", 8) + std::string(12, '\0');
    server.poll();
    CHECK(!server.stats().connected);
    connect();
    pipe_.to_server = "\x7F";
    server.poll();
    CHECK(!server.stats().connected);
    // The viewer's clipboard, however long, is passed over; then a key.
    connect();
    desktop.take();
    pipe_.to_server = std::string("\6\0\0\0\0\0\x13\x88", 8) + std::string(5000, 'x');
    for (int i = 0; i < 100; i++) {
        server.poll();
    }
    tap('z');
    CHECK(desktop.take() == "+1D -1D ");
    // A long list of encodings, most unknown, arriving in small pieces.
    std::string list = std::string("\2\0\0\x64", 4);
    for (int i = 0; i < 100; i++) {
        list += std::string("\0\0\0", 3) + static_cast<char>(i == 77 ? 16 : 100 + i);
    }
    for (char c : list) {
        pipe_.to_server += c;
        server.poll();
    }
    CHECK(server.stats().compressed && !server.stats().draws_pointer);

    // A slow network: a whole screen through writes of 100 bytes.
    pipe_.from_server.clear();
    pipe_.room = 100;
    pipe_.to_server = std::string("\3\0\0\0\0\0\2\x80\1\xE0", 10);
    int polls = 0;
    size_t last = 1;
    while (polls < 100000 && (polls < 10 || pipe_.from_server.size() != last)) {
        last = pipe_.from_server.size();
        clock_.now += 10000;
        server.poll();
        polls++;
    }
    CHECK(server.stats().connected && pipe_.from_server.size() > 80 * 16);
    CHECK(static_cast<uint8_t>(pipe_.from_server[3]) == 80); // 10 x 8 blocks of 64
    pipe_.room = 1 << 20;
}

// For tools/test_vnc_sim.py: pieces of every kind through one stream.
int dump(const char *base)
{
    static uint8_t work[net::Deflater::kMemory];
    static uint8_t piece[net::Deflater::kMaxPiece], packed[net::Deflater::bound(net::Deflater::kMaxPiece)];
    net::Deflater d;
    d.init(work);
    FILE *raw = fopen((std::string(base) + ".raw").c_str(), "wb");
    FILE *z = fopen((std::string(base) + ".z").c_str(), "wb");
    if (raw == nullptr || z == nullptr) {
        return 1;
    }
    uint32_t seed = 12345;
    const auto next = [&seed]() { return (seed = seed * 1664525u + 1013904223u) >> 16; };
    for (int round = 0; round < 400; round++) {
        size_t size = round % 7 == 0 ? next() % net::Deflater::kMaxPiece + 1 : next() % 5000;
        size = round == 5 ? net::Deflater::kMaxPiece : size;
        const int kind = round % 5;
        for (size_t i = 0; i < size; i++) {
            piece[i] = kind == 0 ? static_cast<uint8_t>(next())            // noise
                     : kind == 1 ? static_cast<uint8_t>(i % 4 == 0 ? 0x92 : 0x49) // dither
                     : kind == 2 ? static_cast<uint8_t>(round)             // flat
                     : kind == 3 ? static_cast<uint8_t>(next() % 3 + 200)  // few values
                                 : (i >= 300 ? piece[i - 300] : static_cast<uint8_t>(next())); // repeats
        }
        const size_t n = d.compress(piece, size, packed);
        if (n > net::Deflater::bound(size)) {
            printf("piece of %zu took %zu\n", size, n);
            return 1;
        }
        fwrite(piece, 1, size, raw);
        fwrite(packed, 1, n, z);
    }
    fclose(raw);
    fclose(z);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1) {
        return dump(argv[1]);
    }
    test_greeting();
    test_keys();
    test_pointer();
    test_bad_input();
    if (failures == 0) {
        printf("rfb: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
