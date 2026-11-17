// The services loop (backlog item 8b): its rounds, its sleeps and its forced
// rests; a viewer's keys and mouse as messages to the UI task; and the whole
// thing with real threads and a real socket: one thread paints and reports
// damage as the UI task does, another serves a VNC viewer as the services
// task does, and at the end the viewer's picture must be the screen's.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <sys/time.h>

#include "core/msg/input.hpp"
#include "core/msg/queue.hpp"
#include "core/net/posix_tcp.hpp"
#include "core/net/rfb.hpp"
#include "core/net/services.hpp"
#include "core/net/web.hpp"
#include "../host_adapters.hpp"
#include "../thread_queue.hpp"

static std::atomic<int> failures{0};
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

using Millis = std::chrono::milliseconds;

/* ---- The loop itself: turns, sleeps, rests ------------------------------------------- */

class FakeWait : public net::Wait {
public:
    void clear() override { added = 0; }
    void add(int, bool, bool) override { added++; }
    bool sleep(uint32_t max_us) override
    {
        slept.push_back(max_us);
        added_at_sleep = added;
        clock->now += max_us == net::kNever ? 1000000 : max_us;
        return ready;
    }
    host::FixedClock *clock = nullptr;
    std::vector<uint32_t> slept;
    int added = 0, added_at_sleep = 0;
    bool ready = false;
};

class FakeService : public net::Service {
public:
    void poll() override
    {
        polls++;
        clock->now += poll_costs_us;
    }
    uint32_t watch(net::Watch &watch) override
    {
        watch.add(7, true, false);
        return wants;
    }
    void stop() override { stops++; }
    host::FixedClock *clock = nullptr;
    int polls = 0, stops = 0;
    uint32_t wants = net::kNever;
    uint32_t poll_costs_us = 0;
};

// Stopping the loop (backlog 5b): asked from any task, done at the next
// round, each service told once.
void test_stop()
{
    host::FixedClock clock;
    FakeWait wait;
    wait.clock = &clock;
    net::Services services(wait, clock);
    FakeService a, b;
    a.clock = b.clock = &clock;
    services.add(a);
    services.add(b);
    CHECK(services.round() && !services.stopped() && wait.slept.back() == net::kNever);
    // A limit on the sleep, so a stop is noticed in time; a shorter wish
    // still wins.
    services.set_longest_sleep(1000000);
    CHECK(services.round() && wait.slept.back() == 1000000);
    a.wants = 5000;
    CHECK(services.round() && wait.slept.back() == 5000);
    services.request_stop();
    const size_t sleeps = wait.slept.size();
    CHECK(!services.round() && services.stopped() && a.stops == 1 && b.stops == 1);
    CHECK(a.polls == 3 && wait.slept.size() == sleeps); // no more turns, no more sleeps
    CHECK(!services.round() && a.stops == 1 && b.stops == 1);
}

void test_rounds()
{
    host::FixedClock clock;
    FakeWait wait;
    wait.clock = &clock;
    net::Services services(wait, clock);
    FakeService a, b;
    a.clock = b.clock = &clock;
    CHECK(services.add(a) && services.add(b));

    // Nobody wants anything by the clock: one sleep with no limit, on what
    // both asked to watch.
    services.round();
    CHECK(a.polls == 1 && b.polls == 1);
    CHECK(wait.slept.size() == 1 && wait.slept[0] == net::kNever && wait.added_at_sleep == 2);
    // The shortest wish wins.
    a.wants = 50000;
    b.wants = 10000;
    wait.ready = true;
    services.round();
    CHECK(wait.slept.back() == 10000);
    CHECK(services.stats().rounds == 2 && services.stats().sleeps == 2 && services.stats().woken == 1);

    // Work left: straight back, no sleep, for kWorkUs. Then a rest that
    // nothing can end early (nothing is watched), and the count starts again.
    b.wants = 0;
    b.poll_costs_us = 4000;
    const size_t before = wait.slept.size();
    int rounds = 0;
    while (wait.slept.size() == before && rounds < 100) {
        services.round();
        rounds++;
    }
    CHECK(rounds >= 10 && rounds <= 12); // 40 ms of 4 ms turns
    CHECK(wait.slept.back() == net::Services::kRestUs && wait.added_at_sleep == 0);
    CHECK(services.stats().rests == 1);
    services.round();
    CHECK(wait.slept.size() == before + 1); // working again, not resting every round
    // A real sleep in between ends the count: no rest is owed after it.
    b.wants = 20000;
    services.round();
    b.wants = 0;
    services.round();
    CHECK(services.stats().rests == 1);

    // No more than kMax services.
    net::Services full(wait, clock);
    for (int i = 0; i < net::Services::kMax; i++) {
        CHECK(full.add(a));
    }
    CHECK(!full.add(a));
}

