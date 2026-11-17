// Background jobs (backlog item 10): the job runner with and without real
// tasks, the rule that a process cannot end while one of its jobs runs,
// locked windows, and the Editor's save going through all of it.
//
// The "UI task" is the test's main thread. Jobs run on real threads that the
// test holds back and lets go (host::ThreadTasks), so every check happens at
// a known moment: before the work, after the work but before its result has
// been delivered, and after.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include "core/fs/memory_volume.hpp"
#include "core/job/job.hpp"
#include "core/msg/queue.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"
#include "../thread_queue.hpp"

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

bool shape_is(host::MemoryPointer &p, const video::PointerShape &shape)
{
    const video::PointerState s = p.state();
    return s.shape != nullptr && memcmp(s.shape, &shape, sizeof(video::PointerShape)) == 0;
}

/* ---- The runner by itself ------------------------------------------------------ */

struct Probe {
    proc::Table *procs = nullptr;
    std::atomic<int> worked{0};
    std::atomic<bool> saw_stop{false};
    int done = 0, stops = 0;
    uint32_t result = 0;
    bool stopped = false;
    Pid done_as = proc::kNone;
    job::Jobs *again = nullptr; // done() starts another job

    static uint32_t work(const job::Job &job, void *context)
    {
        auto *p = static_cast<Probe *>(context);
        p->saw_stop = job.stopping();
        return 40u + static_cast<uint32_t>(++p->worked);
    }
    static void finished(void *context, uint32_t result, bool stopped)
    {
        auto *p = static_cast<Probe *>(context);
        p->done++;
        p->result = result;
        p->stopped = stopped;
        p->done_as = p->procs->current();
        if (p->again != nullptr) {
            job::Jobs *jobs = p->again;
            p->again = nullptr;
            CHECK(jobs->start(p->spec()));
        }
    }
    static void stop(void *context) { static_cast<Probe *>(context)->stops++; }

    job::Spec spec()
    {
        job::Spec s;
        s.name = "probe";
        s.work = &Probe::work;
        s.done = &Probe::finished;
        s.stop = &Probe::stop;
        s.context = this;
        return s;
    }
};

