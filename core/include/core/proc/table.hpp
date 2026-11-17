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

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
    kRespawn = 4,   // started again if it ends, whatever ended it: init marks what it looks after
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

// Ends a service: a process without windows (see Table::set_stop).
using StopFn = void (*)(Pid pid, Signal signal, void *context);

// Told of every process that ends (see Table::on_exit).
using ExitFn = void (*)(Pid pid, void *context);

// A call into a program's code, and whoever makes such calls on the
// program's own task (see Table::call).
using CallFn = void (*)(void *context);
using CallerFn = bool (*)(Pid pid, CallFn fn, void *context, size_t size, void *self);
// The most a call's context may be when it is handed over by copy.
constexpr size_t kCallRoom = 384;

struct Process;

// Whoever looks after what ends (see Table::set_supervisor): init. The
// table only says that a process has gone; whether it is started again, how
// often and by whom is the supervisor's business.
class Supervisor {
public:
    // A process is ending: its row, still whole. Called from inside exit(),
    // so nothing is started here; it is noted for restart_due().
    virtual void ended(const Process &process) = 0;
    // Something noted is waiting to be started again.
    virtual bool restarts_waiting() const = 0;
    // Starts what is waiting. Returns how many were started.
    virtual int restart_due() = 0;

protected:
    ~Supervisor() = default;
};

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
    uint32_t born = 0;    // the table's clock when it was made (init's brake on restarts)
    char name[kNameSize] = "";
    StopFn stop = nullptr;     // a service that can be ended (set_stop)
    void *stop_context = nullptr;

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

    // Services (backlog item 5b). A process without windows can be
    // signalled only once it has a stop function: the code that really ends
    // what the row stands for. Whoever acts on signals calls stop_service()
    // at a safe point; the function runs on the UI task, may take its time
    // to finish elsewhere, and removes the row (exit()) when the service
    // has ended. TERM and KILL are the same to most services.
    void set_stop(Pid pid, StopFn stop, void *context);
    bool stop_service(Pid pid, Signal signal);

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

    // Whoever keeps something for processes (the open files of core/os) is
    // told when one ends, from inside exit(), after its row has gone: it
    // gives back what the process left. Asking twice with the same function
    // changes its context.
    static constexpr int kMaxExitFns = 4;
    void on_exit(ExitFn fn, void *context);

    // Starting again what ends is not the table's work: it tells the
    // supervisor (init, core/init) of every ending, and passes on the two
    // questions the window manager asks at its safe points. With no
    // supervisor nothing comes back.
    void set_supervisor(Supervisor *supervisor) { supervisor_ = supervisor; }
    Supervisor *supervisor() const { return supervisor_; }
    bool restarts_waiting() const { return supervisor_ != nullptr && supervisor_->restarts_waiting(); }
    int restart_due() { return supervisor_ != nullptr ? supervisor_->restart_due() : 0; }
    // The table's clock (set_clock), for whoever counts how long a process
    // lived. False: there is no clock.
    bool now(uint32_t &micros) const
    {
        if (micros_ == nullptr) {
            return false;
        }
        micros = micros_(micros_context_);
        return true;
    }

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

    // Calls into a program's code: fn(context) with `pid` as the current
    // process. Every such call from outside the program goes through here
    // (an event for its window, its timer, its menu's action, a message, a
    // job's result), so that a program with a task of its own (core/prog)
    // has it made there; without a caller set, it is made here. With a
    // size, the context is that many bytes, copied for the program and back
    // (prog::Runner::call). False: not made, or not finished in time (the
    // program is not responding).
    bool call(Pid pid, CallFn fn, void *context, size_t size = 0);
    void set_caller(CallerFn caller, void *self)
    {
        caller_ = caller;
        caller_self_ = self;
    }
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

    Supervisor *supervisor_ = nullptr;
    CallerFn caller_ = nullptr;
    void *caller_self_ = nullptr;
    struct {
        ExitFn fn = nullptr;
        void *context = nullptr;
    } exit_fns_[kMaxExitFns];
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

// Table::call for a lambda or any other callable: `call_as(table, pid, [=] {
// ... })`. The callable is handed over by copy and copied back, so what it
// holds by value is safe from a program that does not come back in time,
// and results it keeps in itself can be read afterwards (pass it by name).
// What it holds by reference must outlive a late program. A null table
// calls it here.
template <class F> bool call_as(Table *table, Pid pid, F &&f)
{
    using Fn = typename std::remove_const<typename std::remove_reference<F>::type>::type;
    static_assert(std::is_trivially_copyable<Fn>::value && sizeof(Fn) <= kCallRoom, "handed over by copy");
    if (table == nullptr) {
        f();
        return true;
    }
    return table->call(
        pid, [](void *context) { (*static_cast<Fn *>(context))(); },
        const_cast<void *>(static_cast<const void *>(&f)), sizeof(Fn));
}

// The figures as text, for whoever lists processes: processor time as
// "4.2s", "3:07" or "1:02:45"; memory as "640K" or "1.2M" ("-" for none).
const char *time_text(uint64_t us, char *out, int capacity);
const char *memory_text(uint32_t bytes, char *out, int capacity);

// The machine's one table.
Table &table();

} // namespace proc
