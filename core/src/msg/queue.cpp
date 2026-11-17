#include "core/msg/queue.hpp"

namespace msg {

namespace {
Queue *g_queue = nullptr;
}

bool Inbox::deliver()
{
    Message m;
    for (int i = 0; i < kBurst; i++) {
        if (!queue_.take(m)) {
            return false;
        }
        if (m.fn == nullptr || (procs_ != nullptr && m.to != proc::kNone && !procs_->alive(m.to))) {
            dropped_++;
            continue;
        }
        if (procs_ != nullptr && m.to != proc::kNone) {
            proc::call_as(procs_, m.to, [&m] { m.fn(m); }); // on the program's own task, if it has one
        } else {
            const proc::Scope as(procs_, m.to);
            m.fn(m);
        }
        delivered_++;
    }
    return true;
}

void set_queue(Queue *queue) { g_queue = queue; }

Queue *queue() { return g_queue; }

bool post(proc::Pid to, Handler fn, void *context, uint32_t a, uint32_t b, uint32_t wait_ms)
{
    return g_queue != nullptr && g_queue->post({fn, context, a, b, to}, wait_ms);
}

} // namespace msg
