// Unit tests for the debug protocol core. No test framework: each check
// prints the line that failed, and the program's exit code is the result.
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "core/dbg/cobs.hpp"
#include "core/dbg/crc.hpp"
#include "core/dbg/protocol.hpp"
#include "core/gfx/canvas.hpp"
#include "core/video/pointer.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

using Bytes = std::vector<uint8_t>;

struct Frame {
    uint8_t type;
    uint8_t seq;
    Bytes payload;
};

// Collects what the board sends and splits it back into frames.
class Capture : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override
    {
        wire.insert(wire.end(), data, data + size);
    }

    std::vector<Frame> frames()
    {
        std::vector<Frame> out;
        Bytes current;
        for (uint8_t byte : wire) {
            if (byte != 0) {
                current.push_back(byte);
                continue;
            }
            if (!current.empty()) {
                Bytes raw(current.size());
                size_t size = dbg::cobs_decode(current.data(), current.size(), raw.data());
                CHECK(size >= 4);
                if (size >= 4) {
                    uint16_t crc = static_cast<uint16_t>(raw[size - 2] | (raw[size - 1] << 8));
                    CHECK(dbg::crc16(raw.data(), size - 2) == crc);
                    out.push_back({raw[0], raw[1], Bytes(raw.begin() + 2, raw.begin() + size - 2)});
                }
            }
            current.clear();
        }
        wire.clear();
        return out;
    }

    Bytes wire;
};

class RecordingInput : public ports::InputSink {
public:
    void key(uint16_t usage, bool pressed) override
    {
        last_usage = usage;
        last_pressed = pressed;
        keys++;
    }
    void mouse(const ports::MouseEvent &event) override
    {
        last_mouse = event;
        mice++;
    }
    uint16_t last_usage = 0;
    bool last_pressed = false;
    ports::MouseEvent last_mouse = {};
    int keys = 0, mice = 0;
};

static Bytes make_frame(uint8_t type, uint8_t seq, const Bytes &payload)
{
    Bytes raw = {type, seq};
    raw.insert(raw.end(), payload.begin(), payload.end());
    uint16_t crc = dbg::crc16(raw.data(), raw.size());
    raw.push_back(static_cast<uint8_t>(crc));
    raw.push_back(static_cast<uint8_t>(crc >> 8));
    Bytes wire(dbg::cobs_max_encoded(raw.size()) + 2);
    wire[0] = 0;
    size_t size = dbg::cobs_encode(raw.data(), raw.size(), wire.data() + 1);
    wire[size + 1] = 0;
    wire.resize(size + 2);
    return wire;
}

static Bytes run_length_decode(const Bytes &in)
{
    Bytes out;
    size_t i = 0;
    while (i < in.size()) {
        uint8_t control = in[i++];
        if (control < 128) {
            out.insert(out.end(), in.begin() + i, in.begin() + i + control + 1);
            i += control + 1u;
        } else {
            out.insert(out.end(), control - 125u, in[i++]);
        }
    }
    return out;
}

static Bytes back_reference_decode(const Bytes &in, size_t width)
{
    const size_t distance[6] = {1, 2, 4, width, 2 * width, 4 * width};
    Bytes out;
    size_t i = 0;
    while (i < in.size()) {
        uint8_t control = in[i++];
        if (control < 128) {
            out.insert(out.end(), in.begin() + i, in.begin() + i + control + 1);
            i += control + 1u;
        } else {
            size_t d = distance[in[i++]];
            for (int n = 0; n < control - 125; n++) {
                out.push_back(out[out.size() - d]);
            }
        }
    }
    return out;
}

static void test_cobs()
{
    // Sizes around the 254-byte block limit, with and without zeros.
    for (size_t size : {0u, 1u, 2u, 253u, 254u, 255u, 256u, 600u}) {
        for (int pattern = 0; pattern < 3; pattern++) {
            Bytes in(size);
            for (size_t i = 0; i < size; i++) {
                in[i] = pattern == 0 ? 0 : pattern == 1 ? 0x55 : static_cast<uint8_t>(i * 7);
            }
            Bytes enc(dbg::cobs_max_encoded(size));
            size_t n = dbg::cobs_encode(in.data(), size, enc.data());
            CHECK(n <= enc.size());
            CHECK(memchr(enc.data(), 0, n) == nullptr);
            Bytes dec(n + 1);
            size_t m = dbg::cobs_decode(enc.data(), n, dec.data());
            CHECK(m == size);
            CHECK(size == 0 || memcmp(dec.data(), in.data(), size) == 0);
        }
    }
    const uint8_t broken[] = {5, 1, 2}; // promises more bytes than there are
    uint8_t out[8];
    CHECK(dbg::cobs_decode(broken, sizeof(broken), out) == 0);
}

