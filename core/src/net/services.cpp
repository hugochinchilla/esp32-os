#include "core/net/services.hpp"

namespace net {

bool Services::add(Service &service, bool on, SwitchedFn switched, void *context)
{
    if (count_ == kMax) {
        return false;
    }
    Slot &s = slots_[count_.load()];
    s.service = &service;
    s.switched = switched;
    s.context = context;
    s.wanted = on;
    s.on = on;
    count_++; // only now does the loop see it
    return true;
}

const Services::Slot *Services::slot(const Service &service) const
{
    for (int i = 0, n = count_.load(); i < n; i++) {
        if (slots_[i].service == &service) {
            return &slots_[i];
        }
    }
    return nullptr;
}

bool Services::want(const Service &service, bool on)
{
    Slot *s = const_cast<Slot *>(slot(service));
    if (s == nullptr) {
        return false;
    }
    s->wanted = on;
    return true;
}

bool Services::on(const Service &service) const
{
    const Slot *s = slot(service);
    return s != nullptr && s->on.load();
}

bool Services::round()
{
    if (stop_.load()) {
        if (!stopped_.load()) {
            for (int i = 0, n = count_.load(); i < n; i++) {
                if (slots_[i].on.load()) {
                    slots_[i].service->stop();
                    slots_[i].on = false;
                }
            }
            stopped_ = true;
        }
        return false;
    }
    // What was switched since the last round.
    for (int i = 0, n = count_.load(); i < n; i++) {
        Slot &s = slots_[i];
        const bool wanted = s.wanted.load();
        if (wanted == s.on.load()) {
            continue;
        }
        if (!wanted) {
            s.service->stop();
        }
        // Its ports are closed in there: only then is it off for whoever
        // waits to open them again.
        if (!wanted && s.switched != nullptr) {
            s.switched(false, s.context);
        }
        s.on = wanted;
        if (wanted && s.switched != nullptr) {
            s.switched(true, s.context);
        }
    }
    for (int i = 0, n = count_.load(); i < n; i++) {
        if (slots_[i].on.load()) {
            slots_[i].service->poll();
        }
    }
    wait_.clear();
    uint32_t sleep_us = kNever;
    for (int i = 0, n = count_.load(); i < n; i++) {
        if (!slots_[i].on.load()) {
            continue;
        }
        const uint32_t wants = slots_[i].service->watch(wait_);
        sleep_us = wants < sleep_us ? wants : sleep_us;
    }
    stats_.rounds++;
    const uint32_t now = clock_.micros();
    if (sleep_us == 0) {
        if (!working_) {
            working_ = true;
            working_since_ = now;
        }
        if (now - working_since_ < kWorkUs) {
            return true;
        }
        // A rest is a rest: with nothing watched, nothing ends it early.
        wait_.clear();
        sleep_us = kRestUs;
        stats_.rests++;
    }
    working_ = false;
    stats_.sleeps++;
    stats_.woken += wait_.sleep(sleep_us < longest_us_ ? sleep_us : longest_us_) ? 1 : 0;
    return true;
}

} // namespace net
