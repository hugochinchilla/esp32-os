// The watchdog's bookkeeping, the crash record, the crash log and the error
// screen's waiting. docs/journal/2026-11-17-watchdog.md.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/shell/boot.hpp"
#include "core/sys/crash.hpp"
#include "core/sys/watchdog.hpp"
#include "../host_adapters.hpp"

namespace {

int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                  \
        }                                                                \
    } while (0)

sys::CrashRecord hang_record()
{
    sys::CrashRecord r;
    sys::crash_begin(r, sys::CrashKind::Hang);
    r.pid = 7;
    r.uptime_ms = 754000;
    r.waited_ms = 15400;
    r.core = 1;
    sys::crash_text(r.program, sizeof(r.program), "Editor");
    sys::crash_text(r.task, sizeof(r.task), "ui");
    sys::crash_seal(r);
    return r;
}

sys::CrashRecord panic_record()
{
    sys::CrashRecord r;
    sys::crash_begin(r, sys::CrashKind::Panic);
    r.pid = 12;
    r.uptime_ms = 3723000;
    r.pc = 0x4200ABCD;
    r.address = 0;
    r.trace_count = 8;
    for (int i = 0; i < 8; i++) {
        r.trace[i] = 0x42001000u + static_cast<uint32_t>(i) * 0x111;
    }
    sys::crash_text(r.program, sizeof(r.program), "Cactus Patch");
    sys::crash_text(r.task, sizeof(r.task), "ui");
    sys::crash_text(r.reason, sizeof(r.reason), "StoreProhibited");
    sys::crash_seal(r);
    return r;
}

// A task that beats on time is left alone; one that stops is reported with
// how long it has been silent; the clock may wrap.
void test_watchdog()
{
    sys::Watchdog dog;
    uint32_t waited = 99;
    CHECK(dog.overdue(1000, waited) == sys::Watchdog::kNobody && waited == 0); // nobody watched
    CHECK(dog.watch("off", 0, 0) == sys::Watchdog::kNobody);                   // a limit of 0: not watched
    const int ui = dog.watch("ui", 15000, 1000);
    CHECK(ui == 0 && dog.count() == 1 && strcmp(dog.name(ui), "ui") == 0 && dog.limit_ms(ui) == 15000);
    // Not beaten yet: the watch runs from when it started.
    CHECK(dog.overdue(16000, waited) == sys::Watchdog::kNobody);
    CHECK(dog.overdue(16001, waited) == ui && waited == 15001);
    // Beats keep it quiet for any length of time.
    for (uint32_t now = 1000; now < 200000; now += 900) {
        dog.beat(ui, now);
        CHECK(dog.overdue(now + 899, waited) == sys::Watchdog::kNobody);
    }
    dog.beat(ui, 300000);
    CHECK(dog.overdue(315000, waited) == sys::Watchdog::kNobody);
    CHECK(dog.overdue(320000, waited) == ui && waited == 20000);
    // A beat that lands after the supervisor read its clock is on time.
    dog.beat(ui, 320005);
    CHECK(dog.overdue(320000, waited) == sys::Watchdog::kNobody);
    // The millisecond clock wraps after 49 days.
    dog.beat(ui, 0xFFFFF000u);
    CHECK(dog.overdue(0x00000100u, waited) == sys::Watchdog::kNobody);
    CHECK(dog.overdue(0x00003000u, waited) == ui && waited == 0x4000);
    // A second task; the first overdue one is named. Beats for slots that
    // do not exist are ignored.
    dog.beat(ui, 1000);
    const int shell = dog.watch("shell", 5000, 1000);
    CHECK(shell == 1);
    dog.beat(7, 1);
    dog.beat(-1, 1);
    dog.beat(ui, 9000);
    CHECK(dog.overdue(9000, waited) == shell && waited == 8000);
    CHECK(dog.watch("c", 1, 0) == 2 && dog.watch("d", 1, 0) == 3 && dog.watch("e", 1, 0) == sys::Watchdog::kNobody);

    // The setting: seconds to milliseconds, within limits; 0 and below: off.
    CHECK(sys::watchdog_limit_ms(15) == 15000 && sys::watchdog_limit_ms(0) == 0 && sys::watchdog_limit_ms(-3) == 0);
    CHECK(sys::watchdog_limit_ms(1) == 5000 && sys::watchdog_limit_ms(100000) == 600000);
}

