// init: PID 1, the start-up file and what it starts, the brake on programs
// that end as soon as they are started, and the real desktop as its child.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/init/init.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
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

std::string said; // what init logged, a line each
void keep(const char *line, void *)
{
    said += line;
    said += "\n";
}
bool has(const char *text) { return said.find(text) != std::string::npos; }

// A machine with three things to start, which says in what order they were.
struct Machine {
    proc::Table procs;
    std::string order;
    std::string ran;
    bool net_works = true;
    Pid desktop = proc::kNone, inetd = proc::kNone;
    Pid current_at_start = proc::kNone;

    static bool start_desktop(void *context)
    {
        auto *m = static_cast<Machine *>(context);
        m->order += "desktop ";
        m->current_at_start = m->procs.current();
        m->desktop = m->procs.spawn_service("desktop", proc::kProtected);
        return true;
    }
    static bool start_inetd(void *context)
    {
        auto *m = static_cast<Machine *>(context);
        if (!m->net_works) {
            return false;
        }
        m->order += "inetd ";
        m->inetd = m->procs.spawn_service("inetd");
        return true;
    }
    static bool start_console(void *context)
    {
        static_cast<Machine *>(context)->order += "console ";
        return true;
    }
    static bool run(const char *what, char *why, size_t capacity, void *context)
    {
        auto *m = static_cast<Machine *>(context);
        if (m->desktop == proc::kNone) {
            snprintf(why, capacity, "there is no desktop yet");
            return false;
        }
        if (strcmp(what, "nothing") == 0) {
            snprintf(why, capacity, "no such program");
            return false;
        }
        m->ran += what;
        m->ran += ";";
        return true;
    }
    void set_up(init::Init &i)
    {
        i.add("desktop", &start_desktop, this, true);
        i.add("inetd", &start_inetd, this);
        i.add("console", &start_console, this);
        i.set_run(&run, this);
        i.set_log(&keep, nullptr);
        said.clear();
    }
};

void run(init::Init &i, const char *text, int failed)
{
    const int got = i.run_text(text, strlen(text));
    if (got != failed) {
        printf("  %d lines failed, %d expected:\n%s", got, failed, said.c_str());
    }
    CHECK(got == failed);
}

void test_lines()
{
    // PID 1 is init, a system process nobody can signal, there before
    // anything is started.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        CHECK(i.pid() == proc::kRoot && m.procs.count() == 1);
        const proc::Process *row = m.procs.find(proc::kRoot);
        CHECK(row != nullptr && strcmp(row->name, "init") == 0 && row->parent == proc::kNone);
        CHECK(row != nullptr && row->is(proc::kSystem) && row->is(proc::kProtected));
        CHECK(m.procs.signal(proc::kRoot, proc::Signal::Kill) == proc::Sent::NotPermitted);

        // The file's order is the order; comments, empty lines, spaces, both
        // line ends and either case are fine. What is started is init's child,
        // and init is the current process while it starts.
        run(i, "# what starts\r\n\r\n  START Desktop  \r\nstart inetd\n\t# later\nrun terminal\r\nrun C:\\Documents\\notes.txt", 0);
        CHECK(m.order == "desktop inetd " && m.ran == "terminal;C:\\Documents\\notes.txt;");
        CHECK(m.desktop == 2 && m.procs.find(m.desktop)->parent == proc::kRoot && m.current_at_start == proc::kRoot);
        CHECK(m.inetd == 3 && m.procs.find(m.inetd)->parent == proc::kRoot && m.procs.current() == proc::kNone);
        CHECK(i.started("desktop") && i.started("inetd") && !i.started("console") && !i.started("nobody"));
        CHECK(has("init: started desktop") && has("init: started inetd") && has("init: ran terminal"));
    }
    // Lines that fail are counted and said, and the rest still happens.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        m.net_works = false;
        run(i,
            "run terminal\n"   // the desktop goes first, then this
            "start inetd\n"    // does not start
            "start printer\n"  // no such thing
            "begin desktop\n"  // not a word init knows
            "start\n"          // start what?
            "startdesktop\n"
            "start desktop\n"
            "start desktop\n"  // twice
            "run nothing\n"
            "start console\n",
            7);
        CHECK(m.order == "desktop console " && m.ran == "terminal;");
        CHECK(has("init: desktop goes before terminal") && has("init: inetd did not start"));
        CHECK(has("init: nothing here is called printer") && has("init: not understood: begin desktop"));
        CHECK(has("init: start what?") && has("init: not understood: startdesktop"));
        CHECK(has("init: desktop is already started") && has("init: run nothing: no such program"));
    }
    // A file that leaves the desktop out, or has it after a service or a
    // program: the desktop goes first all the same (the services count on
    // it), and its own line a little later is not a mistake.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        run(i, "start inetd\n", 0);
        CHECK(m.order == "desktop inetd " && has("init: desktop goes before inetd"));
    }
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        run(i, "start inetd\nstart desktop\nstart desktop\n", 1);
        CHECK(m.order == "desktop inetd " && has("init: desktop is already started"));
    }
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        run(i, "run terminal\nstart desktop\n", 0);
        CHECK(m.order == "desktop " && m.ran == "terminal;" && has("init: desktop goes before terminal"));
    }
    // A file with nothing to start: the desktop, after its last line.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        run(i, "# nothing\n", 0);
        CHECK(m.order == "desktop " && has("init: no line starts desktop; started anyway"));
    }
    // An empty file, and a line longer than a line may be.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        run(i, "", 0);
        CHECK(m.order == "desktop ");
        const std::string wide = "start desktop\nrun " + std::string(200, 'x') + "\nstart inetd\n";
        Machine n;
        init::Init j(n.procs);
        n.set_up(j);
        CHECK(j.run_text(wide.data(), wide.size()) == 1);
        CHECK(n.order == "desktop inetd " && n.ran.empty() && has("init: a line is too long"));
    }
    // In a table that already has a root (the tests that start the desktop
    // alone), init is that root and adds no row.
    {
        proc::Table procs;
        const Pid desk = procs.spawn_service("desktop");
        init::Init i(procs);
        CHECK(i.pid() == desk && procs.count() == 1);
    }
    // No more things than it has room for.
    {
        proc::Table procs;
        init::Init i(procs);
        for (int n = 0; n < init::Init::kMaxUnits; n++) {
            CHECK(i.add("x", &Machine::start_console, nullptr));
        }
        CHECK(!i.add("y", &Machine::start_console, nullptr) && !i.add(nullptr, &Machine::start_console, nullptr) &&
              !i.add("z", nullptr, nullptr));
    }
}

