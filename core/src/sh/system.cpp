// The shell's commands about processes and the machine.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "commands.hpp"
#include "core/shell/boot.hpp"

namespace sh {

namespace {

/* ---- ps --------------------------------------------------------------------------- */

// The table is the UI task's. The list is written there, as text, into the
// session's buffer, and shown from the session's own task afterwards.
struct ListJob {
    proc::Table *table;
    char *out;
    int capacity;
};

void list_processes(void *context)
{
    const ListJob &job = *static_cast<const ListJob *>(context);
    int at = snprintf(job.out, static_cast<size_t>(job.capacity), "  PID  PPID  FLAGS  NAME\n");
    for (int i = 0; i < job.table->count() && at < job.capacity - 1; i++) {
        const proc::Process &p = job.table->at(i);
        const char flags[] = {p.is(proc::kSystem) ? 'S' : '-', p.is(proc::kProtected) ? 'P' : '-',
                              p.is(proc::kRespawn) ? 'R' : '-', p.is(proc::kWindowed) ? 'W' : '-', 0};
        at += snprintf(job.out + at, static_cast<size_t>(job.capacity - at), "%5d %5d  %s   %s%s\n", p.pid, p.parent,
                       flags, p.name, p.state == proc::State::Exiting ? " (ending)" : "");
    }
}

/* ---- kill ------------------------------------------------------------------------- */

struct SignalJob {
    proc::Table *table;
    proc::Pid pid;
    proc::Signal signal;
    proc::Sent sent;
};

void send_signal(void *context)
{
    SignalJob &job = *static_cast<SignalJob *>(context);
    job.sent = job.table->signal(job.pid, job.signal);
}

bool same_word(const char *a, const char *b)
{
    for (; *a != 0 && *b != 0; a++, b++) {
        const char x = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 'a' + 'A') : *a;
        if (x != *b) {
            return false;
        }
    }
    return *a == *b;
}

} // namespace

int cmd_ps(Session &s, int, char **argv)
{
    if (s.env().procs == nullptr) {
        return s.fail(argv[0], nullptr, "there is no process table here");
    }
    ListJob job = {s.env().procs, s.buffer(), Session::kBufferSize};
    s.on_ui(&list_processes, &job);
    s.print(s.buffer());
    s.print("S system  P protected  R restarts  W has windows\n");
    return 0;
}

int cmd_kill(Session &s, int argc, char **argv)
{
    if (s.env().procs == nullptr) {
        return s.fail(argv[0], nullptr, "there is no process table here");
    }
    proc::Signal signal = proc::Signal::Term;
    int first = 1;
    if (argc > 1 && argv[1][0] == '-') {
        const char *name = argv[1] + 1;
        if (same_word(name, "KILL") || same_word(name, "9") || same_word(name, "F")) {
            signal = proc::Signal::Kill;
        } else if (!same_word(name, "TERM") && !same_word(name, "15")) {
            return s.fail(argv[0], argv[1], "the signals are -TERM (15) and -KILL (9)");
        }
        first = 2;
    }
    if (first >= argc) {
        return s.fail(argv[0], nullptr, "it needs the number of a process (ps lists them)");
    }
    int result = 0;
    for (int i = first; i < argc; i++) {
        char *end = nullptr;
        const long pid = strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != 0 || pid <= 0 || pid > proc::kMaxPid) {
            result = s.fail(argv[0], argv[i], "that is not the number of a process");
            continue;
        }
        SignalJob job = {s.env().procs, static_cast<proc::Pid>(pid), signal, proc::Sent::Ok};
        s.on_ui(&send_signal, &job);
        switch (job.sent) {
        case proc::Sent::Ok: break;
        case proc::Sent::NoSuchProcess: result = s.fail(argv[0], argv[i], "there is no such process"); break;
        case proc::Sent::NotPermitted: result = s.fail(argv[0], argv[i], "it is protected"); break;
        default: result = s.fail(argv[0], argv[i], "it cannot be ended yet"); break;
        }
    }
    return result;
}

int cmd_free(Session &s, int, char **argv)
{
    if (s.env().system == nullptr) {
        return s.fail(argv[0], nullptr, "the machine does not say");
    }
    ports::SystemStats stats = {};
    s.env().system->stats(stats);
    char number[32];
    s.printf("Internal RAM  %12s bytes free\n", grouped(stats.free_internal_ram, number, sizeof number));
    s.printf("External RAM  %12s bytes free\n", grouped(stats.free_external_ram, number, sizeof number));
    return 0;
}

int cmd_uptime(Session &s, int, char **argv)
{
    if (s.env().system == nullptr) {
        return s.fail(argv[0], nullptr, "the machine does not say");
    }
    ports::SystemStats stats = {};
    s.env().system->stats(stats);
    const uint32_t seconds = stats.uptime_ms / 1000;
    if (seconds >= 86400) {
        s.printf("On for %u day%s, %u:%02u:%02u\n", static_cast<unsigned>(seconds / 86400), seconds / 86400 == 1 ? "" : "s",
                 static_cast<unsigned>(seconds % 86400 / 3600), static_cast<unsigned>(seconds % 3600 / 60),
                 static_cast<unsigned>(seconds % 60));
    } else {
        s.printf("On for %u:%02u:%02u\n", static_cast<unsigned>(seconds / 3600), static_cast<unsigned>(seconds % 3600 / 60),
                 static_cast<unsigned>(seconds % 60));
    }
    return 0;
}

int cmd_ver(Session &s, int, char **)
{
    s.printf("Chinchilla %s\n", shell::kVersion);
    return 0;
}

} // namespace sh
