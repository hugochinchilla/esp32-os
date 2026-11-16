#include "core/proc/table.hpp"

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

Pid Table::init()
{
    if (!alive(kInit)) {
        // Only ever the first row made: next_ is kInit in an empty table.
        if (count_ != 0 || next_ != kInit) {
            return kNone;
        }
        rows_[0].pid = kInit;
        rows_[0].parent = kNone;
        rows_[0].state = State::Running;
        rows_[0].flags = kSystem | kProtected;
        strcpy(rows_[0].name, "init");
        order_[0] = 0;
        count_ = 1;
        next_ = kInit + 1;
        changes_++;
    }
    return kInit;
}

Pid Table::spawn(const char *name, Pid parent, uint8_t flags)
{
    if (parent == kNone) {
        parent = init();
    }
    const Process *above = find(parent);
    if (above == nullptr || count_ == kMaxProcesses) {
        return kNone;
    }
    // The next number not in use. After kMaxPid the count starts again at 2:
    // 1 is init's for good.
    Pid pid = next_;
    for (int tries = 0; tries <= kMaxProcesses; tries++) {
        if (pid > kMaxPid || pid <= kInit) {
            pid = kInit + 1;
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
    if (p == nullptr || pid == kInit) {
        return false;
    }
    const int index = static_cast<int>(p - rows_);
    for (Process &other : rows_) {
        if (other.state != State::Free && other.parent == pid) {
            other.parent = kInit;
        }
    }
    int kept = 0;
    for (int i = 0; i < count_; i++) {
        if (order_[i] != index) {
            order_[kept++] = order_[i];
        }
    }
    count_ = kept;
    *p = Process();
    changes_++;
    return true;
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

void Table::reset()
{
    for (Process &p : rows_) {
        p = Process();
    }
    count_ = 0;
    next_ = kInit;
    current_ = kNone;
    changes_++;
}

Table &table()
{
    static Table the_table;
    return the_table;
}

} // namespace proc
