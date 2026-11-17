// The crash record: what the machine writes down in its last moment, so the
// next start can say what happened. See docs/journal/2026-11-17-watchdog.md.
//
// The record is one plain block of fixed size with no pointers in it. On the
// board it lives in memory that a restart does not clear (but a power cut
// does), so whatever reads it must expect rubbish: valid() checks a magic
// word, the version and a checksum before anything is believed.
//
// It is filled in at the worst possible time (inside the processor's panic
// handler, or by the watchdog while a task is stuck), so the functions that
// fill it use nothing but the record itself: no heap, no locks, no printf.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"

namespace sys {

enum class CrashKind : uint8_t {
    None = 0,
    Hang = 1,  // a watched task stopped coming round; the watchdog restarted
    Panic = 2, // the processor stopped the program: bad pointer, abort, stack overflow
    Reset = 3, // the chip reports an abnormal restart and left no record (power dip, hardware watchdog)
};

struct CrashRecord {
    static constexpr uint32_t kMagic = 0x43524148; // "CRAH"
    static constexpr uint16_t kVersion = 1;
    static constexpr int kTraceSize = 16;
    static constexpr uint8_t kNoCore = 0xFF; // Hang: the task was waiting, not running

    uint32_t magic;
    uint16_t version;
    CrashKind kind;
    uint8_t trace_count;       // entries used in trace[]
    int32_t pid;               // the process the task was working for; 0: none
    uint32_t uptime_ms;        // since start-up, when it happened
    uint32_t waited_ms;        // Hang: how long the task had been silent
    uint32_t pc;               // Panic: where the processor was
    uint32_t address;          // Panic: the address it could not use, if that was the fault
    uint32_t trace[kTraceSize]; // Panic: the chain of calls, innermost first
    char program[24];          // the process's name; "" if none
    char task[16];             // the kernel task
    char reason[48];           // Panic and Reset: the processor's or chip's word for it
    uint8_t core;              // the processor it happened on, or kNoCore
    uint8_t reserved[3];
    uint32_t checksum;         // over everything before it
};

// Starts a record: everything zero, then the kind.
void crash_begin(CrashRecord &r, CrashKind kind);
// Copies text into one of the record's fields, cut to fit; unprintable
// bytes become '?'. `text` may be nullptr.
void crash_text(char *field, size_t size, const char *text);
// The last step: sets the magic word, the version and the checksum.
void crash_seal(CrashRecord &r);
bool crash_valid(const CrashRecord &r);
// Makes the record unreadable, so it is reported once.
void crash_clear(CrashRecord &r);

// A made-up record (the Editor, hung), for looking at the error screen
// without crashing anything.
void crash_sample(CrashRecord &r);

// "The program "Editor" (PID 7) stopped responding." One sentence saying
// what happened, for the error screen and the log. Returns its length.
size_t crash_headline(const CrashRecord &r, char *out, size_t capacity);
// The record as lines of text, each ended by `eol`:
//   What    : ...
//   Program : ...
// Returns the length (at most capacity - 1).
size_t crash_details(const CrashRecord &r, char *out, size_t capacity, const char *eol = "\r\n");

// The crash log, a text file anyone can open in the Editor.
constexpr const char *kCrashLog = "C:\\crash.txt";
constexpr uint32_t kCrashLogMax = 16 * 1024; // started again when it would grow past this
// Adds the record to the end of the log, under a line with `when` (the
// date and time if the caller knows them; may be nullptr).
ports::FsError crash_log_append(const fs::Storage &storage, const CrashRecord &r, const char *when = nullptr,
                                const char *path = kCrashLog);

} // namespace sys
