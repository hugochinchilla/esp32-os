// The telnet service: a caller gets the command shell. The test is the
// services task (it calls poll()) and the client at once, through a pipe in
// memory; the session runs on a thread, as it runs on a task on the board.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/net/telnet.hpp"
#include "core/proc/table.hpp"
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

// The connection: what the client sent and has not been read, what the
// service sent, and how much the client takes at a time.
class Pipe : public net::Stream, public net::Listener {
public:
    std::string to_server, from_server;
    bool waiting = false, open = false;
    size_t room = 1 << 20;
    int closes = 0;

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
    void close() override
    {
        open = false;
        closes++;
    }
};

struct Machine {
    Machine() : pool(512 * 1024), c(pool.data(), pool.size(), "RAM disk")
    {
        storage.mount('C', c);
        storage.set_lock(&files);
        env.storage = &storage;
        env.system = &system;
        env.memory = &memory;
        kit.lock = &lock;
        kit.typed = &typed;
        kit.room = &room;
        kit.tasks = &tasks;
    }
    std::vector<uint8_t> pool;
    fs::MemoryVolume c;
    fs::Storage storage;
    host::ThreadLock files, lock;
    host::ThreadEvent typed, room;
    host::ThreadTasks tasks;
    host::HostSystem system;
    host::HostMemory memory;
    sh::Env env;
    net::TelnetKit kit;
    Pipe pipe;
};

// What the client would show: what was sent, without telnet's own commands.
std::string text_of(const std::string &sent)
{
    std::string text;
    for (size_t i = 0; i < sent.size(); i++) {
        if (static_cast<uint8_t>(sent[i]) == 255 && i + 2 < sent.size() + 1) {
            i += 2; // IAC, a verb, an option
            continue;
        }
        text += sent[i];
    }
    return text;
}

