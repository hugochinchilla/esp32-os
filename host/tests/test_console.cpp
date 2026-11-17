// The serial console sharing the wire with the debug link: which bytes are
// frames and which were typed, the way out after a stray zero, and a whole
// session with the debug tool's requests in between.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/dbg/cobs.hpp"
#include "core/dbg/crc.hpp"
#include "core/dbg/protocol.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/sh/console.hpp"
#include "core/shell/boot.hpp"
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

using Bytes = std::vector<uint8_t>;

struct Frame {
    uint8_t type;
    uint8_t seq;
    Bytes payload;
};

// What the board sent, told apart as the PC's tool does: between two zeros,
// what passes the check is a frame and the rest is text.
class Wire : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        bytes_.insert(bytes_.end(), data, data + size);
    }
    void read(std::vector<Frame> &frames, std::string &text)
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        frames.clear();
        text.clear();
        Bytes piece;
        const auto close = [&] {
            if (piece.empty()) {
                return;
            }
            Bytes raw(piece.size());
            const size_t size = dbg::cobs_decode(piece.data(), piece.size(), raw.data());
            if (size >= 4 && dbg::crc16(raw.data(), size - 2) == static_cast<uint16_t>(raw[size - 2] | (raw[size - 1] << 8))) {
                frames.push_back({raw[0], raw[1], Bytes(raw.begin() + 2, raw.begin() + static_cast<long>(size) - 2)});
            } else {
                text.append(piece.begin(), piece.end());
            }
            piece.clear();
        };
        for (uint8_t byte : bytes_) {
            if (byte != 0) {
                piece.push_back(byte);
            } else {
                close();
            }
        }
        close();
    }
    std::string text()
    {
        std::vector<Frame> frames;
        std::string t;
        read(frames, t);
        return t;
    }
    int replies(uint8_t type)
    {
        std::vector<Frame> frames;
        std::string t;
        read(frames, t);
        int n = 0;
        for (const Frame &f : frames) {
            n += f.type == static_cast<uint8_t>(type | dbg::kReplyBit);
        }
        return n;
    }
    void clear()
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        bytes_.clear();
    }

private:
    std::mutex mutex_;
    Bytes bytes_;
};

class Typed : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override { text.append(data, data + size); }
    std::string text;
};

class NoInput : public ports::InputSink {
public:
    void key(uint16_t, bool) override {}
    void mouse(const ports::MouseEvent &) override {}
};

Bytes make_frame(uint8_t type, uint8_t seq, const std::string &payload, bool good = true)
{
    Bytes raw = {type, seq};
    raw.insert(raw.end(), payload.begin(), payload.end());
    uint16_t crc = dbg::crc16(raw.data(), raw.size());
    if (!good) {
        crc ^= 0x5555;
    }
    raw.push_back(static_cast<uint8_t>(crc));
    raw.push_back(static_cast<uint8_t>(crc >> 8));
    Bytes wire(dbg::cobs_max_encoded(raw.size()) + 2);
    wire[0] = 0;
    const size_t size = dbg::cobs_encode(raw.data(), raw.size(), wire.data() + 1);
    wire[size + 1] = 0;
    wire.resize(size + 2);
    return wire;
}

struct Rig {
    host::MemoryDisplay display{640, 480};
    host::MemoryPointer pointer;
    host::ThreadLock lock;
    host::HostSystem system;
    host::FixedClock clock;
    NoInput input;
    Wire wire;
    dbg::Protocol protocol{wire, lock, display, pointer, input, system, "test"};

    void feed(const Bytes &b) { protocol.feed(b.data(), b.size()); }
    void feed(const char *text) { protocol.feed(reinterpret_cast<const uint8_t *>(text), strlen(text)); }
};

