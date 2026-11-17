#pragma once

// Missed video refills, turned into lines for the system log (dmesg).
//
// The video interrupt only counts; something that runs about once a second
// (on the board the scanlog task) hands the counts to check(), which says
// when to write a line and what it says. A burst gives one line at once and
// then at most one line every kQuietMs with what came meanwhile, so a storm
// cannot fill the 16 KB log.
//
// Misses that happened while the debug link was reading the task list (it
// holds the kernel's lock for most of a millisecond) are counted apart and
// named in the line, so a reader can tell them from the ones to chase:
//
//   video: 3 refills missed, worst 640 us late, last at line 240
//   video: 2 refills missed (all during a debug task list), worst 812 us late, last at line 96
//   video: 5 refills missed (2 during a debug task list), worst 812 us late, last at line 96
//
// tools/dbg.py misses reads these back.

#include <cstddef>
#include <cstdint>

namespace sys {

class MissedRefills {
public:
    static constexpr uint32_t kQuietMs = 10000;

    // missed and debug_missed are running totals since start (never reset);
    // worst_late_us is the worst lateness of a miss since the last call;
    // last_line is the screen line of the most recent miss. Returns the
    // length of the line written to out (0: nothing to write now).
    size_t check(uint32_t now_ms, uint32_t missed, uint32_t debug_missed, uint32_t worst_late_us,
                 uint32_t last_line, char *out, size_t capacity);

private:
    uint32_t seen_ = 0;       // total missed already counted in a line or pending
    uint32_t seen_debug_ = 0; // the same for the debug link's share
    uint32_t pending_ = 0;    // misses not written yet
    uint32_t pending_debug_ = 0;
    uint32_t worst_us_ = 0; // worst lateness among the pending ones
    uint32_t last_line_ms_ = 0;
    bool wrote_ = false;
};

} // namespace sys