/* ---- Keys and mouse as messages ------------------------------------------------------ */

class Recorder : public ports::InputSink {
public:
    void key(uint16_t usage, bool pressed) override
    {
        log += "k" + std::to_string(usage) + (pressed ? "+" : "-") + " ";
        events++;
    }
    void mouse(const ports::MouseEvent &e) override
    {
        log += "m" + std::to_string(e.x) + "," + std::to_string(e.y) + "," + std::to_string(e.buttons) + "," +
               std::to_string(e.wheel) + (e.absolute ? "a" : "r") + " ";
        events++;
    }
    void text(const char *utf8) override
    {
        log += std::string("t[") + utf8 + "] ";
        typed += utf8;
        events++;
    }
    std::string log, typed;
    int events = 0;
};

void test_posted_input()
{
    host::ThreadQueue queue;
    msg::set_queue(&queue);
    msg::Inbox inbox(queue, nullptr);
    Recorder desktop;
    msg::PostedInput input(desktop, 0);

    input.key(0x04, true);
    input.mouse({true, 639, 479, 5, -1});
    input.mouse({false, -3, -200, 0, 1});
    input.key(0xE1, false);
    CHECK(desktop.events == 0); // nothing reaches the desktop on the sender's task
    while (inbox.deliver()) {
    }
    CHECK(desktop.log == "k4+ m639,479,5,-1a m-3,-200,0,1r k225- ");

    // Text: short in one message, long in several, never cut inside a
    // character (ñ and € are two and three bytes).
    desktop.log.clear();
    input.text("a");
    input.text("\xC3\xB1");
    const char longer[] = "se\xC3\xB1or \xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC 123456789";
    input.text(longer);
    input.text("");
    while (inbox.deliver()) {
    }
    CHECK(desktop.typed == std::string("a\xC3\xB1") + longer);
    CHECK(desktop.log.find("t[a] t[\xC3\xB1] t[se\xC3\xB1or ] t[\xE2\x82\xAC\xE2\x82\xAC] t[\xE2\x82\xAC 123] t[456789] ") == 0);

    // A full queue: the event is lost and counted, the sender goes on.
    for (int i = 0; i < host::ThreadQueue::kDepth + 3; i++) {
        input.key(0x05, true);
    }
    CHECK(input.lost() == 3);
    while (inbox.deliver()) {
    }
    msg::set_queue(nullptr);
}

/* ---- What the remote screen asks the loop to watch ------------------------------------ */

class ListWatch : public net::Watch {
public:
    struct One {
        int handle;
        bool readable, writable;
    };
    void add(int handle, bool readable, bool writable) override { all.push_back({handle, readable, writable}); }
    std::vector<One> all;
};

int connect_to(int port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return fd;
}

// Exactly `size` bytes within the time, or false.
bool read_all(int fd, uint8_t *out, size_t size, int timeout_ms)
{
    size_t have = 0;
    while (have < size) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        timeval time = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        if (select(fd + 1, &set, nullptr, nullptr, &time) <= 0) {
            return false;
        }
        const ssize_t got = recv(fd, out + have, size - have, 0);
        if (got <= 0) {
            return false;
        }
        have += static_cast<size_t>(got);
    }
    return true;
}

void send_all(int fd, const uint8_t *data, size_t size) { CHECK(send(fd, data, size, MSG_NOSIGNAL) == static_cast<ssize_t>(size)); }