Bytes operator+(Bytes a, const Bytes &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
Bytes bytes(const char *text) { return Bytes(text, text + strlen(text)); }

void test_which_is_which()
{
    // Without a console, as it always was: what is not a frame is dropped.
    {
        Rig r;
        r.feed("dir\r");
        r.feed(make_frame(dbg::kPing, 1, "a"));
        r.protocol.line_idle();
        CHECK(r.wire.replies(dbg::kPing) == 1 && r.protocol.console_bytes() == 0);
    }

    Rig r;
    Typed typed;
    r.protocol.set_console(&typed, &r.clock);

    // Typed text arrives at once, with no zero needed after it.
    r.feed("di");
    CHECK(typed.text == "di");
    r.feed("r\r");
    CHECK(typed.text == "dir\r" && r.protocol.console_bytes() == 4 && r.protocol.frames_received() == 0);

    // A frame is answered and none of it is typing, though its bytes are
    // letters and an Enter.
    typed.text.clear();
    r.feed(make_frame(dbg::kPing, 2, "del *.*\r\n"));
    CHECK(typed.text.empty() && r.wire.replies(dbg::kPing) == 1 && r.protocol.frames_received() == 1);

    // Both in one piece, and a frame that arrives a byte at a time with
    // typing before and after.
    r.wire.clear();
    r.feed(bytes("ver") + make_frame(dbg::kPing, 3, "x") + bytes("\rps") + make_frame(dbg::kPing, 4, "y") + bytes("\r"));
    CHECK(typed.text == "ver\rps\r" && r.wire.replies(dbg::kPing) == 2);
    typed.text.clear();
    r.wire.clear();
    const Bytes slow = bytes("a") + make_frame(dbg::kPing, 5, "format c:\r") + bytes("b");
    for (uint8_t one : slow) {
        r.protocol.feed(&one, 1);
        r.protocol.line_idle(); // the loop finds nothing more each time
        r.clock.now += 1000;
    }
    CHECK(typed.text == "ab" && r.wire.replies(dbg::kPing) == 1);

    // Frames back to back, and with more zeros than needed between them.
    typed.text.clear();
    r.wire.clear();
    r.feed(make_frame(dbg::kPing, 6, "1") + make_frame(dbg::kPing, 7, "2") + Bytes{0, 0, 0} + make_frame(dbg::kPing, 8, "3"));
    CHECK(typed.text.empty() && r.wire.replies(dbg::kPing) == 3);
    // After a run of zeros the wire is "inside a frame" with nothing in it:
    // typing is held back, until the line has been quiet long enough.
    r.feed(Bytes{0, 0});
    r.feed("ok\r");
    CHECK(typed.text.empty());
    r.protocol.line_idle();
    r.clock.now += dbg::Protocol::kFrameWaitUs - 1;
    r.protocol.line_idle();
    CHECK(typed.text.empty());
    r.clock.now += 1;
    r.protocol.line_idle();
    CHECK(typed.text == "ok\r");

    // A frame that fails its check is dropped, and a good one straight
    // after it is still a frame: its bytes are not typed.
    typed.text.clear();
    r.wire.clear();
    const uint32_t errors = r.protocol.receive_errors();
    r.feed(make_frame(dbg::kPing, 9, "rm -r c:\r", false) + make_frame(dbg::kPing, 10, "rm -r c:\r"));
    CHECK(typed.text.empty() && r.wire.replies(dbg::kPing) == 1 && r.protocol.receive_errors() == errors + 1);
    // The same with the two sharing one zero between them.
    Bytes bad = make_frame(dbg::kPing, 11, "del a\r", false);
    Bytes good = make_frame(dbg::kPing, 12, "del a\r");
    bad.pop_back();
    r.wire.clear();
    r.feed(bad + good);
    CHECK(typed.text.empty() && r.wire.replies(dbg::kPing) == 1);

    // A stray zero (noise, Ctrl+@): what is typed after it waits, and comes
    // through once nothing has arrived for half a second. Bytes arriving
    // start the wait again.
    typed.text.clear();
    r.feed(Bytes{0});
    r.feed("he");
    r.protocol.line_idle();
    r.clock.now += 400000;
    r.feed("lp\r");
    r.protocol.line_idle();
    r.clock.now += 400000;
    r.protocol.line_idle();
    CHECK(typed.text.empty());
    r.clock.now += 100000;
    r.protocol.line_idle();
    CHECK(typed.text == "help\r");
    // And the wire is as new: typing is typing, a frame is a frame.
    r.wire.clear();
    r.feed(bytes("x") + make_frame(dbg::kPing, 13, "z"));
    CHECK(typed.text == "help\rx" && r.wire.replies(dbg::kPing) == 1);

    // A frame whose pieces come slowly, but with no long silence between
    // them, is still a frame; the clock going round changes nothing.
    typed.text.clear();
    r.wire.clear();
    r.clock.now = 0xFFFFFFFFu - 600000;
    const Bytes late = make_frame(dbg::kPing, 14, "copy a b\r");
    for (uint8_t one : late) {
        r.protocol.feed(&one, 1);
        r.protocol.line_idle();
        r.clock.now += 300000;
        r.protocol.line_idle();
    }
    CHECK(typed.text.empty() && r.wire.replies(dbg::kPing) == 1);

    // Longer than any frame: counted as an error, not typed.
    typed.text.clear();
    Bytes huge = {0};
    huge.insert(huge.end(), 3000, 'A');
    huge.push_back(0);
    r.feed(huge);
    r.protocol.line_idle();
    r.clock.now += dbg::Protocol::kFrameWaitUs;
    r.protocol.line_idle();
    CHECK(typed.text.empty());

    // What the console shows goes out as it is, and the tool still finds
    // its frames around it.
    r.wire.clear();
    r.protocol.send_text(reinterpret_cast<const uint8_t *>("C:\\> "), 5);
    r.feed(make_frame(dbg::kPing, 15, "q"));
    r.protocol.send_text(reinterpret_cast<const uint8_t *>("dir"), 3);
    r.protocol.send_log("a log line\n", 11);
    r.protocol.send_text(nullptr, 0);
    std::vector<Frame> frames;
    std::string text;
    r.wire.read(frames, text);
    CHECK(text == "C:\\> dir" && frames.size() == 2 && frames[0].seq == 15 && frames[1].type == dbg::kLog);
}

/* ---- A whole session on the wire --------------------------------------------------- */

// What the session shows goes to the wire through the protocol.
class Shown : public ports::ByteSink {
public:
    explicit Shown(dbg::Protocol &protocol) : protocol_(protocol) {}
    void write(const uint8_t *data, size_t size) override { protocol_.send_text(data, size); }

private:
    dbg::Protocol &protocol_;
};

template <typename Done> bool until(Done done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool ends_with(const std::string &text, const char *end)
{
    const size_t n = strlen(end);
    return text.size() >= n && text.compare(text.size() - n, n, end) == 0;
}

void test_session()
{
    Rig r;
    std::vector<uint8_t> pool(256 * 1024);
    fs::MemoryVolume c(pool.data(), pool.size(), "RAM disk");
    fs::Storage storage;
    host::ThreadLock files, tty_lock;
    host::ThreadEvent typed_event;
    host::ThreadTasks tasks;
    host::HostMemory memory;
    storage.mount('C', c);
    storage.set_lock(&files);
    const char words[] = "one\ntwo\n";
    storage.write_file("C:\\words.txt", words, sizeof(words) - 1);
    sh::Env env;
    env.storage = &storage;
    env.system = &r.system;
    env.memory = &memory;
    sh::ConsoleKit kit;
    kit.lock = &tty_lock;
    kit.typed = &typed_event;

    // No tasks to be had: it says so and nothing runs.
    {
        Shown shown(r.protocol);
        sh::Console none(env, kit, shown);
        CHECK(!none.start() && !none.running());
        kit.tasks = &tasks;
        tasks.refuse = true;
        sh::Console refused(env, kit, shown);
        CHECK(!refused.start() && !refused.running());
        tasks.refuse = false;
    }

    Shown shown(r.protocol);
    const auto prompt = [&] { return ends_with(r.wire.text(), "C:\\> "); };
    {
        sh::Console console(env, kit, shown);
        r.protocol.set_console(&console.typed(), &r.clock);
        CHECK(console.start() && console.running() && console.start());
        CHECK(tasks.started == 1 && tasks.last_priority == ports::Priority::Normal);

        // The greeting and a prompt, as plain text on the wire.
        CHECK(until(prompt));
        CHECK(r.wire.text().rfind(std::string("Chinchilla ") + shell::kVersion + "\r\n", 0) == 0);
        CHECK(console.sessions() == 1);

        // A command typed as a serial terminal sends it, with the tool's
        // requests arriving in the middle of the line.
        r.wire.clear();
        r.feed(bytes("type C:\\wor") + make_frame(dbg::kPing, 1, "p") + bytes("ds.txt\r"));
        CHECK(until(prompt));
        CHECK(r.wire.text() == "type C:\\words.txt\r\none\r\ntwo\r\nC:\\> " && r.wire.replies(dbg::kPing) == 1);

        // The tool goes on working while a command is waiting for a key.
        r.wire.clear();
        r.feed("more C:\\words.txt | more\r");
        CHECK(until(prompt));
        for (uint8_t seq = 2; seq < 30; seq++) {
            r.feed(make_frame(dbg::kPing, seq, "still here"));
        }
        CHECK(r.wire.replies(dbg::kPing) == 28);

        // exit: the session ends and the next one greets, on the same task.
        r.wire.clear();
        r.feed("exit\r");
        CHECK(until([&] { return console.sessions() == 2 && prompt(); }));
        CHECK(r.wire.text().find(std::string("Chinchilla ") + shell::kVersion) != std::string::npos);
        CHECK(tasks.started == 1 && tasks.ended == 0);
        r.wire.clear();
        r.feed("echo again\r");
        CHECK(until(prompt));
        CHECK(r.wire.text() == "echo again\r\nagain\r\nC:\\> ");
        r.protocol.set_console(nullptr, nullptr);
    }
    // Taken down: the task is given back.
    CHECK(tasks.started == 1 && tasks.ended == 1);
}

} // namespace

int main()
{
    test_which_is_which();
    test_session();
    if (failures == 0) {
        printf("console: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