// A sealed record is believed; anything else is not: memory after a power
// cut, one changed bit, another version, a cleared record.
void test_record()
{
    sys::CrashRecord r = hang_record();
    CHECK(sys::crash_valid(r));
    CHECK(sizeof(r) % 4 == 0 && sizeof(r) <= 256);

    sys::CrashRecord rubbish;
    memset(&rubbish, 0xA5, sizeof(rubbish));
    CHECK(!sys::crash_valid(rubbish));
    memset(&rubbish, 0, sizeof(rubbish));
    CHECK(!sys::crash_valid(rubbish));

    for (size_t byte = 0; byte < sizeof(r); byte++) {
        sys::CrashRecord flipped = r;
        reinterpret_cast<uint8_t *>(&flipped)[byte] ^= 0x10;
        CHECK(!sys::crash_valid(flipped));
    }
    sys::CrashRecord other = r;
    other.version = 2;
    other.checksum = 0;
    CHECK(!sys::crash_valid(other));
    // Sealed, but with fields no reader could use.
    other = r;
    other.trace_count = 17;
    sys::crash_seal(other);
    CHECK(!sys::crash_valid(other));
    other = r;
    other.kind = sys::CrashKind::None;
    sys::crash_seal(other);
    CHECK(!sys::crash_valid(other));
    other = r;
    memset(other.program, 'x', sizeof(other.program)); // no end
    sys::crash_seal(other);
    CHECK(!sys::crash_valid(other));

    sys::crash_clear(r);
    CHECK(!sys::crash_valid(r));

    // Text is cut to fit and cleaned.
    char field[8];
    sys::crash_text(field, sizeof(field), "A long name");
    CHECK(strcmp(field, "A long ") == 0);
    sys::crash_text(field, sizeof(field), "a\x01\xF1" "b");
    CHECK(strcmp(field, "a??b") == 0 && field[7] == 0);
    sys::crash_text(field, sizeof(field), nullptr);
    CHECK(field[0] == 0);
}

void test_words()
{
    char t[600];
    sys::CrashRecord r = hang_record();
    sys::crash_headline(r, t, sizeof(t));
    CHECK(strcmp(t, "The program \"Editor\" (PID 7) stopped responding.") == 0);
    sys::crash_details(r, t, sizeof(t), "\n");
    CHECK(strcmp(t, "What    : hang, no sign of life for 15 s\n"
                    "Program : Editor, PID 7\n"
                    "Task    : ui, running on core 1\n"
                    "Uptime  : 0 h 12 min 34 s\n") == 0);

    r.pid = 0;
    r.program[0] = 0;
    r.core = sys::CrashRecord::kNoCore;
    sys::crash_details(r, t, sizeof(t), "\n");
    CHECK(strstr(t, "Program : none\nTask    : ui, waiting (on no core)\n") != nullptr);
    sys::crash_headline(r, t, sizeof(t));
    CHECK(strcmp(t, "The desktop stopped responding (no program was running).") == 0);
    sys::crash_text(r.task, sizeof(r.task), "shell");
    sys::crash_headline(r, t, sizeof(t));
    CHECK(strcmp(t, "The task \"shell\" stopped responding.") == 0);

    r = panic_record();
    sys::crash_headline(r, t, sizeof(t));
    CHECK(strcmp(t, "The program \"Cactus Patch\" (PID 12) made an error the machine could not recover from.") == 0);
    sys::crash_details(r, t, sizeof(t), "\n");
    CHECK(strcmp(t, "What    : panic, StoreProhibited\n"
                    "Program : Cactus Patch, PID 12\n"
                    "Task    : ui, running on core 0\n"
                    "Uptime  : 1 h 2 min 3 s\n"
                    "Where   : PC 0x4200ABCD, address 0x00000000\n"
                    "Trace   : 0x42001000 0x42001111 0x42001222 0x42001333 0x42001444\n"
                    "          0x42001555 0x42001666 0x42001777\n") == 0);

    sys::crash_begin(r, sys::CrashKind::Reset);
    sys::crash_text(r.reason, sizeof(r.reason), "the supply voltage dipped (brownout)");
    sys::crash_seal(r);
    sys::crash_headline(r, t, sizeof(t));
    CHECK(strcmp(t, "The machine restarted by itself: the supply voltage dipped (brownout).") == 0);
    sys::crash_details(r, t, sizeof(t), "\n");
    CHECK(strcmp(t, "What    : reset, the supply voltage dipped (brownout)\n") == 0);

    // A buffer that is too small is filled and ended, never overrun.
    char small[20];
    memset(small, 'Z', sizeof(small));
    const size_t n = sys::crash_details(panic_record(), small, 12, "\n");
    CHECK(n == 11 && small[11] == 0 && small[12] == 'Z');
}

std::string read_all(const fs::Storage &storage, const char *path)
{
    static char data[40000];
    size_t size = 0;
    if (storage.read_file(path, data, sizeof(data), size) != ports::FsError::Ok) {
        return "<missing>";
    }
    return std::string(data, size);
}

