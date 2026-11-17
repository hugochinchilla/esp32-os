// The process table: PIDs, parents and flags; windows and timers that carry
// their owner; programs that get a row when launched and lose it when their
// last window closes; the services' rows; the Task Manager's list; and the
// signals TERM and KILL, protected processes and those that come back.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/net/wifi.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "../fake_wifi.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

using proc::Pid;

/* ---- The table by itself ---------------------------------------------------------- */

// What a process has used: processor time, charged by a clock to whoever is
// current, and memory, added and taken away by whoever hands it out.
void test_figures()
{
    static proc::Table t;
    static uint32_t now = 0;
    const Pid desktop = t.spawn("desktop", proc::kNone, proc::kSystem);
    const Pid editor = t.spawn("Editor", desktop, proc::kWindowed);
    CHECK(t.find(editor)->cpu_us == 0 && t.find(editor)->memory == 0);

    // Without a clock nothing is charged.
    {
        const proc::Scope as(&t, editor);
        now += 500;
    }
    CHECK(t.find(editor)->cpu_us == 0);

    // With one, the time between two changes of the current process goes to
    // the one that was current. Scopes nest: the inner one's time is not the
    // outer one's. Time with nobody current is nobody's.
    t.set_clock([](void *) { return now; }, nullptr);
    now += 1000;
    {
        const proc::Scope outer(&t, desktop);
        now += 300;
        {
            const proc::Scope inner(&t, editor);
            now += 2000;
        }
        now += 50;
    }
    now += 7000;
    CHECK(t.find(desktop)->cpu_us == 350 && t.find(editor)->cpu_us == 2000);
    // The clock is 32 bits of microseconds and wraps every 71 minutes: the
    // difference is still right.
    now = 0xFFFFFF00u;
    {
        const proc::Scope as(&t, editor);
        now += 0x200;
    }
    CHECK(t.find(editor)->cpu_us == 2000 + 0x200);
    // Time used elsewhere for a process (a job's task) is added.
    t.add_cpu(editor, 5000000000ull);
    t.add_cpu(99, 1);
    CHECK(t.find(editor)->cpu_us == 5000000000ull + 2512);

    // Memory: taken, given back, never below nothing, and a process that
    // has gone is ignored.
    t.add_memory(editor, 4096);
    t.add_memory(editor, 600 * 1024);
    CHECK(t.find(editor)->memory == 604 * 1024 && t.find(desktop)->memory == 0);
    t.add_memory(editor, -4096);
    CHECK(t.find(editor)->memory == 600 * 1024);
    t.add_memory(editor, -(700 * 1024));
    CHECK(t.find(editor)->memory == 0);
    t.add_memory(99, 10);
    // A new process in an old row starts from nothing.
    t.add_memory(editor, 10);
    CHECK(t.exit(editor));
    const Pid next = t.spawn("Paint", desktop, proc::kWindowed);
    CHECK(t.find(next)->cpu_us == 0 && t.find(next)->memory == 0);
    t.set_clock(nullptr, nullptr);

    // As text.
    char text[16];
    CHECK(strcmp(proc::time_text(0, text, sizeof text), "0.0s") == 0);
    CHECK(strcmp(proc::time_text(4249999, text, sizeof text), "4.2s") == 0);
    CHECK(strcmp(proc::time_text(59999999, text, sizeof text), "59.9s") == 0);
    CHECK(strcmp(proc::time_text(187000000, text, sizeof text), "3:07") == 0);
    CHECK(strcmp(proc::time_text(3765000000ull, text, sizeof text), "1:02:45") == 0);
    CHECK(strcmp(proc::memory_text(0, text, sizeof text), "-") == 0);
    CHECK(strcmp(proc::memory_text(1, text, sizeof text), "1K") == 0);
    CHECK(strcmp(proc::memory_text(640 * 1024, text, sizeof text), "640K") == 0);
    CHECK(strcmp(proc::memory_text(1200 * 1024, text, sizeof text), "1.2M") == 0);
    CHECK(strcmp(proc::memory_text(8u << 20, text, sizeof text), "8.0M") == 0);
}

