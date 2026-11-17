// The message queue into the UI task: order, the full queue, messages to a
// process that has ended, posting from real threads, the sleep that a wake
// ends, and how long the window manager and the remote screen let the UI
// loop sleep.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/msg/queue.hpp"
#include "core/net/rfb.hpp"
#include "core/proc/table.hpp"
#include "core/saver/saver.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"
#include "../thread_queue.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

using Millis = std::chrono::milliseconds;

uint32_t elapsed_ms(std::chrono::steady_clock::time_point since)
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<Millis>(std::chrono::steady_clock::now() - since).count());
}

/* ---- One task: order, bursts, the full queue --------------------------------------- */

std::vector<uint32_t> g_seen;
void note(const msg::Message &m) { g_seen.push_back(m.a); }

void test_order_and_limits()
{
    host::ThreadQueue q;
    msg::Inbox inbox(q, nullptr);
    msg::Message none;
    CHECK(!q.take(none) && !inbox.deliver() && inbox.delivered() == 0);

    // In the order posted.
    g_seen.clear();
    for (uint32_t i = 0; i < 5; i++) {
        CHECK(q.post({&note, nullptr, i, 0, proc::kNone}));
    }
    CHECK(q.waiting() == 5);
    CHECK(!inbox.deliver() && inbox.delivered() == 5 && q.waiting() == 0);
    CHECK(g_seen == (std::vector<uint32_t>{0, 1, 2, 3, 4}));

    // The queue holds kDepth; one more is refused at once, or after the
    // wait that was asked for, and nothing already in it is lost.
    g_seen.clear();
    for (uint32_t i = 0; i < host::ThreadQueue::kDepth; i++) {
        CHECK(q.post({&note, nullptr, i, 0, proc::kNone}));
    }
    CHECK(!q.post({&note, nullptr, 99, 0, proc::kNone}) && q.refused() == 1);
    const auto before = std::chrono::steady_clock::now();
    CHECK(!q.post({&note, nullptr, 99, 0, proc::kNone}, 30) && elapsed_ms(before) >= 29);
    CHECK(q.most_waiting() == host::ThreadQueue::kDepth);

    // A burst at a time: the loop is told to come back for the rest.
    CHECK(inbox.deliver() && g_seen.size() == msg::Inbox::kBurst);
    CHECK(inbox.deliver() && g_seen.size() == 2 * msg::Inbox::kBurst && q.waiting() == 0);
    CHECK(!inbox.deliver());
    for (uint32_t i = 0; i < g_seen.size(); i++) {
        CHECK(g_seen[i] == i);
    }

    // A message with no handler is dropped, not called.
    CHECK(q.post({nullptr, nullptr, 0, 0, proc::kNone}));
    CHECK(!inbox.deliver() && inbox.dropped() == 1);
}

/* ---- Addressed to a process --------------------------------------------------------- */

proc::Table *g_table;
std::vector<proc::Pid> g_ran_as;
void who(const msg::Message &m)
{
    g_ran_as.push_back(g_table->current());
    *static_cast<int *>(m.context) += 1;
}

void test_processes()
{
    static proc::Table t;
    g_table = &t;
    host::ThreadQueue q;
    msg::Inbox inbox(q, &t);
    const proc::Pid desktop = t.spawn_service("desktop");
    const proc::Pid editor = t.spawn("Editor", desktop, proc::kWindowed);
    const proc::Pid viewer = t.spawn("Viewer", desktop, proc::kWindowed);
    int editor_calls = 0, viewer_calls = 0, os_calls = 0;

    // The handler runs as the process the message is for; afterwards the UI
    // task is back to whoever it was working for.
    t.set_current(desktop);
    CHECK(q.post({&who, &editor_calls, 0, 0, editor}));
    CHECK(q.post({&who, &viewer_calls, 0, 0, viewer}));
    CHECK(q.post({&who, &os_calls, 0, 0, proc::kNone}));
    inbox.deliver();
    CHECK(g_ran_as == (std::vector<proc::Pid>{editor, viewer, proc::kNone}));
    CHECK(t.current() == desktop && inbox.delivered() == 3);

    // A job finishes after its program has gone: the message is posted while
    // the Editor lives and delivered after it has ended. Its handler, which
    // would touch the Editor's memory, is never called.
    CHECK(q.post({&who, &editor_calls, 0, 0, editor}));
    CHECK(q.post({&who, &viewer_calls, 0, 0, viewer}));
    t.exit(editor);
    inbox.deliver();
    CHECK(editor_calls == 1 && viewer_calls == 2 && os_calls == 1);
    CHECK(inbox.dropped() == 1 && inbox.delivered() == 4);
    // A PID nobody ever had.
    CHECK(q.post({&who, &editor_calls, 0, 0, 999}));
    inbox.deliver();
    CHECK(editor_calls == 1 && inbox.dropped() == 2);
}

