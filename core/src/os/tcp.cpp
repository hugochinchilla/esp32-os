// Outgoing connections by number (core/os/tcp.hpp).
#include "core/os/tcp.hpp"

#include <atomic>
#include <climits>
#include <cstdio>

#include "core/job/job.hpp"
#include "core/os/os.hpp"
#include "core/prog/prog.hpp"
#include "core/sys/log.hpp"

namespace os {

namespace {

net::Dialer *g_dialer = nullptr;

constexpr int kPending = INT_MIN;

/* The table of connections. All of it is the UI task's, but `result`, which
 * the job that connects writes once from its own task: the line's number,
 * or minus why there is none. */
struct Open {
    bool used = false;
    bool dropped = false; // closed while it was being made: hung up when it is
    proc::Pid owner = proc::kNone;
    uint16_t serial = 0; // tells this use of the slot from the one before
    int port = 0;
    char host[80] = "";
    std::atomic<int> result{kPending};
};

Open g_open[tcp::kMaxOpen];
bool g_hooked = false;

tcp::Handle handle_of(const Open &o)
{
    return (static_cast<tcp::Handle>(o.serial) << 8) | static_cast<tcp::Handle>(&o - g_open + 1);
}

Open *held(tcp::Handle handle)
{
    const int slot = (handle & 0xFF) - 1;
    if (handle <= 0 || slot < 0 || slot >= tcp::kMaxOpen) {
        return nullptr;
    }
    Open &o = g_open[slot];
    return o.used && !o.dropped && o.serial == (handle >> 8) ? &o : nullptr;
}

void give_back(Open &o)
{
    const int line = o.result.load();
    if (line >= 0 && g_dialer != nullptr) {
        g_dialer->hang_up(line);
    }
    o.used = false;
    o.dropped = false;
    o.owner = proc::kNone;
}

// The job: on a task of its own where there are tasks.
uint32_t dial(const job::Job &, void *context)
{
    Open *o = static_cast<Open *>(context);
    o->result = g_dialer->connect(o->host, o->port);
    return 0;
}

// The UI task, when the job has returned.
void dialled(void *context, uint32_t, bool)
{
    Open *o = static_cast<Open *>(context);
    if (o->used && o->dropped) {
        give_back(*o);
    }
}

// A process has ended (its jobs have, by then): what it left open.
void closed_for(proc::Pid pid, void *)
{
    int count = 0;
    for (Open &o : g_open) {
        if (o.used && o.owner == pid) {
            count += o.dropped ? 0 : 1;
            give_back(o);
        }
    }
    if (count > 0) {
        char line[72];
        const int n = snprintf(line, sizeof(line), "os: closed %d connection%s process %d left open\n", count,
                               count == 1 ? "" : "s", pid);
        sys::log().write(line, static_cast<size_t>(n));
    }
}

} // namespace

void install_dialer(net::Dialer *dialer)
{
    g_dialer = dialer;
    if (!g_hooked) {
        g_hooked = true;
        proc::table().on_exit(&closed_for, nullptr);
    }
}

namespace tcp {

bool available() { return g_dialer != nullptr; }

int open_connections(proc::Pid pid)
{
    const prog::InCall in_call;
    int count = 0;
    for (const Open &o : g_open) {
        count += o.used && (pid == proc::kNone || o.owner == pid);
    }
    return count;
}

Handle connect(const char *host, int port)
{
    const prog::InCall in_call;
    const proc::Pid owner = proc::table().current();
    if (g_dialer == nullptr || open_connections(owner) >= kMaxOpenEach) {
        return kNone;
    }
    for (Open &o : g_open) {
        if (o.used) {
            continue;
        }
        if (snprintf(o.host, sizeof(o.host), "%s", host) >= static_cast<int>(sizeof(o.host))) {
            return kNone;
        }
        o.used = true;
        o.dropped = false;
        o.owner = owner;
        o.serial = o.serial >= 0x7FFF ? 1 : o.serial + 1;
        o.port = port;
        o.result = kPending;
        job::Spec spec;
        spec.name = "dial";
        spec.work = &dial;
        spec.done = &dialled;
        spec.context = &o;
        spec.stack_bytes = 8192; // the name service's calls are deep
        // Without tasks the connection is made here and now.
        if (!job::jobs().start(spec)) {
            o.used = false;
            o.owner = proc::kNone;
            return kNone;
        }
        return handle_of(o);
    }
    return kNone;
}

State state(Handle connection)
{
    const prog::InCall in_call;
    const Open *o = held(connection);
    if (o == nullptr) {
        return State::Closed;
    }
    const int result = o->result.load();
    return result == kPending ? State::Connecting : result < 0 ? State::Failed : State::Open;
}

net::DialFailure failure(Handle connection)
{
    const prog::InCall in_call;
    const Open *o = held(connection);
    const int result = o != nullptr ? o->result.load() : kPending;
    return result < 0 && result != kPending ? static_cast<net::DialFailure>(-result) : net::DialFailure::None;
}

int read(Handle connection, void *out, size_t capacity)
{
    const prog::InCall in_call;
    const Open *o = held(connection);
    const int line = o != nullptr ? o->result.load() : -1;
    return line >= 0 ? g_dialer->read(line, static_cast<uint8_t *>(out), capacity) : -1;
}

int write(Handle connection, const void *data, size_t size)
{
    const prog::InCall in_call;
    const Open *o = held(connection);
    const int line = o != nullptr ? o->result.load() : -1;
    return line >= 0 ? g_dialer->write(line, static_cast<const uint8_t *>(data), size) : -1;
}

void close(Handle connection)
{
    const prog::InCall in_call;
    Open *o = held(connection);
    if (o == nullptr) {
        return;
    }
    if (o->result.load() == kPending) {
        o->dropped = true; // the job still has it: dialled() gives it back
        return;
    }
    give_back(*o);
}

} // namespace tcp
} // namespace os
