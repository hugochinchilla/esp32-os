// init: the first process, PID 1. It reads the start-up file and starts what
// the file says, in the file's order; everything else descends from it.
// Orphans are handed to it, and it is init that starts again what the file
// says comes back when it ends, as `respawn` in a Unix /etc/inittab: the
// process table tells it of every ending (proc::Supervisor). Backlog items
// 18 and 39; docs/journal/2026-11-17-init.md and
// 2026-11-17-init-respawn.md.
//
// init has no task of its own: it is the UI task's own loop, the code that
// was there before any program and that runs between two of them. Its row
// says so by being the root.
//
// The start-up file is text, a line for each thing, WHAT:ACTION as in
// inittab (the owner's choice, 2026-11-17):
//
//     # a comment
//     desktop:once         a part of the system, by its name: started
//     taskbar:respawn      the same, and started again whenever it ends
//     console:off          not started
//     terminal:once        a program or a file, as Start > Run... takes it
//
// The action is after the last colon, so a path keeps its own. This is the
// only form: the verbs of the first one ("start desktop") are not read any
// more (the owner, 2026-11-17: nothing on the board has to be kept).
//
// The parts are what the machine has said it has (add()): the desktop, its
// taskbar and the network services today. What comes back is watched by
// its process, which has the thing's name. Without a file, default_text()
// is used, and written where the file belongs so there is one to edit.
//
// A thing can also be stopped and started by hand while the machine runs
// (stop(), start_again(); the `service` command). What is stopped by hand
// stays stopped: init does not start it again, whatever its line says,
// until it is started by hand or the machine starts the next time
// (docs/journal/2026-11-18-services.md).
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
// Stops a thing that was started (set_stop). It may finish a little later,
// on another task.
using StopFn = void (*)(void *context);
// Opens a program or a file by what was written; false, with the reason in
// `why`, if nothing was opened.
using RunFn = bool (*)(const char *what, char *why, size_t capacity, void *context);
using LogFn = void (*)(const char *line, void *context);

// Whether a thing may be started again when its process ends.
enum class Again : uint8_t {
    No,   // it cannot: NAME:respawn starts it once and says so
    Can,  // if the file says NAME:respawn
    Does, // unless the file says NAME:once (and when no line names it)
};

// What stop() answers.
enum class Stopped : uint8_t {
    Ok,
    NoSuchThing,
    NotStarted,
    CannotBe, // it has no way to be stopped (the desktop, inetd: kill ends that one)
};

// The lines of the start-up file as text, for a tool that edits them (the
// Services dialog). `what` is compared as init compares it; the action is
// what is after the last colon.
//
// The action of the first line that names `what` ("once", "respawn",
// "off"), copied to `action`; false if no line does.
bool line_action(const char *text, size_t length, const char *what, char *action, size_t capacity);
// Gives `what` that action: its first line is changed where it stands,
// later lines for it are taken out, and with no line one is added at the
// end. Every other line stays as it is, comments too. Returns the new
// length; 0 if it does not fit in `capacity`, the text then being as it was.
size_t set_line_action(char *text, size_t length, size_t capacity, const char *what, const char *action);

class Init;
// The init of this machine: the one made last. nullptr: there is none (a
// test that starts the desktop by itself).
Init *the();

class Init : public proc::Supervisor {
public:
    static constexpr int kMaxUnits = 12;
    // The brake. Something that ends less than kQuickUs after it was
    // started, kQuickLimit times one after the other, is not started again:
    // something is wrong with it, and starting it for ever would take the
    // machine along. One that lasts longer starts the count again. It needs
    // the table's clock (proc::Table::set_clock); without one nothing is
    // braked.
    static constexpr uint32_t kQuickUs = 2000000;
    static constexpr int kQuickLimit = 5;
    static constexpr size_t kMaxFile = 2048; // a longer file is read this far
    static constexpr int kMaxLine = 96;

