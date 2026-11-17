#include "core/sh/console.hpp"

#include <new>

namespace sh {

Console::Console(const Env &env, const ConsoleKit &kit, ports::ByteSink &out)
    : env_(env), kit_(kit), tty_(out, kit.lock, kit.typed), typed_(tty_)
{
    tty_.set_size(80, 24); // what a serial terminal program usually is
}

Console::~Console()
{
    if (task_ == nullptr) {
        return;
    }
    stopping_ = true;
    tty_.hang_up();
    while (!finished_.load()) {
        if (kit_.typed != nullptr) {
            kit_.typed->set(); // in case it sleeps
        }
    }
    kit_.tasks->end(task_);
}

bool Console::start()
{
    if (task_ != nullptr || kit_.tasks == nullptr) {
        return task_ != nullptr;
    }
    task_ = kit_.tasks->start("console", &Console::run, this, kStack, ports::Priority::Normal);
    return task_ != nullptr;
}

void Console::run(void *self)
{
    Console &c = *static_cast<Console *>(self);
    while (!c.stopping_.load()) {
        Session *session = new (c.room_) Session(c.tty_, c.env_);
        c.sessions_++;
        session->run(); // until `exit`, or the console is taken down
        session->~Session();
    }
    c.finished_ = true;
}

} // namespace sh
