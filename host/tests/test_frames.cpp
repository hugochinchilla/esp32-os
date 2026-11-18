// sys::Frames: what counts as a frame over budget, what is kept about it,
// what the log line says, and that a slow desktop gives few lines.
#include "core/sys/frames.hpp"

#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void expect(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

} // namespace

int main()
{
    using sys::Frames;
    char line[200];
    {
        Frames f;
        expect(f.totals().budget_us == 16800, "the board's frame by default");
        // A quick mouse event that paints: a frame, in budget.
        f.begin(Frames::kMouse, 1000);
        f.painted(700, 1200);
        expect(!f.end(1900, 5), "in budget");
        expect(f.totals().pieces == 1 && f.totals().painted == 1 && f.totals().over == 0, "counted, not over");
        expect(f.totals().buckets[0] == 1, "under 1 ms");
        expect(f.line(5, line, sizeof(line)) == 0, "nothing to write");
        // A tick with nothing to paint is a piece but not a frame.
        f.begin(Frames::kTick, 2000);
        expect(!f.end(2010, 6), "an empty tick");
        expect(f.totals().pieces == 2 && f.totals().painted == 1, "not painted");
        // Exactly the budget is in budget; one more microsecond is not.
        f.begin(Frames::kKey, 0);
        expect(!f.end(16800, 7), "exactly one frame");
        expect(f.totals().buckets[5] == 1, "the budget itself is in the next bucket");
        f.begin(Frames::kMouse, 100000);
        f.ran(Frames::kWindowPaint, 7, 9000);
        f.ran(Frames::kWindowPaint, 9, 15000);
        f.ran(Frames::kPanelPaint, 1, 2000);
        f.painted(27300, 200000);
        expect(f.end(128412, 3000), "over budget");
        expect(f.totals().over == 1 && f.totals().worst_us == 28412, "counted over");
        expect(f.slow_count() == 1, "kept");
        const Frames::Slow &s = f.slow(0);
        expect(s.at_ms == 3000 && s.total_us == 28412 && s.paint_us == 27300 && s.pixels == 200000, "what it was");
        expect(s.work == Frames::kMouse && s.top == Frames::kWindowPaint && s.top_pid == 9 && s.top_us == 15000,
               "the longest thing in it");
        expect(f.line(3000, line, sizeof(line)) > 0, "written at once");
        expect(strcmp(line, "desktop: 1 frame over the 16800 us budget, worst 28412 us "
                            "(a mouse event: painting 27300 us, timers 0 us)") == 0,
               "one frame text");
        expect(f.line(3001, line, sizeof(line)) == 0, "written once");
    }
    {
        // Timers: the time inside them, and one that fires late.
        Frames f;
        f.set_budget(10000);
        f.begin(Frames::kTick, 0);
        f.late(300);
        f.ran(Frames::kTimer, 4, 500);
        f.late(9999);
        f.ran(Frames::kTimer, 5, 800);
        expect(!f.end(1500, 1), "late by less than a frame");
        expect(f.totals().late == 0 && f.totals().worst_late_us == 9999, "the latest is remembered");
        f.begin(Frames::kTick, 20000);
        f.late(12000);
        f.ran(Frames::kTimer, 5, 11000);
        f.ran(Frames::kTimer, 4, 100);
        f.painted(600, 50);
        expect(f.end(32000, 2), "over and late");
        expect(f.totals().over == 1 && f.totals().late == 1, "both counted");
        expect(f.slow(0).timers_us == 11100 && f.slow(0).top == Frames::kTimer && f.slow(0).top_pid == 5 &&
                   f.slow(0).late_us == 12000,
               "the timers' share and whose");
        expect(f.line(100, line, sizeof(line)) > 0, "written");
        expect(strcmp(line, "desktop: 1 frame over the 10000 us budget, worst 12000 us "
                            "(a tick: painting 600 us, timers 11100 us), 1 timer late, worst 12000 us") == 0,
               "over and late text");
        // Late only.
        f.begin(Frames::kTick, 50000);
        f.late(10000);
        expect(f.end(50100, 3), "late only");
        expect(f.line(200, line, sizeof(line)) == 0, "held back in the quiet time");
        f.begin(Frames::kTick, 60000);
        f.late(30000);
        expect(f.end(60100, 4), "late again");
        expect(f.line(100 + Frames::kQuietMs, line, sizeof(line)) > 0, "written after the quiet time");
        expect(strcmp(line, "desktop: 2 timers late, worst 30000 us") == 0, "late only text");
        expect(f.totals().over == 1 && f.totals().late == 3, "totals go on");
    }
    {
        // A piece inside another is one piece; a message takes the name of
        // what it carried. The clock may wrap.
        Frames f;
        f.begin(Frames::kMessage, 0xFFFFF000u);
        f.begin(Frames::kMouse, 0xFFFFF100u);
        f.painted(100, 10);
        expect(!f.end(0xFFFFFF00u, 1), "the inner end ends nothing");
        expect(f.totals().pieces == 0, "still inside");
        expect(f.end(0xFFFFF000u + 20000u, 1), "the outer one is the piece");
        expect(f.totals().pieces == 1 && f.slow(0).total_us == 20000 && f.slow(0).work == Frames::kMouse,
               "one piece, named by what was inside");
        // Painting outside any piece (start-up) is not counted.
        f.painted(50000, 300000);
        f.ran(Frames::kWindowPaint, 3, 50000);
        expect(!f.end(0, 0), "an end without a begin");
        expect(f.totals().pieces == 1 && f.totals().painted == 1, "nothing added");
    }
    {
        // A storm: every line sums what came since the last, and only the
        // last kSlowKept pieces are kept.
        Frames f;
        int lines = 0;
        for (uint32_t i = 0; i < 100; i++) {
            f.begin(Frames::kKey, i * 100000);
            f.end(i * 100000 + 20000 + i, i * 100);
            lines += f.line(i * 100, line, sizeof(line)) > 0;
        }
        expect(lines == 1, "one line in ten seconds");
        expect(f.totals().over == 100 && f.slow_count() == Frames::kSlowKept, "all counted, the last kept");
        expect(f.slow(0).total_us == 20000 + 84 && f.slow(Frames::kSlowKept - 1).total_us == 20099, "oldest first");
        expect(f.line(20000, line, sizeof(line)) > 0, "the rest in one line");
        expect(strcmp(line, "desktop: 99 frames over the 16800 us budget, worst 20099 us "
                            "(a key: painting 0 us, timers 0 us)") == 0,
               "summed text");
        // A short buffer cuts the line and says how much it wrote.
        f.begin(Frames::kKey, 0);
        f.end(30000, 40000);
        expect(f.line(40000, line, 20) == 19 && strlen(line) == 19, "cut to the room there is");
        f.reset();
        expect(f.totals().pieces == 0 && f.totals().over == 0 && f.slow_count() == 0 &&
                   f.totals().budget_us == 16800,
               "reset keeps the budget");
    }
    printf(failures == 0 ? "frames: all passed\n" : "frames: %d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