void test_runner()
{
    proc::Table procs;
    const Pid root = procs.spawn("desktop", proc::kNone, proc::kSystem);
    const Pid app = procs.spawn("app", root, proc::kWindowed);
    job::Jobs jobs(procs);

    // Without tasks the caller does the work, and done() has run by the time
    // start() returns, as the owner.
    {
        Probe p;
        p.procs = &procs;
        const proc::Scope as(&procs, app);
        CHECK(jobs.start(p.spec()));
        CHECK(p.worked == 1 && p.done == 1 && p.result == 41 && !p.stopped && p.done_as == app);
        CHECK(jobs.running() == 0 && procs.jobs(app) == 0);
        CHECK(!jobs.start(job::Spec())); // no work: nothing to start
    }

    // With tasks and a queue: the work runs on its own thread, and the
    // result waits in the queue until the UI task takes it.
    host::ThreadQueue queue;
    msg::set_queue(&queue);
    msg::Inbox inbox(queue, &procs);
    host::ThreadTasks tasks;
    jobs.set_tasks(&tasks);
    {
        Probe p;
        p.procs = &procs;
        tasks.hold();
        {
            const proc::Scope as(&procs, app);
            CHECK(jobs.start(p.spec()));
        }
        CHECK(p.worked == 0 && p.done == 0 && jobs.running(app) == 1 && procs.jobs(app) == 1);
        // While it runs its stack counts as the owner's memory.
        CHECK(procs.find(app)->memory == job::Spec().stack_bytes && procs.find(root)->memory == 0);
        const uint64_t cpu_before = procs.find(app)->cpu_us;
        tasks.finish();
        CHECK(p.worked == 1 && p.done == 0 && procs.jobs(app) == 1); // done, but not yet told
        CHECK(procs.current() == proc::kNone);
        inbox.deliver();
        CHECK(p.done == 1 && p.result == 41 && p.done_as == app && procs.current() == proc::kNone);
        CHECK(jobs.running() == 0 && procs.jobs(app) == 0);
        // Told: the stack is given back, and what the task used of the
        // processor is the owner's (a thread that has run has used some).
        CHECK(procs.find(app)->memory == 0);
        CHECK(procs.find(app)->cpu_us > cpu_before && procs.find(root)->cpu_us == 0);

        // done() may start the next job: its slot is free by then.
        p.again = &jobs;
        {
            const proc::Scope as(&procs, app);
            CHECK(jobs.start(p.spec()));
        }
        tasks.join();
        inbox.deliver();
        CHECK(p.done == 2 && jobs.running(app) == 1);
        tasks.join();
        inbox.deliver();
        CHECK(p.done == 3 && p.result == 43 && jobs.running() == 0 && procs.jobs(app) == 0);
    }

    // Every task that was started has been given back, once its job was over.
    CHECK(tasks.started == 3 && tasks.ended == 3);

    // The priority class goes with the job to whoever makes the task: Normal
    // unless the program says otherwise.
    {
        Probe p;
        p.procs = &procs;
        const proc::Scope as(&procs, app);
        CHECK(jobs.start(p.spec()) && tasks.last_priority == ports::Priority::Normal);
        job::Spec low = p.spec();
        low.priority = ports::Priority::Low;
        CHECK(jobs.start(low) && tasks.last_priority == ports::Priority::Low);
        job::Spec high = p.spec();
        high.priority = ports::Priority::High;
        CHECK(jobs.start(high) && tasks.last_priority == ports::Priority::High);
        tasks.join();
        inbox.deliver();
        CHECK(p.done == 3 && tasks.ended == tasks.started);
    }

    // A job that is still running: its task's processor time reaches the
    // owner's row whenever the figures are brought up to date, not only at
    // the end, and none of it is counted twice.
    {
        struct Busy {
            std::atomic<bool> go_on{true};
            std::atomic<uint32_t> turns{0};
            static uint32_t work(const job::Job &, void *context)
            {
                auto *b = static_cast<Busy *>(context);
                while (b->go_on) {
                    b->turns++;
                }
                return 0;
            }
        } busy;
        job::Spec spec;
        spec.work = &Busy::work;
        spec.context = &busy;
        const uint64_t before = procs.find(app)->cpu_us;
        const auto began = std::chrono::steady_clock::now();
        {
            const proc::Scope as(&procs, app);
            CHECK(jobs.start(spec));
        }
        uint64_t during = 0;
        for (int i = 0; i < 2000 && during < 2000; i++) { // until it has used 2 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            procs.bring_up_to_date();
            during = procs.find(app)->cpu_us - before;
        }
        CHECK(during >= 2000 && jobs.running(app) == 1);
        procs.bring_up_to_date();
        const uint64_t again = procs.find(app)->cpu_us - before;
        CHECK(again >= during);
        busy.go_on = false;
        tasks.join();
        inbox.deliver();
        const uint64_t whole = procs.find(app)->cpu_us - before;
        CHECK(jobs.running() == 0 && whole >= again);
        procs.bring_up_to_date(); // nothing running: nothing more
        CHECK(procs.find(app)->cpu_us - before == whole);
        // Not twice: the thread cannot have used more than the time that passed.
        const auto passed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began);
        CHECK(whole <= static_cast<uint64_t>(passed.count()) + 1000);
    }

    // A KILL for a process with jobs: the jobs are told, once, the work can
    // see it, and nothing more can be started for that process.
    {
        Probe p, q;
        p.procs = q.procs = &procs;
        tasks.hold();
        const proc::Scope as(&procs, app);
        CHECK(jobs.start(p.spec()));
        CHECK(procs.signal(app, proc::Signal::Term) == proc::Sent::Ok && p.stops == 0);
        CHECK(procs.signal(app, proc::Signal::Kill) == proc::Sent::Ok && p.stops == 1);
        CHECK(procs.signal(app, proc::Signal::Kill) == proc::Sent::Ok && p.stops == 1);
        CHECK(!jobs.start(q.spec()) && q.worked == 0 && procs.jobs(app) == 1);
        tasks.finish();
        inbox.deliver();
        CHECK(p.saw_stop && p.done == 1 && p.stopped && procs.jobs(app) == 0);
        procs.take_signals(app);
    }

    // Too many at once, and a task that cannot be started: refused, and the
    // count is as before. A job whose owner has gone is finished quietly.
    {
        Probe p[job::Jobs::kMaxJobs + 1];
        tasks.hold();
        const Pid other = procs.spawn("other", root, proc::kWindowed);
        for (int i = 0; i < job::Jobs::kMaxJobs + 1; i++) {
            p[i].procs = &procs;
            const proc::Scope as(&procs, i == 0 ? other : app);
            CHECK(jobs.start(p[i].spec()) == (i < job::Jobs::kMaxJobs));
        }
        CHECK(jobs.running() == job::Jobs::kMaxJobs && jobs.running(app) == job::Jobs::kMaxJobs - 1);
        procs.exit(other); // not what the window manager does; the runner must still cope
        tasks.finish();
        while (inbox.deliver()) {
        }
        CHECK(jobs.running() == 0 && procs.jobs(app) == 0);
        CHECK(p[0].worked == 1 && p[0].done == 0 && p[1].done == 1 && p[job::Jobs::kMaxJobs].worked == 0);

        tasks.refuse = true;
        const proc::Scope as(&procs, app);
        CHECK(!jobs.start(p[1].spec()) && jobs.running() == 0 && procs.jobs(app) == 0);
        tasks.refuse = false;
    }
    msg::set_queue(nullptr);
}

