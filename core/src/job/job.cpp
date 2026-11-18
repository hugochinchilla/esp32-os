#include "core/job/job.hpp"

namespace job {

Jobs::Jobs(proc::Table &procs) : procs_(procs)
{
    for (Slot &s : slots_) {
        s.jobs = this;
    }
    procs_.set_stop_jobs(&Jobs::stop_for_table, this);
    procs_.set_figures_source(&Jobs::charge_all, this);
}

Jobs::~Jobs()
{
    procs_.set_stop_jobs(nullptr, nullptr);
    procs_.set_figures_source(nullptr, nullptr);
}

bool Jobs::start(const Spec &spec)
{
    if (spec.work == nullptr) {
        return false;
    }
    Slot *slot = nullptr;
    for (Slot &s : slots_) {
        if (!s.used) {
            slot = &s;
            break;
        }
    }
    const proc::Pid owner = procs_.current();
    if (slot == nullptr || !procs_.job_started(owner)) {
        return false;
    }
    slot->used = true;
    slot->told = false;
    slot->job.stop_.store(false);
    slot->spec = spec;
    slot->owner = owner;
    slot->job.owner_ = owner;
    slot->result = 0;
    slot->charged = 0;
    slot->stack = 0;
    slot->task = nullptr;
    if (tasks_ != nullptr && msg::queue() != nullptr) {
        // The stack is memory the process holds while the job runs. Counted
        // before the task exists: it may have finished by the next line.
        slot->stack = spec.stack_bytes;
        procs_.add_memory(owner, static_cast<int32_t>(spec.stack_bytes));
        slot->task = tasks_->start(spec.name, &Jobs::task, slot, spec.stack_bytes, spec.priority);
        if (slot->task != nullptr) {
            return true;
        }
        procs_.add_memory(owner, -static_cast<int32_t>(spec.stack_bytes));
        slot->used = false;
        procs_.job_finished(owner);
        return false;
    }
    // No tasks here: the caller does the work.
    slot->result = spec.work(slot->job, spec.context);
    finish(*slot);
    return true;
}

// The job's own task.
void Jobs::task(void *context)
{
    Slot &slot = *static_cast<Slot *>(context);
    slot.result = slot.spec.work(slot.job, slot.spec.context);
    // To the OS, not to the owner: the count must come down even if the
    // owner's row has gone some other way. It waits for room as long as it
    // takes; a result that was dropped would leave the process unable to end.
    msg::post(proc::kNone, &Jobs::finished, &slot, 0, 0, msg::kForever);
}

void Jobs::finished(const msg::Message &m)
{
    Slot &slot = *static_cast<Slot *>(m.context);
    slot.jobs->finish(slot);
}

// The UI task. What the job's task has used of the processor since the last
// look goes to its owner's row.
void Jobs::charge(Slot &slot)
{
    if (slot.task == nullptr) {
        return;
    }
    const uint64_t used = tasks_->run_time_us(slot.task);
    if (used > slot.charged) {
        procs_.add_cpu(slot.owner, used - slot.charged);
        slot.charged = used;
    }
}

void Jobs::charge_all(void *self)
{
    Jobs &jobs = *static_cast<Jobs *>(self);
    for (Slot &s : jobs.slots_) {
        if (s.used) {
            jobs.charge(s);
        }
    }
}

// The UI task. The slot is free before `done` runs, so `done` may start
// another job.
void Jobs::finish(Slot &slot)
{
    // The task has returned from the work, or is about to: its last figure,
    // and then the task itself is given back.
    charge(slot);
    if (slot.task != nullptr) {
        tasks_->end(slot.task);
        slot.task = nullptr;
    }
    const Spec spec = slot.spec;
    const proc::Pid owner = slot.owner;
    const uint32_t result = slot.result;
    const bool stopped = slot.job.stopping();
    slot.used = false;
    slot.owner = proc::kNone;
    procs_.job_finished(owner);
    procs_.add_memory(owner, -static_cast<int32_t>(slot.stack));
    if (spec.done != nullptr && procs_.alive(owner)) {
        // The result waits for an owner that is not responding and is
        // handed over when it answers again: the one thing kept for a late
        // program (the owner's choice, 2026-11-17), since a save nobody
        // heard the end of would leave its window locked for good.
        proc::call_as(
            &procs_, owner, [spec, result, stopped] { spec.done(spec.context, result, stopped); }, proc::kKept);
    }
}

void Jobs::stop(proc::Pid pid)
{
    for (Slot &s : slots_) {
        if (!s.used || s.owner != pid) {
            continue;
        }
        s.job.stop_.store(true);
        if (!s.told && s.spec.stop != nullptr) {
            s.told = true;
            {
                Slot *slot = &s;
                proc::call_as(&procs_, pid, [slot] { slot->spec.stop(slot->spec.context); });
            }
        }
    }
}

void Jobs::stop_for_table(proc::Pid pid, void *self) { static_cast<Jobs *>(self)->stop(pid); }

int Jobs::running() const
{
    int n = 0;
    for (const Slot &s : slots_) {
        n += s.used;
    }
    return n;
}

int Jobs::running(proc::Pid pid) const
{
    int n = 0;
    for (const Slot &s : slots_) {
        n += s.used && s.owner == pid;
    }
    return n;
}

Jobs &jobs()
{
    static Jobs instance;
    return instance;
}

} // namespace job
