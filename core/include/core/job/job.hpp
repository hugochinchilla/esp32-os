// Background jobs: work a program hands to a task of its own, so that the
// desktop goes on while it runs. The Editor's save is the first: the file is
// written on a worker task, the window is locked meanwhile, and the other
// windows stay in use (docs/DECISIONS.md, "Saves"; docs/journal/
// 2026-11-17-jobs.md).
//
//     static uint32_t write_out(const job::Job &job, void *context);  // worker task
//     static void saved(void *context, uint32_t result, bool stopped); // UI task
//     ...
//     job::Spec spec;
//     spec.name = "save";
//     spec.work = &write_out;
//     spec.done = &saved;
//     spec.context = this;
//     if (!job::jobs().start(spec)) { ... }   // UI task, as the program
//
// A job belongs to the process that started it, and that process cannot end
// until the job has: a KILL waits on its row and its closed windows are not
// destroyed, so whatever `context` points to is still there for the work and
// for `done`. That is the whole promise, and the reason `work` may read the
// program's memory from another task. What it may not do is touch windows,
// timers or the process table: those are the UI task's. The result travels
// back through the UI message queue (core/msg/queue.hpp) and `done` runs on
// the UI task as the owner.
//
// A machine without tasks (the browser build, the plain PC desktop, most
// tests) has no ports::Tasks: start() then does the work itself and calls
// `done` before it returns. A program must be written so that both orders
// are fine: set its own state first, call start() last.
#pragma once

#include <atomic>
#include <cstdint>

#include "core/msg/queue.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"

namespace job {

// What the work can ask about itself.
class Job {
public:
    // True once the owner has been sent a KILL: the work should leave at the
    // next place where what it is writing stays whole. It is a request; the
    // process waits for the work either way.
    bool stopping() const { return stop_.load(); }

private:
    friend class Jobs;
    std::atomic<bool> stop_{false};
};

// The work. Runs on the job's own task (or inside start() without tasks).
// Its return value is handed to `done`.
using Work = uint32_t (*)(const Job &job, void *context);
// Runs on the UI task, as the owner, when the work has returned. `stopped`
// says the job had been asked to stop. Not called if the owner is gone.
using Done = void (*)(void *context, uint32_t result, bool stopped);
// Runs on the UI task when the job is asked to stop, for work that sleeps on
// something and has to be woken (a shell session waiting for a key).
using Stop = void (*)(void *context);

struct Spec {
    const char *name = "job"; // the task's name: a literal, 15 letters at most
    Work work = nullptr;
    Done done = nullptr;      // may be null
    Stop stop = nullptr;      // may be null
    void *context = nullptr;
    uint32_t stack_bytes = 6144;
    // Normal for nearly everything. Low for work nobody is waiting for;
    // High only for work that must keep pace with the desktop.
    ports::Priority priority = ports::Priority::Normal;
};

class Jobs {
public:
    // ponytail: eight at once in the whole machine; a save takes one for a
    // few seconds and a shell session will take one while it is open.
    static constexpr int kMaxJobs = 8;

    explicit Jobs(proc::Table &procs = proc::table());
    ~Jobs();
    Jobs(const Jobs &) = delete;
    Jobs &operator=(const Jobs &) = delete;

    // Where tasks come from. Null (the default): the work is done inside
    // start(). Tasks are used only while there is a message queue too
    // (msg::set_queue), since that is how the result comes back.
    void set_tasks(ports::Tasks *tasks) { tasks_ = tasks; }
    // True if a job started now would get a task of its own. Work that
    // never returns by itself (a shell session) must not be started without.
    bool threaded() const { return tasks_ != nullptr && msg::queue() != nullptr; }

    // The UI task only. Starts a job for the current process. False if it
    // could not be started (too many jobs, no memory for the task, the
    // process has a KILL waiting): nothing was called then.
    bool start(const Spec &spec);

    // The UI task only. Asks every job of the process to stop.
    void stop(proc::Pid pid);

    // The UI task only. Jobs not yet finished: all, or one process's.
    int running() const;
    int running(proc::Pid pid) const;

private:
    struct Slot {
        Job job;
        Jobs *jobs = nullptr;
        Spec spec;
        proc::Pid owner = proc::kNone;
        uint32_t result = 0;
        ports::TaskId task = nullptr;
        uint64_t charged = 0; // what of its task's processor time the owner's row already has
        uint32_t stack = 0;  // charged to the owner as memory while it runs
        bool used = false;
        bool told = false; // spec.stop has been called
    };

    static void task(void *slot);
    static void finished(const msg::Message &m);
    void finish(Slot &slot);
    void charge(Slot &slot);
    static void charge_all(void *self);
    static void stop_for_table(proc::Pid pid, void *self);

    proc::Table &procs_;
    ports::Tasks *tasks_ = nullptr;
    Slot slots_[kMaxJobs];
};

// The machine's one job runner.
Jobs &jobs();

} // namespace job
