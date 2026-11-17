// The console: one command shell on a wire that is always there (the serial
// port), as opposed to a Terminal window or a telnet connection, which come
// and go. Backlog item 14; docs/journal/2026-11-17-serial-console.md.
//
// A TTY that lasts and, on a task of its own, one session after another:
// when someone types `exit` the next session greets them, as a login prompt
// comes back on a Unix console. What is typed is given to typed() by
// whoever reads the wire; what is shown goes to the sink the machine gave.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"
#include "core/sh/sh.hpp"
#include "core/tty/tty.hpp"

namespace sh {

// What the machine lends the console.
struct ConsoleKit {
    ports::Lock *lock = nullptr;   // the TTY's
    ports::Event *typed = nullptr; // the session sleeps on it until a line is typed
    ports::Tasks *tasks = nullptr; // where its task comes from
};

class Console {
public:
    static constexpr uint32_t kStack = 12288; // as a Terminal's session

    // `env.ui` must reach the UI task: the session is never on it.
    Console(const Env &env, const ConsoleKit &kit, ports::ByteSink &out);
    // Ends the session and gives its task back.
    ~Console();
    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;

    // Starts the task. False if there is none to be had.
    bool start();
    bool running() const { return task_ != nullptr; }

    // Where typed bytes go. Any task.
    ports::ByteSink &typed() { return typed_; }
    tty::Tty &terminal() { return tty_; }
    // How many sessions have greeted: 1 until someone types `exit`.
    int sessions() const { return sessions_.load(); }

private:
    class Typed : public ports::ByteSink {
    public:
        explicit Typed(tty::Tty &tty) : tty_(tty) {}
        void write(const uint8_t *data, size_t size) override { tty_.input(data, size); }

    private:
        tty::Tty &tty_;
    };

    static void run(void *self);

    Env env_;
    ConsoleKit kit_;
    tty::Tty tty_;
    Typed typed_;
    ports::TaskId task_ = nullptr;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> finished_{false};
    std::atomic<int> sessions_{0};
    alignas(Session) unsigned char room_[sizeof(Session)];
};

} // namespace sh
