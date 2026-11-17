#include "core/proc/table.hpp"

#include <cstdio>
#include <cstring>

namespace proc {

Process *Table::row(Pid pid)
{
    if (pid == kNone) {
        return nullptr;
    }
    for (Process &p : rows_) {
        if (p.state != State::Free && p.pid == pid) {
            return &p;
        }
    }
    return nullptr;
}

const Process *Table::find(Pid pid) const { return const_cast<Table *>(this)->row(pid); }

bool Table::pid_in_use(Pid pid) const { return find(pid) != nullptr; }

Pid Table::find_by_name(const char *name) const
{
    for (int i = 0; i < count_; i++) {
        if (strcmp(at(i).name, name) == 0) {
            return at(i).pid;
        }
    }
    return kNone;
}

Pid Table::spawn(const char *name, Pid parent, uint8_t flags)
{
    if (count_ == 0 && next_ == kRoot) {
        // The first process made is the root of the tree: PID 1, no parent.
        if (parent != kNone) {
            return kNone;
        }
        Process &p = rows_[0];
        p.pid = kRoot;
        p.parent = kNone;
        p.state = State::Running;
        p.flags = flags;
        strncpy(p.name, name, Process::kNameSize - 1);
        p.name[Process::kNameSize - 1] = 0;
        order_[0] = 0;
        count_ = 1;
        next_ = kRoot + 1;
        changes_++;
        return kRoot;
    }
    if (parent == kNone) {
        parent = kRoot;
    }
    const Process *above = find(parent);
    if (above == nullptr || count_ == kMaxProcesses) {
        return kNone;
    }
    // The next number not in use. After kMaxPid the count starts again at 2:
    // 1 is the root's for good.
    Pid pid = next_;
    for (int tries = 0; tries <= kMaxProcesses; tries++) {
        if (pid > kMaxPid || pid <= kRoot) {
            pid = kRoot + 1;
        }
        if (!pid_in_use(pid)) {
            break;
        }
        pid++;
    }
    next_ = pid + 1;
    for (int i = 0; i < kMaxProcesses; i++) {
        Process &p = rows_[i];
        if (p.state != State::Free) {
            continue;
        }
        p.pid = pid;
        p.parent = parent;
        p.state = State::Running;
        p.flags = flags;
        p.born = micros_ != nullptr ? micros_(micros_context_) : 0;
        strncpy(p.name, name, Process::kNameSize - 1);
        p.name[Process::kNameSize - 1] = 0;
        order_[count_++] = static_cast<uint8_t>(i);
        changes_++;
        return pid;
    }
    return kNone;
}

bool Table::exit(Pid pid)
{
    Process *p = row(pid);
    if (p == nullptr || pid == kRoot) {
        return false;
    }
    const int index = static_cast<int>(p - rows_);
    for (Process &other : rows_) {
        if (other.state != State::Free && other.parent == pid) {
            other.parent = kRoot;
        }
    }
    int kept = 0;
    for (int i = 0; i < count_; i++) {
        if (order_[i] != index) {
            order_[kept++] = order_[i];
        }
    }
    count_ = kept;
    if (p->pending != 0) {
        waiting_--;
    }
    if (p->is(kRespawn) && p->restart != nullptr && may_restart(*p) && due_count_ < kMaxDue) {
        due_[due_count_++] = {p->restart, p->restart_context};
    }
    *p = Process();
    changes_++;
    return true;
}

// The brake on restarts: false when this program has just ended quickly
// once too often.
bool Table::may_restart(const Process &p)
{
    if (micros_ == nullptr) {
        return true;
    }
    const uint32_t lived = micros_(micros_context_) - p.born; // the clock wraps; the difference does not
    Quick *mine = nullptr;
    Quick *empty = nullptr;
    for (Quick &q : quick_) {
        if (q.start == p.restart && q.context == p.restart_context) {
            mine = &q;
        } else if (q.start == nullptr && empty == nullptr) {
            empty = &q;
        }
    }
    if (lived >= kQuickUs) {
        if (mine != nullptr) {
            *mine = Quick();
        }
        return true;
    }
    if (mine == nullptr) {
        if (empty == nullptr) {
            return true; // nowhere to count it
        }
        mine = empty;
        mine->start = p.restart;
        mine->context = p.restart_context;
    }
    if (++mine->count < kQuickLimit) {
        return true;
    }
    *mine = Quick();
    given_up_++;
    if (gave_up_ != nullptr) {
        gave_up_(p.name, gave_up_context_);
    }
    return false;
}

Sent Table::signal(Pid pid, Signal signal)
{
    Process *p = row(pid);
    if (p == nullptr) {
        return Sent::NoSuchProcess;
    }
    if (p->is(kProtected)) {
        return Sent::NotPermitted;
    }
    if (!p->is(kWindowed)) {
        // Only window programs can be ended so far. A process without
        // windows will be a real task with a way to stop it; until that is
        // built, saying "ended" would be a lie.
        return Sent::NotSupported;
    }
    if (p->pending == 0) {
        waiting_++;
    }
    p->pending |= static_cast<uint8_t>(signal);
    if (signal == Signal::Kill && p->jobs > 0 && stop_jobs_ != nullptr) {
        stop_jobs_(pid, stop_jobs_context_);
    }
    return Sent::Ok;
}

bool Table::job_started(Pid pid)
{
    Process *p = row(pid);
    if (p == nullptr || p->jobs == 255 || (p->pending & static_cast<uint8_t>(Signal::Kill)) != 0) {
        return false;
    }
    p->jobs++;
    return true;
}

void Table::job_finished(Pid pid)
{
    Process *p = row(pid);
    if (p != nullptr && p->jobs > 0) {
        p->jobs--;
    }
}

int Table::jobs(Pid pid) const
{
    const Process *p = find(pid);
    return p != nullptr ? p->jobs : 0;
}

uint8_t Table::take_signals(Pid pid)
{
    Process *p = row(pid);
    if (p == nullptr || p->pending == 0) {
        return 0;
    }
    const uint8_t bits = p->pending;
    p->pending = 0;
    waiting_--;
    return bits;
}

void Table::set_restart(Pid pid, StartFn start, void *context)
{
    Process *p = row(pid);
    if (p != nullptr && pid != kRoot) {
        p->flags |= kRespawn;
        p->restart = start;
        p->restart_context = context;
        changes_++;
    }
}

int Table::restart_due()
{
    // The list is emptied first: a start function may end a process too.
    Restart now[kMaxDue];
    const int n = due_count_;
    for (int i = 0; i < n; i++) {
        now[i] = due_[i];
    }
    due_count_ = 0;
    for (int i = 0; i < n; i++) {
        const Scope as_root(this, kRoot);
        now[i].start(now[i].context);
    }
    return n;
}

void Table::set_state(Pid pid, State state)
{
    Process *p = row(pid);
    if (p != nullptr && state != State::Free && p->state != state) {
        p->state = state;
        changes_++;
    }
}

void Table::set_flags(Pid pid, uint8_t flags)
{
    Process *p = row(pid);
    if (p != nullptr && p->flags != flags) {
        p->flags = flags;
        changes_++;
    }
}

void Table::set_clock(uint32_t (*micros)(void *context), void *context)
{
    micros_ = micros;
    micros_context_ = context;
    since_ = micros != nullptr ? micros(context) : 0;
}

void Table::set_current(Pid pid)
{
    if (micros_ != nullptr) {
        const uint32_t now = micros_(micros_context_);
        if (current_ != kNone) {
            if (Process *p = row(current_)) {
                p->cpu_us += static_cast<uint32_t>(now - since_); // the clock wraps; the difference does not
            }
        }
        since_ = now;
    }
    current_ = pid;
}

void Table::set_figures_source(void (*source)(void *context), void *context)
{
    figures_source_ = source;
    figures_context_ = context;
}

void Table::bring_up_to_date()
{
    if (figures_source_ != nullptr) {
        figures_source_(figures_context_);
    }
}

void Table::add_cpu(Pid pid, uint64_t us)
{
    if (Process *p = row(pid)) {
        p->cpu_us += us;
    }
}

void Table::add_memory(Pid pid, int32_t bytes)
{
    Process *p = row(pid);
    if (p == nullptr) {
        return;
    }
    if (bytes >= 0) {
        p->memory += static_cast<uint32_t>(bytes);
    } else {
        const uint32_t back = static_cast<uint32_t>(-bytes);
        p->memory = p->memory > back ? p->memory - back : 0;
    }
}

void Table::reset()
{
    for (Process &p : rows_) {
        p = Process();
    }
    count_ = 0;
    next_ = kRoot;
    current_ = kNone;
    waiting_ = 0;
    due_count_ = 0;
    given_up_ = 0;
    for (Quick &q : quick_) {
        q = Quick();
    }
    changes_++;
}

const char *time_text(uint64_t us, char *out, int capacity)
{
    const uint32_t tenths = static_cast<uint32_t>(us / 100000);
    const uint32_t seconds = tenths / 10;
    if (seconds < 60) {
        snprintf(out, static_cast<size_t>(capacity), "%u.%us", static_cast<unsigned>(seconds),
                 static_cast<unsigned>(tenths % 10));
    } else if (seconds < 3600) {
        snprintf(out, static_cast<size_t>(capacity), "%u:%02u", static_cast<unsigned>(seconds / 60),
                 static_cast<unsigned>(seconds % 60));
    } else {
        snprintf(out, static_cast<size_t>(capacity), "%u:%02u:%02u", static_cast<unsigned>(seconds / 3600),
                 static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60));
    }
    return out;
}

const char *memory_text(uint32_t bytes, char *out, int capacity)
{
    if (bytes == 0) {
        snprintf(out, static_cast<size_t>(capacity), "-");
    } else if (bytes < 1000 * 1024) {
        snprintf(out, static_cast<size_t>(capacity), "%uK", static_cast<unsigned>((bytes + 1023) / 1024));
    } else {
        const unsigned tenths = static_cast<unsigned>((static_cast<uint64_t>(bytes) * 10 + (1 << 19)) >> 20);
        snprintf(out, static_cast<size_t>(capacity), "%u.%uM", tenths / 10, tenths % 10);
    }
    return out;
}

Table &table()
{
    static Table the_table;
    return the_table;
}

} // namespace proc