/* ---- The window manager's side ------------------------------------------------- */

int g_alive = 0, g_asked = 0;

class Plain final : public wm::Window {
public:
    Plain() : Window("Plain", {100, 100, 240, 160}, nullptr) { g_alive++; }
    ~Plain() { g_alive--; }
    static Plain *make()
    {
        auto *w = new Plain();
        w->set_dispose([](wm::Window &x) { delete static_cast<Plain *>(&x); });
        return w;
    }
    bool key(const wm::KeyMsg &k) override
    {
        keys += k.pressed;
        return true;
    }
    void text(const char *) override { texts++; }
    void mouse(const wm::MouseMsg &m) override { clicks += m.kind == wm::MouseKind::Down; }
    void right_click(int, int) override { right_clicks++; }
    bool close_requested() override
    {
        asked++;
        g_asked++;
        return true;
    }
    int keys = 0, texts = 0, clicks = 0, right_clicks = 0, asked = 0;
};

struct Rig {
    host::MemoryDisplay display{640, 480};
    host::MemoryPointer pointer;
    host::FixedClock clock;
    wm::Wm wm{display, pointer, clock};
    proc::Table procs;
    Pid root = proc::kNone;

    Rig()
    {
        root = procs.spawn("desktop", proc::kNone, proc::kSystem | proc::kProtected);
        wm.set_processes(&procs, root);
        wm.set_animations(false);
        wm.start();
    }
    void tick()
    {
        clock.now += 1000;
        wm.tick();
    }
    void move(int x, int y, uint8_t buttons)
    {
        clock.now += 600000;
        wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
    }
    void click(int x, int y, uint8_t button = 1)
    {
        move(x, y, 0);
        move(x, y, button);
        move(x, y, 0);
    }
    void key(uint16_t usage, bool alt = false)
    {
        if (alt) {
            wm.key(wm::key::LeftAlt, true);
        }
        wm.key(usage, true);
        wm.key(usage, false);
        if (alt) {
            wm.key(wm::key::LeftAlt, false);
        }
    }
    Plain *open(Pid &pid)
    {
        pid = procs.spawn("app", root, proc::kWindowed);
        const proc::Scope as(&procs, pid);
        Plain *w = Plain::make();
        wm.add(*w);
        return w;
    }
};

void test_process_waits_for_jobs()
{
    Rig r;
    Pid pid;

    // KILL while a job runs: nothing happens to the process or its window
    // until the job has finished; then it goes at the next safe point.
    Plain *w = r.open(pid);
    CHECK(r.procs.job_started(pid));
    CHECK(r.procs.signal(pid, proc::Signal::Kill) == proc::Sent::Ok);
    r.tick();
    r.tick();
    CHECK(r.procs.alive(pid) && w->open() && g_alive == 1);
    r.procs.job_finished(pid);
    r.tick();
    CHECK(!r.procs.alive(pid) && g_alive == 0 && r.wm.window_count() == 0);

    // A program that closes its last window while a job runs stays in the
    // table, and the window object stays too: the job may be reading it.
    w = r.open(pid);
    CHECK(r.procs.job_started(pid));
    w->close();
    r.tick();
    CHECK(r.procs.alive(pid) && g_alive == 1 && r.wm.window_count() == 0);
    r.tick();
    CHECK(r.procs.alive(pid) && g_alive == 1);
    r.procs.job_finished(pid);
    r.tick();
    CHECK(!r.procs.alive(pid) && g_alive == 0);

    // The same, but the program opens the window again before the job ends:
    // it simply goes on.
    w = r.open(pid);
    CHECK(r.procs.job_started(pid));
    w->close();
    r.tick();
    {
        const proc::Scope as(&r.procs, pid);
        r.wm.add(*w);
    }
    r.procs.job_finished(pid);
    r.tick();
    CHECK(r.procs.alive(pid) && g_alive == 1 && w->open());
    w->close();
    r.tick();
    CHECK(!r.procs.alive(pid) && g_alive == 0);
}

