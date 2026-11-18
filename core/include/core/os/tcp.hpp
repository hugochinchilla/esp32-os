// The OS calls for reaching a server: an outgoing connection is a number, a
// handle, as an open file is (core/os/os.hpp, namespace file). The system
// keeps the table of which process holds which and closes what a process
// leaves open when it ends.
//
//     os::tcp::Handle c = os::tcp::connect("frogfind.com", 80);
//     ... later, from a timer: os::tcp::state(c) is Open ...
//     os::tcp::write(c, request, size);
//     int got = os::tcp::read(c, buffer, sizeof(buffer));
//     os::tcp::close(c);
//
// No call waits. connect() answers at once and the connection is made in a
// background job of the asking process (finding a host by name takes time);
// state() says how far it is. read() and write() do what can be done now.
// All for the UI task.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/net/dialer.hpp"
#include "core/proc/table.hpp"

namespace os {
namespace tcp {

using Handle = int;
constexpr Handle kNone = 0;
// ponytail: how many connections may be open through these calls at once,
// in the whole machine and by one process. The browser holds one.
constexpr int kMaxOpen = 4;
constexpr int kMaxOpenEach = 2;

enum class State : uint8_t {
    Connecting,
    Open,
    Failed, // failure() says why; close() it all the same
    Closed, // not a connection: closed, or never given
};

// False: this machine has no way to reach a server at all.
bool available();
// kNone: the process, or the machine, has its share of connections, or
// there is no way out.
Handle connect(const char *host, int port);
State state(Handle connection);
net::DialFailure failure(Handle connection);
// Bytes done; 0 for none now (nothing has arrived, nothing fits); -1 when
// the other side has closed, the connection broke, or it is not open.
int read(Handle connection, void *out, size_t capacity);
int write(Handle connection, const void *data, size_t size);
// Also while it is still connecting: the connection is dropped when made.
void close(Handle connection);
// How many a process holds; every process for proc::kNone.
int open_connections(proc::Pid pid = proc::kNone);

} // namespace tcp

// For the system, not for programs: the machine's way out.
void install_dialer(net::Dialer *dialer);

} // namespace os
