// init: the first process, PID 1. It reads the start-up file and starts what
// the file says, in the file's order; everything else descends from it.
// Orphans are handed to it and a program that comes back when it ends
// (proc::kRespawn) is started again as its child, which the process table
// already does for whoever is the root (core/proc/table.hpp). Backlog item
// 18; docs/journal/2026-11-17-init.md.
//
// init has no task of its own: it is the UI task's own loop, the code that
// was there before any program and that runs between two of them. Its row
// says so by being the root.
//
// The start-up file is text, a line for each thing:
//
//     # a comment
//     start desktop        something the machine can start, by its name
//     run terminal         a program or a file, as Start > Run... takes it
//
// What `start` can name is what the machine has said it has (add()): the
// desktop and the network services today. Without a file, default_text() is
// used, and written where the file belongs so there is one to edit.
//
// The UI task only. No heap.
#pragma once

#include <cstddef>

#include "core/fs/fs.hpp"
#include "core/proc/table.hpp"

namespace init {

constexpr char kPath[] = "C:\\.config\\startup.txt";

// Starts a thing; false if it could not.
using StartFn = bool (*)(void *context);
// Opens a program or a file by what was written; false, with the reason in
// `why`, if nothing was opened.
using RunFn = bool (*)(const char *what, char *why, size_t capacity, void *context);
using LogFn = void (*)(const char *line, void *context);

class Init {
public:
    static constexpr int kMaxUnits = 8;
    static constexpr size_t kMaxFile = 2048; // a longer file is read this far
    static constexpr int kMaxLine = 96;

    // Makes the row: PID 1, if the table is empty, as it is on a machine
    // that is starting. In a table that already has a root, init is that
    // root and makes nothing.
    explicit Init(proc::Table &procs);

    // Something `start NAME` can start. `name` must stay valid. `needed`:
    // started after the file's last line if no line started it, so a file
    // that leaves the desktop out does not leave a machine with nothing on
    // its screen (until there is a console to fall back on, item 14). And
    // started before anything that is not needed, wherever its line is: a
    // service or a program may count on the desktop being there.
    bool add(const char *name, StartFn start, void *context, bool needed = false);
    void set_run(RunFn run, void *context)
    {
        run_ = run;
        run_context_ = context;
    }
    // One line for each thing started and each line that failed.
    void set_log(LogFn log, void *context)
    {
        log_ = log;
        log_context_ = context;
    }

    // Reads the file and does what it says. Returns how many lines failed.
    int boot(const fs::Storage &storage, const char *path = kPath);
    // The same for text already in memory.
    int run_text(const char *text, size_t length);

    bool started(const char *name) const;
    proc::Pid pid() const { return pid_; }
    // Whether boot() found a file, and whether it wrote the default one.
    bool had_file() const { return had_file_; }
    bool wrote_file() const { return wrote_file_; }

    static const char *default_text();

private:
    struct Unit {
        const char *name = nullptr;
        StartFn start = nullptr;
        void *context = nullptr;
        bool needed = false;
        bool started = false;
        bool early = false; // started before its line, for something that needs it
    };

    void take(const char *text, size_t length);
    void end_line();
    int finish();
    bool line(const char *text);
    bool needed_first(const char *before);
    bool start(Unit &unit);
    void log(const char *format, ...) __attribute__((format(printf, 2, 3)));
    static void gave_up(const char *name, void *context);

    proc::Table &procs_;
    proc::Pid pid_ = proc::kNone;
    Unit units_[kMaxUnits];
    int count_ = 0;
    RunFn run_ = nullptr;
    void *run_context_ = nullptr;
    LogFn log_ = nullptr;
    void *log_context_ = nullptr;
    bool had_file_ = false;
    bool wrote_file_ = false;
    char line_[kMaxLine] = "";
    size_t pending_ = 0;
    bool cut_ = false;
    int failed_ = 0;
};

} // namespace init
