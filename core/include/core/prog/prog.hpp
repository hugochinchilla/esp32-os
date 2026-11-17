// Programs on tasks of their own (backlog item 26). A program that has one
// runs all of its code there: its events, its timers, its menu actions, the
// results of its jobs. The UI task hands each call over and waits until the
// program's task has done it, as Win16's SendMessage did: one of the two
// runs at a time, so the windows, the timers and the process table still
// need no locks (docs/journal/2026-11-17-program-tasks.md).
//
// A machine without tasks (the browser build, the plain PC desktop, most
// tests) has no ports::Tasks: no program gets a task, and every call is made
// by the caller as it always was.
#pragma once

#include <cstdint>

#include "core/ports.hpp"
#include "core/proc/table.hpp"

namespace prog {

// What one program task sleeps on, and what whoever handed it a call sleeps
// on until it is done. The machine lends as many pairs as programs may have
// tasks at once (Runner::set_tasks).
struct Kit {
    ports::Event *go = nullptr;
    ports::Event *done = nullptr;
};

class Runner {
public:
    static constexpr int kMaxTasks = 8;
    // ponytail: one size for every program; to be measured on the board.
    static constexpr uint32_t kStackBytes = 8 * 1024;

    explicit Runner(proc::Table &procs = proc::table());
    ~Runner();
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;

    // Where tasks come from. Null tasks (the default), or no kits: no
    // program gets a task of its own.
    void set_tasks(ports::Tasks *tasks, Kit *kits, int count);
    bool threaded() const { return tasks_ != nullptr && kit_count_ > 0; }

    // The UI task. Gives the process a task of its own, with its stack
    // counted as the process's memory. False if it has none now (no tasks
    // here, all the kits lent, no memory for the stack): it then runs on the
    // caller's task. True too if it already had one.
    bool start(proc::Pid pid);
    bool has_task(proc::Pid pid) const { return slot_of(pid) != nullptr; }
    // Program tasks alive now (those not yet given back included).
    int tasks() const;

    // Runs fn(context) with `pid` as the current process: on the process's
    // own task if it has one, waiting until it is done; otherwise here. A
    // process whose task is already in the middle of a call (it asked for
    // something that calls back into it) is called here too, so nothing
    // waits for itself.
    void call(proc::Pid pid, proc::CallFn fn, void *context);

private:
    struct Slot {
        Runner *runner = nullptr;
        Kit kit;
        proc::Pid pid = proc::kNone;
        ports::TaskId task = nullptr;
        proc::CallFn fn = nullptr;
        void *context = nullptr;
        int depth = 0;     // calls of the process under way, here or nested
        bool used = false;
        bool quit = false; // the process has ended: the task goes at the next safe point
    };

    static void task(void *slot);
    static void call_for_table(proc::Pid pid, proc::CallFn fn, void *context, void *self);
    static void exited(proc::Pid pid, void *self);
    Slot *slot_of(proc::Pid pid);
    const Slot *slot_of(proc::Pid pid) const;
    void give_back(Slot &slot);

    proc::Table &procs_;
    ports::Tasks *tasks_ = nullptr;
    Kit *kits_ = nullptr;
    int kit_count_ = 0;
    Slot slots_[kMaxTasks];
};

// The machine's one runner.
Runner &runner();

} // namespace prog
