// The message queue into the UI task: how any other task gets work done on
// the desktop. Windows, timers and the process table belong to the UI task
// and are not guarded by locks, so a background job, a shell session or a
// service never touches them. It posts a message instead, and the UI task
// runs the message's handler in its own time, between two rounds of its
// loop. The same queue is what lets the UI loop sleep: it waits until a
// message, serial bytes or the next timer arrive (docs/DECISIONS.md, "OS
// architecture"; docs/journal/2026-11-16-message-queue.md).
//
// A message is "call this function on the UI task", addressed to a process:
//
//     static void saved(const msg::Message &m)          // runs on the UI task
//     {
//         static_cast<Editor *>(m.context)->save_finished(m.a != 0);
//     }
//     ...
//     msg::post(my_pid, &saved, this, ok ? 1 : 0);      // from any task
//
// If the process has ended by the time the message is delivered, the message
// is dropped and the handler never runs: a job that outlives its window
// cannot call into a program that is gone. The handler runs as that process
// (proc::Table::current()), so a window or timer it makes is the process's
// own. Messages to one process arrive in the order they were posted.
//
// The handler is a plain function and the message carries no memory of its
// own: whatever `context` points to must still be there when the message is
// delivered, which the PID check covers when it belongs to the process.
#pragma once

#include <cstdint>

#include "core/proc/table.hpp"

namespace msg {

struct Message;
using Handler = void (*)(const Message &m);

struct Message {
    Handler fn;      // called on the UI task
    void *context;   // the handler's own
    uint32_t a, b;   // two small values, the handler's own
    proc::Pid to;    // dropped unless this process is alive; proc::kNone:
                     // for the OS itself, always delivered
};

constexpr uint32_t kNoWait = 0;
constexpr uint32_t kForever = 0xFFFFFFFF;

// The port. Adapters: FreeRTOS on the board (firmware/os/main/rtos_queue.hpp)
// and threads of the PC for the tests (host/thread_queue.hpp).
class Queue {
public:
    // Any task. Copies the message in and wakes the UI task. When the queue
    // is full it waits up to wait_ms for room (kNoWait: not at all, kForever:
    // as long as it takes) and returns false if there was none. The UI task
    // itself may post, but only with kNoWait: it is the one that makes room.
    virtual bool post(const Message &m, uint32_t wait_ms = kNoWait) = 0;
    // Any task. Makes the UI task's sleep() return, with no message.
    virtual void wake() = 0;

    // The UI task only. The oldest message, false if there is none.
    virtual bool take(Message &out) = 0;
    // The UI task only. Sleeps until a post() or a wake(), or until max_us
    // have gone by (0: does not sleep). True if it was woken. A wake that
    // came before the call is not lost: the call returns at once.
    virtual bool sleep(uint32_t max_us) = 0;

protected:
    ~Queue() = default;
};

// The UI task's end of the queue: takes the messages and runs them.
class Inbox {
public:
    // Messages run per call, so that a task that posts without pause cannot
    // keep the desktop from its timers and its painting.
    static constexpr int kBurst = 16;

    // A null table delivers everything, whatever `to` says (tests).
    explicit Inbox(Queue &queue, proc::Table *procs = &proc::table()) : queue_(queue), procs_(procs) {}

    // Runs the waiting messages, kBurst at most. True if it stopped at the
    // limit: there may be more, so the loop should come back without sleeping.
    bool deliver();

    uint32_t delivered() const { return delivered_; }
    uint32_t dropped() const { return dropped_; } // their process had ended

private:
    Queue &queue_;
    proc::Table *procs_;
    uint32_t delivered_ = 0, dropped_ = 0;
};

// The machine's one queue, for code that has no other way to reach it. Set
// once at start-up, before any other task runs.
void set_queue(Queue *queue);
Queue *queue();

// Posts to the machine's queue; false if there is none or it is full.
bool post(proc::Pid to, Handler fn, void *context = nullptr, uint32_t a = 0, uint32_t b = 0,
          uint32_t wait_ms = kNoWait);

} // namespace msg