static void test_crc()
{
    const uint8_t *text = reinterpret_cast<const uint8_t *>("123456789");
    CHECK(dbg::crc16(text, 9) == 0x29B1);
    CHECK(dbg::crc32(text, 9) == 0xCBF43926);
    CHECK(dbg::crc32(text + 4, 5, dbg::crc32(text, 4)) == 0xCBF43926);
}

struct Rig {
    host::MemoryDisplay display{640, 480};
    host::MemoryPointer pointer;
    host::NoLock lock;
    host::HostSystem system;
    RecordingInput input;
    Capture wire;
    dbg::Protocol protocol{wire, lock, display, pointer, input, system, "test"};

    std::vector<Frame> request(uint8_t type, uint8_t seq, const Bytes &payload)
    {
        Bytes frame = make_frame(type, seq, payload);
        protocol.feed(frame.data(), frame.size());
        return wire.frames();
    }
};

static void test_ping_and_info()
{
    Rig rig;
    auto got = rig.request(dbg::kPing, 7, {1, 0, 2, 0, 0});
    CHECK(got.size() == 1);
    CHECK(got[0].type == (dbg::kPing | dbg::kReplyBit) && got[0].seq == 7);
    CHECK(got[0].payload == Bytes({0, 1, 0, 2, 0, 0}));

    got = rig.request(dbg::kInfo, 8, {});
    CHECK(got.size() == 1);
    const Bytes &p = got[0].payload;
    CHECK(p.size() == 7 + 4);
    CHECK(p[0] == dbg::kOk && p[1] == dbg::kProtocolVersion);
    CHECK((p[2] | (p[3] << 8)) == 640 && (p[4] | (p[5] << 8)) == 480);
    CHECK(p[6] == dbg::kRgb332);
    CHECK(memcmp(p.data() + 7, "test", 4) == 0);

    got = rig.request(0x55, 9, {});
    CHECK(got.size() == 1 && got[0].payload == Bytes({dbg::kUnknownCommand}));
}

static void test_input()
{
    Rig rig;
    auto got = rig.request(dbg::kKey, 1, {0x28, 0x00, 1});
    CHECK(got.size() == 1 && got[0].payload == Bytes({dbg::kOk}));
    CHECK(rig.input.keys == 1 && rig.input.last_usage == 0x28 && rig.input.last_pressed);

    got = rig.request(dbg::kKey, 2, {0x28});
    CHECK(got.size() == 1 && got[0].payload == Bytes({dbg::kBadArguments}));
    CHECK(rig.input.keys == 1);

    // Relative move by (-3, 300), left button, wheel -1.
    got = rig.request(dbg::kMouse, 3, {0, 0xFD, 0xFF, 0x2C, 0x01, 1, 0xFF});
    CHECK(got.size() == 1 && got[0].payload == Bytes({dbg::kOk}));
    CHECK(rig.input.mice == 1 && !rig.input.last_mouse.absolute);
    CHECK(rig.input.last_mouse.x == -3 && rig.input.last_mouse.y == 300);
    CHECK(rig.input.last_mouse.buttons == 1 && rig.input.last_mouse.wheel == -1);
}

static uint64_t get64(const Bytes &p, size_t at)
{
    uint64_t value = 0;
    for (int i = 7; i >= 0; i--) {
        value = (value << 8) | p[at + static_cast<size_t>(i)];
    }
    return value;
}

static ports::TaskInfo make_task(const char *name, uint16_t id, ports::TaskState state, uint8_t priority, int8_t core,
                                 uint64_t run_time_us, uint32_t stack_free, bool external)
{
    ports::TaskInfo t = {};
    snprintf(t.name, sizeof(t.name), "%s", name);
    t.id = id;
    t.state = state;
    t.priority = priority;
    t.core = core;
    t.run_time_us = run_time_us;
    t.stack_free = stack_free;
    t.stack_external = external;
    return t;
}