void test_table()
{
    static proc::Table t;
    CHECK(t.count() == 0 && t.current() == proc::kNone && t.find(1) == nullptr);

    // The first process made is the root of the tree: PID 1, no parent,
    // whatever it is called (the desktop today, init one day). It cannot be
    // given a parent, and nothing is made by itself.
    CHECK(t.spawn("child first", 1) == proc::kNone && t.count() == 0);
    CHECK(t.spawn("init", proc::kNone, proc::kSystem | proc::kProtected) == proc::kRoot && t.count() == 1);
    const proc::Process *init = t.find(proc::kRoot);
    CHECK(init != nullptr && strcmp(init->name, "init") == 0 && init->parent == proc::kNone);
    CHECK(init != nullptr && init->is(proc::kSystem) && init->is(proc::kProtected) && init->state == proc::State::Running);
    // From then on "no parent" means the root's child.
    const Pid service = t.spawn_service("dbgd", proc::kProtected);
    CHECK(service == 2 && t.count() == 2);
    const proc::Process *dbgd = t.find(service);
    CHECK(dbgd->parent == 1 && dbgd->is(proc::kSystem) && dbgd->is(proc::kProtected) && !dbgd->is(proc::kWindowed));

    // PIDs count up; each row has its parent, name, state and flags.
    const Pid desktop = t.spawn_service("desktop");
    const Pid editor = t.spawn("Editor", desktop, proc::kWindowed);
    const Pid dialog_child = t.spawn("Picture viewer", editor, proc::kWindowed);
    CHECK(desktop == 3 && editor == 4 && dialog_child == 5);
    CHECK(t.find(editor)->parent == desktop && strcmp(t.find(editor)->name, "Editor") == 0);
    CHECK(t.find(editor)->flags == proc::kWindowed && t.find(editor)->state == proc::State::Running);
    CHECK(t.find_by_name("Editor") == editor && t.find_by_name("editor") == proc::kNone);
    CHECK(t.at(0).pid == 1 && t.at(4).pid == dialog_child && t.count() == 5);

    // No parent, no process. A name too long is cut, not overrun.
    CHECK(t.spawn("orphan", 99) == proc::kNone && t.count() == 5);
    const Pid longer = t.spawn("a name that is much longer than a row has room for", desktop);
    CHECK(longer == 6 && strlen(t.find(longer)->name) == proc::Process::kNameSize - 1);

    // State and flags can change; each change is counted.
    uint32_t changes = t.changes();
    t.set_state(editor, proc::State::Exiting);
    CHECK(t.find(editor)->state == proc::State::Exiting && t.changes() == changes + 1);
    t.set_state(editor, proc::State::Exiting);
    CHECK(t.changes() == changes + 1);
    t.set_state(editor, proc::State::Free); // not a way to end a process
    CHECK(t.alive(editor));
    t.set_flags(editor, proc::kWindowed | proc::kRespawn);
    CHECK(t.find(editor)->is(proc::kRespawn));

    // A process that ends leaves its children to the root. Its number is not
    // given out again: the next one is still the next.
    CHECK(t.exit(editor) && !t.alive(editor) && t.count() == 5);
    CHECK(t.find(dialog_child)->parent == proc::kRoot);
    CHECK(!t.exit(editor) && !t.exit(99) && !t.exit(proc::kRoot) && t.alive(proc::kRoot));
    CHECK(t.spawn("Culebra", desktop) == 7);
    CHECK(t.at(0).pid == 1 && t.at(1).pid == 2 && t.at(2).pid == 3 && t.at(3).pid == 5 && t.at(5).pid == 7);

    // The current process, and scopes that nest and put it back.
    CHECK(t.current() == proc::kNone);
    {
        const proc::Scope outer(&t, desktop);
        CHECK(t.current() == desktop);
        {
            const proc::Scope inner(&t, dialog_child);
            CHECK(t.current() == dialog_child);
        }
        CHECK(t.current() == desktop);
        const proc::Scope nothing(nullptr, 5); // no table: no effect, no crash
        CHECK(t.current() == desktop);
    }
    CHECK(t.current() == proc::kNone);

    // Full: 32 rows. Then no more until one ends.
    while (t.count() < proc::Table::kMaxProcesses) {
        CHECK(t.spawn("filler", desktop) != proc::kNone);
    }
    CHECK(t.spawn("one too many", desktop) == proc::kNone);
    CHECK(t.exit(t.find_by_name("filler")) && t.spawn("fits again", desktop) != proc::kNone);

    // Round the clock: after 32767 the numbers start again at 2 (1 is
    // the root's), and skip every number still in use.
    t.reset();
    CHECK(t.count() == 0);
    t.spawn("init", proc::kNone, proc::kSystem | proc::kProtected);
    const Pid old_timer = t.spawn_service("desktop"); // PID 2, alive throughout
    const Pid keeper = t.spawn("keeper", old_timer);  // PID 3, alive throughout
    Pid last = keeper;
    bool rising = true;
    for (int i = 0; i < proc::kMaxPid - 3; i++) {
        const Pid p = t.spawn("short", old_timer);
        rising = rising && p == last + 1;
        last = p;
        t.exit(p);
    }
    CHECK(rising && last == proc::kMaxPid);
    const Pid wrapped = t.spawn("after the wrap", old_timer);
    CHECK(wrapped == 4); // 1, 2 and 3 are taken
    CHECK(t.spawn("next", old_timer) == 5);
    t.reset();
}

/* ---- Signals and restarts, in the table alone ------------------------------------- */

proc::Table *restart_table = nullptr;
int restarts = 0;
Pid restarted_as = proc::kNone, restarted_by = proc::kNone;

void start_again(void *context)
{
    restarts++;
    restarted_by = restart_table->current();
    restarted_as = restart_table->spawn(static_cast<const char *>(context), restart_table->current(), proc::kWindowed);
    restart_table->set_restart(restarted_as, &start_again, context);
}

