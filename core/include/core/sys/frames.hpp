#pragma once

// Is the desktop keeping up with the monitor? (backlog item 63,
// docs/journal/2026-11-18-frame-budget.md)
//
// The monitor shows a new frame every 16.8 ms (640x480 with a 25 MHz pixel
// clock: 800 x 525 pixels a frame, 59.52 frames a second). The desktop does
// its work in pieces on one task: a key, a mouse event, typed text, a
// message from another task, or a tick (the timers that are due, then the
// painting). While a piece runs nothing else reaches the screen, so a piece
// that takes longer than one frame is a frame the user saw late: a frame
// over budget. This counts the pieces, keeps the last slow ones with where
// their time went, and writes lines for the system log (dmesg):
//
//   desktop: 1 frame over the 16800 us budget, worst 28412 us (a mouse event: painting 27300 us, timers 0 us)
//   desktop: 3 frames over the 16800 us budget, worst 21003 us (a tick: painting 1200 us, timers 19650 us), 2 timers late, worst 9120 us
//
// A timer that fires a frame or more after it was due is counted too (a
// game's step shown late): that is what a busy desktop looks like from a
// program, and it also catches the desktop's task being kept from running.
//
// A burst gives one line at once and then at most one line every kQuietMs,
// as sys::MissedRefills does, so a slow desktop cannot fill the log. The
// exact figures are read by the debug link (dbg::kFrames, tools/dbg.py
// frames). This is not the missed video refills (sys::MissedRefills): those
// are scanlines the video interrupt could not hand over in time, a torn or
// shifted picture; these are whole frames that showed old content.
//
// One task only: the desktop's.

#include <cstddef>
#include <cstdint>

namespace sys {

class Frames {
public:
    // 800 x 525 pixels at 25 MHz.
    static constexpr uint32_t kDefaultBudgetUs = 16800;
    static constexpr uint32_t kQuietMs = 10000;
    static constexpr int kSlowKept = 16;
    static constexpr int kBuckets = 8;

    enum Work : uint8_t { kKey, kMouse, kText, kTick, kMessage };
    // The longest single thing inside a piece of work.
    enum Top : uint8_t { kNothing, kTimer, kWindowPaint, kPanelPaint };

    // A piece of work that was over budget or had a late timer.
    struct Slow {
        uint32_t at_ms;     // when it ended, since start-up
        uint32_t total_us;  // all of it
        uint32_t paint_us;  // of that, painting
        uint32_t pixels;    // the area that was painted
        uint32_t timers_us; // of the total, inside timer callbacks
        uint32_t top_us;    // the longest timer callback or the longest window's painting
        uint32_t late_us;   // how late its latest timer fired (0: none was due)
        uint16_t top_pid;   // whose that was
        uint8_t work;       // Work
        uint8_t top;        // Top
    };

    struct Totals {
        uint32_t budget_us;
        uint32_t pieces;        // pieces of work measured
        uint32_t painted;       // of those, the ones that painted: the frames drawn
        uint32_t over;          // pieces longer than the budget
        uint32_t late;          // pieces with a timer a frame or more late
        uint32_t worst_us;      // the longest piece
        uint32_t worst_late_us; // the latest timer
        uint64_t busy_us;       // all the pieces added up
        // How long the pieces took: under 1, 2, 4 and 8 ms, under the
        // budget, under 2 and 4 budgets, and longer.
        uint32_t buckets[kBuckets];
    };

    void set_budget(uint32_t us) { totals_.budget_us = us; }

    // A piece of work starts and ends. A piece inside another (a message
    // that turns out to be a mouse event) is part of the outer one and
    // gives it its name. end() returns true if the piece was slow.
    void begin(Work work, uint32_t now_us);
    bool end(uint32_t now_us, uint32_t at_ms);

    // Inside a piece: the screen was painted; a timer callback or the
    // painting of one window ran, for process `pid`; a timer fired `us`
    // after it was due.
    void painted(uint32_t us, uint32_t pixels);
    void ran(Top what, int pid, uint32_t us);
    void late(uint32_t us);

    // The line for the system log, if one is due now (0: nothing to write).
    size_t line(uint32_t now_ms, char *out, size_t capacity);

    const Totals &totals() const { return totals_; }
    int slow_count() const { return slow_count_; }
    const Slow &slow(int i) const { return slow_[(slow_first_ + i) % kSlowKept]; } // 0: the oldest kept
    // Starts counting again (the budget stays).
    void reset();

    static const char *work_name(uint8_t work);

private:
    Totals totals_ = {kDefaultBudgetUs, 0, 0, 0, 0, 0, 0, 0, {}};
    Slow slow_[kSlowKept] = {};
    int slow_first_ = 0;
    int slow_count_ = 0;

    int depth_ = 0;
    uint32_t start_us_ = 0;
    Slow now_ = {}; // the piece being measured
    bool did_paint_ = false;

    // For the log line: what has not been written yet.
    uint32_t pending_over_ = 0;
    uint32_t pending_late_ = 0;
    uint32_t pending_late_us_ = 0;
    Slow pending_worst_ = {};
    uint32_t last_line_ms_ = 0;
    bool wrote_ = false;
};

} // namespace sys