void test_watch_sockets()
{
    host::MemoryDisplay display(640, 480);
    host::MemoryPointer pointer;
    host::SteadyClock clock;
    host::HostMemory memory;
    Recorder input;
    rfb::Server server(display, pointer, input, clock, memory);
    net::PosixTcpListener vnc;
    net::PosixTcpPool web_port;
    net::WebGate web;
    CHECK(vnc.listen(0, true) && web_port.listen(0, true));
    web.set_port(&web_port);
    web.set_page("<p>hello</p>", 12);
    server.set_listener(&vnc);
    server.set_listener(&web, 1);
    net::PosixWait wait;

    // Nobody there: the two listening sockets, and no limit on the sleep.
    ListWatch idle;
    CHECK(server.watch(idle) == net::kNever);
    CHECK(idle.all.size() == 2 && idle.all[0].readable && !idle.all[0].writable);
    wait.clear();
    server.watch(wait);
    CHECK(!wait.sleep(20000)); // and nothing ends it

    // Someone knocks: the sleep ends at once, long before its limit.
    const int viewer = connect_to(vnc.port());
    CHECK(viewer >= 0);
    wait.clear();
    server.watch(wait);
    const auto start = std::chrono::steady_clock::now();
    CHECK(wait.sleep(5000000));
    CHECK(std::chrono::steady_clock::now() - start < Millis(1000));
    server.poll();
    CHECK(server.stats().connected);
    // Greeting sent; the server waits for the viewer's bytes, with no limit.
    ListWatch greeting;
    CHECK(server.watch(greeting) == net::kNever);
    CHECK(greeting.all.size() == 3 && greeting.all[2].readable && !greeting.all[2].writable);

    uint8_t bytes[64];
    CHECK(read_all(viewer, bytes, 12, 1000));
    send_all(viewer, reinterpret_cast<const uint8_t *>("RFB 003.008\n"), 12);
    const auto turn = [&] {
        wait.clear();
        server.watch(wait);
        CHECK(wait.sleep(1000000));
        server.poll();
    };
    turn();
    CHECK(read_all(viewer, bytes, 2, 1000));
    const uint8_t one = 1;
    send_all(viewer, &one, 1);
    turn();
    CHECK(read_all(viewer, bytes, 4, 1000));
    send_all(viewer, &one, 1);
    turn();
    CHECK(read_all(viewer, bytes, 24 + 10, 1000));
    // Running, and the viewer has asked for nothing: no limit.
    ListWatch quiet;
    CHECK(server.watch(quiet) == net::kNever);
    // It asks for what changes, and nothing has: the damage would come from
    // the UI task, which cannot end the sleep, so the loop looks by the clock.
    const uint8_t request[10] = {3, 1, 0, 0, 0, 0, 640 >> 8, 640 & 255, 480 >> 8, 480 & 255};
    send_all(viewer, request, sizeof(request));
    turn();
    ListWatch waiting;
    CHECK(server.watch(waiting) == rfb::Server::kBusyPollUs);
    // Something is painted: sent at the next turn, and the viewer has what
    // it asked for again.
    server.damage({0, 0, 16, 16});
    server.poll();
    CHECK(read_all(viewer, bytes, 4 + 12, 1000));
    CHECK(server.watch(quiet) == net::kNever);

    // The web page: a browser connects and says nothing yet (watched for
    // reading), then asks (answered within the same turn here, and its
    // connection is closed).
    const int browser = connect_to(web_port.port());
    CHECK(browser >= 0);
    wait.clear();
    server.watch(wait);
    CHECK(wait.sleep(1000000));
    server.poll();
    ListWatch reading;
    server.watch(reading);
    CHECK(reading.all.size() == 4 && reading.all[2].readable && !reading.all[2].writable);
    const char get[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    send_all(browser, reinterpret_cast<const uint8_t *>(get), sizeof(get) - 1);
    wait.clear();
    server.watch(wait);
    CHECK(wait.sleep(1000000));
    server.poll();
    char answer[256] = {};
    CHECK(recv(browser, answer, sizeof(answer) - 1, 0) > 0);
    CHECK(strstr(answer, "200 OK") != nullptr && strstr(answer, "<p>hello</p>") != nullptr);
    close(browser);
    close(viewer);
    wait.clear();
    server.watch(wait);
    CHECK(wait.sleep(1000000));
    server.poll();
    CHECK(!server.stats().connected);
}

/* ---- Two tasks: one paints, one serves ------------------------------------------------ */

// A VNC viewer just clever enough: raw pixels, 8 bits, the screen's own
// format, which is what the server offers when nothing else is asked for.
class Viewer {
public:
    bool open(int port)
    {
        fd_ = connect_to(port);
        uint8_t in[64];
        if (fd_ < 0 || !read_all(fd_, in, 12, 2000)) {
            return false;
        }
        send_all(fd_, reinterpret_cast<const uint8_t *>("RFB 003.008\n"), 12);
        const uint8_t one = 1;
        if (!read_all(fd_, in, 2, 2000)) {
            return false;
        }
        send_all(fd_, &one, 1);
        if (!read_all(fd_, in, 4, 2000)) {
            return false;
        }
        send_all(fd_, &one, 1);
        if (!read_all(fd_, in, 24, 2000)) {
            return false;
        }
        width_ = in[0] << 8 | in[1];
        height_ = in[2] << 8 | in[3];
        const uint32_t name = static_cast<uint32_t>(in[20]) << 24 | in[21] << 16 | in[22] << 8 | in[23];
        picture.assign(static_cast<size_t>(width_) * height_, 0);
        return in[4] == 8 && name < sizeof(in) && read_all(fd_, in, name, 2000);
    }
    void request(bool incremental)
    {
        const uint8_t m[10] = {3,
                               static_cast<uint8_t>(incremental),
                               0,
                               0,
                               0,
                               0,
                               static_cast<uint8_t>(width_ >> 8),
                               static_cast<uint8_t>(width_),
                               static_cast<uint8_t>(height_ >> 8),
                               static_cast<uint8_t>(height_)};
        send_all(fd_, m, sizeof(m));
    }
    void key(uint32_t keysym, bool down)
    {
        const uint8_t m[8] = {4, static_cast<uint8_t>(down), 0, 0, 0, 0, static_cast<uint8_t>(keysym >> 8),
                              static_cast<uint8_t>(keysym)};
        send_all(fd_, m, sizeof(m));
    }
    void pointer(int x, int y)
    {
        const uint8_t m[6] = {5, 0, static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x),
                              static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y)};
        send_all(fd_, m, sizeof(m));
    }
    // One update into `picture`. False: none came in that time (or the
    // stream broke, which `broken` says).
    bool update(int timeout_ms)
    {
        uint8_t head[12];
        if (!read_all(fd_, head, 1, timeout_ms)) {
            return false;
        }
        if (head[0] != 0 || !read_all(fd_, head + 1, 3, 2000)) {
            broken = true;
            return false;
        }
        const int count = head[2] << 8 | head[3];
        for (int i = 0; i < count; i++) {
            if (!read_all(fd_, head, 12, 2000)) {
                broken = true;
                return false;
            }
            const int x = head[0] << 8 | head[1], y = head[2] << 8 | head[3];
            const int w = head[4] << 8 | head[5], h = head[6] << 8 | head[7];
            const bool raw = head[8] == 0 && head[9] == 0 && head[10] == 0 && head[11] == 0;
            if (!raw || x + w > width_ || y + h > height_) {
                broken = true;
                return false;
            }
            for (int row = 0; row < h; row++) {
                if (!read_all(fd_, picture.data() + static_cast<size_t>(y + row) * width_ + x,
                              static_cast<size_t>(w), 2000)) {
                    broken = true;
                    return false;
                }
            }
            rectangles++;
        }
        updates++;
        return true;
    }
    void shut()
    {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    std::vector<uint8_t> picture;
    bool broken = false;
    std::atomic<int> updates{0}, rectangles{0};

private:
    int fd_ = -1;
    int width_ = 0, height_ = 0;
};