void test_signals_in_the_table()
{
    static proc::Table t;
    t.spawn("init", proc::kNone, proc::kSystem | proc::kProtected);
    const Pid service = t.spawn_service("netd");
    const Pid guarded = t.spawn_service("desktop", proc::kProtected);
    const Pid program = t.spawn("Editor", guarded, proc::kWindowed);
    const Pid shielded = t.spawn("Console", guarded, proc::kWindowed | proc::kProtected);

    // Who can be signalled: nobody that is not there, nothing protected
    // (the root here is), and no service yet. Refusing leaves nothing waiting.
    CHECK(t.signal(99, proc::Signal::Term) == proc::Sent::NoSuchProcess);
    CHECK(t.signal(proc::kRoot, proc::Signal::Kill) == proc::Sent::NotPermitted);
    CHECK(t.signal(guarded, proc::Signal::Term) == proc::Sent::NotPermitted);
    CHECK(t.signal(shielded, proc::Signal::Kill) == proc::Sent::NotPermitted);
    CHECK(t.signal(service, proc::Signal::Kill) == proc::Sent::NotSupported);
    CHECK(!t.signals_waiting() && t.take_signals(guarded) == 0 && t.take_signals(service) == 0);

    // A signal waits on the row until it is taken. Sending does nothing to
    // the process, and the same signal twice is one signal.
    CHECK(t.signal(program, proc::Signal::Term) == proc::Sent::Ok);
    CHECK(t.signals_waiting() && t.alive(program) && t.find(program)->state == proc::State::Running);
    CHECK(t.signal(program, proc::Signal::Term) == proc::Sent::Ok);
    CHECK(t.signal(program, proc::Signal::Kill) == proc::Sent::Ok);
    const uint8_t both = static_cast<uint8_t>(proc::Signal::Term) | static_cast<uint8_t>(proc::Signal::Kill);
    CHECK(t.take_signals(program) == both);
    CHECK(!t.signals_waiting() && t.take_signals(program) == 0 && t.take_signals(99) == 0);
    // A process that ends with one waiting takes it along.
    CHECK(t.signal(program, proc::Signal::Term) == proc::Sent::Ok && t.signals_waiting());
    t.exit(program);
    CHECK(!t.signals_waiting() && t.signal(program, proc::Signal::Term) == proc::Sent::NoSuchProcess);

    // Respawn: ending the process does not start it at once. The start
    // function waits for restart_due(), and runs with the root as the current
    // process, so the new one is the root's child under a new number.
    restart_table = &t;
    static const char name[] = "Taskbar";
    const Pid first = t.spawn(name, guarded, proc::kWindowed);
    t.set_restart(first, &start_again, const_cast<char *>(name));
    CHECK(t.find(first)->is(proc::kRespawn) && t.find(first)->is(proc::kWindowed));
    CHECK(t.restart_due() == 0 && restarts == 0); // nothing has ended
    t.exit(first);
    CHECK(restarts == 0 && t.find_by_name(name) == proc::kNone);
    {
        const proc::Scope as(&t, guarded);
        CHECK(t.restart_due() == 1 && t.current() == guarded);
    }
    CHECK(restarts == 1 && restarted_by == proc::kRoot && restarted_as == first + 1);
    CHECK(t.find(restarted_as)->parent == proc::kRoot && t.find(restarted_as)->is(proc::kRespawn));
    CHECK(t.restart_due() == 0 && restarts == 1); // once per ending
    // It comes back every time it ends.
    t.exit(restarted_as);
    CHECK(t.restart_due() == 1 && restarts == 2 && restarted_as == first + 2 && t.alive(restarted_as));
    // The flag without a start function does nothing; the root is never one.
    const Pid flag_only = t.spawn("flag only", guarded, proc::kRespawn);
    t.exit(flag_only);
    t.set_restart(proc::kRoot, &start_again, const_cast<char *>(name));
    CHECK(t.restart_due() == 0 && restarts == 2 && !t.find(proc::kRoot)->is(proc::kRespawn));
    // More endings than the list holds (8) between two safe points: the
    // extra ones are not started again, and nothing is overrun.
    Pid many[10];
    for (Pid &pid : many) {
        pid = t.spawn("many", guarded);
        t.set_restart(pid, &start_again, const_cast<char *>(name));
    }
    for (const Pid pid : many) {
        t.exit(pid);
    }
    CHECK(t.restart_due() == 8 && restarts == 10);
    t.reset();
    CHECK(!t.signals_waiting() && t.restart_due() == 0);
}

/* ---- Windows and timers that know their owner ------------------------------------- */

proc::Table procs;

class OwnedWindow final : public wm::Window {
public:
    OwnedWindow(const char *title, const gfx::Rect &frame) : Window(title, frame, nullptr)
    {
        items_[0] = {"&Do it", &OwnedWindow::menu_action, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
    }
    void paint(gfx::Canvas &c) override
    {
        paint_pid = procs.current();
        Window::paint(c);
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed) {
            key_pid = procs.current();
            if (k.usage == wm::key::A && next != nullptr) {
                // A dialog handing over: this window goes, another comes.
                close();
                manager_->add(*next);
            }
            if (k.usage == wm::key::Z) {
                timer = manager_->start_timer(1000, 1000, &OwnedWindow::tick, this);
            }
        }
        return true;
    }
    void mouse(const wm::MouseMsg &) override { mouse_pid = procs.current(); }
    bool close_requested() override
    {
        asked_pid = procs.current();
        asked++;
        return !stays;
    }
    void closed() override
    {
        closed_pid = procs.current();
        closed_at = ++close_counter;
        if (reopens > 0) {
            // A program that will not go: every window closed opens again.
            reopens--;
            manager_->add(*this);
        }
    }
    static inline int close_counter = 0;
    bool stays = false;
    int asked = 0, closed_at = 0, reopens = 0;

    static void tick(void *context)
    {
        auto *self = static_cast<OwnedWindow *>(context);
        self->tick_pid = procs.current();
        self->ticks++;
    }
    static void menu_action(void *context) { static_cast<OwnedWindow *>(context)->menu_pid = procs.current(); }

