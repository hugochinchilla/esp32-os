// The shell's commands about processes and the machine.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "commands.hpp"
#include "core/init/init.hpp"
#include "core/sys/log.hpp"
#include "core/net/wifi.hpp"
#include "core/shell/boot.hpp"

namespace sh {

const char *date_text(int64_t utc_seconds, const net::TimeZone *zone, char *out, int capacity)
{
    if (utc_seconds <= 0) {
        snprintf(out, static_cast<size_t>(capacity), "%s", "");
        return out;
    }
    const net::LocalTime t = net::local_time(utc_seconds, zone != nullptr ? *zone : net::kDefaultZone);
    snprintf(out, static_cast<size_t>(capacity), "%04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
    return out;
}

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
    job.table->bring_up_to_date(); // the time of jobs still running
    int at = snprintf(job.out, static_cast<size_t>(job.capacity), "  PID  PPID  FLAGS     TIME    MEM  NAME\n");
    for (int i = 0; i < job.table->count() && at < job.capacity - 1; i++) {
        const proc::Process &p = job.table->at(i);
        const char flags[] = {p.is(proc::kSystem) ? 'S' : '-', p.is(proc::kProtected) ? 'P' : '-',
                              p.is(proc::kRespawn) ? 'R' : '-', p.is(proc::kWindowed) ? 'W' : '-', 0};
        char time[16], held[16];
        at += snprintf(job.out + at, static_cast<size_t>(job.capacity - at), "%5d %5d  %s  %7s %6s  %s%s\n", p.pid,
                       p.parent, flags, proc::time_text(p.cpu_us, time, sizeof time),
                       proc::memory_text(p.memory, held, sizeof held), p.name,
                       p.state == proc::State::Exiting ? " (ending)" : "");
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

int cmd_date(Session &s, int, char **argv)
{
    int64_t utc = 0;
    if (s.env().system == nullptr || !s.env().system->utc_time(utc)) {
        return s.fail(argv[0], nullptr, "the machine does not know the time yet (it learns it from the network)");
    }
    static const char *const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const net::LocalTime t = net::local_time(utc, s.env().zone != nullptr ? *s.env().zone : net::kDefaultZone);
    s.printf("%s %d %s %d %02d:%02d:%02d\n", kDays[t.weekday % 7], t.day, kMonths[(t.month + 11) % 12], t.year, t.hour,
             t.minute, t.second);
    return 0;
}

// The system log, oldest line first. Colour codes a logger may have put in
// are left out: they are for a terminal that knows them.
int cmd_dmesg(Session &s, int, char **argv)
{
    sys::Log &log = sys::log();
    if (!log.kept()) {
        return s.fail(argv[0], nullptr, "this machine keeps no log");
    }
    uint64_t at = log.oldest();
    char piece[128];
    char clean[sizeof(piece) + 1];
    int escape = 0; // 1 after ESC, 2 inside ESC [ ... until a letter
    char last = '\n';
    size_t got;
    while (!s.stopped() && (got = log.read(at, piece, sizeof(piece))) > 0) {
        size_t n = 0;
        for (size_t i = 0; i < got; i++) {
            const char c = piece[i];
            if (escape == 1) {
                escape = c == '[' ? 2 : 0;
            } else if (escape == 2) {
                escape = (c >= '@' && c <= '~') ? 0 : 2;
            } else if (c == 27) {
                escape = 1;
            } else if (c != '\r' && c != 0) {
                clean[n++] = c;
                last = c;
            }
        }
        clean[n] = 0;
        s.write(clean, n);
    }
    if (last != '\n') {
        s.write("\n", 1);
    }
    return 0;
}

int cmd_ver(Session &s, int, char **)
{
    s.printf("Chinchilla %s\n", shell::kVersion);
    return 0;
}

/* ---- top -------------------------------------------------------------------------- */

namespace {

struct Reading {
    proc::Pid pid;
    uint64_t cpu_us;
    uint32_t memory;
    uint32_t share; // tenths of a percent of one processor, over the last interval
    char name[proc::Process::kNameSize];
};

struct Readings {
    proc::Table *table;
    Reading rows[proc::Table::kMaxProcesses];
    int count;
};

// The UI task: the figures of every process, copied out.
void read_processes(void *context)
{
    Readings &r = *static_cast<Readings *>(context);
    r.table->bring_up_to_date(); // the jobs still running
    r.count = 0;
    for (int i = 0; i < r.table->count(); i++) {
        const proc::Process &p = r.table->at(i);
        Reading &to = r.rows[r.count++];
        to.pid = p.pid;
        to.cpu_us = p.cpu_us;
        to.memory = p.memory;
        to.share = 0;
        snprintf(to.name, sizeof to.name, "%s", p.name);
    }
}

} // namespace

int cmd_top(Session &s, int argc, char **argv)
{
    if (s.env().procs == nullptr || s.env().system == nullptr) {
        return s.fail(argv[0], nullptr, "there is no process table here");
    }
    // "-n 3": that many screens and then back to the prompt.
    long screens = 0;
    if (argc > 1) {
        char *end = nullptr;
        const char *digits = strcmp(argv[1], "-n") == 0 && argc > 2 ? argv[2] : (argv[1][0] == '-' ? argv[1] + 1 : "");
        screens = strtol(digits, &end, 10);
        if (*digits == 0 || *end != 0 || screens < 1) {
            return s.fail(argv[0], argv[1], "it takes -n and a number of screens");
        }
    }
    tty::Tty &terminal = s.terminal();
    // It comes back every two seconds only where there is someone to press
    // q and a terminal that can be waited on; otherwise it shows the table
    // once, with the totals and no shares.
    const bool live = terminal.can_wait() && !s.output_redirected();
    static constexpr uint32_t kEveryMs = 2000;

    // Two readings a moment apart give the first shares.
    Readings *now = static_cast<Readings *>(static_cast<void *>(s.buffer()));
    static_assert(sizeof(Readings) <= Session::kBufferSize, "the readings live in the session's working room");
    static_assert(alignof(Readings) <= 8, "and it is aligned for them");
    Reading before[proc::Table::kMaxProcesses];
    int before_count = 0;
    uint32_t before_ms = 0;
    for (long shown = 0; !s.stopped();) {
        now->table = s.env().procs;
        s.on_ui(&read_processes, now);
        ports::SystemStats stats = {};
        s.env().system->stats(stats);
        const uint32_t span_ms = stats.uptime_ms - before_ms;
        for (int i = 0; i < now->count; i++) {
            for (int j = 0; j < before_count && span_ms > 0; j++) {
                if (before[j].pid == now->rows[i].pid && now->rows[i].cpu_us >= before[j].cpu_us) {
                    now->rows[i].share = static_cast<uint32_t>((now->rows[i].cpu_us - before[j].cpu_us) / span_ms);
                }
            }
        }
        const bool first = before_count == 0;
        before_count = now->count;
        memcpy(before, now->rows, sizeof(Reading) * static_cast<size_t>(now->count));
        before_ms = stats.uptime_ms;
        if (first && live) {
            // Nothing to show yet: wait a moment for a second reading.
            char key = 0;
            terminal.set_raw(true);
            const int got = terminal.read(&key, 1, 400);
            terminal.set_raw(false);
            if (got == 1 && (key == 'q' || key == 'Q')) {
                break;
            }
            if (got == tty::kInterrupted || got == tty::kEnd) {
                break;
            }
            continue;
        }

        // The busiest first; ties in the order of the table.
        for (int i = 1; i < now->count; i++) {
            const Reading moved = now->rows[i];
            int j = i;
            for (; j > 0 && now->rows[j - 1].share < moved.share; j--) {
                now->rows[j] = now->rows[j - 1];
            }
            now->rows[j] = moved;
        }
        if (live) {
            s.clear_screen();
        }
        const uint32_t up = stats.uptime_ms / 1000;
        s.printf("%d processes, on for %u:%02u:%02u%s\n\n", now->count, static_cast<unsigned>(up / 3600),
                 static_cast<unsigned>(up / 60 % 60), static_cast<unsigned>(up % 60), live ? "   (q to leave)" : "");
        s.print(live ? "  PID   CPU     TIME    MEM  NAME\n" : "  PID     TIME    MEM  NAME\n");
        const int room = live && terminal.rows() > 5 ? terminal.rows() - 4 : now->count;
        for (int i = 0; i < now->count && i < room; i++) {
            const Reading &r = now->rows[i];
            char time[16], held[16];
            if (live) {
                s.printf("%5d %3u.%u%% %7s %6s  %s\n", r.pid, static_cast<unsigned>(r.share / 10),
                         static_cast<unsigned>(r.share % 10), proc::time_text(r.cpu_us, time, sizeof time),
                         proc::memory_text(r.memory, held, sizeof held), r.name);
            } else {
                s.printf("%5d  %7s %6s  %s\n", r.pid, proc::time_text(r.cpu_us, time, sizeof time),
                         proc::memory_text(r.memory, held, sizeof held), r.name);
            }
        }
        shown++;
        if (!live || (screens > 0 && shown >= screens)) {
            break;
        }
        char key = 0;
        terminal.set_raw(true);
        const int got = terminal.read(&key, 1, kEveryMs);
        terminal.set_raw(false);
        if ((got == 1 && (key == 'q' || key == 'Q')) || got == tty::kInterrupted || got == tty::kEnd) {
            break;
        }
    }
    return 0;
}

/* ---- The desktop: open, run, edit ---------------------------------------------------- */

namespace {

// One thing asked of the desktop, done by the UI task.
struct Ask {
    Desktop *desktop;
    int what; // 0 open, 1 run, 2 edit
    const char *text; // nullptr for edit without a file
    bool done;
    char why[160];
};

void ask_desktop(void *context)
{
    Ask &a = *static_cast<Ask *>(context);
    a.why[0] = 0;
    switch (a.what) {
    case 0: a.done = a.desktop->open(a.text, a.why, sizeof a.why); break;
    case 1: a.done = a.desktop->run(a.text, a.why, sizeof a.why); break;
    default: a.done = a.desktop->edit(a.text, a.why, sizeof a.why); break;
    }
}

bool no_desktop(Session &s, const char *command)
{
    if (s.env().desktop != nullptr) {
        return false;
    }
    s.fail(command, nullptr, "there is no desktop here to open it on");
    return true;
}

int asked(Session &s, const char *command, int what, const char *text, const char *shown)
{
    Ask a = {s.env().desktop, what, text, false, ""};
    s.on_ui(&ask_desktop, &a);
    return a.done ? 0 : s.fail(command, shown, a.why[0] != 0 ? a.why : "it could not be opened");
}

struct Programs {
    Desktop *desktop;
    Session *session;
};

void list_programs(void *context)
{
    // The names are fixed text, so the UI task only has to read them out.
    Programs &p = *static_cast<Programs *>(context);
    for (int i = 0; i < p.desktop->program_count(); i++) {
        p.session->printf("  %s\n", p.desktop->program_name(i));
    }
}

} // namespace

int cmd_open(Session &s, int argc, char **argv)
{
    if (no_desktop(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs the name of a file or a folder");
    }
    int result = 0;
    for (int i = 1; i < argc && !s.stopped(); i++) {
        char full[fs::kMaxPath];
        if (!s.resolve(argv[i], full)) {
            result |= s.fail(argv[0], argv[i], "the name is too long");
            continue;
        }
        result |= asked(s, argv[0], 0, full, argv[i]);
    }
    return result;
}

int cmd_run(Session &s, int argc, char **argv)
{
    if (no_desktop(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        s.print("The programs:\n");
        Programs p = {s.env().desktop, &s};
        list_programs(&p); // only reads fixed names: no need to trouble the UI task
        return 0;
    }
    // The name may have spaces: "run file manager" is one program.
    char name[64];
    int used = 0;
    for (int i = 1; i < argc; i++) {
        used += snprintf(name + used, sizeof name - static_cast<size_t>(used), "%s%s", i > 1 ? " " : "", argv[i]);
        if (used >= static_cast<int>(sizeof name) - 1) {
            break;
        }
    }
    return asked(s, argv[0], 1, name, name);
}

/* ---- service ---------------------------------------------------------------------- */

namespace {

// init is the UI task's. What is asked of it is done there, and the answer
// comes back as text or as a number.
struct ServiceJob {
    const char *name; // nullptr: the list
    int what;         // 0 status, 1 start, 2 stop
    char *out;
    int capacity;
    int result;       // 0 done; otherwise why not, below
};
enum { kNoInit = 1, kNoSuchService, kRunsAlready, kDidNotStart, kNotRunning, kCannotStop };

const char *state_text(const init::Init::Info &u)
{
    if (u.started) {
        return u.held ? "stopping" : u.respawn ? "running, comes back if it ends" : "running";
    }
    return u.held ? "stopped by hand, until the next start" : "stopped";
}

void ask_init(void *context)
{
    ServiceJob &job = *static_cast<ServiceJob *>(context);
    init::Init *first = init::the();
    job.result = 0;
    if (first == nullptr) {
        job.result = kNoInit;
        return;
    }
    if (job.name == nullptr) {
        int at = snprintf(job.out, static_cast<size_t>(job.capacity), "  %-10s STATE\n", "NAME");
        for (int i = 0; i < first->units() && at < job.capacity - 1; i++) {
            const init::Init::Info u = first->info(i);
            at += snprintf(job.out + at, static_cast<size_t>(job.capacity - at), "  %-10s %s\n", u.name, state_text(u));
        }
        return;
    }
    // "telnet" and "vnc" are telnetd and vncd.
    char full[32];
    snprintf(full, sizeof full, "%s", job.name);
    if (!first->has(full)) {
        snprintf(full, sizeof full, "%.30sd", job.name);
    }
    const init::Init::Info u = first->info(full);
    if (u.name == nullptr) {
        job.result = kNoSuchService;
        return;
    }
    switch (job.what) {
    case 1:
        job.result = u.started ? kRunsAlready : first->start_again(u.name) ? 0 : kDidNotStart;
        break;
    case 2:
        switch (first->stop(u.name)) {
        case init::Stopped::Ok: break;
        case init::Stopped::NotStarted: job.result = kNotRunning; break;
        case init::Stopped::CannotBe: job.result = kCannotStop; break;
        default: job.result = kNoSuchService; break;
        }
        break;
    default:
        snprintf(job.out, static_cast<size_t>(job.capacity), "%s: %s\n", u.name, state_text(u));
        break;
    }
}

} // namespace

int cmd_service(Session &s, int argc, char **argv)
{
    ServiceJob job = {nullptr, 0, s.buffer(), Session::kBufferSize, 0};
    s.buffer()[0] = 0;
    if (argc >= 2) {
        job.name = argv[1];
        const char *verb = argc >= 3 ? argv[2] : "status";
        if (strcmp(verb, "start") == 0) {
            job.what = 1;
        } else if (strcmp(verb, "stop") == 0) {
            job.what = 2;
        } else if (strcmp(verb, "status") != 0) {
            return s.fail(argv[0], verb, "it is start, stop or status");
        }
    }
    s.on_ui(&ask_init, &job);
    switch (job.result) {
    case 0: break;
    case kNoInit: return s.fail(argv[0], nullptr, "there is no init here to ask");
    case kNoSuchService: return s.fail(argv[0], argv[1], "there is no such service (service lists them)");
    case kRunsAlready: return s.fail(argv[0], argv[1], "it is running already");
    case kDidNotStart: return s.fail(argv[0], argv[1], "it did not start (dmesg says why)");
    case kNotRunning: return s.fail(argv[0], argv[1], "it is not running");
    default: return s.fail(argv[0], argv[1], "it cannot be stopped this way");
    }
    s.print(s.buffer());
    if (job.what == 2) {
        s.print("Stopped until the next start of the machine, or service NAME start.\n");
    }
    return 0;
}

int cmd_edit(Session &s, int argc, char **argv)
{
    // On a console with no desktop to open the Editor on, the console's own
    // editor does the job.
    if (s.env().desktop == nullptr && s.terminal().can_wait() && argc <= 2) {
        return cmd_ted(s, argc, argv);
    }
    if (no_desktop(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        return asked(s, argv[0], 2, nullptr, nullptr);
    }
    int result = 0;
    for (int i = 1; i < argc && !s.stopped(); i++) {
        char full[fs::kMaxPath];
        if (!s.resolve(argv[i], full)) {
            result |= s.fail(argv[0], argv[i], "the name is too long");
            continue;
        }
        result |= asked(s, argv[0], 2, full, argv[i]);
    }
    return result;
}

} // namespace sh
