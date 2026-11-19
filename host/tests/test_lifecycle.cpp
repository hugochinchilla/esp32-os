// The life of a program (backlog items 19 and 34): its window, widgets and
// buffers are made when it opens and destroyed once it has closed, so the
// memory it took goes back. The tests count the blocks of the Memory port
// before and after; the PC build's address checker would stop on any use of
// a window after it was destroyed.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/fs/samples.hpp"
#include "core/input/bluetooth.hpp"
#include "core/net/wifi.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "../../programs/test_hooks.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "../fake_bluetooth.hpp"
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

namespace shell {
void open_trash(); // the Trash icon's action (core/src/shell/internal.hpp)
}

namespace {

using proc::Pid;

/* ---- The window manager by itself --------------------------------------------- */

// A window made with new, which counts how many of its kind exist and what
// happened to it.
int g_alive = 0;
int g_timer_calls = 0;

class Throwaway final : public wm::Window {
public:
    explicit Throwaway(const char *title) : Window(title, {40, 40, 200, 120}, nullptr) { g_alive++; }
    ~Throwaway() { g_alive--; }

    static Throwaway *make(const char *title)
    {
        auto *w = new Throwaway(title);
        w->set_dispose(&Throwaway::dispose);
        return w;
    }
    static void dispose(wm::Window &w) { delete static_cast<Throwaway *>(&w); }
    static void timer(void *) { g_timer_calls++; }

    void closed() override
    {
        closed_calls++;
        if (reopen_in != nullptr) {
            wm::Wm *m = reopen_in;
            reopen_in = nullptr;
            m->add(*this);
        }
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close(); // from inside its own code: it must outlive this call
            touched_after_close = closed_calls;
            return true;
        }
        return false;
    }

    int closed_calls = 0;
    int touched_after_close = 0;
    wm::Wm *reopen_in = nullptr;
};

void test_window_manager()
{
    host::MemoryDisplay display(640, 480);
    host::MemoryPointer pointer;
    host::FixedClock clock;
    wm::Wm m(display, pointer, clock);
    m.set_animations(false);
    m.start();
    auto tick = [&] {
        clock.now += 1000;
        m.tick();
    };

    // Closed from its own key handler: the object is still there when the
    // handler goes on, and gone when the event is over.
    Throwaway *a = Throwaway::make("A");
    m.add(*a);
    CHECK(g_alive == 1 && a->open());
    m.key(wm::key::Escape, true);
    CHECK(g_alive == 0 && m.window_count() == 0);

    // Closed by the manager outside an event: it waits for the safe point.
    a = Throwaway::make("A");
    m.add(*a);
    m.close(*a);
    CHECK(g_alive == 1 && !a->open());
    tick();
    CHECK(g_alive == 0);

    // A window that opens itself again from closed() is kept, and so is one
    // opened again before the safe point.
    a = Throwaway::make("A");
    m.add(*a);
    a->reopen_in = &m;
    m.close(*a);
    tick();
    CHECK(g_alive == 1 && a->open() && a->closed_calls == 1);
    m.close(*a);
    m.add(*a);
    tick();
    CHECK(g_alive == 1 && a->open());
    m.close(*a);
    tick();
    CHECK(g_alive == 0);

    // One that was never opened is destroyed the same way when discarded;
    // discarding an open one does nothing.
    a = Throwaway::make("A");
    m.discard(*a);
    m.discard(*a); // twice is once
    CHECK(g_alive == 1);
    tick();
    CHECK(g_alive == 0);
    a = Throwaway::make("A");
    m.add(*a);
    m.discard(*a);
    tick();
    CHECK(g_alive == 1 && a->open());

    // A dialog that belongs to a window which is destroyed goes first, and
    // its closed() still finds its owner. A timer left pointing at the
    // window is stopped; other timers are not.
    Throwaway *d = Throwaway::make("Dialog");
    d->set_dialog(true);
    d->set_modal(a);
    m.add(*d);
    const int stale = m.start_timer(5000, 5000, &Throwaway::timer, a);
    const int other = m.start_timer(5000, 5000, &Throwaway::timer, &g_timer_calls);
    CHECK(stale != 0 && other != 0);
    m.close(*a); // not the usual order: the owner first
    CHECK(d->open());
    tick();
    CHECK(g_alive == 0 && m.window_count() == 0);
    g_timer_calls = 0;
    clock.now += 6000;
    m.tick();
    CHECK(g_timer_calls == 1);
    m.stop_timer(other);

    // A window without a dispose function is left alone, as before.
    static Throwaway fixed("Fixed");
    const int before = g_alive;
    m.add(fixed);
    m.close(fixed);
    tick();
    CHECK(g_alive == before && !fixed.open());
}

