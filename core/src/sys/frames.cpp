#include "core/sys/frames.hpp"

#include <cstdio>

namespace sys {

void Frames::begin(Work work, uint32_t now_us)
{
    if (depth_++ > 0) {
        if (now_.work == kMessage) {
            now_.work = work; // the message carried this
        }
        return;
    }
    now_ = {};
    now_.work = work;
    did_paint_ = false;
    start_us_ = now_us;
}

void Frames::painted(uint32_t us, uint32_t pixels)
{
    if (depth_ == 0) {
        return; // painting outside any piece: start-up, the debug link's repaint
    }
    now_.paint_us += us;
    now_.pixels += pixels;
    did_paint_ = true;
}

void Frames::ran(Top what, int pid, uint32_t us)
{
    if (depth_ == 0) {
        return;
    }
    if (what == kTimer) {
        now_.timers_us += us;
    }
    if (us > now_.top_us) {
        now_.top_us = us;
        now_.top_pid = static_cast<uint16_t>(pid);
        now_.top = what;
    }
}

void Frames::late(uint32_t us)
{
    if (depth_ > 0 && us > now_.late_us) {
        now_.late_us = us;
    }
}

bool Frames::end(uint32_t now_us, uint32_t at_ms)
{
    if (depth_ == 0 || --depth_ > 0) {
        return false;
    }
    Totals &t = totals_;
    const uint32_t total = now_us - start_us_; // right across a wrap of the clock
    now_.total_us = total;
    now_.at_ms = at_ms;
    t.pieces++;
    t.painted += did_paint_;
    t.busy_us += total;
    t.worst_us = total > t.worst_us ? total : t.worst_us;
    t.worst_late_us = now_.late_us > t.worst_late_us ? now_.late_us : t.worst_late_us;
    const uint32_t edges[kBuckets - 1] = {1000, 2000, 4000, 8000, t.budget_us, 2 * t.budget_us, 4 * t.budget_us};
    int bucket = 0;
    while (bucket < kBuckets - 1 && total >= edges[bucket]) {
        bucket++;
    }
    t.buckets[bucket]++;

    const bool over = total > t.budget_us;
    const bool was_late = now_.late_us >= t.budget_us;
    if (!over && !was_late) {
        return false;
    }
    t.over += over;
    t.late += was_late;
    if (slow_count_ < kSlowKept) {
        slow_[(slow_first_ + slow_count_++) % kSlowKept] = now_;
    } else {
        slow_[slow_first_] = now_;
        slow_first_ = (slow_first_ + 1) % kSlowKept;
    }
    if (over) {
        if (pending_over_++ == 0 || total > pending_worst_.total_us) {
            pending_worst_ = now_;
        }
    }
    if (was_late) {
        pending_late_++;
        pending_late_us_ = now_.late_us > pending_late_us_ ? now_.late_us : pending_late_us_;
    }
    return true;
}

const char *Frames::work_name(uint8_t work)
{
    switch (work) {
    case kKey:
        return "a key";
    case kMouse:
        return "a mouse event";
    case kText:
        return "typed text";
    case kTick:
        return "a tick";
    case kMessage:
        return "a message";
    default:
        return "?";
    }
}

size_t Frames::line(uint32_t now_ms, char *out, size_t capacity)
{
    if ((pending_over_ == 0 && pending_late_ == 0) || (wrote_ && now_ms - last_line_ms_ < kQuietMs) ||
        capacity == 0) {
        return 0;
    }
    int n = 0;
    if (pending_over_ > 0) {
        n = snprintf(out, capacity, "desktop: %u frame%s over the %u us budget, worst %u us (%s: painting %u us, timers %u us)",
                     static_cast<unsigned>(pending_over_), pending_over_ == 1 ? "" : "s",
                     static_cast<unsigned>(totals_.budget_us), static_cast<unsigned>(pending_worst_.total_us),
                     work_name(pending_worst_.work), static_cast<unsigned>(pending_worst_.paint_us),
                     static_cast<unsigned>(pending_worst_.timers_us));
    }
    if (pending_late_ > 0 && n >= 0 && static_cast<size_t>(n) < capacity) {
        const int more = snprintf(out + n, capacity - static_cast<size_t>(n), "%s%u timer%s late, worst %u us",
                                  n > 0 ? ", " : "desktop: ", static_cast<unsigned>(pending_late_),
                                  pending_late_ == 1 ? "" : "s", static_cast<unsigned>(pending_late_us_));
        n = more < 0 ? more : n + more;
    }
    pending_over_ = pending_late_ = pending_late_us_ = 0;
    last_line_ms_ = now_ms;
    wrote_ = true;
    if (n < 0) {
        return 0;
    }
    return static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : capacity - 1;
}

void Frames::reset()
{
    const uint32_t budget = totals_.budget_us;
    totals_ = {};
    totals_.budget_us = budget;
    slow_first_ = slow_count_ = 0;
    pending_over_ = pending_late_ = pending_late_us_ = 0;
}

} // namespace sys
