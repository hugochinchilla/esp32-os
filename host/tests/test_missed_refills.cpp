// sys::MissedRefills: when a line is written, what it says, and that a storm
// gives few lines.
#include "core/sys/missed_refills.hpp"

#include <cassert>
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
    char line[160];
    {
        sys::MissedRefills m;
        // Nothing missed: nothing written, however often it is asked.
        expect(m.check(1000, 0, 0, 0, 0, line, sizeof(line)) == 0, "quiet");
        expect(m.check(2000, 0, 0, 0, 0, line, sizeof(line)) == 0, "still quiet");
        // The first miss is written at once.
        expect(m.check(3000, 1, 0, 412, 240, line, sizeof(line)) > 0, "first miss written");
        expect(strcmp(line, "video: 1 refill missed, worst 412 us late, last at line 240") == 0, "one miss text");
        // More within the quiet time wait, and are added up.
        expect(m.check(4000, 3, 0, 500, 100, line, sizeof(line)) == 0, "held back");
        expect(m.check(5000, 4, 0, 300, 120, line, sizeof(line)) == 0, "held back again");
        expect(m.check(13000, 4, 0, 0, 120, line, sizeof(line)) > 0, "written after the quiet time");
        expect(strcmp(line, "video: 3 refills missed, worst 500 us late, last at line 120") == 0, "summed text");
        // Nothing new: nothing more.
        expect(m.check(30000, 4, 0, 0, 120, line, sizeof(line)) == 0, "nothing new");
        // After a quiet spell a new miss is written at once again.
        expect(m.check(31000, 5, 0, 350, 7, line, sizeof(line)) > 0, "new burst at once");
    }
    {
        sys::MissedRefills m;
        // All during the debug link's task list.
        expect(m.check(1000, 2, 2, 812, 96, line, sizeof(line)) > 0, "debug written");
        expect(strcmp(line, "video: 2 refills missed (all during a debug task list), worst 812 us late, last at line 96") ==
                   0,
               "all debug text");
        // Some during it, some not.
        m.check(2000, 5, 4, 640, 30, line, sizeof(line));
        expect(m.check(12000, 7, 4, 0, 30, line, sizeof(line)) > 0, "mixed written");
        expect(strcmp(line, "video: 5 refills missed (2 during a debug task list), worst 640 us late, last at line 30") == 0,
               "mixed text");
        // The debug share can never be more than the misses.
        expect(m.check(30000, 8, 9, 100, 1, line, sizeof(line)) > 0, "share capped written");
        expect(strstr(line, "video: 1 refill missed (all during a debug task list)") == line, "share capped");
    }
    {
        // A storm: a miss every second for ten minutes gives one line every
        // ten seconds, about 60 lines, not 600.
        sys::MissedRefills m;
        int lines = 0;
        size_t bytes = 0;
        for (uint32_t s = 1; s <= 600; s++) {
            const size_t n = m.check(s * 1000, s, s / 2, 700, 10, line, sizeof(line));
            if (n > 0) {
                lines++;
                bytes += n + 1;
            }
        }
        expect(lines >= 59 && lines <= 61, "storm rate limited");
        printf("storm: %d lines, %u bytes in ten minutes\n", lines, static_cast<unsigned>(bytes));
        // A small buffer: cut, never past its end.
        char small[16];
        sys::MissedRefills t;
        const size_t n = t.check(1000, 1, 0, 1, 1, small, sizeof(small));
        expect(n == sizeof(small) - 1 && small[n] == 0, "cut to the buffer");
    }
    if (failures == 0) {
        printf("missed refills: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
