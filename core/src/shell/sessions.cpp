// Terminal sessions, as the desktop's side of the system hands them out
// (os::open_session; docs/journal/2026-11-17-terminal-split.md). A session
// is a process `sh` with a TTY and a command shell (core/sh/
// terminal_session.hpp); whoever asks for one is its device, and for the
// Terminal window that is all the window is.
//
// This file is the system's, not a program's: it makes the process, picks
// the lock and events a session's task needs from the few the machine handed
// in, says what the shell may reach, and takes the process away when the
// session is over.
#include <new>

#include "core/job/job.hpp"
#include "core/net/wifi.hpp"
#include "core/sh/terminal_session.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

SessionKit *g_kits = nullptr;
int g_kit_count = 0;

// Large memory for a command of the session (ted's text): program memory,
// counted as the session's own. Called on the UI task (Session::on_ui),
// where no process is current, so it says whose it is.
class SessionMemory : public ports::Memory {
public:
    void *allocate(size_t bytes) override
    {
        const proc::Scope as(&proc::table(), owner);
        return program_memory(bytes);
    }
    void release(void *block) override { free_program_memory(block); }
    proc::Pid owner = proc::kNone;
};

sh::Env environment(SessionMemory &memory)
{
    sh::Env env;
    env.storage = &storage();
    env.procs = &proc::table();
    env.system = &system_port();
    env.desktop = &desktop_for_sessions(); // open, run, edit
    env.zone = network() != nullptr ? &network()->zone() : nullptr;
    env.memory = &memory;
    return env;
}

// One session and what the system keeps beside it. Program memory of the
// `sh` process, so `ps` shows what a session costs.
struct Hosted {
    Hosted(proc::Pid process, ports::ByteSink &device, SessionKit *kit)
        : pid(process), session(device, environment(owned(process)), kit)
    {
    }
    SessionMemory &owned(proc::Pid process)
    {
        memory.owner = process;
        return memory;
    }

    SessionMemory memory;
    proc::Pid pid;
    sh::TerminalSession session;
};

void take_away(Hosted *h)
{
    SessionKit *const kit = h->session.kit();
    const proc::Pid pid = h->pid;
    h->~Hosted();
    free_program_memory(h);
    if (kit != nullptr) {
        kit->in_use = false;
    }
    manager().end(pid);
}

// The session is over and its task has returned.
void gone(void *context) { take_away(static_cast<Hosted *>(context)); }

// `kill` or End Task on the `sh` process: the same as its terminal going.
void stop(proc::Pid, proc::Signal, void *context) { static_cast<Hosted *>(context)->session.hang_up(); }

} // namespace

void set_session_kits(SessionKit *kits, int count)
{
    g_kits = kits;
    g_kit_count = count;
}

sh::TerminalSession *open_session(ports::ByteSink &device, const char *&why)
{
    // A task of its own where the machine can give one.
    SessionKit *kit = nullptr;
    if (g_kit_count > 0 && job::jobs().threaded()) {
        for (int i = 0; i < g_kit_count && kit == nullptr; i++) {
            kit = !g_kits[i].in_use ? &g_kits[i] : nullptr;
        }
        if (kit == nullptr) {
            // Every kit is held, some by sessions whose terminal has closed
            // and that are waiting for a command to stop.
            why = "A Terminal is still closing.\nTry again in a moment.";
            return nullptr;
        }
    }
    proc::Table &t = proc::table();
    const proc::Pid pid = t.spawn("sh", t.current());
    if (pid == proc::kNone) {
        why = "The session could not start:\ntoo many programs are running.";
        return nullptr;
    }
    const proc::Scope as(&t, pid);
    void *place = program_memory(sizeof(Hosted));
    if (place == nullptr) {
        t.exit(pid);
        why = "There is not enough memory to open\nTerminal.";
        return nullptr;
    }
    Hosted *h = new (place) Hosted(pid, device, kit);
    if (kit != nullptr) {
        kit->in_use = true;
    }
    t.set_stop(pid, &stop, h);
    if (!h->session.start(&gone, h)) {
        take_away(h);
        why = "The Terminal could not start:\nthe system is too busy.";
        return nullptr;
    }
    return &h->session;
}

sh::Env session_environment(sh::UiCall *ui, proc::Pid owner)
{
    // One for each caller (telnet, the serial console): each charges its
    // own process.
    static SessionMemory memories[4];
    static int used = 0;
    SessionMemory &memory = memories[used < 3 ? used++ : 3];
    memory.owner = owner;
    sh::Env env = environment(memory);
    env.ui = ui;
    return env;
}

} // namespace shell