    wm::Wm *manager_ = nullptr;
    OwnedWindow *next = nullptr;
    Pid paint_pid = -1, key_pid = -1, mouse_pid = -1, asked_pid = -1, closed_pid = -1, tick_pid = -1, menu_pid = -1;
    int timer = 0, ticks = 0;

private:
    wm::MenuItem items_[1];
    wm::Menu menu_ = {items_, 1};
    const wm::MenuBarEntry entries_[1] = {{"&Menu", &menu_}};
    const wm::MenuBar bar_ = {entries_, 1};
};

void test_owners()
{
    static host::MemoryDisplay display(640, 480);
    static host::MemoryPointer pointer;
    static host::FixedClock clock;
    static wm::Wm m(display, pointer, clock);
    constexpr uint8_t kAltBit = 4;
    auto key = [&](uint16_t usage, uint8_t mods = 0) {
        if (mods & kAltBit) {
            m.key(wm::key::LeftAlt, true);
        }
        m.key(usage, true);
        m.key(usage, false);
        if (mods & kAltBit) {
            m.key(wm::key::LeftAlt, false);
        }
    };
    auto wait = [&](uint32_t us) {
        clock.now += us;
        m.tick();
    };

    // Without a table nothing is recorded and nothing breaks.
    static OwnedWindow plain("Plain", {10, 10, 200, 120});
    m.add(plain);
    CHECK(plain.owner() == 0 && m.processes() == nullptr);
    m.close(plain);

    procs.reset();
    procs.spawn("init", proc::kNone, proc::kSystem | proc::kProtected);
    const Pid desktop = procs.spawn_service("desktop");
    m.set_processes(&procs, desktop);
    m.start();
    // The manager's own timers (the caret, the clock) are the desktop's.
    CHECK(m.timers_of(desktop) == 2 && m.windows_of(desktop) == 0);

    // A window belongs to whoever is current when it opens.
    static OwnedWindow a("A", {20, 20, 240, 160}), b("B", {300, 40, 240, 160}), second("A2", {60, 200, 200, 120});
    a.manager_ = b.manager_ = second.manager_ = &m;
    const Pid pa = procs.spawn("A", desktop, proc::kWindowed);
    const Pid pb = procs.spawn("B", desktop, proc::kWindowed);
    {
        const proc::Scope as(&procs, pa);
        m.add(a);
    }
    {
        const proc::Scope as(&procs, pb);
        m.add(b);
    }
    CHECK(a.owner() == pa && b.owner() == pb && m.windows_of(pa) == 1 && m.windows_of(pb) == 1);
    CHECK(m.main_window_of(pa) == &a && m.main_window_of(pb) == &b && m.main_window_of(desktop) == nullptr);
    char json[512];
    json[m.describe_windows(json, sizeof(json))] = 0;
    char expect[32];
    snprintf(expect, sizeof(expect), "\"pid\":%d", pb);
    CHECK(strstr(json, expect) != nullptr);

    // Every call into a window is made as its owner, and the desktop is
    // current again afterwards (nothing, outside an event).
    m.update();
    CHECK(a.paint_pid == pa && b.paint_pid == pb);
    key(wm::key::A + 1); // B is in front
    CHECK(b.key_pid == pb && a.key_pid == -1 && procs.current() == proc::kNone);
    const gfx::Rect ca = a.client();
    m.mouse({true, static_cast<int16_t>(ca.x + 50), static_cast<int16_t>(ca.y + 50), 1, 0});
    m.mouse({true, static_cast<int16_t>(ca.x + 50), static_cast<int16_t>(ca.y + 50), 0, 0});
    CHECK(a.mouse_pid == pa && m.active() == &a);

    // A timer belongs to whoever started it, and fires as its owner.
    key(wm::key::Z);
    CHECK(a.timer != 0 && m.timer_owner(a.timer) == pa && m.timers_of(pa) == 1 && m.timers_of(pb) == 0);
    wait(1000);
    CHECK(a.ticks == 1 && a.tick_pid == pa && procs.current() == proc::kNone);

    // A menu's action runs for the window whose menu it is, also when the
    // menu was opened with the keyboard from the desktop's side.
    key(wm::key::A + ('m' - 'a'), kAltBit);
    CHECK(m.menu_open());
    key(wm::key::Enter);
    CHECK(a.menu_pid == pa);

    // A second window of the same process: closing one leaves it running.
    {
        const proc::Scope as(&procs, pa);
        m.add(second);
    }
    CHECK(m.windows_of(pa) == 2);
    m.close(second);
    wait(10);
    CHECK(procs.alive(pa) && procs.find(pa)->state == proc::State::Running && second.closed_pid == pa);

    // The last window closes (Alt+F4, asked as the owner): the process is
    // on its way out, and gone at the end of the event, with the timer it
    // left running.
    key(wm::key::F4, kAltBit);
    CHECK(a.asked_pid == pa && a.closed_pid == pa && !a.open());
    CHECK(!procs.alive(pa) && m.timers_of(pa) == 0 && m.timer_owner(a.timer) == 0);
    const int ticks = a.ticks;
    wait(5000);
    CHECK(a.ticks == ticks); // the timer really stopped
    CHECK(procs.alive(pb) && procs.alive(desktop) && m.timers_of(desktop) == 2);

    // Closed from outside an event (another program's End Task): marked
    // first, removed at the next tick.
    m.close(b);
    CHECK(procs.alive(pb) && procs.find(pb)->state == proc::State::Exiting);
    wait(10);
    CHECK(!procs.alive(pb));

    // A dialog that closes and opens the program's next window in the same
    // event keeps the process.
    const Pid pc = procs.spawn("C", desktop, proc::kWindowed);
    a.next = &b;
    {
        const proc::Scope as(&procs, pc);
        m.add(a);
    }
    key(wm::key::A);
    CHECK(!a.open() && b.open() && b.owner() == pc);
    CHECK(procs.alive(pc) && procs.find(pc)->state == proc::State::Running);
    a.next = nullptr;

    // A process that is not a window program (a service, the desktop)
    // stays without windows.
    static OwnedWindow panel("Panel", {100, 100, 200, 120});
    {
        const proc::Scope as(&procs, desktop);
        m.add(panel);
    }
    m.close(panel);
    wait(10);
    CHECK(procs.alive(desktop) && procs.find(desktop)->state == proc::State::Running);
    m.close(b);
    wait(10);
    CHECK(procs.count() == 2); // init and the desktop

    // ---- Signals.
    auto open_as = [&](Pid pid, OwnedWindow &w) {
        const proc::Scope as(&procs, pid);
        m.add(w);
    };
    auto fresh = [&](OwnedWindow &w) {
        w.asked = 0;
        w.asked_pid = w.closed_pid = -1;
        w.stays = false;
    };
    static OwnedWindow dialog("Question", {120, 120, 200, 100});
    dialog.manager_ = &m;

    // TERM is the close button: the window is asked, as its owner, and
    // closes. Nothing happens when the signal is sent; it takes effect at
    // the next safe point (here a tick). The process goes by the usual rule.
    Pid pt = procs.spawn("T", desktop, proc::kWindowed);
    const Pid pu = procs.spawn("U", desktop, proc::kWindowed);
    fresh(a);
    fresh(b);
    open_as(pt, a);
    open_as(pu, b); // in front
    CHECK(procs.signal(pt, proc::Signal::Term) == proc::Sent::Ok);
    CHECK(a.open() && a.asked == 0 && procs.alive(pt));
    wait(10);
    CHECK(a.asked == 1 && a.asked_pid == pt && !a.open() && a.closed_pid == pt && !procs.alive(pt));
    CHECK(m.active() == &b && b.asked == 0); // whoever was in front still is

    // A program may refuse: it stays, and so does who was in front.
    pt = procs.spawn("T", desktop, proc::kWindowed);
    fresh(a);
    a.stays = true;
    open_as(pt, a);
    m.activate(b);
    procs.signal(pt, proc::Signal::Term);
    wait(10);
    CHECK(a.asked == 1 && a.open() && procs.alive(pt) && procs.find(pt)->state == proc::State::Running);
    CHECK(m.active() == &b);
    // Every window of the process is asked, each once; a refusal by one
    // does not stop the others being asked.
    fresh(second);
    open_as(pt, second);
    m.activate(b);
    procs.signal(pt, proc::Signal::Term);
    wait(10);
    CHECK(a.asked == 2 && a.open() && second.asked == 1 && !second.open() && procs.alive(pt));

    // A window held by its dialog is not asked: the dialog comes forward
    // (the program is in the middle of a question), and the process stays.
    dialog.set_modal(&a);
    fresh(dialog);
    open_as(pt, dialog);
    m.activate(b);
    a.asked = 0;
    procs.signal(pt, proc::Signal::Term);
    wait(10);
    CHECK(a.asked == 0 && dialog.asked == 0 && a.open() && dialog.open() && m.active() == &dialog);

    // KILL asks nobody. The dialog goes before its window, the timer and
    // the open menu go, and the row is removed at the same safe point.
    // closed() still runs, as the owner: the window objects are used again.
    m.close(dialog);
    key(wm::key::Z); // A starts a timer
    key(wm::key::A + ('m' - 'a'), kAltBit);
    CHECK(m.timers_of(pt) == 1 && m.menu_open() && m.active() == &a);
    dialog.set_modal(&a);
    open_as(pt, dialog); // opening it closes the menu...
    m.open_menu_bar(a, 0); // ...so open it again behind the dialog
    CHECK(m.menu_open() && m.windows_of(pt) == 2);
    fresh(a);
    fresh(dialog);
    a.stays = true;
    const int kill_ticks = a.ticks;
    CHECK(procs.signal(pt, proc::Signal::Kill) == proc::Sent::Ok && a.open());
    wait(10);
    CHECK(!a.open() && !dialog.open() && a.asked == 0 && dialog.asked == 0);
    CHECK(a.closed_pid == pt && dialog.closed_pid == pt && dialog.closed_at < a.closed_at);
    CHECK(!procs.alive(pt) && m.timers_of(pt) == 0 && m.windows_of(pt) == 0 && !m.menu_open());
    wait(5000);
    CHECK(a.ticks <= kill_ticks + 1 && m.active() == &b && b.open() && procs.alive(pu));
    const int after_kill = a.ticks;
    wait(5000);
    CHECK(a.ticks == after_kill); // the timer is gone for good

    // TERM and KILL waiting together: KILL wins, and nothing is asked.
    pt = procs.spawn("T", desktop, proc::kWindowed);
    fresh(a);
    open_as(pt, a);
    procs.signal(pt, proc::Signal::Term);
    procs.signal(pt, proc::Signal::Kill);
    wait(10);
    CHECK(!a.open() && a.asked == 0 && !procs.alive(pt));

    // A program that opens a window again whenever one closes cannot hold
    // on: KILL closes those too, a bounded number of times, and the row
    // goes whatever it does.
    pt = procs.spawn("T", desktop, proc::kWindowed);
    fresh(a);
    open_as(pt, a);
    a.reopens = 5;
    procs.signal(pt, proc::Signal::Kill);
    wait(10);
    CHECK(!a.open() && a.reopens == 0 && !procs.alive(pt) && m.windows_of(pt) == 0);

    // Sent from inside an event (a program signalling itself, or the Task
    // Manager's button): it takes effect when that event has been handled.
    pt = procs.spawn("T", desktop, proc::kWindowed);
    fresh(a);
    open_as(pt, a);
    {
        const proc::Scope as(&procs, pt);
        procs.signal(pt, proc::Signal::Kill);
        CHECK(a.open());
    }
    key(wm::key::A + 2); // any event: its end is a safe point
    CHECK(!a.open() && a.key_pid == pt && !procs.alive(pt));

    // Protected: no signal reaches it, so nothing happens at the safe
    // point. Its own close button still works; protection is about signals.
    const Pid pp = procs.spawn("Console", desktop, proc::kWindowed | proc::kProtected);
    fresh(a);
    open_as(pp, a);
    CHECK(procs.signal(pp, proc::Signal::Term) == proc::Sent::NotPermitted);
    CHECK(procs.signal(pp, proc::Signal::Kill) == proc::Sent::NotPermitted);
    wait(10);
    CHECK(a.open() && a.asked == 0 && procs.alive(pp));
    key(wm::key::F4, kAltBit);
    CHECK(!a.open() && a.asked == 1 && !procs.alive(pp));
    // A service cannot be signalled yet, and the desktop is protected... if
    // it says so (this test's desktop does not, so it reads as a service).
    CHECK(procs.signal(desktop, proc::Signal::Kill) == proc::Sent::NotSupported);
    CHECK(procs.signal(proc::kRoot, proc::Signal::Kill) == proc::Sent::NotPermitted);

    // Respawn: killed, the process is back after the same safe point, as
    // the root's child with a new number, with its window open again.
    struct Again {
        wm::Wm *m;
        OwnedWindow *w;
        Pid pid;
        int starts;
        static void start(void *context)
        {
            auto *self = static_cast<Again *>(context);
            self->starts++;
            self->pid = procs.spawn("Taskbar", procs.current(), proc::kWindowed);
            procs.set_restart(self->pid, &Again::start, self);
            const proc::Scope as(&procs, self->pid);
            self->m->add(*self->w);
        }
    };
    static Again again = {&m, &a, proc::kNone, 0};
    fresh(a);
    {
        const proc::Scope as(&procs, desktop);
        Again::start(&again);
    }
    const Pid bar1 = again.pid;
    CHECK(a.open() && a.owner() == bar1 && procs.find(bar1)->parent == desktop);
    procs.signal(bar1, proc::Signal::Kill);
    wait(10);
    CHECK(!procs.alive(bar1) && again.starts == 2 && again.pid != bar1 && procs.alive(again.pid));
    CHECK(a.open() && a.owner() == again.pid && procs.find(again.pid)->parent == proc::kRoot);
    CHECK(procs.current() == proc::kNone);
    // Closed politely, by its own close button, it comes back all the same.
    const Pid bar2 = again.pid;
    m.activate(a);
    key(wm::key::F4, kAltBit);
    CHECK(!procs.alive(bar2) && again.starts == 3 && a.open() && a.owner() == again.pid);
    // Without the flag it stays away.
    procs.set_flags(again.pid, proc::kWindowed);
    procs.signal(again.pid, proc::Signal::Kill);
    wait(10);
    CHECK(!a.open() && again.starts == 3 && !procs.alive(again.pid));

    m.close(b);
    wait(10);
    CHECK(procs.count() == 2 && m.window_count() == 0 && !procs.signals_waiting());
    m.set_processes(nullptr, 0);
}