/* ---- The shell ---------------------------------------------------------------- */

// The Memory port with a switch to say "none left".
class CountedMemory : public host::HostMemory {
public:
    void *allocate(size_t bytes) override
    {
        if (empty || (limit != 0 && bytes > limit)) {
            refused++;
            return nullptr;
        }
        return HostMemory::allocate(bytes);
    }
    bool empty = false;
    size_t limit = 0; // blocks larger than this are refused (0: none)
    int refused = 0;
};

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
CountedMemory memory;
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

int processes_named(const char *name)
{
    int n = 0;
    for (int i = 0; i < proc::table().count(); i++) {
        n += strcmp(proc::table().at(i).name, name) == 0;
    }
    return n;
}

void test_programs()
{
    proc::Table &t = proc::table();
    // What the OS itself holds, from start-up: the clipboard, and what the
    // desktop keeps of the program files once it has listed them. Nothing
    // else may be left over by anything below.
    const int programs = shell::program_count();
    const int base = memory.in_use();
    const size_t base_bytes = memory.bytes_in_use();
    CHECK(t.count() == 2 && desktop.window_count() == 0 && programs >= 10);

    // Every program on the Start menu, the system's own and the files of
    // C:\Programs alike: while open it holds memory; closed, it holds none,
    // and its process is gone. Twice over, so the second run is a new
    // program and not the first one found again.
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < shell::program_count(); i++) {
            const std::string name = shell::program(i).name;
            const struct {
                const char *name;
            } app = {name.c_str()};
            shell::start_program(app.name);
            wait(1000000);
            CHECK(desktop.window_count() >= 1 && memory.in_use() > base);
            CHECK(processes_named(app.name) == 1);
            // Its own close button, as the user would.
            if (strcmp(app.name, "Picture viewer") == 0 || strcmp(app.name, "Chess") == 0) {
                key(wm::key::Escape); // the dialog it starts with: Open, or New game
            }
            key(wm::key::F4, kAlt);
            wait(10000);
            if (desktop.window_count() != 0 || memory.in_use() != base || t.count() != 2) {
                printf("%s left %d windows, %d blocks, %d processes\n", app.name, desktop.window_count(),
                       memory.in_use() - base, t.count() - 1);
                failures++;
                close_all();
            }
        }
    }
    CHECK(memory.bytes_in_use() == base_bytes);

    // The big ones give their buffers back too: the Editor's text and undo
    // (576 KB), and the Picture viewer's picture and working memory.
    shell::open_in_editor("C:\\Documents\\notes.txt");
    CHECK(strcmp(title(), "notes.txt - Editor") == 0);
    CHECK(memory.bytes_in_use() - base_bytes > 576 * 1024);
    key(wm::key::F4, kAlt);
    CHECK(memory.in_use() == base && memory.bytes_in_use() == base_bytes);
    shell::open_picture("C:\\pictures\\landscape.jpg");
    shell::finish_picture();
    CHECK(strstr(title(), "Picture viewer") != nullptr);
    CHECK(memory.bytes_in_use() - base_bytes > 1600 * 1200);
    key(wm::key::F4, kAlt);
    CHECK(memory.in_use() == base && memory.bytes_in_use() == base_bytes);
    // Closed while still decoding: the timer stops and nothing is left.
    shell::open_picture("C:\\pictures\\landscape.jpg");
    key(wm::key::F4, kAlt);
    wait(1000000);
    CHECK(memory.in_use() == base && desktop.timers_of(0) == 0);

    // The File manager: each folder window by itself, and the Trash.
    shell::open_folder("C:\\Documents");
    const int one_window = memory.in_use();
    shell::open_folder("C:\\");
    shell::open_folder(nullptr);
    CHECK(desktop.window_count() == 3 && memory.in_use() > one_window);
    CHECK(processes_named("File manager") == 1);
    key(wm::key::F4, kAlt);
    key(wm::key::F4, kAlt);
    CHECK(desktop.window_count() == 1 && memory.in_use() == one_window && processes_named("File manager") == 1);
    key(wm::key::F4, kAlt);
    CHECK(memory.in_use() == base && t.count() == 2);
    shell::open_trash();
    CHECK(strcmp(title(), "Trash") == 0 && memory.in_use() > base);
    key(wm::key::F4, kAlt);
    CHECK(memory.in_use() == base && t.count() == 2);
}