uint32_t next_random(uint32_t &state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

void test_two_tasks()
{
    static host::MemoryDisplay display(640, 480);
    static host::MemoryPointer pointer;
    static host::SteadyClock clock;
    static host::HostMemory memory;
    static host::ThreadQueue queue;
    static host::ThreadLock damage_lock;
    static Recorder desktop; // touched by the "UI task" only: this thread
    msg::set_queue(&queue);
    msg::Inbox inbox(queue, nullptr);
    static msg::PostedInput input(desktop);
    static rfb::Server server(display, pointer, input, clock, memory);
    server.set_lock(&damage_lock);
    static net::PosixTcpListener vnc;
    CHECK(vnc.listen(0, true));
    server.set_listener(&vnc);
    static net::PosixWait wait;
    static net::Services services(wait, clock);
    services.add(server);
    memset(display.pixels(), 0x49, 640 * 480);

    // The services task.
    std::atomic<bool> stop{false};
    std::atomic<uint32_t> rounds{0};
    std::thread serving([&] {
        while (!stop) {
            services.round();
            rounds++;
        }
    });

    // With nobody connected it sleeps: not one round in 300 ms.
    std::this_thread::sleep_for(Millis(100));
    const uint32_t rounds_idle = rounds;
    std::this_thread::sleep_for(Millis(300));
    CHECK(rounds == rounds_idle);

    // The viewer, a third thread (a program somewhere else, really).
    std::atomic<bool> painting{true}, viewer_done{false};
    std::atomic<int> keys_sent{0};
    static Viewer viewer;
    std::string sent_log;
    std::thread viewing([&] {
        if (!viewer.open(vnc.port())) {
            CHECK(!"the viewer could not connect");
            viewer_done = true;
            return;
        }
        viewer.request(false);
        CHECK(viewer.update(2000)); // the whole screen
        bool asked = false;
        int n = 0;
        while (!viewer.broken) {
            const bool still_painting = painting;
            if (!asked) {
                viewer.request(true);
                asked = true;
                if (still_painting && n < 400) {
                    // A key down and up and a pointer move with every request.
                    const uint32_t letter = 'a' + static_cast<uint32_t>(n % 26);
                    const int x = n % 640, y = (n * 7) % 480;
                    viewer.key(letter, true);
                    viewer.key(letter, false);
                    viewer.pointer(x, y);
                    const std::string usage = std::to_string(4 + n % 26);
                    sent_log += "k" + usage + "+ k" + usage + "- m" + std::to_string(x) + "," + std::to_string(y) +
                                ",0,0a ";
                    n++;
                    keys_sent = n * 3;
                }
            }
            if (viewer.update(300)) {
                asked = false;
            } else if (!still_painting) {
                break; // painting was over before a quiet 300 ms: it has everything
            }
        }
        viewer_done = true;
    });

    // The UI task: paints rectangles straight into the screen and reports
    // each one after painting it, as the window manager does; takes its
    // messages in between.
    // It goes on for at least half a second and until the viewer has had 40
    // updates, however slow the computer (or the thread checker) is.
    uint32_t seed = 12345;
    const auto began = std::chrono::steady_clock::now();
    const auto painted_enough = [&] {
        const auto spent = std::chrono::steady_clock::now() - began;
        return (spent > Millis(500) && viewer.updates >= 40) || spent > Millis(15000);
    };
    for (int i = 0; !painted_enough() && !viewer_done; i++) {
        const int x = static_cast<int>(next_random(seed) % 600), y = static_cast<int>(next_random(seed) % 440);
        const int w = 1 + static_cast<int>(next_random(seed) % 120), h = 1 + static_cast<int>(next_random(seed) % 90);
        const gfx::Rect r = gfx::Rect{x, y, w, h}.intersect({0, 0, 640, 480});
        const uint8_t colour = static_cast<uint8_t>(next_random(seed));
        for (int row = r.y; row < r.bottom(); row++) {
            memset(display.pixels() + row * 640 + r.x, colour, static_cast<size_t>(r.w));
        }
        server.damage(r);
        inbox.deliver();
        if (i % 8 == 7) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    painting = false;
    const auto deadline = std::chrono::steady_clock::now() + Millis(20000);
    while (!viewer_done && std::chrono::steady_clock::now() < deadline) {
        inbox.deliver();
        std::this_thread::sleep_for(Millis(1));
    }
    CHECK(viewer_done);
    viewing.join();
    while (desktop.events < keys_sent && std::chrono::steady_clock::now() < deadline) {
        inbox.deliver();
        std::this_thread::sleep_for(Millis(1));
    }

    // The viewer has the screen as it is, every pixel: no damage was lost
    // between the two tasks, and nothing half painted was left standing.
    CHECK(!viewer.broken);
    CHECK(viewer.updates > 20);
    CHECK(memcmp(viewer.picture.data(), display.pixels(), 640 * 480) == 0);
    // Every key and pointer event arrived on this thread, in order.
    CHECK(keys_sent > 30);
    CHECK(desktop.events == keys_sent);
    CHECK(desktop.log == sent_log);
    CHECK(input.lost() == 0);
    printf("two tasks: %d updates, %d rectangles, %d input events, %u rounds of the services loop\n",
           viewer.updates.load(), viewer.rectangles.load(), desktop.events, static_cast<unsigned>(rounds.load()));

    // The viewer leaves; the services task goes back to sleep.
    viewer.shut();
    std::this_thread::sleep_for(Millis(200));
    const uint32_t rounds_after = rounds;
    std::this_thread::sleep_for(Millis(300));
    CHECK(rounds == rounds_after);

    // To end it: a knock at the door wakes it, and it sees the flag.
    stop = true;
    const int knock = connect_to(vnc.port());
    serving.join();
    close(knock);
    msg::set_queue(nullptr);
}

} // namespace

int main()
{
    test_rounds();
    test_stop();
    test_posted_input();
    test_watch_sockets();
    test_two_tasks();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
