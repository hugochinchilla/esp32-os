#include "core/sh/terminal_session.hpp"

#include <new>

#include "core/msg/queue.hpp"

namespace sh {

TerminalSession::TerminalSession(ports::ByteSink &device, const Env &env, SessionKit *kit)
    : kit_(kit), tty_(device, kit != nullptr ? kit->lock : nullptr, kit != nullptr ? kit->typed : nullptr),
      session_(tty_, with_ui(env))
{
}

// On the UI task the shell calls straight in. From a task of its own it asks
// the UI task, through the queue, for what belongs to it (the process table).
Env TerminalSession::with_ui(Env env)
{
    env.ui = kit_ != nullptr ? new (call_) QueueCall(*msg::queue(), *kit_->answered) : nullptr;
    return env;
}

bool TerminalSession::start(EndedFn gone, void *context)
{
    gone_ = gone;
    gone_context_ = context;
    if (kit_ == nullptr) {
        session_.greet();
        pump();
        return true;
    }
    job::Spec spec;
    spec.name = "sh";
    spec.work = &TerminalSession::run;
    spec.done = &TerminalSession::over;
    spec.stop = &TerminalSession::stop;
    spec.context = this;
    spec.stack_bytes = kStack;
    running_ = job::jobs().start(spec);
    return running_;
}

void TerminalSession::typed(const uint8_t *bytes, size_t size)
{
    tty_.input(bytes, size);
    if (kit_ != nullptr) {
        return; // the shell's task wakes up for it
    }
    pump();
    if (session_.ended()) {
        finish(); // exit, or Ctrl+D
    }
}

void TerminalSession::hang_up()
{
    if (kit_ == nullptr) {
        finish();
    } else if (running_) {
        tty_.input("\x03");
        tty_.hang_up();
    }
}

// Runs what has been typed: every finished line, then the prompt for the
// next one. Only for a session on the UI task.
void TerminalSession::pump()
{
    for (int guard = 0; guard < 64 && !session_.ended() && (tty_.readable() || !session_.waiting()); guard++) {
        session_.step();
    }
}

// The session is over. The device hears first, while the session is still
// whole; `gone` is the last thing done, since the session is destroyed there.
void TerminalSession::finish()
{
    if (finished_) {
        return;
    }
    finished_ = true;
    if (ended_ != nullptr) {
        const EndedFn ended = ended_;
        ended_ = nullptr;
        ended(ended_context_);
    }
    if (gone_ != nullptr) {
        gone_(gone_context_);
    }
}

// The shell's task.
uint32_t TerminalSession::run(const job::Job &, void *context)
{
    static_cast<TerminalSession *>(context)->session_.run();
    return 0;
}

// The UI task: the shell has ended (exit, Ctrl+D, or its terminal went).
void TerminalSession::over(void *context, uint32_t, bool)
{
    auto *s = static_cast<TerminalSession *>(context);
    s->running_ = false;
    s->finish();
}

// The UI task: the process was sent a KILL.
void TerminalSession::stop(void *context) { static_cast<TerminalSession *>(context)->hang_up(); }

} // namespace sh
