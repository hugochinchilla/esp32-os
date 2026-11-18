#include "core/net/switch.hpp"

namespace net {

Switch::Switch(Services &loop, Service &service, const Ports &ports, const char *row)
    : loop_(loop), service_(service), ports_(ports), row_(row)
{
    loop_.add(service_, false, &switched, this);
}

bool Switch::start(const char *&why)
{
    if (running_) {
        why = "it is running already";
        return false;
    }
    if (loop_.stopped()) {
        why = "the network services (inetd) have ended";
        return false;
    }
    if (loop_.on(service_)) {
        why = "it is still stopping; try again in a moment";
        return false;
    }
    if (closing_) {
        finish(); // the loop is done; its message is still on the way, or was lost
    }
    if (!ports_.open(ports_.context)) {
        why = "its port could not be opened";
        return false;
    }
    if (row_ != nullptr) {
        pid_ = proc::table().spawn_service(row_);
        if (row_made_ != nullptr) {
            row_made_(pid_, row_context_);
        }
    }
    running_ = true;
    loop_.want(service_, true);
    return true;
}

void Switch::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;
    closing_ = true;
    stops_++;
    loop_.want(service_, false);
}

void Switch::switched(bool on, void *self)
{
    Switch &s = *static_cast<Switch *>(self);
    if (on) {
        return;
    }
    s.ports_.close(s.ports_.context);
    msg::post(proc::kNone, &closed, &s, s.stops_.load(), 0, 1000);
}

void Switch::closed(const msg::Message &m)
{
    Switch &s = *static_cast<Switch *>(m.context);
    // An old message, for a stop that start() has already tidied up after:
    // the row there is now is a new one.
    if (s.closing_ && m.a == s.stops_.load()) {
        s.finish();
    }
}

void Switch::finish()
{
    closing_ = false;
    if (pid_ != proc::kNone) {
        proc::table().exit(pid_);
        pid_ = proc::kNone;
    }
}

void Switch::loop_ended()
{
    running_ = false;
    finish();
}

} // namespace net