void test_editors()
{
    proc::Table &t = proc::table();
    const int base = memory.in_use();

    // Each launch is another Editor: its own process, window and text.
    shell::start_program("Editor");
    wm::Window *first = desktop.active();
    desktop.text("one");
    shell::start_program("Editor");
    wm::Window *second = desktop.active();
    CHECK(first != second && first != nullptr && second != nullptr);
    CHECK(processes_named("Editor") == 2 && first->owner() != second->owner());
    CHECK(first->frame().x != second->frame().x); // not one on top of the other
    CHECK(strcmp(first->title(), "Untitled* - Editor") == 0 && strcmp(second->title(), "Untitled - Editor") == 0);
    const int two = memory.in_use();

    // A file that an Editor has open comes forward; another file is
    // another Editor.
    shell::open_in_editor("C:\\Documents\\notes.txt");
    wm::Window *notes = desktop.active();
    CHECK(processes_named("Editor") == 3 && strcmp(title(), "notes.txt - Editor") == 0);
    desktop.activate(*first);
    shell::open_in_editor("C:\\Documents\\notes.txt");
    CHECK(processes_named("Editor") == 3 && desktop.active() == notes);

    // No limit of the Editor's own (the owner's decision): a fifth opens,
    // as long as there is memory, and gives it all back when it closes.
    shell::start_program("Editor");
    CHECK(processes_named("Editor") == 4);
    const int four = memory.in_use();
    shell::start_program("Editor");
    CHECK(processes_named("Editor") == 5 && strcmp(title(), "Untitled - Editor") == 0);
    key(wm::key::F4, kAlt);
    wait(10000);
    wait(10000);
    CHECK(processes_named("Editor") == 4 && memory.in_use() == four);
    // The one that finds no memory says so, and the message stays when
    // whoever started it has gone: asked for from the Run dialog, which
    // closes at once, as the board did it (the message went with the dialog).
    memory.limit = 64 * 1024;
    shell::run_dialog();
    desktop.text("editor");
    key(wm::key::Enter);
    wait(10000);
    wait(10000);
    CHECK(find("Run") == nullptr && strcmp(title(), "Editor") == 0 && processes_named("Editor") == 5);
    key(wm::key::Enter);
    wait(10000);
    wait(10000);
    CHECK(processes_named("Editor") == 4 && memory.in_use() == four);
    memory.limit = 0;

    // Each has its own dialogs at the same time: Open in one, Save As in
    // another, the question about unsaved text in a third.
    desktop.activate(*notes);
    letter('f', kAlt);
    letter('o');
    CHECK(strcmp(title(), "Open") == 0 && desktop.active()->owner() == notes->owner());
    desktop.activate(*second);
    letter('f', kAlt);
    letter('a');
    CHECK(strcmp(title(), "Save As") == 0 && desktop.active()->owner() == second->owner());
    CHECK(find("Open") != nullptr && find("Save As") != nullptr);
    // Asked again, a window that has a dialog up shows that one.
    desktop.activate(*notes);
    CHECK(strcmp(title(), "Open") == 0);
    key(wm::key::Escape);
    CHECK(desktop.active() == notes && find("Open") == nullptr && find("Save As") != nullptr);

    // KILL on an Editor with a dialog open and unsaved text: everything it
    // had is gone at once, and the others are as they were.
    const Pid killed = second->owner();
    CHECK(t.signal(killed, proc::Signal::Kill) == proc::Sent::Ok);
    wait(10000);
    CHECK(!t.alive(killed) && find("Save As") == nullptr && processes_named("Editor") == 3);
    CHECK(strcmp(first->title(), "Untitled* - Editor") == 0 && first->open() && notes->open());

    // The one with unsaved text asks; No lets it go.
    desktop.activate(*first);
    key(wm::key::F4, kAlt);
    CHECK(strcmp(title(), "Editor") == 0 && processes_named("Editor") == 3);
    letter('n', kAlt);
    wait(10000);
    CHECK(processes_named("Editor") == 2);
    close_all();
    CHECK(processes_named("Editor") == 0 && t.count() == 2 && memory.in_use() == base);
    (void)two;
}