/* ---- The machine's one queue --------------------------------------------------------- */

void test_global()
{
    CHECK(msg::queue() == nullptr && !msg::post(proc::kNone, &note)); // none yet: refused, no crash
    host::ThreadQueue q;
    msg::set_queue(&q);
    msg::Inbox inbox(q, nullptr);
    g_seen.clear();
    CHECK(msg::post(proc::kNone, &note, nullptr, 7) && msg::queue() == &q);
    inbox.deliver();
    CHECK(g_seen == (std::vector<uint32_t>{7}));
    msg::set_queue(nullptr);
}

/* ---- Sleeping and waking -------------------------------------------------------------- */

void test_sleep()
{
    host::ThreadQueue q;
    // Nothing happens: the sleep lasts as long as asked, and says so.
    auto before = std::chrono::steady_clock::now();
    CHECK(!q.sleep(40000) && elapsed_ms(before) >= 39);
    CHECK(!q.sleep(0)); // 0 does not sleep

    // A wake that came first is not lost, and is used up by one sleep.
    q.wake();
    before = std::chrono::steady_clock::now();
    CHECK(q.sleep(2000000) && elapsed_ms(before) < 1000);
    CHECK(!q.sleep(1000));
    // A post is a wake too.
    CHECK(q.post({&note, nullptr, 0, 0, proc::kNone}));
    CHECK(q.sleep(2000000));

    // Another thread ends a long sleep early.
    before = std::chrono::steady_clock::now();
    std::thread other([&q] {
        std::this_thread::sleep_for(Millis(30));
        q.wake();
    });
    CHECK(q.sleep(5000000) && elapsed_ms(before) >= 29 && elapsed_ms(before) < 4000);
    other.join();
}

/* ---- Real threads ----------------------------------------------------------------------- */

constexpr int kThreads = 4;
constexpr uint32_t kEach = 2000;
uint32_t g_next[kThreads];
uint32_t g_out_of_order;
std::thread::id g_ui_thread;
uint32_t g_wrong_thread;

void from_thread(const msg::Message &m)
{
    // m.a: which thread, m.b: its count. Each thread's messages in order.
    g_out_of_order += m.b != g_next[m.a];
    g_next[m.a] = m.b + 1;
    g_wrong_thread += std::this_thread::get_id() != g_ui_thread;
}

void test_threads()
{
    host::ThreadQueue q;
    msg::Inbox inbox(q, nullptr);
    g_ui_thread = std::this_thread::get_id();
    std::atomic<int> running{kThreads};
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < kThreads; t++) {
        threads.emplace_back([&q, &running, t] {
            for (uint32_t i = 0; i < kEach; i++) {
                // Faster than the UI task takes them: they wait for room.
                if (!q.post({&from_thread, nullptr, t, i, proc::kNone}, msg::kForever)) {
                    break;
                }
            }
            running--;
            q.wake();
        });
    }
    // The UI loop: run what is there, sleep until there is more.
    uint32_t rounds = 0, woken = 0;
    while (running > 0 || q.waiting() > 0) {
        const bool more = inbox.deliver();
        woken += q.sleep(more ? 0 : 1000000);
        rounds++;
    }
    for (std::thread &t : threads) {
        t.join();
    }
    inbox.deliver();
    CHECK(inbox.delivered() == kThreads * kEach && inbox.dropped() == 0);
    CHECK(g_out_of_order == 0 && g_wrong_thread == 0 && q.refused() == 0);
    for (uint32_t t = 0; t < kThreads; t++) {
        CHECK(g_next[t] == kEach);
    }
    CHECK(q.most_waiting() <= host::ThreadQueue::kDepth);
    printf("threads: %u messages in %u rounds, %u wakes, at most %d waiting\n", inbox.delivered(), rounds, woken,
           q.most_waiting());
}

/* ---- How long the window manager lets the loop sleep ------------------------------------ */

int g_fired;
void fire(void *) { g_fired++; }

