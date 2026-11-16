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
// One task only for now: the UI task makes and ends every process. No heap.
#pragma once

#include <cstdint>

namespace proc {

// A process number. They count up from 1 and one is not given out again
// until the count has gone all the way round. 0 means "no process".
using Pid = int;

constexpr Pid kNone = 0;
constexpr Pid kInit = 1;      // the first process: everything descends from it
constexpr Pid kMaxPid = 32767; // then back to the lowest free number above 1

enum class State : uint8_t {
    Free,    // an empty row
    Running,
    Exiting, // has lost its last window; removed at the next safe point
};

// Flag bits. What each one makes the OS do is built separately (backlog
// item 4); the table only stores them.
enum Flag : uint8_t {
    kSystem = 1,    // part of the OS: hidden in Task Manager's default view
    kProtected = 2, // the user cannot signal it
    kRespawn = 4,   // started again if it ends
    kWindowed = 8,  // a window program: it ends when its last window closes
};

struct Process {
    static constexpr int kNameSize = 24;

    Pid pid = kNone;
    Pid parent = kNone;
    State state = State::Free;
    uint8_t flags = 0;
    char name[kNameSize] = "";

    bool is(Flag f) const { return (flags & f) != 0; }
};

class Table {
public:
    static constexpr int kMaxProcesses = 32;

    // The first process, made if it is not there yet: PID 1, "init", system
    // and protected. It stands for the start-up code until init is a real
    // program with a start-up file (backlog item 18).
    Pid init();

    // A new process. The parent must be alive, or kNone for init's children
    // (init is made if needed). Returns kNone when the table is full or
    // there is no such parent.
    Pid spawn(const char *name, Pid parent, uint8_t flags = 0);
    // A service started by the machine: a child of init with the system flag.
    Pid spawn_service(const char *name, uint8_t flags = 0) { return spawn(name, kNone, flags | kSystem); }

    // Removes the row. Its children are handed to init, as on Unix. Returns
    // false if there is no such process, or it is init.
    bool exit(Pid pid);

    // The row for a PID, nullptr if none. The pointer is good until the next
    // spawn() or exit().
    const Process *find(Pid pid) const;
    // The oldest live process with this name, kNone if none.
    Pid find_by_name(const char *name) const;
    bool alive(Pid pid) const { return find(pid) != nullptr; }
    void set_state(Pid pid, State state);
    void set_flags(Pid pid, uint8_t flags);

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
    Pid next_ = kInit;
    Pid current_ = kNone;
    uint32_t changes_ = 0;
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