    // Makes the row: PID 1, if the table is empty, as it is on a machine
    // that is starting. In a table that already has a root, init is that
    // root and makes nothing.
    explicit Init(proc::Table &procs);
    ~Init();
    Init(const Init &) = delete;
    Init &operator=(const Init &) = delete;

    // Something a line can name. `name` must stay valid. `needed`:
    // started after the file's last line if no line started it, so a file
    // that leaves the desktop out does not leave a machine with nothing on
    // its screen (until there is a console to fall back on, item 14). And
    // started before anything that is not needed, wherever its line is: a
    // service or a program may count on the desktop being there.
    // `again`: see Again. What comes back must make a process with its own
    // name each time it starts: that is how init knows it has ended. May be
    // called while the file is being done (the desktop adds its taskbar).
    // False: no room, or the name is taken.
    bool add(const char *name, StartFn start, void *context, bool needed = false, Again again = Again::No);
    bool has(const char *name) const;
    // How a thing is stopped by hand, with the context it was added with.
    // Without one, a thing whose process init watches is stopped by asking
    // that process to end (TERM), and anything else cannot be stopped.
    bool set_stop(const char *name, StopFn stop);
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
    // The file is being done now (boot() or run_text() has not returned).
    bool booting() const { return booting_; }
    // Starts a thing by hand, as it would start at its line: after init
    // gave up on it, or one added after the file was done. It has all its
    // tries again. False: there is no such thing, it is running, or it did
    // not start.
    bool start_again(const char *name);
    // Stops a thing by hand. It is not started again when it ends, even if
    // its line says respawn, until start_again() or the next start of the
    // machine.
    Stopped stop(const char *name);
    // The machine says a thing is not running any more (its service ended
    // with the loop it lived in): it counts as not started, and can be
    // started by hand.
    void down(const char *name);
    // What there is, in the order it was added, for a list.
    struct Info {
        const char *name;
        bool started;
        bool respawn;  // its line says it comes back
        bool held;     // stopped by hand
        bool can_stop; // stop() has a way to
    };
    int units() const { return count_; }
    Info info(int index) const;
    Info info(const char *name) const; // its name nullptr: there is none
    // How many things init has stopped starting again, since the start.
    int given_up() const { return given_up_; }

    // proc::Supervisor: a process has ended; what waits to come back.
    void ended(const proc::Process &process) override;
    bool restarts_waiting() const override { return due_count_ > 0; }
    int restart_due() override;
    proc::Pid pid() const { return pid_; }
    // Whether boot() found a file, and whether it wrote the default one.
    bool had_file() const { return had_file_; }
    bool wrote_file() const { return wrote_file_; }

    static const char *default_text();

private:
    struct Unit {
        const char *name = nullptr;
        StartFn start = nullptr;
        StopFn stop = nullptr;
        void *context = nullptr;
        bool needed = false;
        bool started = false;
        bool early = false; // started before its line, for something that needs it
        Again again = Again::No;
        bool respawn = false;      // it is started again when it ends
        proc::Pid pid = proc::kNone; // its process, while it is watched
        int quick = 0;             // quick endings, one after the other
        bool off = false;          // the file says it is not to be started
        bool held = false;         // stopped by hand: not started again by init
    };

    void take(const char *text, size_t length);
    void end_line();
    int finish();
    enum class Action : uint8_t { Once, Respawn, Off };
    bool line(const char *text);
    bool part(const char *name, Action action);
    bool program(const char *what);
    bool needed_first(const char *before, const char *off = nullptr);
    bool start(Unit &unit);
    Unit *unit(const char *name);
    bool may_restart(Unit &unit, const proc::Process &process);
    void log(const char *format, ...) __attribute__((format(printf, 2, 3)));

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
    bool booting_ = false;
    bool done_ = false; // the file has been done once
    Unit *due_[kMaxUnits] = {};
    int due_count_ = 0;
    int given_up_ = 0;
};

} // namespace init
