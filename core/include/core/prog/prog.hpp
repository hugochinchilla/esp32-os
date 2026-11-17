// Programs on tasks of their own (backlog item 26). A program that has one
// runs all of its code there: its events, its timers, its menu actions, the
// results of its jobs. The UI task hands each call over and waits until the
// program's task has done it, as Win16's SendMessage did: one of the two
// runs at a time, so the windows, the timers and the process table still
// need no locks (docs/journal/2026-11-17-program-tasks.md).
//
// The wait has a limit. A program that has not come back by then is late:
// "not responding". The caller goes on without it, and calls for it are not
// made until it has finished the one it is in. What keeps a late program
// from the desktop's things while the desktop is using them is that every
// OS call begins with enter() and ends with leave(): a late program waits in
// enter() until the UI task, with nothing else under way (serve()), lets it
// run for a moment.
//
// A machine without tasks (the browser build, the plain PC desktop, most
// tests) has no ports::Tasks: no program gets a task, and every call is made
// by the caller as it always was.
#pragma once

#include <atomic>
#include <cstddef>
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

constexpr uint32_t kForever = 0xFFFFFFFF;

class Runner {
public:
    static constexpr int kMaxTasks = 8;
    // ponytail: one size for every program; to be measured on the board.
    static constexpr uint32_t kStackBytes = 8 * 1024;
    // How long the machine waits for a program before it goes on without
    // it (set_limit; the board and the simulator use it).
    static constexpr uint32_t kLimitMs = 2000;
    // How long a late program is let run OS calls at a time (serve()).
    static constexpr uint32_t kSliceMs = 10;

    explicit Runner(proc::Table &procs = proc::table());
    ~Runner();
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;

    // Where tasks come from. Null tasks (the default), or no kits: no
    // program gets a task of its own.
    void set_tasks(ports::Tasks *tasks, Kit *kits, int count);
    bool threaded() const { return tasks_ != nullptr && kit_count_ > 0; }
    // How long a caller waits for a program, in ms (kForever, the default:
    // as long as it takes).
    void set_limit(uint32_t ms) { limit_ms_ = ms; }
    uint32_t limit() const { return limit_ms_; }
    // Told, on the UI task, when a program stops responding (false) and
    // when it has come back (true): its windows are to be painted again.
    using NewsFn = void (*)(proc::Pid pid, bool responding, void *context);
    void set_news(NewsFn news, void *context)
    {
        news_ = news;
        news_context_ = context;
    }
    // Called from a program's task when the UI task has something to do
    // for it in serve(): makes the UI loop go round.
    void set_wake(void (*wake)(void *context), void *context)
    {
        wake_ = wake;
        wake_context_ = context;
    }

    // The UI task. Gives the process a task of its own, with its stack
    // counted as the process's memory. False if it has none now (no tasks
    // here, all the kits lent, no memory for the stack): it then runs on the
    // caller's task. True too if it already had one.
    bool start(proc::Pid pid);
    bool has_task(proc::Pid pid) const { return slot_of(pid) != nullptr; }
    // Not responding: in a call that has taken longer than the limit.
    bool late(proc::Pid pid) const;
    // Program tasks alive now (those not yet given back included).
    int tasks() const;

    // Runs fn(context) with `pid` as the current process: on the process's
    // own task if it has one, waiting until it is done; otherwise here. With
    // a size, the context is that many bytes (proc::kCallRoom at most),
    // copied for the program and copied back when it is done, so that a
    // late program is not left with a pointer into its caller's stack.
    // False if the call was not made, or not finished in time: the process
    // is late.
    //
    // A process whose task is already in the middle of a call (it asked for
    // something that calls back into it) is called here, so nothing waits
    // for itself.
    bool call(proc::Pid pid, proc::CallFn fn, void *context, size_t size = 0);

    // The UI task, with nothing else under way (once a round of its loop):
    // lets late programs that are waiting to make an OS call run for a
    // moment, and takes back those that have finished.
    void serve();
    // Whether serve() has something to do.
    bool waiting() const;

    // Any task. Around every OS call. Nothing for the UI task, or for a
    // program that is being waited for; a late program sleeps in enter()
    // until serve() lets it in.
    void enter();
    void leave();

private:
    enum State : int { Idle, Running, Done, Late, LateDone };
    struct Slot {
        Runner *runner = nullptr;
        Kit kit;
        proc::Pid pid = proc::kNone;
        ports::TaskId task = nullptr;
        proc::CallFn fn = nullptr;
        void *context = nullptr;
        int depth = 0;     // calls of the process under way, here or nested
        bool used = false;
        std::atomic<bool> quit{false}; // the process has ended: the task goes at the next safe point
        std::atomic<bool> gone{false}; // its task has left its loop for good
        std::atomic<int> state{Idle};
        std::atomic<int> in_call{0};    // OS calls its task is inside (nested ones counted)
        std::atomic<bool> wants{false}; // late, and waiting in enter()
        alignas(std::max_align_t) unsigned char room[proc::kCallRoom];
    };

    static void task(void *slot);
    static bool call_for_table(proc::Pid pid, proc::CallFn fn, void *context, size_t size, void *self);
    static void exited(proc::Pid pid, void *self);
    Slot *slot_of(proc::Pid pid);
    const Slot *slot_of(proc::Pid pid) const;
    Slot *mine();
    void give_back(Slot &slot);
    void take_back(Slot &slot);
    void wait_out_of_calls(Slot &slot);

    proc::Table &procs_;
    ports::Tasks *tasks_ = nullptr;
    Kit *kits_ = nullptr;
    int kit_count_ = 0;
    uint32_t limit_ms_ = kForever;
    NewsFn news_ = nullptr;
    void *news_context_ = nullptr;
    void (*wake_)(void *context) = nullptr;
    void *wake_context_ = nullptr;
    // Whose task may be inside OS calls now: null, the UI task's side (and
    // any program on it); otherwise the program being waited for.
    std::atomic<Slot *> owner_{nullptr};
    Slot slots_[kMaxTasks];
};

// The machine's one runner.
Runner &runner();

// For the first line of an OS call: enter() now, leave() at the end.
class InCall {
public:
    InCall() { runner().enter(); }
    ~InCall() { runner().leave(); }
    InCall(const InCall &) = delete;
    InCall &operator=(const InCall &) = delete;
};

} // namespace prog
