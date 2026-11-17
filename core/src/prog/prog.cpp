#include "core/prog/prog.hpp"

namespace prog {

Runner::Runner(proc::Table &procs) : procs_(procs)
{
    procs_.set_caller(&Runner::call_for_table, this);
    procs_.on_exit(&Runner::exited, this);
}

Runner::~Runner()
{
    for (Slot &s : slots_) {
        if (s.used && s.depth == 0) {
            give_back(s);
        }
    }
    procs_.set_caller(nullptr, nullptr);
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
        if (s.used && !s.quit && s.pid == pid) {
            return &s;
        }
    }
    return nullptr;
}

const Runner::Slot *Runner::slot_of(proc::Pid pid) const { return const_cast<Runner *>(this)->slot_of(pid); }

int Runner::tasks() const
{
    int n = 0;
    for (const Slot &s : slots_) {
        n += s.used;
    }
    return n;
}

bool Runner::start(proc::Pid pid)
{
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
        s.quit = false;
        s.used = true;
        const proc::Process *row = procs_.find(pid);
        s.task = tasks_->start(row->name, &Runner::task, &s, kStackBytes, ports::Priority::High);
        if (s.task == nullptr) {
            s.used = false;
            return false;
        }
        procs_.add_memory(pid, static_cast<int32_t>(kStackBytes));
        return true;
    }
    return false;
}

// The program's own task: sleeps until it is handed a call, makes it, says
// so, and sleeps again, until its process has ended.
void Runner::task(void *context)
{
    Slot &s = *static_cast<Slot *>(context);
    for (;;) {
        s.kit.go->wait(0xFFFFFFFF);
        if (s.fn != nullptr) {
            const proc::Scope as(&s.runner->procs_, s.pid);
            s.fn(s.context);
            s.fn = nullptr;
        }
        const bool last = s.quit;
        s.kit.done->set();
        if (last) {
            return;
        }
    }
}

void Runner::call(proc::Pid pid, proc::CallFn fn, void *context)
{
    Slot *s = slot_of(pid);
    if (s == nullptr || s->depth > 0) {
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
        return;
    }
    s->fn = fn;
    s->context = context;
    s->depth = 1;
    s->kit.go->set();
    s->kit.done->wait(0xFFFFFFFF);
    s->depth = 0;
    if (s->quit) {
        // It ended during the call and its task has returned.
        tasks_->end(s->task);
        s->used = false;
    }
}

// The task of a process that is not in the middle of anything: told to
// leave, waited for, given back.
void Runner::give_back(Slot &s)
{
    s.quit = true;
    s.fn = nullptr;
    s.kit.go->set();
    s.kit.done->wait(0xFFFFFFFF);
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
    if (s->depth > 0) {
        s->quit = true; // its code is still running: call() gives the task back
    } else {
        r.give_back(*s);
    }
}

void Runner::call_for_table(proc::Pid pid, proc::CallFn fn, void *context, void *self)
{
    static_cast<Runner *>(self)->call(pid, fn, context);
}

Runner &runner()
{
    static Runner r;
    return r;
}

} // namespace prog