void test_file()
{
    static host::HostMemory memory;
    fs::Storage storage;
    static fs::MemoryVolume disk(memory.allocate(1 << 18), 1 << 18, "RAM disk");
    storage.mount('C', disk);

    // No file: the usual things start, and the file is written for whoever
    // wants to change it.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        CHECK(i.boot(storage) == 0 && !i.had_file() && i.wrote_file());
        CHECK(m.order == "desktop inetd " && m.ran.empty());
        char text[init::Init::kMaxFile];
        size_t size = 0;
        CHECK(storage.read_file(init::kPath, text, sizeof(text), size) == ports::FsError::Ok);
        CHECK(std::string(text, size) == init::Init::default_text() && size < sizeof(text));
        CHECK(has("init: C:\\.config\\startup.txt written"));
    }
    // The file as written: the same again, and nothing is written.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        CHECK(i.boot(storage) == 0 && i.had_file() && !i.wrote_file() && m.order == "desktop inetd ");
    }
    // The owner's file: no network services, a Terminal open from the start.
    {
        const char mine[] = "start desktop\r\n# start inetd\r\nrun terminal\r\n";
        CHECK(storage.write_file(init::kPath, mine, sizeof(mine) - 1) == ports::FsError::Ok);
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        CHECK(i.boot(storage) == 0 && i.had_file() && m.order == "desktop " && m.ran == "terminal;");
        CHECK(m.inetd == proc::kNone && m.procs.count() == 2);
        // It is not put back as it was.
        char text[128];
        size_t size = 0;
        storage.read_file(init::kPath, text, sizeof(text), size);
        CHECK(std::string(text, size) == mine);
    }
    // Another path, in a folder that is not there yet.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        CHECK(i.boot(storage, "C:\\etc\\rc.txt") == 0 && i.wrote_file());
        ports::DirEntry e;
        CHECK(storage.stat("C:\\etc\\rc.txt", e) == ports::FsError::Ok);
    }
    // No drive at all: the usual things start and nothing else happens.
    {
        fs::Storage none;
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        CHECK(i.boot(none) == 0 && !i.had_file() && !i.wrote_file() && m.order == "desktop inetd ");
    }
}

/* ---- The brake ---------------------------------------------------------------------- */

struct Comes {
    proc::Table *procs;
    const char *name;
    Pid pid = proc::kNone;
    int starts = 0;

    static void start(void *context)
    {
        auto *c = static_cast<Comes *>(context);
        c->pid = c->procs->spawn(c->name, proc::kNone);
        c->procs->set_restart(c->pid, &start, c);
        c->starts++;
    }
};

uint32_t now_us = 0;