void test_dialogs()
{
    proc::Table &t = proc::table();
    const int base = memory.in_use();

    // The settings tools are programs with a life of their own: a process
    // row and memory while open, neither once closed.
    struct Tool {
        const char *process;
        const char *title;
        void (*open)();
        int blocks; // while open
    };
    const Tool tools[] = {
        {"Display", "Display", &shell::display_dialog, 20}, // the window and nineteen controls (five more for the Resolution page)
        {"Screen Saver", "Screen Saver", &shell::screen_saver_dialog, 11}, // by number: the window and ten controls
        {"Network", "Network", &shell::open_network, 14},
        {"Bluetooth", "Bluetooth", &shell::open_bluetooth, 12}, // the window and eleven controls
        {"Services", "Services", &shell::services_dialog, 8}, // the window and seven controls
        {"Shut Down", "Shut Down Chinchilla", &shell::shut_down_dialog, 6}, // by number (os::ui): the window and five controls
        {"Task Manager", "Task Manager", &shell::open_task_manager, 20},
    };
    for (const Tool &tool : tools) {
        tool.open();
        CHECK(strcmp(title(), tool.title) == 0 && processes_named(tool.process) == 1);
        CHECK(desktop.active()->owner() == t.find_by_name(tool.process));
        CHECK(memory.in_use() == base + tool.blocks); // one object for a window class with its widgets
        tool.open();                                  // asked for again: the same one
        CHECK(memory.in_use() == base + tool.blocks && desktop.window_count() == 1);
        wait(1500000);
        key(wm::key::Escape);
        wait(10000);
        if (memory.in_use() != base || t.count() != 2 || desktop.window_count() != 0) {
            printf("%s left %d windows, %d blocks, %d processes\n", tool.process, desktop.window_count(),
                   memory.in_use() - base, t.count() - 1);
            failures++;
            close_all();
        }
    }

    // Display again, by OK and by the close button, and with a change
    // applied: what it shows is read again each time it opens.
    shell::display_dialog();
    key(wm::key::Right); // the next colour
    key(wm::key::Enter); // OK
    CHECK(memory.in_use() == base && t.count() == 2);
    const wm::Wm::Backdrop chosen = desktop.backdrop();
    shell::display_dialog();
    key(wm::key::Right);
    letter('a', kAlt); // Apply
    CHECK(memcmp(&desktop.backdrop(), &chosen, sizeof(chosen)) != 0);
    key(wm::key::Escape); // Cancel takes it back
    CHECK(memcmp(&desktop.backdrop(), &chosen, sizeof(chosen)) == 0 && memory.in_use() == base);
    shell::display_dialog();
    key(wm::key::Left); // back to the first colour
    key(wm::key::Enter);
    CHECK(memory.in_use() == base);

    // A message box is made when it is shown and gone when answered. It is
    // the asker's: here nobody's program, so the desktop's.
    shell::message_box("Note", "Something to read.");
    CHECK(strcmp(title(), "Note") == 0 && memory.in_use() == base + 1);
    // A new message takes the place of the one shown: still one box.
    shell::message_box("Note 2", "Something else.");
    CHECK(strcmp(title(), "Note 2") == 0 && desktop.window_count() == 1);
    wait(10000);
    CHECK(memory.in_use() == base + 1);
    key(wm::key::Enter);
    CHECK(desktop.window_count() == 0 && memory.in_use() == base);
    // An answer that asks the next question, from inside the first.
    static int answers = 0;
    static const char *const yes_no[] = {"&Yes", "&No"};
    answers = 0;
    shell::ask("First", "Go on?", yes_no, 2,
               [](int choice, void *) {
                   answers++;
                   if (choice == 0) {
                       shell::ask("Second", "Sure?", yes_no, 2, [](int, void *) { answers += 10; }, nullptr);
                   }
               },
               nullptr);
    key(wm::key::Enter); // Yes
    CHECK(answers == 1 && strcmp(title(), "Second") == 0 && memory.in_use() == base + 1);
    key(wm::key::Escape);
    CHECK(answers == 11 && desktop.window_count() == 0 && memory.in_use() == base);

    // The Task Manager's question about a program that will not close goes
    // with the Task Manager if that is closed first.
    shell::open_task_manager();
    {
        // Asked by the Task Manager itself, as it is: a message another
        // program put over its window would stay.
        const proc::Scope as(&t, t.find_by_name("Task Manager"));
        shell::ask("Question", "Still there?", yes_no, 2, [](int, void *) { answers = 100; }, nullptr);
    }
    CHECK(desktop.window_count() == 2);
    desktop.close(*find("Task Manager"));
    wait(10000);
    wait(10000); // the question goes at the first safe point, its process's row at the next
    CHECK(desktop.window_count() == 0 && answers == 100 && memory.in_use() == base && t.count() == 2);

    // The Picture viewer's Options and the Network panel's password dialog
    // are made for one showing as well.
    shell::open_picture("C:\\pictures\\landscape.jpg");
    shell::finish_picture();
    const int viewer = memory.in_use();
    letter('v', kAlt);
    letter('o');
    CHECK(strcmp(title(), "Options") == 0 && memory.in_use() == viewer + 6); // the dialog and its five controls
    key(wm::key::Escape);
    CHECK(strcmp(title(), "Options") != 0 && memory.in_use() == viewer);
    letter('v', kAlt);
    letter('o');
    close_all(); // the viewer and its dialog together
    CHECK(memory.in_use() == base && t.count() == 2);

    // The Network panel's Advanced dialog too: no process of its own (it
    // is the Network program's), and nothing kept once it has closed.
    shell::open_network();
    const int panel = memory.in_use();
    letter('a', kAlt);
    CHECK(strcmp(title(), "Advanced") == 0 && memory.in_use() == panel + 9); // the dialog and its eight controls
    CHECK(processes_named("Network") == 1 && desktop.active()->owner() == t.find_by_name("Network"));
    key(wm::key::Escape);
    CHECK(strcmp(title(), "Network") == 0 && memory.in_use() == panel);
    letter('a', kAlt);
    close_all(); // the panel and its dialog together
    wait(10000);
    CHECK(memory.in_use() == base && t.count() == 2);
}

