// The watchdog: notices a task that has stopped coming round. See
// docs/journal/2026-11-17-watchdog.md.
//
// A watched task calls beat() every time it comes round its loop. A
// supervisor (on the board: a callback of the high-priority timer task, once
// a second) calls overdue(); when a task's last beat is older than its
// limit, the supervisor writes a crash record and restarts the machine.
//
// This class only keeps the times. It has no clock and restarts nothing, so
// it runs the same on the PC. beat() and overdue() may be called from
// different tasks: each beat is one atomic word.
#pragma once

#include <atomic>
#include <cstdint>

namespace sys {

class Watchdog {
public:
    static constexpr int kMaxWatched = 4;
    static constexpr int kNobody = -1;

    // Starts watching: `name` (not copied) must beat at least every
    // `limit_ms`. The watch starts at `now_ms`. Returns the slot to pass to
    // beat(), kNobody when all are taken. Call before the supervisor starts.
    int watch(const char *name, uint32_t limit_ms, uint32_t now_ms)
    {
        if (count_ == kMaxWatched || limit_ms == 0) {
            return kNobody;
        }
        watched_[count_].name = name;
        watched_[count_].limit_ms = limit_ms;
        watched_[count_].last_ms.store(now_ms, std::memory_order_relaxed);
        return count_++;
    }

    // "I am alive", from the watched task itself.
    void beat(int slot, uint32_t now_ms)
    {
        if (slot >= 0 && slot < count_) {
            watched_[slot].last_ms.store(now_ms, std::memory_order_relaxed);
        }
    }

    // The first slot whose beat is older than its limit, kNobody if all are
    // on time. `waited_ms` gets how long it has been silent. The clock may
    // wrap (49 days in milliseconds); a beat that arrives between the
    // supervisor reading the clock and this call counts as on time.
    int overdue(uint32_t now_ms, uint32_t &waited_ms) const
    {
        for (int i = 0; i < count_; i++) {
            const int32_t silent = static_cast<int32_t>(now_ms - watched_[i].last_ms.load(std::memory_order_relaxed));
            if (silent > 0 && static_cast<uint32_t>(silent) > watched_[i].limit_ms) {
                waited_ms = static_cast<uint32_t>(silent);
                return i;
            }
        }
        waited_ms = 0;
        return kNobody;
    }

    const char *name(int slot) const { return slot >= 0 && slot < count_ ? watched_[slot].name : ""; }
    uint32_t limit_ms(int slot) const { return slot >= 0 && slot < count_ ? watched_[slot].limit_ms : 0; }
    int count() const { return count_; }

private:
    struct Watched {
        const char *name = "";
        uint32_t limit_ms = 0;
        std::atomic<uint32_t> last_ms{0};
    };
    Watched watched_[kMaxWatched];
    int count_ = 0;
};

// [watchdog] timeout_s in C:\.config\boot.ini: how long the desktop's loop
// may stay silent. 0 switches the watchdog off.
constexpr int kWatchdogDefaultS = 15, kWatchdogMinS = 5, kWatchdogMaxS = 600;
inline uint32_t watchdog_limit_ms(int timeout_s)
{
    if (timeout_s <= 0) {
        return 0;
    }
    const int s = timeout_s < kWatchdogMinS ? kWatchdogMinS : (timeout_s > kWatchdogMaxS ? kWatchdogMaxS : timeout_s);
    return static_cast<uint32_t>(s) * 1000;
}

} // namespace sys
