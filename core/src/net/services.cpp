#include "core/net/services.hpp"

namespace net {

bool Services::add(Service &service)
{
    if (count_ == kMax) {
        return false;
    }
    services_[count_++] = &service;
    return true;
}

bool Services::round()
{
    if (stop_.load()) {
        if (!stopped_.load()) {
            for (int i = 0; i < count_; i++) {
                services_[i]->stop();
            }
            stopped_ = true;
        }
        return false;
    }
    for (int i = 0; i < count_; i++) {
        services_[i]->poll();
    }
    wait_.clear();
    uint32_t sleep_us = kNever;
    for (int i = 0; i < count_; i++) {
        const uint32_t wants = services_[i]->watch(wait_);
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