static void test_tasks()
{
    // A machine that keeps no task statistics: the command is unknown.
    {
        Rig rig;
        auto got = rig.request(dbg::kTasks, 1, {});
        CHECK(got.size() == 1 && got[0].payload == Bytes({dbg::kUnknownCommand}));
    }
    // Two tasks, every field, with a run time past 32 bits (71 minutes).
    {
        Rig rig;
        rig.system.task_clock_us = 0x123456789AULL;
        rig.system.task_list.push_back(
            make_task("ui", 7, ports::TaskState::Running, 5, -1, 0x1FFFFFFFFULL, 9200, false));
        rig.system.task_list.push_back(
            make_task("IDLE1", 300, ports::TaskState::Ready, 0, 1, 42, 700, true));
        auto got = rig.request(dbg::kTasks, 2, {});
        CHECK(got.size() == 1 && got[0].type == (dbg::kTasks | dbg::kReplyBit) && got[0].seq == 2);
        const Bytes &p = got[0].payload;
        CHECK(p.size() == 1 + 11 + (19 + 2) + (19 + 5));
        if (p.size() == 1 + 11 + (19 + 2) + (19 + 5)) {
            CHECK(p[0] == dbg::kOk);
            CHECK(get64(p, 1) == 0x123456789AULL);
            CHECK(p[9] == 2 && p[10] == 2 && p[11] == 0); // cores, tasks, left out
            size_t at = 12;
            CHECK((p[at] | (p[at + 1] << 8)) == 7);
            CHECK(p[at + 2] == 0 && p[at + 3] == 5 && p[at + 4] == 0xFF && p[at + 5] == 0);
            CHECK(get64(p, at + 6) == 0x1FFFFFFFFULL);
            CHECK((p[at + 14] | (p[at + 15] << 8)) == 9200 && p[at + 16] == 0 && p[at + 17] == 0);
            CHECK(p[at + 18] == 2 && p[at + 19] == 'u' && p[at + 20] == 'i');
            at += 21;
            CHECK((p[at] | (p[at + 1] << 8)) == 300);
            CHECK(p[at + 2] == 1 && p[at + 3] == 0 && p[at + 4] == 1 && p[at + 5] == 1);
            CHECK(get64(p, at + 6) == 42);
            CHECK(p[at + 18] == 5 && memcmp(&p[at + 19], "IDLE1", 5) == 0);
        }
    }
    // More tasks than one reply holds: whole records only, and the count
    // of the ones left out. Names of 15 letters have no closing zero.
    {
        Rig rig;
        for (int i = 0; i < 40; i++) {
            ports::TaskInfo t = make_task("", static_cast<uint16_t>(i + 1), ports::TaskState::Blocked, 3, 0, 1, 2, false);
            memcpy(t.name, "fifteen-letters", 15);
            t.name[15] = static_cast<char>('A' + i % 26); // no zero at the end
            rig.system.task_list.push_back(t);
        }
        auto got = rig.request(dbg::kTasks, 3, {});
        CHECK(got.size() == 1);
        const Bytes &p = got[0].payload;
        CHECK(p[0] == dbg::kOk);
        const int sent = p[10];
        CHECK(sent > 0 && sent <= dbg::kMaxTasks && sent + p[11] == 40);
        CHECK(p.size() <= dbg::kMaxReplyPayload);
        size_t at = 12;
        for (int i = 0; i < sent && at + 19 <= p.size(); i++) {
            CHECK((p[at] | (p[at + 1] << 8)) == i + 1);
            CHECK(p[at + 18] == 16);
            at += 19 + p[at + 18];
        }
        CHECK(at == p.size());
    }
}

static void test_bad_frames()
{
    Rig rig;
    // Text between frames is dropped and does not disturb the next frame,
    // even when it contains a stray zero.
    const char noise[] = "hello\r\n\0more";
    rig.protocol.feed(reinterpret_cast<const uint8_t *>(noise), sizeof(noise) - 1);
    auto got = rig.request(dbg::kPing, 1, {});
    CHECK(got.size() == 1);
    CHECK(rig.protocol.receive_errors() == 2);

    // A damaged frame is counted and dropped.
    Bytes bad = make_frame(dbg::kPing, 2, {9, 9, 9});
    bad[3] ^= 0x10;
    rig.protocol.feed(bad.data(), bad.size());
    CHECK(rig.wire.frames().empty());
    CHECK(rig.protocol.receive_errors() == 3);

    // So is one that is too long, and the link recovers afterwards.
    Bytes huge = make_frame(dbg::kPing, 3, Bytes(2000, 0x41));
    rig.protocol.feed(huge.data(), huge.size());
    CHECK(rig.wire.frames().empty());
    CHECK(rig.protocol.receive_errors() == 4);
    got = rig.request(dbg::kPing, 4, {});
    CHECK(got.size() == 1 && got[0].seq == 4);

    // A frame may arrive one byte at a time.
    Bytes slow = make_frame(dbg::kPing, 5, {1, 2, 3});
    for (uint8_t byte : slow) {
        rig.protocol.feed(&byte, 1);
    }
    got = rig.wire.frames();
    CHECK(got.size() == 1 && got[0].seq == 5);
}

