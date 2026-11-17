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
    // The taskbar of the usual file: something that comes back.
    static bool start_taskbar(void *context)
    {
        auto *m = static_cast<Machine *>(context);
        m->procs.spawn("taskbar", m->procs.current(), proc::kSystem);
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
        i.add("taskbar", &start_taskbar, this, false, init::Again::Does);
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
        static const char *const names[init::Init::kMaxUnits] = {"a", "b", "c", "d", "e", "f", "g", "h"};
        for (int n = 0; n < init::Init::kMaxUnits; n++) {
            CHECK(i.add(names[n], &Machine::start_console, nullptr));
            CHECK(!i.add(names[n], &Machine::start_console, nullptr)); // the name is taken
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

// Something init can start again: each start makes a process with the
// thing's name.
struct Comes {
    proc::Table *procs;
    const char *name;
    Pid pid = proc::kNone;
    int starts = 0;

    static bool start(void *context)
    {
        auto *c = static_cast<Comes *>(context);
        c->pid = c->procs->spawn(c->name, c->procs->current());
        c->starts++;
        return true;
    }
};

uint32_t now_us = 0;

void test_respawn()
{
    Machine m;
    init::Init i(m.procs);
    m.set_up(i);
    Comes bar{&m.procs, "bar"}, clock{&m.procs, "clock"}, once{&m.procs, "once"};
    i.add("bar", &Comes::start, &bar, false, init::Again::Does);
    i.add("clock", &Comes::start, &clock, false, init::Again::Can);
    i.add("once", &Comes::start, &once, false, init::Again::No);
    CHECK(!i.add("bar", &Comes::start, &bar)); // the name is taken
    CHECK(init::the() == &i && m.procs.supervisor() == &i);

    // respawn: started, init's child, marked; ended, it is back at the next
    // safe point and not before.
    run(i, "start desktop\nrespawn bar\nrespawn clock\nrespawn once\nrespawn inetd\n", 0);
    CHECK(bar.starts == 1 && clock.starts == 1 && once.starts == 1);
    CHECK(m.procs.find(bar.pid)->parent == proc::kRoot && m.procs.find(bar.pid)->is(proc::kRespawn));
    CHECK(m.procs.find(clock.pid)->is(proc::kRespawn) && !m.procs.find(once.pid)->is(proc::kRespawn));
    CHECK(has("init: once cannot be started again when it ends; started once"));
    CHECK(has("init: inetd cannot be started again when it ends; started once"));
    const Pid bar1 = bar.pid;
    m.procs.exit(bar1);
    CHECK(bar.starts == 1 && m.procs.restarts_waiting() && !i.started("bar"));
    {
        const proc::Scope as(&m.procs, m.desktop);
        CHECK(m.procs.restart_due() == 1 && m.procs.current() == m.desktop);
    }
    CHECK(bar.starts == 2 && bar.pid != bar1 && m.procs.find(bar.pid)->parent == proc::kRoot);
    CHECK(i.started("bar") && has("init: bar ended; starting it again"));
    CHECK(!m.procs.restarts_waiting() && m.procs.restart_due() == 0);
    // What cannot come back, and what nobody watches, stay away.
    m.procs.exit(once.pid);
    m.procs.exit(m.inetd);
    CHECK(m.procs.restart_due() == 0 && once.starts == 1);

    // start: once. Ended, it stays away, and init knows it is not running:
    // it can be started by hand.
    {
        Machine m2;
        init::Init i2(m2.procs);
        m2.set_up(i2);
        Comes bar2{&m2.procs, "bar"};
        i2.add("bar", &Comes::start, &bar2, false, init::Again::Does);
        run(i2, "start desktop\nstart bar\n", 0);
        CHECK(bar2.starts == 1 && !m2.procs.find(bar2.pid)->is(proc::kRespawn));
        m2.procs.exit(bar2.pid);
        CHECK(m2.procs.restart_due() == 0 && bar2.starts == 1 && !i2.started("bar"));
        CHECK(i2.start_again("bar") && bar2.starts == 2 && !i2.start_again("bar"));
        CHECK(!i2.start_again("nothing"));
    }
    CHECK(init::the() == nullptr); // the one made last has gone
}

void test_needed_and_late()
{
    // Something needed that comes back, with no line of its own (a file
    // from before there was `respawn`): started anyway, and it comes back.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        Comes bar{&m.procs, "bar"};
        i.add("bar", &Comes::start, &bar, true, init::Again::Does);
        run(i, "start desktop\nstart inetd\n", 0);
        CHECK(bar.starts == 1 && m.order == "desktop inetd ");
        CHECK(has("init: bar goes before inetd"));
        m.procs.exit(bar.pid);
        CHECK(m.procs.restart_due() == 1 && bar.starts == 2);
    }
    // Added while the file is being done, by what the file starts (the
    // desktop adds its taskbar): its line finds it, wherever it is.
    {
        struct Late {
            init::Init *i;
            Machine *m;
            Comes bar;
            static bool start_desktop(void *context)
            {
                auto *l = static_cast<Late *>(context);
                Machine::start_desktop(l->m);
                l->i->add("taskbar", &Comes::start, &l->bar, true, init::Again::Does);
                CHECK(l->i->booting());
                return true;
            }
        };
        for (const char *text : {"start desktop\nrespawn taskbar\n", "respawn taskbar\nstart desktop\n",
                                 "start taskbar\n", "# nothing\n"}) {
            Machine m;
            init::Init i(m.procs);
            Late late{&i, &m, {&m.procs, "taskbar"}};
            i.add("desktop", &Late::start_desktop, &late, true);
            i.set_log(&keep, nullptr);
            said.clear();
            run(i, text, 0);
            CHECK(!i.booting() && late.bar.starts == 1 && i.started("taskbar"));
            const bool once = strcmp(text, "start taskbar\n") == 0;
            CHECK(m.procs.find(late.bar.pid)->is(proc::kRespawn) == !once);
            m.procs.exit(late.bar.pid);
            CHECK(m.procs.restart_due() == (once ? 0 : 1));
        }
    }
    // Started by hand before the file is done (a machine that starts its
    // desktop first): its line is not a line gone wrong.
    {
        Machine m;
        init::Init i(m.procs);
        m.set_up(i);
        Comes bar{&m.procs, "bar"};
        i.add("bar", &Comes::start, &bar, true, init::Again::Does);
        CHECK(i.start_again("bar"));
        run(i, "start desktop\nrespawn bar\n", 0);
        CHECK(bar.starts == 1);
        run(i, "respawn bar\n", 1); // a second time it is
    }
}

void test_brake()
{
    Machine m;
    init::Init i(m.procs);
    m.set_up(i);
    now_us = 4000000000u; // the clock goes round during the test
    m.procs.set_clock([](void *) { return now_us; }, nullptr);

    Comes bar{&m.procs, "Panel"};
    i.add("Panel", &Comes::start, &bar, false, init::Again::Does);
    CHECK(i.start_again("Panel"));
    CHECK(bar.starts == 1 && m.procs.find(bar.pid)->is(proc::kRespawn));

    // Ended after a good while, any number of times: it comes back each time.
    for (int n = 0; n < 8; n++) {
        now_us += 60 * 1000000u;
        m.procs.exit(bar.pid);
        CHECK(m.procs.restart_due() == 1 && m.procs.alive(bar.pid));
    }
    CHECK(bar.starts == 9 && i.given_up() == 0);

    // Ending as soon as it starts: four times it is started again, the fifth
    // it is not, and init says so. Another thing is not touched by that.
    Comes other{&m.procs, "Clock"};
    i.add("Clock", &Comes::start, &other, false, init::Again::Does);
    CHECK(i.start_again("Clock"));
    for (int n = 1; n <= init::Init::kQuickLimit; n++) {
        now_us += 100000;
        m.procs.exit(bar.pid);
        const int due = m.procs.restart_due();
        CHECK(due == (n < init::Init::kQuickLimit ? 1 : 0));
    }
    CHECK(bar.starts == 9 + init::Init::kQuickLimit - 1 && !m.procs.alive(bar.pid));
    CHECK(m.procs.find_by_name("Panel") == proc::kNone && i.given_up() == 1 && !i.started("Panel"));
    CHECK(has("init: Panel keeps ending as soon as it starts; not started again"));
    now_us += 100000;
    m.procs.exit(other.pid);
    CHECK(m.procs.restart_due() == 1 && m.procs.alive(other.pid) && other.starts == 2);

    // Quick endings that are not one after the other do not add up: one
    // that lasts starts the count again.
    Comes shaky{&m.procs, "Shaky"};
    i.add("Shaky", &Comes::start, &shaky, false, init::Again::Does);
    CHECK(i.start_again("Shaky"));
    for (int round = 0; round < 3; round++) {
        for (int n = 0; n < init::Init::kQuickLimit - 1; n++) {
            now_us += init::Init::kQuickUs - 1;
            m.procs.exit(shaky.pid);
            CHECK(m.procs.restart_due() == 1);
        }
        now_us += init::Init::kQuickUs;
        m.procs.exit(shaky.pid);
        CHECK(m.procs.restart_due() == 1);
    }
    CHECK(m.procs.alive(shaky.pid) && i.given_up() == 1);

    // Started by hand after init gave up, it has its five tries again.
    CHECK(i.start_again("Panel"));
    for (int n = 1; n < init::Init::kQuickLimit; n++) {
        now_us += 1000;
        m.procs.exit(bar.pid);
        CHECK(m.procs.restart_due() == 1 && m.procs.alive(bar.pid));
    }

    // Without a clock there is no brake.
    proc::Table plain;
    init::Init plain_init(plain);
    Comes free_one{&plain, "Free"};
    plain_init.add("Free", &Comes::start, &free_one, false, init::Again::Does);
    CHECK(plain_init.start_again("Free"));
    for (int n = 0; n < 20; n++) {
        plain.exit(free_one.pid);
        CHECK(plain.restart_due() == 1);
    }
    CHECK(plain_init.given_up() == 0);
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
    test_respawn();
    test_needed_and_late();
    test_brake();
    test_desktop();
    if (failures == 0) {
        printf("init: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