void test_idle()
{
    static host::MemoryDisplay display(640, 480);
    static host::MemoryPointer pointer;
    static host::FixedClock clock;
    static wm::Wm desktop(display, pointer, clock);
    desktop.start();
    desktop.tick();
    // Its own timers (the caret, the taskbar clock) are the only ones: the
    // loop may sleep, but not for ever.
    const uint32_t own = desktop.idle_us();
    CHECK(own > 100000 && own <= 1000000);

    // A program's timer brings it forward, to the microsecond.
    const int once = desktop.start_timer(5000, 0, &fire, nullptr);
    CHECK(once != 0 && desktop.idle_us() == 5000);
    clock.now += 2000;
    CHECK(desktop.idle_us() == 3000);
    clock.now += 3000;
    CHECK(desktop.idle_us() == 0); // due now
    g_fired = 0;
    desktop.tick();
    CHECK(g_fired == 1 && desktop.idle_us() > 100000); // a one-shot is gone once fired
    // Stopped: it no longer counts.
    const int stopped = desktop.start_timer(700, 700, &fire, nullptr);
    CHECK(desktop.idle_us() == 700);
    desktop.stop_timer(stopped);
    CHECK(desktop.idle_us() > 100000);

    // Something to paint: at once.
    desktop.invalidate({10, 10, 20, 20});
    CHECK(desktop.idle_us() == 0);
    desktop.tick();
    CHECK(desktop.idle_us() > 100000);

    // Sleeping exactly as long as allowed fires a 70 ms timer as often as
    // looking every 10 ms did, with far fewer rounds of the loop.
    g_fired = 0;
    const int periodic = desktop.start_timer(70000, 70000, &fire, nullptr);
    const uint32_t ten_seconds = clock.now + 10000000;
    int rounds = 0;
    while (static_cast<int32_t>(ten_seconds - clock.now) > 0) {
        const uint32_t left = ten_seconds - clock.now;
        const uint32_t idle = desktop.idle_us();
        clock.now += idle < left ? idle : left;
        desktop.tick();
        rounds++;
    }
    CHECK(g_fired == 142);
    CHECK(rounds < 300); // 1000 rounds at one every 10 ms
    printf("idle: %d timer calls in %d rounds over 10 s (1000 rounds when polling)\n", g_fired, rounds);
    desktop.stop_timer(periodic);

    // The screensaver: the loop wakes to start it, then for each frame.
    static saver::Runner runner(desktop);
    desktop.set_screen_saver(&runner, 60);
    desktop.stop_timer(desktop.start_timer(0, 0, &fire, nullptr)); // no effect; keeps ids moving
    uint64_t waited = 0;
    for (int i = 0; i < 1000 && !desktop.screen_saver_active(); i++) {
        const uint32_t idle = desktop.idle_us();
        CHECK(idle != wm::Wm::kIdleForever);
        clock.now += idle;
        waited += idle;
        desktop.tick();
    }
    CHECK(desktop.screen_saver_active());
    CHECK(waited >= 60000000 && waited < 60100000); // on time, not a round late
    desktop.tick();
    int frames = 0;
    const uint32_t second = clock.now + 1000000;
    while (static_cast<int32_t>(second - clock.now) > 0) {
        const uint32_t idle = desktop.idle_us();
        CHECK(idle <= saver::kFrameUs);
        clock.now += idle;
        desktop.tick();
        frames++;
    }
    CHECK(frames >= 29 && frames <= 62); // 30 frames a second, not a spin
    desktop.stop_screen_saver();
    desktop.set_screen_saver(nullptr, 0);
}

/* ---- How often the remote screen wants its turn ------------------------------------------ */

class QuietStream : public net::Stream {
public:
    int read(uint8_t *, size_t) override { return 0; }
    int write(const uint8_t *, size_t size) override { return static_cast<int>(size); }
    void close() override {}
};

class Door : public net::Listener {
public:
    net::Stream *accept() override
    {
        net::Stream *s = knocking ? &stream : nullptr;
        knocking = false;
        return s;
    }
    bool busy() const override { return busy_now; }
    bool knocking = false, busy_now = false;
    QuietStream stream;
};

class NoInput : public ports::InputSink {
public:
    void key(uint16_t, bool) override {}
    void mouse(const ports::MouseEvent &) override {}
};

// What the server watches does not matter here: these listeners and this
// stream are not sockets, so it can only ask for the clock.
class NoWatch : public net::Watch {
public:
    void add(int, bool, bool) override { added++; }
    int added = 0;
};

void test_network_pace()
{
    static host::MemoryDisplay display(640, 480);
    static host::MemoryPointer pointer;
    static host::FixedClock clock;
    static host::HostMemory memory;
    static NoInput input;
    static rfb::Server server(display, pointer, input, clock, memory);
    NoWatch watch;
    // No port open (no radio): the network never needs the loop.
    CHECK(server.watch(watch) == rfb::Server::kNever);
    // Waiting for a viewer on something that cannot be slept on: now and then.
    static Door vnc, web;
    server.set_listener(&vnc);
    server.set_listener(&web, 1);
    CHECK(server.watch(watch) == rfb::Server::kListenPollUs);
    // The web server is sending a page: often.
    web.busy_now = true;
    CHECK(server.watch(watch) == rfb::Server::kBusyPollUs);
    web.busy_now = false;
    CHECK(server.watch(watch) == rfb::Server::kListenPollUs);
    // A viewer is connected: often.
    vnc.knocking = true;
    server.poll();
    CHECK(server.stats().connected && server.watch(watch) == rfb::Server::kBusyPollUs);
    CHECK(watch.added == 0);
}

} // namespace

int main()
{
    test_global();
    test_order_and_limits();
    test_processes();
    test_sleep();
    test_threads();
    test_idle();
    test_network_pace();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
