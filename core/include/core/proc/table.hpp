// The process table: what is running, under which number, and who started
// it. A process here is a row, not a task: window programs all run on the UI
// task, called by the window manager, and a row may later also own real
// tasks (jobs, shell sessions, services). See docs/DECISIONS.md, "OS
// architecture", and docs/journal/2026-11-16-process-table.md.
//
// The table also knows which process the UI task is working for right now
// (current()). The window manager sets it around every call into a program,
// so whatever the program asks for meanwhile (a window, a timer) is recorded
// as its own.
//
// Signals are how a process is told to end (docs/journal/
// 2026-11-16-signals-and-flags.md). Table::signal() is the one way in: it
// checks that the process may be signalled and leaves the signal waiting on
// the row. Whoever runs the process's code (the window manager, for window
// programs) acts on it at its next safe point, between two events.
//
// One task only for now: the UI task makes and ends every process. No heap.
#pragma once

#include <cstdint>

namespace proc {

// A process number. They count up from 1 and one is not given out again
// until the count has gone all the way round. 0 means "no process".
using Pid = int;

constexpr Pid kNone = 0;
constexpr Pid kRoot = 1;      // the first process: everything descends from it
constexpr Pid kMaxPid = 32767; // then back to the lowest free number above 1

enum class State : uint8_t {
    Free,    // an empty row
    Running,
    Exiting, // has lost its last window; removed at the next safe point
};

// Flag bits.
enum Flag : uint8_t {
    kSystem = 1,    // part of the OS: hidden in Task Manager's default view
    kProtected = 2, // cannot be signalled: signal() answers NotPermitted
    kRespawn = 4,   // started again if it ends, whatever ended it (set_restart)
    kWindowed = 8,  // a window program: it ends when its last window closes
};

// What a process can be told.
enum class Signal : uint8_t {
    Term = 1, // please close: the same as its close button, so it may ask first, or stay
    Kill = 2, // close now: it is not asked, and the OS takes away what it owns
};

// The answer to signal(), named after what Unix's kill prints.
enum class Sent : uint8_t {
    Ok,            // left for the process; acted on at the next safe point
    NoSuchProcess,
    NotPermitted,  // the process is protected
    NotSupported,  // a service without windows: nothing can end those yet
};

// Tells a process's jobs to stop early (see Table::set_stop_jobs).
using StopJobsFn = void (*)(Pid pid, void *context);

// Starts a program again (see Table::set_restart).
using StartFn = void (*)(void *context);

// Told the name of a program that is no longer started again (see
// Table::set_gave_up).
using GaveUpFn = void (*)(const char *name, void *context);

struct Process {
    static constexpr int kNameSize = 24;

    Pid pid = kNone;
    Pid parent = kNone;
    State state = State::Free;
    uint8_t flags = 0;
    uint8_t pending = 0; // Signal bits sent and not yet acted on
    uint8_t jobs = 0;    // background jobs running for it (core/job/job.hpp)
    // What it has used (docs/journal/2026-11-17-process-figures.md).
    uint64_t cpu_us = 0;  // processor time: the UI task's while it was the
                          // current process, and its finished jobs' tasks
    uint32_t memory = 0;  // bytes it holds now: what its windows asked the
                          // Memory port for, and the stacks of its jobs
    uint32_t born = 0;    // the table's clock when it was made (the brake on restarts)
    char name[kNameSize] = "";
    StartFn restart = nullptr; // for kRespawn
    void *restart_context = nullptr;

    bool is(Flag f) const { return (flags & f) != 0; }
};

class Table {
public:
    static constexpr int kMaxProcesses = 32;

    // A new process. The first one made in an empty table is the root of the
    // tree: PID 1, without a parent (pass kNone), and it never ends. On a
    // machine that is init (core/init/init.hpp); where the desktop is
    // started alone, as most tests do, it is the desktop. After that the parent must be alive, or kNone for
    // the root's children. Returns kNone when the table is full or there is
    // no such parent.
    Pid spawn(const char *name, Pid parent, uint8_t flags = 0);
    // A system process started by the machine: a child of the root with the
    // system flag. Only for what really is a task of its own (the desktop is
    // the UI task; `inetd` is the network services' task). A service polled
    // from the UI loop gets no row: a row without a task behind it says
    // something that is not true.
    Pid spawn_service(const char *name, uint8_t flags = 0) { return spawn(name, kNone, flags | kSystem); }

    // Removes the row. Its children are handed to the root, as orphans go to init on Unix. Returns
    // false if there is no such process, or it is the root.
    bool exit(Pid pid);

    // The row for a PID, nullptr if none. The pointer is good until the next
    // spawn() or exit().
    const Process *find(Pid pid) const;
    // The oldest live process with this name, kNone if none.
    Pid find_by_name(const char *name) const;
    bool alive(Pid pid) const { return find(pid) != nullptr; }
    void set_state(Pid pid, State state);
    void set_flags(Pid pid, uint8_t flags);

    // Sends a signal to a process: the one function every sender uses (End
    // Task today, the shell's kill later). Nothing happens to the process
    // here. The signal waits on its row until whoever runs its code takes it
    // with take_signals() at a safe point.
    Sent signal(Pid pid, Signal signal);
    // For whoever acts on signals: the Signal bits waiting for a process,
    // which are cleared. signals_waiting() says whether any row has some.
    uint8_t take_signals(Pid pid);
    bool signals_waiting() const { return waiting_ != 0; }