/* ---- The shell: programs and services ---------------------------------------------- */

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

constexpr uint8_t kCtrl = 2, kAlt = 4;

void key(uint16_t usage, uint8_t mods = 0)
{
    const uint16_t mod_keys[3] = {wm::key::LeftShift, wm::key::LeftCtrl, wm::key::LeftAlt};
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], true);
        }
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], false);
        }
    }
    clock_.now += 100000;
}

void letter(char c, uint8_t mods = 0) { key(static_cast<uint16_t>(wm::key::A + (c - 'a')), mods); }

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

const char *title() { return desktop.active() ? desktop.active()->title() : ""; }

wm::Window *find(const char *name)
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (strcmp(desktop.window_at(z)->title(), name) == 0) {
            return desktop.window_at(z);
        }
    }
    return nullptr;
}

void close_all()
{
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(10000);
}

const proc::Process *process(const char *name)
{
    return proc::table().find(proc::table().find_by_name(name));
}

std::string screen() { return std::string(reinterpret_cast<char *>(display.pixels()), 640 * 480); }

void test_shell()
{
    proc::Table &t = proc::table();

    // After start-up: the desktop is PID 1, the root of the tree, and
    // nothing else is listed. There is no init row until init is a real
    // program. The debug link, the Wi-Fi manager, the remote screen
    // and the web page are not tasks of their own (they run on the UI
    // task), so they have no rows. No program runs yet.
    const proc::Process *desk = process("desktop");
    CHECK(desk != nullptr && desk->pid == proc::kRoot && desk->parent == proc::kNone);
    CHECK(desk != nullptr && desk->is(proc::kSystem) && desk->is(proc::kProtected));
    CHECK(t.count() == 1 && desktop.processes() == &t);
    CHECK(process("init") == nullptr && process("dbgd") == nullptr && process("netd") == nullptr && process("vncd") == nullptr &&
          process("httpd") == nullptr);
    const Pid desk_pid = desk->pid;
    // Timers: the caret and the clock, the desktop's look at its folder, the
    // screensaver's and the Wi-Fi manager's are all the desktop's.
    CHECK(desktop.timers_of(desk_pid) >= 4 && desktop.timers_of(0) == 0);

    // Starting the shell again (the tests do) makes no second desktop.
    CHECK(t.find_by_name("desktop") == desk_pid);

    // A program launched gets the next PID, the launcher as its parent, and
    // its window. Launching it again is the same process.
    shell::launch(*shell::find_app("Welcome"));
    const proc::Process *welcome = process("Welcome");
    CHECK(welcome != nullptr && welcome->pid == 2 && welcome->is(proc::kWindowed) && !welcome->is(proc::kSystem));
    CHECK(welcome != nullptr && welcome->parent == proc::kRoot); // launched from outside any process
    CHECK(find("Welcome to Chinchilla")->owner() == 2 && desktop.windows_of(2) == 1);
    shell::launch(*shell::find_app("Welcome"));
    CHECK(t.count() == 2 && desktop.window_count() == 1);

    // From the Start menu the parent is the desktop: Ctrl+Esc, Programs,
    // the first entry (About Chinchilla).
    key(wm::key::Escape, kCtrl);
    key(wm::key::Down); // Programs
    key(wm::key::Right);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "About Chinchilla") == 0);
    const proc::Process *about = process("About Chinchilla");
    CHECK(about != nullptr && about->pid == 3 && about->parent == desk_pid && desktop.active()->owner() == 3);

    // Closing its last window ends the process; the others stay.
    key(wm::key::Enter); // OK
    CHECK(process("About Chinchilla") == nullptr && t.count() == 2 && process("Welcome") != nullptr);
    // Started again it is a new process with a new number.
    shell::launch(*shell::find_app("About Chinchilla"));
    CHECK(process("About Chinchilla") != nullptr && process("About Chinchilla")->pid == 4);
    close_all();
    CHECK(t.count() == 1);

    // A program opened by another is its child: a text file opened from a
    // File manager window goes to the Editor.
    shell::open_folder("C:\\Documents");
    const Pid files = t.find_by_name("File manager");
    CHECK(files != proc::kNone && desktop.active()->owner() == files);
    {
        const proc::Scope as(&t, files); // as when its window handles the double click
        CHECK(shell::open_file("C:\\Documents\\notes.txt"));
    }
    const proc::Process *editor = process("Editor");
    CHECK(editor != nullptr && editor->parent == files && strcmp(title(), "notes.txt - Editor") == 0);
    CHECK(editor != nullptr && editor->memory > 100 * 1024); // its text buffer, asked of the Memory port
    const Pid editor_pid = editor->pid;
    CHECK(desktop.active()->owner() == editor_pid);
    // A second folder window is the same File manager.
    shell::open_folder("C:\\");
    CHECK(desktop.windows_of(files) == 2 && t.find_by_name("File manager") == files);

    // The Editor's dialogs are the Editor's: Save As, and the "save
    // changes?" question when its close button is pressed.
    desktop.activate(*find("notes.txt - Editor"));
    letter('f', kAlt);
    letter('a');
    CHECK(strcmp(title(), "Save As") == 0 && desktop.active()->owner() == editor_pid);
    key(wm::key::Escape);
    desktop.text("x");
    key(wm::key::F4, kAlt);
    CHECK(desktop.active() != find("notes.txt* - Editor") && desktop.active()->owner() == editor_pid);
    CHECK(desktop.windows_of(editor_pid) == 2);
    // Shut Down asks every program to close; the question that stops it is
    // the Editor's too, not the desktop's.
    key(wm::key::Escape);
    while (desktop.windows_of(files) > 0) {
        desktop.close(*desktop.main_window_of(files));
    }
    wait(10000);
    CHECK(!t.alive(files));
    CHECK(t.find(editor_pid)->parent == proc::kRoot); // its parent ended: the root takes it
    {
        const proc::Scope as(&t, desk_pid);
        CHECK(!shell::power_down());
    }
    CHECK(desktop.windows_of(editor_pid) == 2 && desktop.windows_of(desk_pid) == 0);
    letter('n'); // No: do not save
    wait(10000);

    // The Task Manager. By default the programs with windows, in the order
    // they started, without itself: End Task on the first row closes
    // Culebra and the process goes.
    close_all();
    CHECK(t.count() == 1);
    shell::open_culebra();
    shell::launch(*shell::find_app("Widget gallery"));
    const Pid culebra = t.find_by_name("Culebra"), gallery = t.find_by_name("Widget gallery");
    CHECK(culebra != proc::kNone && gallery == culebra + 1);
    key(wm::key::Delete, kCtrl | kAlt);
    const Pid manager = t.find_by_name("Task Manager");
    CHECK(strcmp(title(), "Task Manager") == 0 && manager == gallery + 1 && desktop.active()->owner() == manager);
    CHECK(desktop.timers_of(manager) == 1); // its once-a-second look
    key(wm::key::Home);
    letter('e', kAlt);
    CHECK(!t.alive(culebra) && find("Culebra") == nullptr && t.alive(gallery));
    CHECK(strcmp(title(), "Task Manager") == 0);
    wait(10000); // the list is read again at the next tick
    // Switch To on what is now the first row.
    key(wm::key::Home);
    letter('s', kAlt);
    CHECK(strcmp(title(), "Widget gallery") == 0);

    // "Show all processes": every row of the table, the desktop first. On a
    // protected process (the desktop) End Task is
    // greyed out: its letter does nothing, a click does nothing, and the
    // button is drawn differently.
    key(wm::key::Delete, kCtrl | kAlt);
    const std::string programs_only = screen();
    letter('a', kAlt);
    wm::Window *const its_window = desktop.active();
    CHECK(programs_only != screen());
    letter('p', kAlt); // the list, by its heading's letter
    key(wm::key::End); // the Task Manager's own row: End Task can be used
    const gfx::Rect at = its_window->client();
    auto end_button = [&] {
        std::string pixels;
        for (int y = at.y + 136; y < at.y + 159; y++) {
            pixels.append(reinterpret_cast<char *>(display.pixels()) + y * 640 + at.x + 8, 75);
        }
        return pixels;
    };
    auto click_end = [&] {
        desktop.mouse({true, static_cast<int16_t>(at.x + 40), static_cast<int16_t>(at.y + 147), 1, 0});
        desktop.mouse({true, static_cast<int16_t>(at.x + 40), static_cast<int16_t>(at.y + 147), 0, 0});
    };
    const std::string usable = end_button();
    key(wm::key::Home); // the desktop, PID 1
    CHECK(end_button() != usable);
    letter('e', kAlt);
    click_end();
    wait(10000);
    CHECK(desktop.active() == its_window && desktop.windows_of(manager) == 1 && t.count() == 3);
    CHECK(t.alive(proc::kRoot) && process("desktop") != nullptr);
    // The next row is a program: the button can be used again.
    key(wm::key::Down); // desktop, Widget gallery
    CHECK(end_button() == usable);
    // A click on the button of a program's row works as before.
    click_end();
    wait(10000);
    CHECK(!t.alive(gallery) && find("Widget gallery") == nullptr);
    // The last row is the Task Manager itself: End Task closes it.
    letter('p', kAlt);
    key(wm::key::End);
    letter('e', kAlt);
    CHECK(find("Task Manager") == nullptr && !t.alive(manager) && desktop.timers_of(manager) == 0);
    close_all();

    // Every program in the table: open it, close what it opened, and it is
    // gone with all its windows and timers. PIDs keep counting up.
    Pid highest = manager;
    for (int i = 0; i < shell::app_count(); i++) {
        const shell::App &app = shell::app(i);
        shell::launch(app);
        const Pid pid = t.find_by_name(app.name);
        CHECK(pid > highest && desktop.windows_of(pid) >= 1);
        highest = pid;
        wait(1000000);
        close_all();
        CHECK(!t.alive(pid) && desktop.windows_of(pid) == 0 && desktop.timers_of(pid) == 0);
    }
    // The other ways in: settings windows are programs too, and so is the
    // Shut Down dialog (the owner's rule, 2026-11-17).
    shell::screen_saver_dialog();
    CHECK(process("Screen Saver") != nullptr && desktop.active()->owner() == process("Screen Saver")->pid);
    shell::open_network();
    CHECK(process("Network") != nullptr && desktop.active()->owner() == process("Network")->pid);
    close_all();

    // The quick-launch icons by the Start button: File manager, Editor,
    // Terminal. One click starts the program, as a child of the desktop,
    // like the Start menu does. The Editor and the Terminal open another
    // copy each time; the File manager comes to the front.
    CHECK(desktop.quick_launch_count() == 3 && t.count() == 1);
    const auto quick = [&](int i) {
        const wm::Rect r = desktop.quick_launch_rect(i);
        desktop.mouse({true, static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 8), 0, 0});
        desktop.mouse({true, static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 8), 1, 0});
        desktop.mouse({true, static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 8), 0, 0});
        wait(500000); // never a double click
    };
    quick(0);
    CHECK(process("File manager") != nullptr && process("File manager")->parent == desk_pid &&
          strcmp(title(), "My Board") == 0);
    quick(1);
    CHECK(process("Editor") != nullptr && process("Editor")->parent == desk_pid && t.count() == 3);
    quick(1);
    CHECK(t.count() == 4 && desktop.window_count() == 3);
    quick(0);
    CHECK(t.count() == 4 && strcmp(title(), "My Board") == 0);
    quick(2);
    CHECK(process("Terminal") != nullptr && process("Terminal")->parent == desk_pid &&
          desktop.active()->owner() == process("Terminal")->pid);
    close_all();
    wait(10000);
    CHECK(t.count() == 1);

    key(wm::key::Escape, kCtrl);
    key(wm::key::Up);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "Shut Down Chinchilla") == 0 && process("Shut Down") != nullptr &&
          desktop.active()->owner() == process("Shut Down")->pid);
    key(wm::key::Escape);
    wait(10000);
    CHECK(t.count() == 1 && t.alive(desk_pid) && desktop.window_count() == 0);
    CHECK(desktop.timers_of(0) == 0); // nothing running that nobody owns
}

} // namespace

int main()
{
    test_table();
    test_figures();
    test_signals_in_the_table();
    test_owners();

    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    static host::FakeWifi radio;
    static host::MemoryKeyStore settings;
    static net::Manager wifi(radio, settings);
    shell::set_network(&wifi);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    test_shell();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