void test_locked_window()
{
    Rig r;
    Pid pid;
    Plain *w = r.open(pid);
    const gfx::Rect f = w->frame();
    const int cx = f.x + f.w / 2, cy = f.y + f.h / 2; // in the client area
    const int close_x = f.right() - 14, title_y = f.y + 12;

    r.click(cx, cy);
    r.key(wm::key::A);
    CHECK(w->clicks == 1 && w->keys == 1);
    r.move(cx, cy, 0);
    CHECK(shape_is(r.pointer, video::arrow_pointer()));

    // Locked: the hourglass over it, the arrow beside it, and nothing
    // reaches the program.
    w->set_locked(true);
    CHECK(w->locked() && shape_is(r.pointer, video::busy_pointer()));
    r.move(f.x - 30, cy, 0);
    CHECK(shape_is(r.pointer, video::arrow_pointer()));
    r.click(cx, cy);
    r.click(cx, cy, 2);
    r.key(wm::key::A);
    r.wm.text("x");
    r.key(wm::key::F4, true);
    r.click(close_x, title_y);
    CHECK(w->clicks == 1 && w->right_clicks == 0 && w->keys == 1 && w->texts == 0 && w->asked == 0 && w->open());
    CHECK(shape_is(r.pointer, video::busy_pointer()));

    // TERM is not put to it either, and it stays.
    CHECK(r.procs.signal(pid, proc::Signal::Term) == proc::Sent::Ok);
    r.tick();
    CHECK(w->asked == 0 && w->open() && r.procs.alive(pid));

    // It can still be moved out of the way by its title bar.
    r.move(f.x + 60, title_y, 0);
    r.move(f.x + 60, title_y, 1);
    r.move(f.x + 90, title_y + 20, 1);
    r.move(f.x + 90, title_y + 20, 0);
    CHECK(w->frame().x == f.x + 30 && w->frame().y == f.y + 20);

    // Unlocked: as before.
    w->set_locked(false);
    r.move(cx + 30, cy + 20, 0);
    CHECK(shape_is(r.pointer, video::arrow_pointer()));
    r.click(cx + 30, cy + 20);
    r.key(wm::key::A);
    CHECK(w->clicks == 2 && w->keys == 2);
    g_asked = 0;
    r.key(wm::key::F4, true); // asked, says yes, and is gone when the event is over
    CHECK(g_asked == 1);
    r.tick();
    CHECK(g_alive == 0 && !r.procs.alive(pid));
}

/* ---- The Editor's save ---------------------------------------------------------- */

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
host::ThreadLock storage_lock;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);
host::ThreadQueue queue;
host::ThreadTasks tasks;

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

std::string file(const char *path)
{
    char buf[8192];
    size_t n = 0;
    if (storage.read_file(path, buf, sizeof(buf), n) != ports::FsError::Ok) {
        return "<missing>";
    }
    return std::string(buf, n);
}

const char *title() { return desktop.active() ? desktop.active()->title() : ""; }

int editors()
{
    int n = 0;
    for (int i = 0; i < proc::table().count(); i++) {
        n += strcmp(proc::table().at(i).name, "Editor") == 0;
    }
    return n;
}

// The job's thread runs to its end and the UI task takes the result.
void let_jobs_finish(msg::Inbox &inbox)
{
    tasks.finish();
    inbox.deliver();
    wait(1000);
}

