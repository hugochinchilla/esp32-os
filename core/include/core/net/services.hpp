// The services loop: one task for every network service, asleep on all
// their sockets at once, the way Unix's inetd listened for many services in
// one process. The remote screen and its web page live here today; a telnet
// listener is the next one (docs/journal/2026-11-17-services-loop.md,
// backlog item 8b).
//
// Each round gives every service its turn (poll(), which never waits), then
// asks each what it is waiting for (watch()) and sleeps until one of those
// things happens. A board nobody is connected to does not wake at all.
//
// The services task is not the UI task. Windows, timers and the process
// table belong to the UI task and have no locks, so a service never touches
// them: keys and mouse from a viewer go through msg::PostedInput
// (core/msg/input.hpp), and anything else it wants done on the desktop is a
// msg::post().
#pragma once

#include <cstdint>

#include "core/net/stream.hpp"
#include "core/ports.hpp"

namespace net {

constexpr uint32_t kNever = 0xFFFFFFFF;

class Service {
public:
    // Its turn: takes what has arrived, sends what fits. Never waits.
    virtual void poll() = 0;
    // What it waits for now, added to `watch`; returns the longest the loop
    // may sleep, in µs, if none of that happens. kNever: as long as it takes.
    // 0: there is work left, come straight back.
    virtual uint32_t watch(Watch &watch) = 0;

protected:
    ~Service() = default;
};

// The port: a sleep that ends when something watched is ready. Adapter:
// net::PosixWait (select(), on the PC and on the board).
class Wait : public Watch {
public:
    // Forgets what was added.
    virtual void clear() = 0;
    // Sleeps until something added is ready or max_us have gone by (0: only
    // looks; kNever: no limit). True if something was ready.
    virtual bool sleep(uint32_t max_us) = 0;

protected:
    ~Wait() = default;
};

class Services {
public:
    static constexpr int kMax = 4;
    // A service with work left comes straight back, without sleeping. After
    // this long without a sleep the loop rests anyway, so that the tasks
    // below it (and the kernel's idle task, which a watchdog looks at) get
    // the processor however much a viewer asks for.
    static constexpr uint32_t kWorkUs = 40000;
    static constexpr uint32_t kRestUs = 10000; // one tick of the board's kernel

    struct Stats {
        uint32_t rounds;
        uint32_t sleeps; // rounds that slept (the others had work left)
        uint32_t woken;  // sleeps ended by the network, not by the clock
        uint32_t rests;  // sleeps forced after kWorkUs of work
    };

    Services(Wait &wait, ports::Clock &clock) : wait_(wait), clock_(clock) {}

    bool add(Service &service);
    // One round: every service's turn, then the sleep. The task is this in a
    // loop.
    void round();
    const Stats &stats() const { return stats_; }

private:
    Wait &wait_;
    ports::Clock &clock_;
    Service *services_[kMax] = {};
    int count_ = 0;
    uint32_t working_since_ = 0;
    bool working_ = false;
    Stats stats_ = {};
};

} // namespace net