static size_t last_packed_size = 0;

// Requests a screenshot and checks it against `expected` (by default the
// framebuffer itself).
static void check_screenshot(Rig &rig, const Bytes &request = {}, const uint8_t *expected = nullptr)
{
    if (expected == nullptr) {
        expected = rig.display.pixels();
    }
    auto got = rig.request(dbg::kScreenshot, 42, request);
    CHECK(got.size() >= 3);
    if (got.size() < 3) {
        return;
    }
    const Bytes &head = got.front().payload;
    CHECK(got.front().type == (dbg::kScreenshot | dbg::kReplyBit));
    CHECK(head.size() == 11 && head[0] == dbg::kOk);
    const bool patterns = !request.empty() && (request[0] & dbg::kAcceptBackReference);
    CHECK(head[5] == dbg::kRgb332 && head[6] == (patterns ? dbg::kBackReference : dbg::kRunLength));

    Bytes packed;
    for (size_t i = 1; i + 1 < got.size(); i++) {
        CHECK(got[i].type == dbg::kShotData && got[i].seq == 42);
        CHECK(got[i].payload.size() <= dbg::kMaxReplyPayload);
        CHECK((got[i].payload[0] | (got[i].payload[1] << 8)) == static_cast<int>(i - 1));
        packed.insert(packed.end(), got[i].payload.begin() + 2, got[i].payload.end());
    }
    const Bytes &tail = got.back().payload;
    CHECK(got.back().type == dbg::kShotEnd && tail.size() == 6);
    CHECK((tail[0] | (tail[1] << 8)) == static_cast<int>(got.size() - 2));

    Bytes picture = patterns ? back_reference_decode(packed, 640) : run_length_decode(packed);
    last_packed_size = packed.size();
    CHECK(picture.size() == 640u * 480u);
    CHECK(memcmp(picture.data(), expected, picture.size()) == 0);
    uint32_t crc = static_cast<uint32_t>(tail[2]) | (static_cast<uint32_t>(tail[3]) << 8) |
                   (static_cast<uint32_t>(tail[4]) << 16) | (static_cast<uint32_t>(tail[5]) << 24);
    CHECK(crc == dbg::crc32(picture.data(), picture.size()));
}

static void test_screenshot()
{
    // Every picture is checked with both encodings.
    auto both = [](Rig &rig) {
        check_screenshot(rig);
        size_t one = last_packed_size;
        check_screenshot(rig, {dbg::kAcceptBackReference});
        check_screenshot(rig, {dbg::kAcceptBackReference | dbg::kWithoutPointer});
        return std::make_pair(one, last_packed_size);
    };
    Rig rig;
    // Flat colour: the best case for both.
    memset(rig.display.pixels(), 0x1F, 640 * 480);
    auto flat = both(rig);
    CHECK(flat.second < 5000);

    // The 4x4 dither of the desktop: poor with run lengths, small with
    // back-references.
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            rig.display.pixels()[y * 640 + x] = gfx::dither({58, 110, 165}, x, y);
        }
    }
    auto dithered = both(rig);
    CHECK(dithered.first > 100000);
    CHECK(dithered.second < 12000);

    // Noise: the worst case, nothing repeats.
    uint32_t state = 12345;
    for (int i = 0; i < 640 * 480; i++) {
        state = state * 1664525u + 1013904223u;
        rig.display.pixels()[i] = static_cast<uint8_t>(state >> 24);
    }
    auto noise = both(rig);
    CHECK(noise.second < 640 * 480 * 102 / 100);

    // Runs of every length from 1 to 300, to cover the encoders' limits.
    uint8_t *p = rig.display.pixels();
    int at = 0;
    for (int length = 1; length <= 300 && at + length < 640 * 480; length++) {
        memset(p + at, length & 1 ? 0xAA : 0x33, static_cast<size_t>(length));
        at += length;
    }
    both(rig);
    printf("screenshot sizes (run-length / back-reference): flat %zu / %zu, dithered %zu / %zu, noise %zu / %zu\n",
           flat.first, flat.second, dithered.first, dithered.second, noise.first, noise.second);
}