void test_brake()
{
    Machine m;
    init::Init i(m.procs);
    m.set_up(i);
    now_us = 4000000000u; // the clock goes round during the test
    m.procs.set_clock([](void *) { return now_us; }, nullptr);

    Comes bar{&m.procs, "Taskbar"};
    Comes::start(&bar);
    CHECK(bar.starts == 1 && m.procs.find(bar.pid)->is(proc::kRespawn));

    // Ended after a good while, any number of times: it comes back each time.
    for (int n = 0; n < 8; n++) {
        now_us += 60 * 1000000u;
        m.procs.exit(bar.pid);
        CHECK(m.procs.restart_due() == 1 && m.procs.alive(bar.pid));
    }
    CHECK(bar.starts == 9 && m.procs.given_up() == 0);

    // Ending as soon as it starts: four times it is started again, the fifth
    // it is not, and init says so. Another program is not touched by that.
    Comes other{&m.procs, "Clock"};
    Comes::start(&other);
    for (int n = 1; n <= proc::Table::kQuickLimit; n++) {
        now_us += 100000;
        m.procs.exit(bar.pid);
        const int due = m.procs.restart_due();
        CHECK(due == (n < proc::Table::kQuickLimit ? 1 : 0));
    }
    CHECK(bar.starts == 9 + proc::Table::kQuickLimit - 1 && !m.procs.alive(bar.pid));
    CHECK(m.procs.find_by_name("Taskbar") == proc::kNone && m.procs.given_up() == 1);
    CHECK(has("init: Taskbar keeps ending as soon as it starts; not started again"));
    now_us += 100000;
    m.procs.exit(other.pid);
    CHECK(m.procs.restart_due() == 1 && m.procs.alive(other.pid) && other.starts == 2);

    // Quick endings that are not one after the other do not add up: one
    // that lasts starts the count again.
    Comes shaky{&m.procs, "Shaky"};
    Comes::start(&shaky);
    for (int round = 0; round < 3; round++) {
        for (int n = 0; n < proc::Table::kQuickLimit - 1; n++) {
            now_us += proc::Table::kQuickUs - 1;
            m.procs.exit(shaky.pid);
            CHECK(m.procs.restart_due() == 1);
        }
        now_us += proc::Table::kQuickUs;
        m.procs.exit(shaky.pid);
        CHECK(m.procs.restart_due() == 1);
    }
    CHECK(m.procs.alive(shaky.pid) && m.procs.given_up() == 1);

    // Started by hand after init gave up, it has its five tries again.
    Comes::start(&bar);
    now_us += 1000;
    m.procs.exit(bar.pid);
    CHECK(m.procs.restart_due() == 1 && m.procs.alive(bar.pid));

    // Without a clock there is no brake.
    proc::Table plain;
    plain.spawn_service("init");
    Comes free_one{&plain, "Free"};
    Comes::start(&free_one);
    for (int n = 0; n < 20; n++) {
        plain.exit(free_one.pid);
        CHECK(plain.restart_due() == 1);
    }
    CHECK(plain.given_up() == 0);
}

/* ---- The real desktop under init ---------------------------------------------------- */

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

const proc::Process *process(const char *name)
{
    return proc::table().find(proc::table().find_by_name(name));
}

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

void test_desktop()
{
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    const char mine[] = "start desktop\r\nrun terminal\r\nrun nothing like it\r\n";
    storage.make_folder("C:\\.config");
    CHECK(storage.write_file(init::kPath, mine, sizeof(mine) - 1) == ports::FsError::Ok);

    proc::Table &t = proc::table();
    t.reset();
    static init::Init first(t);
    said.clear();
    first.set_log(&keep, nullptr);
    first.add(
        "desktop",
        [](void *) {
            shell::start(desktop, system_, storage, memory);
            desktop.start();
            return true;
        },
        nullptr, true);
    first.set_run(
        [](const char *what, char *why, size_t capacity, void *) { return shell::run_text(what, "C:\\", why, capacity); },
        nullptr);
    CHECK(first.boot(storage) == 1); // the last line names nothing
    desktop.set_animations(false);
    wait(10000);

    // init is 1 and the desktop its child, with the windows and timers it
    // always had.
    const proc::Process *root = process("init");
    const proc::Process *desk = process("desktop");
    CHECK(root != nullptr && root->pid == proc::kRoot);
    CHECK(desk != nullptr && desk->pid == 2 && desk->parent == proc::kRoot);
    CHECK(desk != nullptr && desk->is(proc::kSystem) && desk->is(proc::kProtected));
    CHECK(desktop.processes() == &t && desktop.timers_of(2) >= 2 && desktop.timers_of(proc::kRoot) == 0);

    // What the file ran is init's child, as a program a start-up script
    // starts is on Unix; what the desktop opens by itself is the desktop's.
    const proc::Process *terminal = process("Terminal");
    CHECK(terminal != nullptr && terminal->parent == proc::kRoot && terminal->is(proc::kWindowed));
    CHECK(has("init: ran terminal") && has("init: run nothing like it:"));
    const proc::Process *welcome = process("Welcome");
    CHECK(welcome == nullptr || welcome->parent == 2);

    // A program whose parent ends goes to init, not to the desktop.
    Pid parent, child;
    {
        const proc::Scope as(&t, 2);
        parent = t.spawn("parent", 2);
        child = t.spawn("child", parent);
    }
    t.exit(parent);
    CHECK(t.find(child)->parent == proc::kRoot);
    t.exit(child);

    // A line that failed is said on the screen too, in a box of the
    // desktop's; no failed line, no box.
    wm::Window *before = desktop.active();
    shell::startup_trouble(0);
    wait(10000);
    CHECK(desktop.active() == before);
    shell::startup_trouble(1);
    wait(10000);
    wm::Window *box = desktop.active();
    CHECK(box != nullptr && box != before && strcmp(box->title(), "Start-up") == 0 && box->owner() == 2);
    if (box != nullptr && box != before) {
        desktop.close(*box);
    }
    wait(10000);
}

} // namespace

int main()
{
    test_lines();
    test_file();
    test_brake();
    test_desktop();
    if (failures == 0) {
        printf("init: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
