#include "core/sys/crash.hpp"

#include <cstdio>
#include <cstring>

namespace sys {

namespace {

// FNV-1a over the record up to its checksum. Kept here, with no table and
// no call into other code, because crash_seal() runs inside the panic
// handler.
uint32_t checksum_of(const CrashRecord &r)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(&r);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(CrashRecord, checksum); i++) {
        h = (h ^ bytes[i]) * 16777619u;
    }
    return h;
}

struct Text {
    char *out;
    size_t capacity, n = 0;
    template <typename... Args> void add(const char *format, Args... args)
    {
        if (capacity == 0 || n >= capacity - 1) {
            return;
        }
        const int k = snprintf(out + n, capacity - n, format, args...);
        n += k > 0 ? static_cast<size_t>(k) : 0;
        n = n < capacity ? n : capacity - 1;
    }
};

const char *or_unknown(const char *text) { return text[0] != 0 ? text : "unknown"; }

} // namespace

void crash_begin(CrashRecord &r, CrashKind kind)
{
    auto *bytes = reinterpret_cast<volatile uint8_t *>(&r);
    for (size_t i = 0; i < sizeof(r); i++) {
        bytes[i] = 0;
    }
    r.kind = kind;
}

void crash_text(char *field, size_t size, const char *text)
{
    size_t i = 0;
    for (; text != nullptr && i + 1 < size && text[i] != 0; i++) {
        const char ch = text[i];
        field[i] = ch >= 0x20 && ch < 0x7F ? ch : '?';
    }
    for (; i < size; i++) {
        field[i] = 0;
    }
}

void crash_seal(CrashRecord &r)
{
    r.magic = CrashRecord::kMagic;
    r.version = CrashRecord::kVersion;
    r.checksum = checksum_of(r);
}

bool crash_valid(const CrashRecord &r)
{
    return r.magic == CrashRecord::kMagic && r.version == CrashRecord::kVersion && r.checksum == checksum_of(r) &&
           r.kind != CrashKind::None && r.kind <= CrashKind::Reset && r.trace_count <= CrashRecord::kTraceSize &&
           r.program[sizeof(r.program) - 1] == 0 && r.task[sizeof(r.task) - 1] == 0 &&
           r.reason[sizeof(r.reason) - 1] == 0;
}

void crash_clear(CrashRecord &r)
{
    r.magic = 0;
    r.checksum = 0;
}

void crash_sample(CrashRecord &r)
{
    crash_begin(r, CrashKind::Hang);
    r.pid = 7;
    r.uptime_ms = 754000;
    r.waited_ms = 15400;
    r.core = 1;
    crash_text(r.program, sizeof(r.program), "Editor");
    crash_text(r.task, sizeof(r.task), "ui");
    crash_seal(r);
}

size_t crash_headline(const CrashRecord &r, char *out, size_t capacity)
{
    Text t{out, capacity};
    if (capacity > 0) {
        out[0] = 0;
    }
    if (r.kind == CrashKind::Reset) {
        t.add("The machine restarted by itself: %s.", or_unknown(r.reason));
        return t.n;
    }
    const char *what =
        r.kind == CrashKind::Hang ? "stopped responding" : "made an error the machine could not recover from";
    if (r.pid != 0) {
        t.add("The program \"%s\" (PID %d) %s.", or_unknown(r.program), static_cast<int>(r.pid), what);
    } else if (strcmp(r.task, "ui") == 0) {
        t.add("The desktop %s (no program was running).", what);
    } else {
        t.add("The task \"%s\" %s.", or_unknown(r.task), what);
    }
    return t.n;
}

size_t crash_details(const CrashRecord &r, char *out, size_t capacity, const char *eol)
{
    Text t{out, capacity};
    if (capacity > 0) {
        out[0] = 0;
    }
    switch (r.kind) {
    case CrashKind::Hang:
        t.add("What    : hang, no sign of life for %u s%s", static_cast<unsigned>(r.waited_ms / 1000), eol);
        break;
    case CrashKind::Panic:
        t.add("What    : panic, %s%s", or_unknown(r.reason), eol);
        break;
    default:
        t.add("What    : reset, %s%s", or_unknown(r.reason), eol);
        break;
    }
    if (r.kind != CrashKind::Reset) {
        if (r.pid != 0) {
            t.add("Program : %s, PID %d%s", or_unknown(r.program), static_cast<int>(r.pid), eol);
        } else {
            t.add("Program : none%s", eol);
        }
        if (r.core == CrashRecord::kNoCore) {
            // It was not running: stuck waiting for something (a lock, a reply).
            t.add("Task    : %s, waiting (on no core)%s", or_unknown(r.task), eol);
        } else {
            // It was running: stuck in a loop.
            t.add("Task    : %s, running on core %u%s", or_unknown(r.task), static_cast<unsigned>(r.core), eol);
        }
        const uint32_t s = r.uptime_ms / 1000;
        t.add("Uptime  : %u h %u min %u s%s", static_cast<unsigned>(s / 3600), static_cast<unsigned>(s / 60 % 60),
              static_cast<unsigned>(s % 60), eol);
    }
    if (r.kind == CrashKind::Panic) {
        t.add("Where   : PC 0x%08lX, address 0x%08lX%s", static_cast<unsigned long>(r.pc),
              static_cast<unsigned long>(r.address), eol);
        for (int i = 0; i < r.trace_count; i++) {
            if (i % 5 == 0) {
                t.add("%s", i == 0 ? "Trace   :" : "         ");
            }
            t.add(" 0x%08lX", static_cast<unsigned long>(r.trace[i]));
            if (i % 5 == 4 || i + 1 == r.trace_count) {
                t.add("%s", eol);
            }
        }
    }
    return t.n;
}

ports::FsError crash_log_append(const fs::Storage &storage, const CrashRecord &r, const char *when,
                                const char *path)
{
    char text[900];
    Text t{text, sizeof(text)};
    t.add("--- %s ---\r\n", when != nullptr && when[0] != 0 ? when : "Chinchilla restarted");
    t.n += crash_headline(r, text + t.n, sizeof(text) - t.n);
    t.add("%s", "\r\n");
    t.n += crash_details(r, text + t.n, sizeof(text) - t.n);
    t.add("%s", "\r\n");

    // Added to the end; a log that would pass its limit starts again.
    ports::DirEntry e;
    fs::File file;
    const bool add = storage.stat(path, e) == ports::FsError::Ok && !e.folder && e.size + t.n <= kCrashLogMax;
    ports::FsError err = storage.open(path, add ? ports::OpenMode::Update : ports::OpenMode::Write, file);
    if (err != ports::FsError::Ok) {
        return err;
    }
    if (add) {
        err = storage.seek(file, e.size);
    }
    if (err == ports::FsError::Ok) {
        err = storage.write(file, text, t.n);
    }
    const ports::FsError closed = storage.close(file);
    return err != ports::FsError::Ok ? err : closed;
}

} // namespace sys
