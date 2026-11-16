// The process table: PIDs, parents and flags; windows and timers that carry
// their owner; programs that get a row when launched and lose it when their
// last window closes; the services' rows; and the Task Manager's list.
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

void test_table()
{
    static proc::Table t;
    CHECK(t.count() == 0 && t.current() == proc::kNone && t.find(1) == nullptr);

    // The first process is init, PID 1, whoever asks first.
    const Pid service = t.spawn_service("dbgd", proc::kProtected);
    CHECK(service == 2 && t.count() == 2);
    const proc::Process *init = t.find(proc::kInit);
    CHECK(init != nullptr && strcmp(init->name, "init") == 0 && init->parent == proc::kNone);
    CHECK(init != nullptr && init->is(proc::kSystem) && init->is(proc::kProtected) && init->state == proc::State::Running);
    CHECK(t.init() == 1 && t.count() == 2); // asking again makes nothing
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

    // A process that ends leaves its children to init. Its number is not
    // given out again: the next one is still the next.
    CHECK(t.exit(editor) && !t.alive(editor) && t.count() == 5);
    CHECK(t.find(dialog_child)->parent == proc::kInit);
    CHECK(!t.exit(editor) && !t.exit(99) && !t.exit(proc::kInit) && t.alive(proc::kInit));
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
    // init's), and skip every number still in use.
    t.reset();
    CHECK(t.count() == 0);
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
        return true;
    }
    void closed() override { closed_pid = procs.current(); }

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

    // After start-up: init is 1; the debug service (made by the adapter
    // before the shell starts), the desktop and the Wi-Fi manager are its
    // children, all system processes. No program runs yet.
    const proc::Process *init = t.find(1), *dbgd = process("dbgd"), *desk = process("desktop"), *netd = process("netd");
    CHECK(init != nullptr && strcmp(init->name, "init") == 0);
    CHECK(dbgd != nullptr && dbgd->pid == 2 && dbgd->parent == 1 && dbgd->is(proc::kProtected));
    CHECK(desk != nullptr && desk->pid == 3 && desk->parent == 1 && desk->is(proc::kSystem) && desk->is(proc::kProtected));
    CHECK(netd != nullptr && netd->pid == 4 && netd->parent == 1 && netd->is(proc::kSystem) && !netd->is(proc::kProtected));
    CHECK(t.count() == 4 && desktop.processes() == &t);
    const Pid desk_pid = desk->pid, netd_pid = netd->pid;
    // Timers: the caret and the clock, the desktop's look at its folder and
    // the screensaver's are the desktop's; the Wi-Fi manager's is its own.
    CHECK(desktop.timers_of(netd_pid) == 1 && desktop.timers_of(desk_pid) >= 3 && desktop.timers_of(0) == 0);

    // Starting the shell again (the tests do) makes no second desktop.
    CHECK(t.find_by_name("desktop") == desk_pid);

    // A program launched gets the next PID, the launcher as its parent, and
    // its window. Launching it again is the same process.
    shell::launch(*shell::find_app("Welcome"));
    const proc::Process *welcome = process("Welcome");
    CHECK(welcome != nullptr && welcome->pid == 5 && welcome->is(proc::kWindowed) && !welcome->is(proc::kSystem));
    CHECK(welcome != nullptr && welcome->parent == proc::kInit); // launched from outside any process
    CHECK(find("Welcome to Chinchilla")->owner() == 5 && desktop.windows_of(5) == 1);
    shell::launch(*shell::find_app("Welcome"));
    CHECK(t.count() == 5 && desktop.window_count() == 1);

    // From the Start menu the parent is the desktop: Ctrl+Esc, Programs,
    // the first entry (About Chinchilla).
    key(wm::key::Escape, kCtrl);
    key(wm::key::Down); // Programs
    key(wm::key::Right);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "About Chinchilla") == 0);
    const proc::Process *about = process("About Chinchilla");
    CHECK(about != nullptr && about->pid == 6 && about->parent == desk_pid && desktop.active()->owner() == 6);

    // Closing its last window ends the process; the others stay.
    key(wm::key::Enter); // OK
    CHECK(process("About Chinchilla") == nullptr && t.count() == 5 && process("Welcome") != nullptr);
    // Started again it is a new process with a new number.
    shell::launch(*shell::find_app("About Chinchilla"));
    CHECK(process("About Chinchilla") != nullptr && process("About Chinchilla")->pid == 7);
    close_all();
    CHECK(t.count() == 4);

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
    CHECK(t.find(editor_pid)->parent == proc::kInit); // its parent ended: init takes it
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
    CHECK(t.count() == 4);
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
    // Switch To on what is now the first row.
    key(wm::key::Home);
    letter('s', kAlt);
    CHECK(strcmp(title(), "Widget gallery") == 0);

    // "Show all processes": every row of the table, init first. End Task
    // on a process without windows says it cannot, and ends nothing.
    key(wm::key::Delete, kCtrl | kAlt);
    const std::string programs_only = screen();
    letter('a', kAlt);
    wm::Window *const its_window = desktop.active();
    CHECK(programs_only != screen());
    letter('p', kAlt); // the list, by its heading's letter
    key(wm::key::Home);
    letter('e', kAlt);
    CHECK(desktop.active() != its_window && desktop.active()->owner() == manager); // its message
    CHECK(desktop.windows_of(manager) == 2 && t.alive(proc::kInit) && t.count() == 6);
    key(wm::key::Enter);
    CHECK(desktop.active() == its_window);
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
    // The other ways in: settings windows are programs too, and what the
    // desktop itself shows (Shut Down) is the desktop's.
    shell::screen_saver_dialog();
    CHECK(process("Screen Saver") != nullptr && desktop.active()->owner() == process("Screen Saver")->pid);
    shell::open_network();
    CHECK(process("Network") != nullptr && desktop.active()->owner() == process("Network")->pid);
    close_all();
    key(wm::key::Escape, kCtrl);
    key(wm::key::Up);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "Shut Down Chinchilla") == 0 && desktop.active()->owner() == desk_pid);
    key(wm::key::Escape);
    wait(10000);
    CHECK(t.count() == 4 && t.alive(desk_pid) && t.alive(netd_pid) && desktop.window_count() == 0);
    CHECK(desktop.timers_of(0) == 0); // nothing running that nobody owns
}

} // namespace

int main()
{
    test_table();
    test_owners();

    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    static host::FakeWifi radio;
    static host::MemoryKeyStore settings;
    static net::Manager wifi(radio, settings);
    proc::table().spawn_service("dbgd", proc::kProtected); // as the board and the simulator do
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