// The pointer drawn the slow, obvious way, for comparison.
static void reference_pointer(uint8_t *screen, int width, int height, const video::PointerState &state)
{
    if (!state.visible || state.shape == nullptr) {
        return;
    }
    const video::PointerShape &shape = *state.shape;
    for (int row = 0; row < shape.height; row++) {
        for (int col = 0; col < shape.width; col++) {
            int x = state.x - shape.hot_x + col, y = state.y - shape.hot_y + row;
            uint8_t p = shape.pixels[row * shape.width + col];
            if (x >= 0 && x < width && y >= 0 && y < height && p != video::kPointerClear) {
                screen[y * width + x] = p == video::kPointerBlack ? 0x00 : 0xFF;
            }
        }
    }
}

static void test_compose_pointer()
{
    // A small screen, a shape with its hotspot in the middle, and positions
    // all around and past every edge and corner.
    const int w = 40, h = 30;
    video::PointerShape shape = {};
    shape.width = 9;
    shape.height = 7;
    shape.hot_x = 4;
    shape.hot_y = 3;
    for (int i = 0; i < shape.width * shape.height; i++) {
        shape.pixels[i] = static_cast<uint8_t>(i % 3);
    }
    Bytes background(w * h);
    for (int i = 0; i < w * h; i++) {
        background[i] = static_cast<uint8_t>(0x20 + i % 50);
    }
    int checked = 0;
    for (int y = -10; y <= h + 10; y++) {
        for (int x = -12; x <= w + 12; x++) {
            video::PointerState state = {static_cast<int16_t>(x), static_cast<int16_t>(y), true, &shape};
            Bytes want = background;
            reference_pointer(want.data(), w, h, state);
            // Runs of several lengths, including ones that end mid-row and
            // ones longer than a row, as the board's line buffers are.
            for (int run : {1, 7, 40, 64, 1200}) {
                Bytes got = background;
                for (int at = 0; at < w * h; at += run) {
                    int n = at + run > w * h ? w * h - at : run;
                    video::compose_pointer(got.data() + at, at, n, w, state);
                }
                CHECK(got == want);
                checked++;
            }
        }
    }
    CHECK(checked > 1000);

    // Hidden, or no shape: nothing changes.
    Bytes got = background;
    video::PointerState hidden = {10, 10, false, &shape};
    video::compose_pointer(got.data(), 0, w * h, w, hidden);
    video::PointerState no_shape = {10, 10, true, nullptr};
    video::compose_pointer(got.data(), 0, w * h, w, no_shape);
    CHECK(got == background);
}

static void test_screenshot_pointer()
{
    Rig rig;
    for (int i = 0; i < 640 * 480; i++) {
        rig.display.pixels()[i] = static_cast<uint8_t>(0x40 + (i / 640) % 7);
    }
    rig.pointer.set_shape(video::arrow_pointer());
    rig.pointer.set_visible(true);
    // Middle, and clipped at each edge and corner.
    const int spots[][2] = {{320, 240}, {0, 0}, {639, 0}, {0, 479}, {639, 479}, {635, 200}, {300, 470}};
    for (const auto &spot : spots) {
        rig.pointer.set_position(spot[0], spot[1]);
        Bytes with(rig.display.pixels(), rig.display.pixels() + 640 * 480);
        reference_pointer(with.data(), 640, 480, rig.pointer.state());
        check_screenshot(rig, {}, with.data());                   // default: with pointer
        check_screenshot(rig, {0}, with.data());                  // flag clear: with pointer
        check_screenshot(rig, {dbg::kAcceptBackReference}, with.data()); // encoding 2, with pointer
        check_screenshot(rig, {dbg::kWithoutPointer});            // without: the framebuffer
        // They differ, and only inside the pointer's box.
        int differ = 0;
        for (int i = 0; i < 640 * 480; i++) {
            if (with[i] != rig.display.pixels()[i]) {
                int x = i % 640, y = i / 640;
                CHECK(x >= spot[0] && x < spot[0] + 12 && y >= spot[1] && y < spot[1] + 19);
                differ++;
            }
        }
        CHECK(differ > 0);
    }
}

int main()
{
    test_cobs();
    test_crc();
    test_ping_and_info();
    test_input();
    test_tasks();
    test_bad_frames();
    test_screenshot();
    test_compose_pointer();
    test_screenshot_pointer();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
