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

// Starts a program again (see Table::set_restart).
using StartFn = void (*)(void *context);

struct Process {
    static constexpr int kNameSize = 24;

    Pid pid = kNone;
    Pid parent = kNone;
    State state = State::Free;
    uint8_t flags = 0;
    uint8_t pending = 0; // Signal bits sent and not yet acted on
    char name[kNameSize] = "";
    StartFn restart = nullptr; // for kRespawn
    void *restart_context = nullptr;

    bool is(Flag f) const { return (flags & f) != 0; }
};

class Table {
public:
    static constexpr int kMaxProcesses = 32;

    // A new process. The first one made in an empty table is the root of the
    // tree: PID 1, without a parent (pass kNone), and it never ends. Today
    // that is the desktop; init takes the place when it is a real program
    // (backlog item 18). After that the parent must be alive, or kNone for
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

    // Respawn: sets the flag and remembers how to start the program again.
    // When the process ends, by a signal or by itself, `start` is put on a
    // list, and restart_due() runs the list with the root as the current process
    // (so what it starts is the root's child). It is called at safe points, not
    // from inside exit(). `start` must make the new process itself, and call
    // set_restart() on it if that one is to come back too.
    void set_restart(Pid pid, StartFn start, void *context);
    int restart_due(); // how many were started

    // Live processes in order of creation (lowest PID first, until the
    // numbers have gone round).
    int count() const { return count_; }
    const Process &at(int index) const { return rows_[order_[index]]; }
    // Goes up by one with every spawn and exit: a cheap "did the list change".
    uint32_t changes() const { return changes_; }

    // The process the UI task is working for now; kNone outside any.
    Pid current() const { return current_; }
    void set_current(Pid pid) { current_ = pid; }

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

    struct Restart {
        StartFn start;
        void *context;
    };
    static constexpr int kMaxDue = 8;
    Restart due_[kMaxDue] = {};
    int due_count_ = 0;
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

// The machine's one table.
Table &table();

} // namespace proc