// The log: made when missing, added to after that, started again when it
// would pass its limit.
void test_log()
{
    static host::HostMemory memory;
    static fs::Storage storage;
    static fs::MemoryVolume disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', disk);

    CHECK(sys::crash_log_append(storage, hang_record(), "2026-11-17 21:04") == ports::FsError::Ok);
    std::string log = read_all(storage, sys::kCrashLog);
    CHECK(log == "--- 2026-11-17 21:04 ---\r\n"
                 "The program \"Editor\" (PID 7) stopped responding.\r\n"
                 "What    : hang, no sign of life for 15 s\r\n"
                 "Program : Editor, PID 7\r\n"
                 "Task    : ui, running on core 1\r\n"
                 "Uptime  : 0 h 12 min 34 s\r\n"
                 "\r\n");
    const size_t first = log.size();
    CHECK(sys::crash_log_append(storage, panic_record()) == ports::FsError::Ok);
    log = read_all(storage, sys::kCrashLog);
    CHECK(log.size() > first && log.compare(0, first, read_all(storage, sys::kCrashLog), 0, first) == 0);
    CHECK(log.find("--- Chinchilla restarted ---\r\nThe program \"Cactus Patch\"", first) == first);
    CHECK(log.find("0x42001777\r\n\r\n") == log.size() - 14);

    // Up to the limit it grows; the entry that would pass it starts it again.
    size_t before = log.size();
    int entries = 2;
    while (true) {
        CHECK(sys::crash_log_append(storage, panic_record()) == ports::FsError::Ok);
        entries++;
        const size_t now = read_all(storage, sys::kCrashLog).size();
        CHECK(now <= sys::kCrashLogMax);
        if (now < before) {
            CHECK(read_all(storage, sys::kCrashLog).find("--- Chinchilla restarted ---") == 0);
            break;
        }
        before = now;
        if (entries > 500) {
            CHECK(false);
            break;
        }
    }
    CHECK(entries > 30);

    // No drive: an error, nothing else.
    static fs::Storage empty;
    CHECK(sys::crash_log_append(empty, hang_record()) != ports::FsError::Ok);
}

// A clock that moves on 1 ms each time it is read.
class SteppingClock : public ports::Clock {
public:
    uint32_t micros() override { return now += 1000; }
    uint32_t now = 0;
};

// The error screen counts down and goes on by itself; a key ends it at once
// and does not skip the screens after it; the settings are read.
void test_screen_waits()
{
    host::MemoryDisplay display(640, 480);
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    static int logged = 0;
    shell::BootIo io{display, system, memory, clock, nullptr, nullptr, nullptr, [](const char *line, void *) {
                         logged += strstr(line, "\"Editor\" (PID 7) stopped responding") != nullptr;
                     }};
    io.crash_hold_s = 15;
    shell::show_crash_screen(io, hang_record(), true);
    CHECK(clock.now >= 15000000 && clock.now < 15200000 && !io.skipped && logged == 1);
    // Something was drawn, and not only black.
    int lit = 0;
    for (int i = 0; i < 640 * 480; i++) {
        lit += display.pixels()[i] != 0;
    }
    CHECK(lit > 2000);

    // A monitor that is slow to show a picture gets the whole countdown.
    clock.now = 0;
    io.video_start_us = 0;
    io.monitor_wait_ms = 2500;
    io.crash_hold_s = 3;
    shell::show_crash_screen(io, hang_record(), true);
    CHECK(clock.now >= 5500000 && clock.now < 5600000);

    // A key.
    static int polls = 0;
    io.skip = [](void *) { return ++polls > 40; };
    io.monitor_wait_ms = 0;
    io.crash_hold_s = 0; // no limit: only a key ends it
    clock.now = 0;
    shell::show_crash_screen(io, panic_record(), false);
    CHECK(clock.now < 200000 && !io.skipped);

    // Without hold (the debug link asking to see it): drawn, no waiting.
    clock.now = 0;
    shell::show_crash_screen(io, panic_record(), true, false);
    CHECK(clock.now == 0);

    // boot.ini.
    static fs::Storage storage;
    static fs::MemoryVolume disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', disk);
    shell::BootSettings s = shell::boot_settings(storage);
    CHECK(s.crash_hold_s == 15 && s.watchdog_s == 15);
    storage.make_folder("C:\\.config");
    const char ini[] = "[boot]\ncrash_hold_s = 4\n[watchdog]\ntimeout_s = 0\n";
    storage.write_file("C:\\.config\\boot.ini", ini, sizeof(ini) - 1);
    s = shell::boot_settings(storage);
    CHECK(s.crash_hold_s == 4 && s.watchdog_s == 0);
}

} // namespace

int main()
{
    test_watchdog();
    test_record();
    test_words();
    test_log();
    test_screen_waits();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