// The services task's loop, until `done` says yes.
template <typename Done> bool rounds(net::Telnet &telnet, Done done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        telnet.poll();
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

void test_telnet()
{
    Machine m;
    net::Telnet telnet(m.env, m.kit);
    Pipe &pipe = m.pipe;
    const auto shown = [&] { return text_of(pipe.from_server); };
    const auto prompt = [&] { return ends_with(shown(), "C:\\> "); };
    const auto type = [&](const char *keys) { pipe.to_server += keys; };

    // Nobody listening for, nobody connected: nothing happens.
    telnet.poll();
    telnet.set_listener(&pipe);
    telnet.poll();
    CHECK(!telnet.connected() && telnet.sessions() == 0 && m.tasks.started == 0);

    // A caller: telnet's opening (we echo, no go-ahead, tell us your size),
    // then the shell's greeting and its prompt.
    pipe.waiting = true;
    CHECK(rounds(telnet, prompt));
    CHECK(telnet.connected() && telnet.sessions() == 1 && m.tasks.started == 1);
    CHECK(pipe.from_server.rfind("\xFF\xFB\x01\xFF\xFB\x03\xFF\xFD\x1F", 0) == 0);
    CHECK(shown().rfind(std::string("Chinchilla ") + shell::kVersion + "\r\n", 0) == 0);

    // Typing is echoed a key at a time; Enter (CR LF, or CR NUL) runs the line.
    pipe.from_server.clear();
    type("echo hi\r\n");
    CHECK(rounds(telnet, prompt));
    CHECK(shown() == "echo hi\r\nhi\r\nC:\\> ");
    pipe.from_server.clear();
    type("echo");
    type(std::string(" again\r\0", 8).c_str());
    pipe.to_server.push_back('\0');
    CHECK(rounds(telnet, prompt));
    CHECK(shown() == "echo again\r\nagain\r\nC:\\> ");

    // The client's answers and its own offers: its window size is taken,
    // what we do not do is refused, and none of it is typing.
    pipe.from_server.clear();
    type("\xFF\xFD\x01\xFF\xFD\x03\xFF\xFB\x1F");         // DO ECHO, DO SGA, WILL NAWS
    pipe.to_server += std::string("\xFF\xFA\x1F\x00\x64\x00\x28\xFF\xF0", 9); // SB NAWS 100 x 40 SE
    type("\xFF\xFB\x18\xFF\xFD\x05");                     // WILL TERMINAL-TYPE, DO STATUS
    CHECK(rounds(telnet, [&] { return pipe.from_server.size() >= 6 && pipe.to_server.empty(); }));
    CHECK(pipe.from_server == "\xFF\xFE\x18\xFF\xFC\x05"); // DONT TERMINAL-TYPE, WONT STATUS
    CHECK(telnet.terminal()->columns() == 100 && telnet.terminal()->rows() == 40);

    // The same drives as everyone.
    pipe.from_server.clear();
    type("echo kept > t.txt\r\ncd \\\r\nmkdir d\r\ncd d\r\n");
    CHECK(rounds(telnet, [&] { return ends_with(shown(), "C:\\d> "); }));
    fs::DirEntry entry;
    CHECK(m.storage.stat("C:\\t.txt", entry) == fs::FsError::Ok && entry.size == 5);

    // A client that reads slowly, or not at all for a while: the command
    // waits, and nothing is lost.
    {
        std::string big;
        for (int i = 0; i < 4000; i++) {
            big += "line " + std::to_string(i) + "\n";
        }
        m.storage.write_file("C:\\big.txt", big.data(), big.size());
        pipe.from_server.clear();
        pipe.room = 0;
        type("type \\big.txt\r\n");
        for (int i = 0; i < 200; i++) {
            telnet.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(pipe.from_server.empty() && telnet.connected());
        pipe.room = 300;
        CHECK(rounds(telnet, [&] { return ends_with(shown(), "C:\\d> "); }));
        const std::string all = shown();
        CHECK(all.find("line 0\r\nline 1\r\n") != std::string::npos);
        CHECK(all.find("line 3998\r\nline 3999\r\nC:\\d> ") != std::string::npos);
        CHECK(static_cast<size_t>(std::count(all.begin(), all.end(), '\n')) == 4000 + 1);
        CHECK(telnet.dropped() == 0);

        // Ctrl+C gets through while a command is writing, and stops it.
        pipe.from_server.clear();
        pipe.room = 40;
        type("type \\big.txt\r\n");
        CHECK(rounds(telnet, [&] { return shown().find("line 20\r\n") != std::string::npos; }));
        type("\x03");
        CHECK(rounds(telnet, [&] { return ends_with(shown(), "^C\r\nC:\\d> "); }));
        CHECK(shown().find("line 3999") == std::string::npos);
        // Telnet's own "interrupt process" does the same.
        pipe.from_server.clear();
        type("type \\big.txt\r\n");
        CHECK(rounds(telnet, [&] { return shown().find("line 20\r\n") != std::string::npos; }));
        type("\xFF\xF4");
        CHECK(rounds(telnet, [&] { return ends_with(shown(), "^C\r\nC:\\d> "); }));
        pipe.room = 1 << 20;
    }

    // A program that draws the whole screen: ted, 40 rows as the client said.
    pipe.from_server.clear();
    type("ted \\t.txt\r\n");
    CHECK(rounds(telnet, [&] { return shown().find("Ln 1, Col 1") != std::string::npos; }));
    CHECK(shown().find("\x1b[40;1H") != std::string::npos); // the status line, on the last row
    type("\x11");
    CHECK(rounds(telnet, [&] { return ends_with(shown(), "C:\\d> "); }));
    CHECK(m.memory.in_use() == 0);

    // exit: the session ends, its task is given back and the connection closed.
    pipe.from_server.clear();
    type("exit\r\n");
    CHECK(rounds(telnet, [&] { return !telnet.connected(); }));
    CHECK(!pipe.open && pipe.closes == 1 && m.tasks.ended == 1 && telnet.terminal() == nullptr);

    // The next caller gets a session of their own, from the start.
    pipe.from_server.clear();
    pipe.waiting = true;
    CHECK(rounds(telnet, prompt));
    CHECK(telnet.sessions() == 2 && m.tasks.started == 2 && telnet.terminal()->columns() == 80);
    // They leave without a word, in the middle of a command: it is stopped,
    // and everything goes back just the same.
    pipe.room = 10;
    type("type big.txt\r\n");
    CHECK(rounds(telnet, [&] { return shown().find("line 3\r\n") != std::string::npos; }));
    pipe.open = false;
    CHECK(rounds(telnet, [&] { return !telnet.connected(); }));
    CHECK(m.tasks.ended == 2);
    pipe.room = 1 << 20;

    // No memory for a session's task: the caller is told and let go.
    m.tasks.refuse = true;
    pipe.from_server.clear();
    pipe.waiting = true;
    CHECK(rounds(telnet, [&] { return !telnet.connected() && !pipe.from_server.empty(); }));
    CHECK(pipe.from_server == "No memory for a session. Try again later.\r\n" && !pipe.open);
    m.tasks.refuse = false;

    // Taken down with someone connected: their session is ended first.
    pipe.from_server.clear();
    pipe.waiting = true;
    CHECK(rounds(telnet, prompt));
    CHECK(telnet.connected());
}

} // namespace

int main()
{
    test_telnet();
    if (failures == 0) {
        printf("telnet: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
