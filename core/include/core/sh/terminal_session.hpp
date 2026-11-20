// A terminal session: a TTY with a command shell reading from it, running as
// a process of its own (`sh`). It is the half of a terminal that is not on
// the screen (docs/DECISIONS.md, "The Terminal is an emulator; the session is
// a process of its own"; docs/journal/2026-11-17-terminal-split.md).
//
//     the device                        the session (this file)
//     a window, a connection   keys --> typed() --> TTY --> shell
//                              sink <-------------- TTY <-- shell
//
// The device is whatever shows the characters and has the keys: the Terminal
// window is one (core/src/shell/terminal.cpp, a terminal emulator). The
// session knows nothing of it but a ports::ByteSink to write to. It has no
// window, no grid and no font, and it is not part of the desktop: it lives
// on when the window is busy, is listed by `ps` under its own number, and is
// charged its own processor time and memory.
//
// Where the machine has tasks the shell runs on one of its own, as a job of
// the `sh` process, and sleeps until a line is typed. Where it has none (the
// browser, the plain PC desktop) it is stepped on the UI task each time
// something is typed.
//
// Everything here is called on the UI task, as the `sh` process.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/job/job.hpp"
#include "core/ports.hpp"
#include "core/sh/sh.hpp"
#include "core/tty/tty.hpp"

namespace sh {

// What a session needs to run on a task of its own, and the portable code
// cannot make: a lock for its TTY and two things to sleep on. The machine
// hands in a few, one per session that may run at once.
struct SessionKit {
    ports::Lock *lock = nullptr;
    ports::Event *typed = nullptr;    // a line has been typed
    ports::Event *answered = nullptr; // the UI task has done what was asked
    bool in_use = false;
};

class TerminalSession {
public:
    // ponytail: the stack of a session's task. The commands keep their large
    // buffers in the session, not on the stack; the deepest is a copy or a
    // removal of nested folders. To be measured on the board.
    static constexpr uint32_t kStack = 12288;

    using EndedFn = void (*)(void *context);

    // `device`: where what the terminal shows is written. It is called with
    // the TTY's lock held, from the session's task. `env`: what the shell
    // may reach; its `ui` is filled in here. `kit`: for a task of its own;
    // nullptr for a session stepped on the UI task.
    TerminalSession(ports::ByteSink &device, const Env &env, SessionKit *kit);
    TerminalSession(const TerminalSession &) = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;

    // Starts the shell, as the current process. `gone` is called once, last
    // of all, when the session is over and its task has returned: whoever
    // made the session destroys it there. False: the task could not be
    // started, and `gone` will not be called.
    bool start(EndedFn gone, void *context);

    /* ---- The device end ---- */

    // Called once when the session ends by itself (exit, Ctrl+D, a signal),
    // before `gone`: the device closes. nullptr: the device has gone first
    // and does not want to hear.
    void on_ended(EndedFn fn, void *context)
    {
        ended_ = fn;
        ended_context_ = context;
    }
    // Keys, as the bytes a terminal sends for them.
    void typed(const uint8_t *bytes, size_t size);
    // The device's size in characters.
    void set_size(int columns, int rows);
    // The device is gone (the window closed). A command that is running is
    // stopped as Ctrl+C would stop it, the shell ends, and nothing more is
    // written to the device once this has returned. `gone` follows: at once
    // for a session on the UI task, when the task has returned otherwise.
    void hang_up();
    // A program has taken the keys for itself (ted, top).
    bool raw() const { return tty_.raw(); }
    // What the device holds while it reads what the session writes from
    // another task (its grid); nullptr for a session on the UI task.
    ports::Lock *lock() const { return kit_ != nullptr ? kit_->lock : nullptr; }

    SessionKit *kit() const { return kit_; }

private:
    Env with_ui(Env env);
    void pump();
    void finish();
    static uint32_t run(const job::Job &job, void *context);
    static void over(void *context, uint32_t result, bool stopped);
    static void stop(void *context);

    SessionKit *kit_;
    alignas(QueueCall) unsigned char call_[sizeof(QueueCall)];
    tty::Tty tty_;
    Session session_;
    bool running_ = false;  // the task has been started and has not returned
    bool finished_ = false;
    EndedFn ended_ = nullptr, gone_ = nullptr;
    void *ended_context_ = nullptr, *gone_context_ = nullptr;
};

} // namespace sh