void test_editor_save()
{
    msg::Inbox inbox(queue);
    const int blocks = memory.in_use();
    const std::string before = "one\r\ntwo\r\n";
    storage.write_file("C:\\Documents\\a.txt", before.data(), before.size());
    shell::open_in_editor("C:\\Documents\\a.txt");
    wm::Window *w = desktop.active();
    CHECK(w != nullptr && strcmp(title(), "a.txt - Editor") == 0 && editors() == 1);
    const Pid pid = w->owner();
    const gfx::Rect f = w->frame();
    desktop.mouse({true, static_cast<int16_t>(f.x + f.w / 2), static_cast<int16_t>(f.y + f.h / 2), 0, 0});

    // Save with the job held back: the window is locked, the file is still
    // the old one, and the rest of the desktop is not waiting.
    key(wm::key::End, 2);
    desktop.text("three");
    tasks.hold();
    letter('f', 4);
    letter('s');
    CHECK(w->locked() && job::jobs().running(pid) == 1 && proc::table().jobs(pid) == 1);
    CHECK(!desktop.waiting() && shape_is(pointer, video::busy_pointer()));
    CHECK(file("C:\\Documents\\a.txt") == before && strcmp(title(), "a.txt* - Editor") == 0);

    // Nothing gets into the text meanwhile, by keys, menus or Alt+F4; another
    // save does not start; and other programs can be used.
    desktop.text("LOST");
    key(wm::key::Backspace);
    letter('f', 4);
    letter('s');
    key(wm::key::F4, 4);
    wait(1000);
    CHECK(w->open() && desktop.active() == w && job::jobs().running() == 1 && tasks.started == 1);
    shell::open_in_editor(nullptr);
    CHECK(strcmp(title(), "Untitled - Editor") == 0 && editors() == 2);
    desktop.text("meanwhile");
    CHECK(strcmp(title(), "Untitled* - Editor") == 0);
    wm::Window *second = desktop.active();
    desktop.activate(*w);

    // The job runs; its result is delivered; the window is the user's again.
    tasks.finish();
    CHECK(w->locked()); // written, but the UI task has not been told yet
    inbox.deliver();
    CHECK(!w->locked() && job::jobs().running() == 0 && proc::table().jobs(pid) == 0);
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\nthree" && strcmp(title(), "a.txt - Editor") == 0);
    CHECK(shape_is(pointer, video::arrow_pointer()));
    desktop.text("!");
    CHECK(strcmp(title(), "a.txt* - Editor") == 0);

    // Close with changes, "Yes": the save runs as a job and the Editor
    // leaves when it has finished, not before.
    tasks.hold();
    key(wm::key::F4, 4);
    CHECK(strcmp(title(), "Editor") == 0); // the question
    key(wm::key::Enter);
    wait(1000);
    CHECK(w->open() && w->locked() && proc::table().alive(pid));
    let_jobs_finish(inbox);
    CHECK(!proc::table().alive(pid) && editors() == 1);
    CHECK(file("C:\\Documents\\a.txt") == "one\r\ntwo\r\nthree!");

    // End Task with force while a save runs: the file is written whole
    // first, then the Editor is taken away.
    desktop.activate(*second);
    w = second;
    const Pid pid2 = w->owner();
    letter('f', 4);
    letter('s'); // no name yet: Save As
    for (const char *c = "b.txt"; *c != 0; c++) {
        const char one[2] = {*c, 0};
        desktop.text(one);
    }
    tasks.hold();
    key(wm::key::Enter);
    CHECK(w->locked() && job::jobs().running(pid2) == 1);
    CHECK(proc::table().signal(pid2, proc::Signal::Kill) == proc::Sent::Ok);
    wait(1000);
    wait(1000);
    CHECK(proc::table().alive(pid2) && w->open() && file("C:\\Documents\\b.txt") == "<missing>");
    let_jobs_finish(inbox);
    CHECK(!proc::table().alive(pid2) && editors() == 0 && desktop.window_count() == 0);
    CHECK(file("C:\\Documents\\b.txt") == "meanwhile");
    CHECK(memory.in_use() == blocks);

    // A save that fails says so when the job comes back, and the window is
    // unlocked with its changes kept.
    shell::open_in_editor("C:\\Documents\\a.txt");
    w = desktop.active();
    desktop.text("x");
    storage.remove("C:\\Documents\\a.txt");
    storage.make_folder("C:\\Documents\\a.txt"); // a folder now has the file's name
    tasks.hold();
    letter('f', 4);
    letter('s');
    let_jobs_finish(inbox);
    CHECK(!w->locked() && strcmp(title(), "Editor") == 0); // the error message
    key(wm::key::Enter);
    CHECK(strcmp(title(), "a.txt* - Editor") == 0);
    proc::table().signal(w->owner(), proc::Signal::Kill);
    wait(1000);
    CHECK(editors() == 0 && memory.in_use() == blocks);
}

} // namespace

int main()
{
    test_runner();
    test_process_waits_for_jobs();
    test_locked_window();

    static uint8_t disk[256 * 1024];
    static fs::MemoryVolume volume(disk, sizeof(disk), "RAM disk");
    storage.mount('C', volume);
    storage.set_lock(&storage_lock);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(1000);
    msg::set_queue(&queue);
    job::jobs().set_tasks(&tasks);
    test_editor_save();
    job::jobs().set_tasks(nullptr);
    msg::set_queue(nullptr);

    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