void test_no_memory()
{
    proc::Table &t = proc::table();
    const int base = memory.in_use();

    // No memory at all: nothing opens, nothing is left half made, and the
    // desktop goes on. (Not even the message can be shown.)
    memory.empty = true;
    for (int i = 0; i < shell::app_count(); i++) {
        shell::launch(shell::app(i));
        wait(10000);
    }
    shell::display_dialog();
    shell::screen_saver_dialog();
    shell::shut_down_dialog();
    shell::open_task_manager();
    shell::open_network();
    shell::open_trash();
    shell::open_folder("C:\\");
    shell::message_box("Note", "Not shown.");
    wait(10000);
    CHECK(memory.refused > 0 && desktop.window_count() == 0 && t.count() == 2 && memory.in_use() == base);
    memory.empty = false;

    // Room for windows but not for the big buffers: the program says so and
    // does not stay.
    memory.limit = 64 * 1024;
    shell::open_in_editor("C:\\Documents\\notes.txt");
    CHECK(strcmp(title(), "Editor") == 0); // "not enough memory"
    key(wm::key::Enter);
    wait(10000);
    CHECK(desktop.window_count() == 0 && t.count() == 2 && memory.in_use() == base);
    shell::open_picture("C:\\pictures\\landscape.jpg");
    CHECK(strcmp(title(), "Picture viewer") == 0);
    key(wm::key::Enter);
    wait(10000);
    CHECK(desktop.window_count() == 0 && t.count() == 2 && memory.in_use() == base);
    memory.limit = 0;

    // More windows than the lists of them begin with (16). There is no
    // limit of the system's (the owner's decision): the lists grow, and
    // go back when the windows have gone.
    static const char *const folders[] = {"C:\\", "C:\\Documents", "C:\\pictures"};
    for (const char *folder : folders) {
        shell::open_folder(folder);
    }
    for (int i = 0; i < shell::app_count(); i++) {
        shell::launch(shell::app(i));
    }
    shell::start_program("Editor");
    shell::start_program("Editor");
    shell::start_program("Picture viewer");
    shell::open_trash();
    shell::display_dialog();
    shell::screen_saver_dialog();
    shell::open_network();
    const int many = desktop.window_count();
    CHECK(many > wm::Wm::kFirstWindows);
    wait(10000);
    const int rows = t.count();
    shell::start_program("Editor");
    shell::open_folder("C:\\Documents\\Desktop"); // a fourth folder window
    shell::message_box("Note", "There is room for this.");
    wait(10000);
    CHECK(desktop.window_count() == many + 3 && t.count() == rows + 1);
    close_all();
    for (int i = 0; i < 4 && desktop.window_count() != 0; i++) {
        key(wm::key::Enter); // anything that asked
        close_all();
    }
    CHECK(desktop.window_count() == 0 && t.count() == 2 && memory.in_use() == base);
}

