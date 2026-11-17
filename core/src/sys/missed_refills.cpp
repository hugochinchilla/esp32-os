#include "core/sys/missed_refills.hpp"

#include <cstdio>

namespace sys {

size_t MissedRefills::check(uint32_t now_ms, uint32_t missed, uint32_t debug_missed, uint32_t worst_late_us,
                            uint32_t last_line, char *out, size_t capacity)
{
    if (missed != seen_) {
        pending_ += missed - seen_;
        // The debug link's share can never be more than the misses themselves.
        uint32_t debug_new = debug_missed - seen_debug_;
        if (debug_new > missed - seen_) {
            debug_new = missed - seen_;
        }
        pending_debug_ += debug_new;
        seen_ = missed;
        seen_debug_ = debug_missed;
        if (worst_late_us > worst_us_) {
            worst_us_ = worst_late_us;
        }
    }
    if (pending_ == 0 || (wrote_ && now_ms - last_line_ms_ < kQuietMs) || capacity == 0) {
        return 0;
    }
    char share[48] = "";
    if (pending_debug_ == pending_) {
        snprintf(share, sizeof(share), " (all during a debug task list)");
    } else if (pending_debug_ > 0) {
        snprintf(share, sizeof(share), " (%u during a debug task list)", static_cast<unsigned>(pending_debug_));
    }
    const int n = snprintf(out, capacity, "video: %u refill%s missed%s, worst %u us late, last at line %u",
                           static_cast<unsigned>(pending_), pending_ == 1 ? "" : "s", share,
                           static_cast<unsigned>(worst_us_), static_cast<unsigned>(last_line));
    pending_ = pending_debug_ = worst_us_ = 0;
    last_line_ms_ = now_ms;
    wrote_ = true;
    if (n < 0) {
        return 0;
    }
    return static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : capacity - 1;
}

} // namespace sys