    // Background jobs. A process with jobs running is not ended: whoever
    // acts on signals leaves a KILL waiting on the row, and a window program
    // that has lost its last window stays in the table, until the count is
    // back to zero. A job reads and writes the program's memory from another
    // task, so the memory must outlive it, and a task is never taken away
    // while it may hold an OS lock (docs/DECISIONS.md, "Signals").
    // job_started() answers false for a process that is gone or has a KILL
    // waiting. The job runner (job::Jobs) calls these; nobody else should.
    bool job_started(Pid pid);
    void job_finished(Pid pid);
    int jobs(Pid pid) const;
    // Called from signal() when a KILL is sent to a process with jobs: the
    // jobs are asked to stop early, and the KILL takes effect when they have.
    void set_stop_jobs(StopJobsFn fn, void *context)
    {
        stop_jobs_ = fn;
        stop_jobs_context_ = context;
    }

    // Respawn: sets the flag and remembers how to start the program again.
    // When the process ends, by a signal or by itself, `start` is put on a
    // list, and restart_due() runs the list with the root as the current process
    // (so what it starts is the root's child). It is called at safe points, not
    // from inside exit(). `start` must make the new process itself, and call
    // set_restart() on it if that one is to come back too.
    void set_restart(Pid pid, StartFn start, void *context);
    int restart_due(); // how many were started
    // The brake. A program that ends less than kQuickUs after it was made,
    // kQuickLimit times one after the other, is not started again: something
    // is wrong with it, and starting it for ever would take the machine
    // along. One that lasts longer starts the count again. A program is
    // known by its start function and context. It needs the clock
    // (set_clock); without one nothing is braked. init says so in the log.
    static constexpr uint32_t kQuickUs = 2000000;
    static constexpr int kQuickLimit = 5;
    void set_gave_up(GaveUpFn fn, void *context)
    {
        gave_up_ = fn;
        gave_up_context_ = context;
    }
    int given_up() const { return given_up_; } // how many programs, since the start

    // Live processes in order of creation (lowest PID first, until the
    // numbers have gone round).
    int count() const { return count_; }
    const Process &at(int index) const { return rows_[order_[index]]; }
    // Goes up by one with every spawn and exit: a cheap "did the list change".
    uint32_t changes() const { return changes_; }

    // The process the UI task is working for now; kNone outside any.
    Pid current() const { return current_; }
    void set_current(Pid pid);

    // Figures. With a clock set, the time between two set_current() calls is
    // charged to the process that was current in between: the window manager
    // makes a program current around every call into it, so this is the
    // program's share of the UI task. Time with no process current is the
    // OS's own and is not kept here.
    void set_clock(uint32_t (*micros)(void *context), void *context);
    // Adds processor time used elsewhere on the process's behalf (a job's task).
    void add_cpu(Pid pid, uint64_t us);
    // Memory taken (bytes > 0) or given back (bytes < 0). A process that has
    // gone is ignored: what it held is given back after its row.
    void add_memory(Pid pid, int32_t bytes);
    // Whoever keeps figures of its own until asked (the job runner, for jobs
    // still running) says so here; whoever is about to show the figures
    // calls bring_up_to_date() first. The UI task.
    void set_figures_source(void (*source)(void *context), void *context);
    void bring_up_to_date();

    // Tests: an empty table that counts from 1 again.
    void reset();

private:
    Process *row(Pid pid);
    bool pid_in_use(Pid pid) const;

    Process rows_[kMaxProcesses];
    uint8_t order_[kMaxProcesses] = {}; // rows in order of creation
    int count_ = 0;
    Pid next_ = kRoot;
    Pid current_ = kNone;
    uint32_t changes_ = 0;
    int waiting_ = 0; // rows with a signal pending
    void (*figures_source_)(void *context) = nullptr;
    void *figures_context_ = nullptr;
    uint32_t (*micros_)(void *context) = nullptr;
    void *micros_context_ = nullptr;
    uint32_t since_ = 0; // when current_ was set
    StopJobsFn stop_jobs_ = nullptr;
    void *stop_jobs_context_ = nullptr;

    struct Restart {
        StartFn start;
        void *context;
    };
    static constexpr int kMaxDue = 8;
    Restart due_[kMaxDue] = {};
    int due_count_ = 0;

    // Programs that ended quickly the last time, and how many times in a row.
    struct Quick {
        StartFn start = nullptr;
        void *context = nullptr;
        int count = 0;
    };
    bool may_restart(const Process &p);
    Quick quick_[kMaxDue] = {};
    GaveUpFn gave_up_ = nullptr;
    void *gave_up_context_ = nullptr;
    int given_up_ = 0;
};

// Makes `pid` the current process until the end of the block, then puts the
// previous one back. A null table makes it do nothing.
class Scope {
public:
    Scope(Table *table, Pid pid) : table_(table), before_(table != nullptr ? table->current() : kNone)
    {
        if (table_ != nullptr) {
            table_->set_current(pid);
        }
    }
    ~Scope()
    {
        if (table_ != nullptr) {
            table_->set_current(before_);
        }
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Table *table_;
    Pid before_;
};

// The figures as text, for whoever lists processes: processor time as
// "4.2s", "3:07" or "1:02:45"; memory as "640K" or "1.2M" ("-" for none).
const char *time_text(uint64_t us, char *out, int capacity);
const char *memory_text(uint32_t bytes, char *out, int capacity);

// The machine's one table.
Table &table();

} // namespace proc