// What a game remembers is in its settings file, since nothing of it stays
// in memory.
void test_settings_survive()
{
    shell::start_program("Culebra");
    letter('g', kAlt);
    letter('s'); // Speed
    letter('s'); // Slow
    key(wm::key::F4, kAlt);
    char text[256] = "";
    size_t n = 0;
    CHECK(storage.read_file("C:\\.config\\culebra.ini", text, sizeof(text) - 1, n) == ports::FsError::Ok);
    text[n] = 0;
    CHECK(strstr(text, "speed = slow") != nullptr);
    shell::start_program("Cactus Patch");
    const int small = desktop.active()->frame().w;
    letter('g', kAlt);
    letter('m'); // Medium
    key(wm::key::F4, kAlt);
    shell::start_program("Cactus Patch");
    CHECK(desktop.active()->frame().w > small && shell::cactus_patch_field().cols() == 16);
    letter('g', kAlt);
    letter('s');
    key(wm::key::F4, kAlt);
    wait(10000);
}

} // namespace

int main()
{
    test_window_manager();

    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    static host::FakeWifi radio;
    static host::MemoryKeyStore settings;
    static net::Manager wifi(radio, settings);
    shell::set_network(&wifi);
    static host::FakeBluetooth bluetooth_radio;
    static bt::Manager bluetooth(bluetooth_radio, &storage);
    shell::set_bluetooth(&bluetooth);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    test_programs();
    test_editors();
    test_dialogs();
    test_no_memory();
    test_settings_survive();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
