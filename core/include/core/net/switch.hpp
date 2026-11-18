// A service of the services loop as something that is started and stopped
// by hand while the machine runs: telnetd and vncd, which init starts from
// their lines of the start-up file and the `service` command stops and
// starts (docs/journal/2026-11-18-services.md).
//
// The loop's task is the only one that touches a service and its
// connections, so the work is in two halves:
//
//     start()  the UI task opens the ports and makes the row, then tells
//              the loop, which gives the service its turns from its next
//              round on
//     stop()   the UI task tells the loop; at its next round the loop lets
//              whoever is connected go, closes the ports (there, on its own
//              task) and posts a message back, and the UI task takes the row
//              away
//
// Between a stop() and that message the service is still stopping, and a
// start() is refused: its ports are not the UI task's to open yet.
//
// Everything here is for the UI task except what the loop calls.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/msg/queue.hpp"
#include "core/net/services.hpp"
#include "core/proc/table.hpp"

namespace net {

class Switch {
public:
    // The service's ports. open: on the UI task, false if none could be
    // had. close: on the services task, after the service has let go.
    struct Ports {
        bool (*open)(void *context);
        void (*close)(void *context);
        void *context;
    };

    // Joins the loop, switched off. `row`: the name of the process the
    // service shows as while it runs; nullptr for none (a service that is
    // not a task of its own and has nothing of its own to show).
    Switch(Services &loop, Service &service, const Ports &ports, const char *row = nullptr);
    Switch(const Switch &) = delete;
    Switch &operator=(const Switch &) = delete;

    // Told of the row each time one is made (whose memory a session is).
    void on_row(void (*fn)(proc::Pid pid, void *context), void *context)
    {
        row_made_ = fn;
        row_context_ = context;
    }

    // False, with the reason in words, if it did not start.
    bool start(const char *&why);
    void stop();
    // Started, and not stopped since.
    bool running() const { return running_; }
    proc::Pid pid() const { return pid_; }
    // The loop itself has ended and closed every port (End Task on inetd):
    // the row goes and the service counts as stopped.
    void loop_ended();

private:
    static void switched(bool on, void *self); // the services task
    static void closed(const msg::Message &m);
    void finish();

    Services &loop_;
    Service &service_;
    Ports ports_;
    const char *row_;
    void (*row_made_)(proc::Pid, void *) = nullptr;
    void *row_context_ = nullptr;
    proc::Pid pid_ = proc::kNone;
    bool running_ = false;
    bool closing_ = false;               // stopped, and the loop has not said it is done
    std::atomic<uint32_t> stops_{0};     // which stop a message is about
};

} // namespace net
