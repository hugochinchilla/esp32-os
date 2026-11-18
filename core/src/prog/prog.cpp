#include "core/prog/prog.hpp"

#include <cstddef>
#include <cstring>

namespace prog {

thread_local void *t_slot = nullptr;
thread_local int t_depth = 0;

Runner::Runner(proc::Table &procs) : procs_(procs)
{
    procs_.set_caller(&Runner::call_for_table, this);
    procs_.set_between([](void *self) { static_cast<Runner *>(self)->serve(); }, this);
    procs_.set_busy([](proc::Pid pid, void *self) { return static_cast<Runner *>(self)->late(pid); }, this);
    procs_.on_exit(&Runner::exited, this);
}

Runner::~Runner()
{
    for (Slot &s : slots_) {
        forget(s);
        if (s.used && s.depth == 0) {
            give_back(s);
        }
    }
    procs_.set_caller(nullptr, nullptr);
    procs_.set_between(nullptr, nullptr);
    procs_.set_busy(nullptr, nullptr);
}

void Runner::set_tasks(ports::Tasks *tasks, Kit *kits, int count)
{
    tasks_ = tasks;
    kits_ = kits;
    kit_count_ = tasks != nullptr && kits != nullptr ? (count < kMaxTasks ? count : kMaxTasks) : 0;
}

Runner::Slot *Runner::slot_of(proc::Pid pid)
{
    for (Slot &s : slots_) {
        if (s.used && !s.quit.load() && s.pid == pid) {
            return &s;
        }
    }
    return nullptr;
}

const Runner::Slot *Runner::slot_of(proc::Pid pid) const { return const_cast<Runner *>(this)->slot_of(pid); }

bool Runner::task_left(proc::Pid pid) const
{
    for (const Slot &s : slots_) {
        if (s.used && s.pid == pid) {
            return true;
        }
    }
    return false;
}

int Runner::tasks() const
{
    int n = 0;
    for (const Slot &s : slots_) {
        n += s.used;
    }
    return n;
}

bool Runner::late(proc::Pid pid) const
{
    const Slot *s = slot_of(pid);
    if (s == nullptr) {
        return false;
    }
    const int state = s->state.load();
    return state == Late || state == LateDone;
}

bool Runner::start(proc::Pid pid, uint32_t stack_bytes)
{
    stack_bytes = stack_bytes < kStackBytes ? kStackBytes : stack_bytes;
    if (!threaded() || !procs_.alive(pid)) {
        return false;
    }
    if (slot_of(pid) != nullptr) {
        return true;
    }
    for (int i = 0; i < kit_count_; i++) {
        Slot &s = slots_[i];
        if (s.used) {
            continue;
        }
        s.runner = this;
        s.kit = kits_[i];
        s.pid = pid;
        s.fn = nullptr;
        s.depth = 0;
        s.quit.store(false);
        s.gone.store(false);
        s.state.store(Idle);
        s.in_call.store(0);
        s.wants.store(false);
        s.handing = false;
        s.used = true;
        // Nothing left over from whoever had the kit before.
        s.kit.go->wait(0);
        s.kit.done->wait(0);
        const proc::Process *row = procs_.find(pid);
        s.task = tasks_->start(row->name, &Runner::task, &s, stack_bytes, ports::Priority::High);
        if (s.task == nullptr) {
            s.used = false;
            return false;
        }
        procs_.add_memory(pid, static_cast<int32_t>(stack_bytes));
        return true;
    }
    return false;
}

// The program's own task: sleeps until it is handed a call, makes it, says
// so, and sleeps again, until its process has ended. It never touches the
// process table itself: whoever hands it the call has made its process the
// current one, and may have gone on without it by the time it is done.
void Runner::task(void *context)
{
    Slot &s = *static_cast<Slot *>(context);
    t_slot = &s;
    for (;;) {
        s.kit.go->wait(kForever);
        if (s.fn != nullptr) {
            s.fn(s.context);
            s.fn = nullptr;
            int expected = Running;
            if (!s.state.compare_exchange_strong(expected, Done)) {
                // Late: nobody is waiting. serve() takes it from here.
                const bool last = s.quit.load();
                if (last) {
                    s.gone.store(true);
                }
                s.state.store(LateDone);
                s.kit.done->set();
                if (s.runner->wake_ != nullptr) {
                    s.runner->wake_(s.runner->wake_context_);
                }
                if (last) {
                    return;
                }
                continue;
            }
        }
        if (s.quit.load()) {
            s.gone.store(true);
            s.kit.done->set();
            return;
        }
        s.kit.done->set();
    }
}

// Until the program's task is outside every OS call: a call is short, and
// the task cannot begin another once it is no longer the owner.
void Runner::wait_out_of_calls(Slot &s)
{
    while (s.in_call.load() > 0) {
        s.kit.done->wait(1);
    }
}

int Runner::kept(proc::Pid pid) const
{
    const Slot *s = slot_of(pid);
    return s != nullptr ? s->kept : 0;
}

// A call that cannot be made now, put behind those already waiting for the
// program if it is one to keep.
void Runner::keep_for(Slot &s, proc::CallFn fn, const void *context, size_t size, proc::Keep keep)
{
    if (keep == proc::kDropped || size == 0 || size > proc::kCallRoom || memory_ == nullptr) {
        return;
    }
    if (s.kept >= kMostKept) {
        return; // more than can be: lost
    }
    const size_t bytes = offsetof(Kept, bytes) + size;
    Kept *k = static_cast<Kept *>(memory_->allocate(bytes));
    if (k == nullptr) {
        return;
    }
    k->next = nullptr;
    k->fn = fn;
    k->size = static_cast<uint32_t>(size);
    memcpy(k->bytes, context, size);
    (s.last != nullptr ? s.last->next : s.first) = k;
    s.last = k;
    s.kept++;
    procs_.add_memory(s.pid, static_cast<int32_t>(bytes));
}

// The program answers again: what waited for it, oldest first, each call
// made as any other. If it is late again in one of them the rest go on
// waiting.
void Runner::hand_out(Slot &s)
{
    s.handing = true;
    while (s.first != nullptr && s.used && !s.quit.load() && s.state.load() == Idle) {
        Kept *k = s.first;
        s.first = k->next;
        if (s.first == nullptr) {
            s.last = nullptr;
        }
        s.kept--;
        procs_.add_memory(s.pid, -static_cast<int32_t>(offsetof(Kept, bytes) + k->size));
        const bool in_time = make(&s, s.pid, k->fn, k->bytes, k->size);
        memory_->release(k);
        if (!in_time) {
            break;
        }
    }
    s.handing = false;
}

// The queue of a program that has ended, or of a slot about to be lent.
void Runner::forget(Slot &s)
{
    while (s.first != nullptr) {
        Kept *k = s.first;
        s.first = k->next;
        memory_->release(k);
    }
    s.last = nullptr;
    s.kept = 0;
}

bool Runner::call(proc::Pid pid, proc::CallFn fn, void *context, size_t size, proc::Keep keep)
{
    Slot *s = slot_of(pid);
    if (s != nullptr && ((s->depth > 0 && s->state.load() != Running) || (s->first != nullptr && !s->handing))) {
        // Late: its task is still in the call that was too long, or has
        // only just left it. Nothing reaches it before what already waits.
        keep_for(*s, fn, context, size, keep);
        return false;
    }
    return make(s, pid, fn, context, size);
}

bool Runner::make(Slot *s, proc::Pid pid, proc::CallFn fn, void *context, size_t size)
{
    if (s == nullptr || s->depth > 0 || size > proc::kCallRoom) {
        if (s != nullptr) {
            s->depth++;
        }
        {
            const proc::Scope as(&procs_, pid);
            fn(context);
        }
        if (s != nullptr) {
            s->depth--;
        }
        return true;
    }
    if (size != 0) {
        memcpy(s->room, context, size);
    }
    s->fn = fn;
    s->context = size != 0 ? s->room : context;
    s->depth = 1;
    s->state.store(Running);
    s->kit.done->wait(0); // nothing left over
    bool in_time = true;
    {
        const proc::Scope as(&procs_, pid);
        Slot *before = owner_.exchange(s);
        s->kit.go->set();
        // The state is what says it has finished; a set() left over from
        // before only wakes the wait.
        bool set;
        do {
            set = s->kit.done->wait(limit_ms_);
        } while (set && s->state.load() == Running);
        int expected = Running;
        if (!set && s->state.compare_exchange_strong(expected, Late)) {
            in_time = false;
        }
        owner_.store(before);
        if (!in_time) {
            wait_out_of_calls(*s);
            // Whatever it is doing, it does with the time nobody else
            // wants, taking turns with the kernel's idle tasks.
            tasks_->set_priority(s->task, ports::Priority::Idle);
        }
    }
    if (!in_time) {
        if (news_ != nullptr) {
            news_(pid, false, news_context_);
        }
        return false;
    }
    s->state.store(Idle);
    s->depth = 0;
    if (size != 0) {
        memcpy(context, s->room, size);
    }
    if (s->quit.load()) {
        give_back(*s); // it ended during the call
    }
    return true;
}

// A late program has finished the call it was in.
void Runner::take_back(Slot &s)
{
    s.state.store(Idle);
    s.depth = 0;
    s.wants.store(false);
    if (s.quit.load()) {
        give_back(s); // ended meanwhile
        return;
    }
    tasks_->set_priority(s.task, ports::Priority::High);
    if (news_ != nullptr) {
        news_(s.pid, true, news_context_);
    }
}

bool Runner::waiting() const
{
    for (const Slot &s : slots_) {
        if (!s.used) {
            continue;
        }
        const int state = s.state.load();
        if (state == LateDone || (state == Late && s.wants.load())) {
            return true;
        }
    }
    return false;
}

void Runner::serve()
{
    for (Slot &s : slots_) {
        if (!s.used) {
            continue;
        }
        if (s.state.load() == Late && s.wants.load() && !s.quit.load()) {
            // It is waiting to make an OS call: let it, for a moment.
            const proc::Scope as(&procs_, s.pid);
            Slot *before = owner_.exchange(&s);
            s.kit.go->set();
            s.kit.done->wait(kSliceMs); // sooner if it finishes
            owner_.store(before);
            wait_out_of_calls(s);
        }
        if (s.state.load() == LateDone) {
            take_back(s);
            hand_out(s);
        }
    }
}

// The outermost OS call of a program's task (InCall counts the ones inside).
// Store, then look: the caller that gives up stores the owner, then looks
// at in_call, so one of the two always sees the other.
void Runner::enter(void *slot)
{
    Slot *s = static_cast<Slot *>(slot);
    Runner &r = *s->runner;
    s->in_call.store(1);
    while (r.owner_.load() != s) {
        // Late, and the desktop has gone on: not now.
        s->in_call.store(0);
        if (s->quit.load()) {
            // Its process was ended while it hung, on a machine that cannot
            // take a task away: it never gets in again.
            s->kit.go->wait(1000);
        } else {
            s->wants.store(true);
            if (r.wake_ != nullptr) {
                r.wake_(r.wake_context_);
            }
            s->kit.go->wait(5);
        }
        s->in_call.store(1);
    }
    s->wants.store(false);
}

void Runner::leave(void *slot) { static_cast<Slot *>(slot)->in_call.store(0); }

// The task of a process that is not in the middle of anything: told to
// leave, waited for, given back.
void Runner::give_back(Slot &s)
{
    s.quit.store(true);
    while (!s.gone.load()) {
        s.kit.go->set();
        s.kit.done->wait(5);
    }
    tasks_->end(s.task);
    s.used = false;
}

void Runner::exited(proc::Pid pid, void *self)
{
    Runner &r = *static_cast<Runner *>(self);
    Slot *s = r.slot_of(pid);
    if (s == nullptr) {
        return;
    }
    r.forget(*s); // what waited for it has nobody to go to
    const int state = s->state.load();
    if (state == Late) {
        // Ended while it hangs. It is outside every OS call (it cannot get
        // in while it is late), so it holds nothing of the system's.
        s->quit.store(true);
        if (r.tasks_->kill(s->task)) {
            s->used = false;
        }
        // Otherwise it stays until it returns by itself (serve()).
    } else if (state == LateDone || s->depth > 0) {
        s->quit.store(true); // call() or serve() gives the task back
    } else {
        r.give_back(*s);
    }
}

bool Runner::call_for_table(proc::Pid pid, proc::CallFn fn, void *context, size_t size, proc::Keep keep,
                            void *self)
{
    return static_cast<Runner *>(self)->call(pid, fn, context, size, keep);
}

Runner &runner()
{
    static Runner r;
    return r;
}

} // namespace prog
