// The OS calls (core/os/os.hpp): each one a few lines over the window
// manager, the drives, the process table or a port. The point is not what
// they do but that programs come through here.
#include "core/os/os.hpp"

#include <atomic>
#include <cstdio>
#include <new>

#include "core/saver/saver.hpp"
#include "core/sys/log.hpp"

namespace os {

namespace file {
void installed();
}

namespace {

Kit g_kit;
uint32_t g_blocks = 0;
uint32_t g_bytes = 0;
net::Manager *g_network = nullptr;
uint32_t g_saver_wait = 0; // seconds; 0: no screensaver is set

// The screensaver: one object for good, a few words when it is not running
// (its frame is the window manager's).
saver::Runner &saver_runner()
{
    static saver::Runner runner(*g_kit.wm);
    return runner;
}

// In front of every block: whose it is and how large, for when it is given
// back (which may be a moment after the process's row has gone: add_memory()
// ignores that).
struct BlockNote {
    proc::Pid owner;
    uint32_t bytes;
    uint32_t pad[2]; // 16 bytes in all: what follows stays aligned as the port gave it
};

void settings_path(const char *program, char *out, size_t n)
{
    snprintf(out, n, "C:\\.config\\%s.ini", program);
}

} // namespace

void install(const Kit &kit)
{
    g_kit = kit;
    file::installed();
}
void install_network(net::Manager *wifi) { g_network = wifi; }

/* ---- The process ------------------------------------------------------------ */

proc::Pid self() { return proc::table().current(); }

proc::Pid spawn(const char *name)
{
    proc::Table &t = proc::table();
    return t.spawn(name, t.current(), proc::kWindowed);
}

proc::Pid find(const char *name) { return proc::table().find_by_name(name); }

As::As(proc::Pid pid) : scope_(&proc::table(), pid) {}

int process_count() { return proc::table().count(); }
const proc::Process &process_at(int index) { return proc::table().at(index); }
const proc::Process *process(proc::Pid pid) { return proc::table().find(pid); }
bool alive(proc::Pid pid) { return proc::table().alive(pid); }
void refresh_figures() { proc::table().bring_up_to_date(); }
proc::Sent signal(proc::Pid pid, proc::Signal signal) { return proc::table().signal(pid, signal); }

/* ---- Time --------------------------------------------------------------------- */

uint64_t now_us() { return g_kit.wm->now_us(); }

int start_timer(uint32_t delay_us, uint32_t period_us, wm::Wm::TimerFn fn, void *context)
{
    return g_kit.wm->start_timer(delay_us, period_us, fn, context);
}

void stop_timer(int id) { g_kit.wm->stop_timer(id); }

/* ---- Memory ------------------------------------------------------------------- */

void *allocate(size_t bytes)
{
    if (g_kit.memory == nullptr) {
        return nullptr;
    }
    auto *note = static_cast<BlockNote *>(g_kit.memory->allocate(bytes + sizeof(BlockNote)));
    if (note == nullptr) {
        return nullptr;
    }
    note->owner = proc::table().current();
    note->bytes = static_cast<uint32_t>(bytes);
    proc::table().add_memory(note->owner, static_cast<int32_t>(bytes));
    g_blocks++;
    g_bytes += note->bytes;
    return note + 1;
}

void release(void *block)
{
    if (block != nullptr) {
        BlockNote *note = static_cast<BlockNote *>(block) - 1;
        proc::table().add_memory(note->owner, -static_cast<int32_t>(note->bytes));
        g_blocks--;
        g_bytes -= note->bytes;
        g_kit.memory->release(note);
    }
}

ports::Memory &memory()
{
    class Port : public ports::Memory {
    public:
        void *allocate(size_t bytes) override { return os::allocate(bytes); }
        void release(void *block) override { os::release(block); }
    };
    static Port port;
    return port;
}

uint32_t blocks_out() { return g_blocks; }
uint32_t bytes_out() { return g_bytes; }

/* ---- Windows ------------------------------------------------------------------ */

void show(wm::Window &window)
{
    if (window.open()) {
        g_kit.wm->activate(window);
    } else {
        g_kit.wm->add(window);
    }
}

void activate(wm::Window &window) { g_kit.wm->activate(window); }
void move(wm::Window &window, const gfx::Rect &frame) { g_kit.wm->resize(window, frame); }
gfx::Rect work_area() { return g_kit.wm->work_area(); }

gfx::Rect centred(int w, int h)
{
    const gfx::Rect area = g_kit.wm->work_area();
    return {(area.w - w) / 2, (area.h - h) / 2, w, h};
}

bool menu_open() { return g_kit.wm->menu_open(); }

void open_menu(wm::Menu &menu, int x, int y) { g_kit.wm->open_menu(menu, x, y); }
wm::Window *blocker(const wm::Window &window) { return g_kit.wm->blocker(window); }
bool in_front(const wm::Window &window) { return g_kit.wm->active() == &window; }
void caret_moved() { g_kit.wm->caret_moved(); }
wm::Window *main_window(proc::Pid pid) { return g_kit.wm->main_window_of(pid); }
void begin_drag(const char *path) { g_kit.wm->begin_drag(path); }

Busy::Busy() { g_kit.wm->begin_wait(); }
Busy::~Busy() { g_kit.wm->end_wait(); }

/* ---- The desktop --------------------------------------------------------------- */

void set_backdrop(const wm::Wm::Backdrop &backdrop) { g_kit.wm->set_backdrop(backdrop); }

void set_screen_saver(int animation, uint32_t wait_seconds)
{
    if (animation < 0 || animation >= saver::kCount || wait_seconds == 0) {
        g_saver_wait = 0;
        g_kit.wm->set_screen_saver(nullptr, 0);
        return;
    }
    g_saver_wait = wait_seconds;
    saver_runner().select(animation);
    g_kit.wm->set_screen_saver(&saver_runner(), wait_seconds);
}

void preview_screen_saver(int animation)
{
    if (animation < 0 || animation >= saver::kCount) {
        return;
    }
    saver_runner().select(animation);
    g_kit.wm->set_screen_saver(&saver_runner(), g_saver_wait); // a wait of 0: only when asked, as here
    g_kit.wm->start_screen_saver();
}

SaverRun last_saver_run()
{
    const saver::Runner &r = saver_runner();
    return {r.frames(), r.shown_us(), r.slowest_us()};
}

bool shut_down() { return g_kit.shut_down != nullptr && g_kit.shut_down(); }

/* ---- The network ---------------------------------------------------------------- */

net::Manager *network() { return g_network; }

/* ---- Terminal sessions ----------------------------------------------------------- */

sh::TerminalSession *open_session(ports::ByteSink &device, const char *&why)
{
    if (g_kit.open_session == nullptr) {
        why = "This machine has no command shell.";
        return nullptr;
    }
    return g_kit.open_session(device, why);
}

/* ---- Files --------------------------------------------------------------------- */

namespace file {

bool has_drive(char letter) { return g_kit.storage->volume(letter) != nullptr; }
int drives(char *letters, int capacity) { return g_kit.storage->drives(letters, capacity); }
Error drive_info(char letter, fs::VolumeInfo &out) { return g_kit.storage->info(letter, out); }
Error stat(const char *path, fs::DirEntry &out) { return g_kit.storage->stat(path, out); }
Error entry(const char *folder, int index, fs::DirEntry &out) { return g_kit.storage->entry(folder, index, out); }
Error make_folder(const char *path) { return g_kit.storage->make_folder(path); }
Error remove(const char *path) { return g_kit.storage->remove(path); }
Error rename(const char *from, const char *to) { return g_kit.storage->rename(from, to); }

/* The table of open files. A slot is claimed with its `used` flag alone, so
 * the UI task and a job's task can each take one without a lock; after that
 * a slot is its holder's until it is closed. The system closes what a
 * process left (closed_for) on the UI task, when the process has no jobs
 * any more. */
namespace {

struct Open {
    std::atomic<bool> used{false};
    proc::Pid owner = proc::kNone;
    uint16_t serial = 0; // tells this use of the slot from the one before
    bool saving = false;
    fs::Save save;       // save.file alone for a file that was just opened
};

Open *g_open = nullptr; // kMaxOpen of them, once there is memory

// The slot in the low byte, counted from 1; which use of it above.
Handle handle_of(const Open &o) { return (static_cast<Handle>(o.serial) << 8) | static_cast<Handle>(&o - g_open + 1); }

Open *held(Handle handle)
{
    const int slot = (handle & 0xFF) - 1;
    if (g_open == nullptr || handle <= 0 || slot < 0 || slot >= kMaxOpen) {
        return nullptr;
    }
    Open &o = g_open[slot];
    return o.used && o.serial == (handle >> 8) ? &o : nullptr;
}

Open *claim(const job::Job *job)
{
    const proc::Pid owner = job != nullptr ? job->owner() : proc::table().current();
    if (g_open == nullptr || open_files(owner) >= kMaxOpenEach) {
        return nullptr;
    }
    for (int i = 0; i < kMaxOpen; i++) {
        Open &o = g_open[i];
        bool free = false;
        if (o.used.compare_exchange_strong(free, true)) {
            o.owner = owner;
            o.serial = o.serial >= 0x7FFF ? 1 : o.serial + 1;
            o.saving = false;
            o.save = fs::Save();
            return &o;
        }
    }
    return nullptr;
}

void give_back(Open &o)
{
    o.owner = proc::kNone;
    o.used = false;
}

// A process has ended: what it left open.
void closed_for(proc::Pid pid, void *)
{
    int count = 0;
    for (int i = 0; g_open != nullptr && i < kMaxOpen; i++) {
        Open &o = g_open[i];
        if (!o.used || o.owner != pid) {
            continue;
        }
        if (o.saving) {
            g_kit.storage->finish_save(o.save, false);
        } else {
            g_kit.storage->close(o.save.file);
        }
        give_back(o);
        count++;
    }
    if (count > 0) {
        char line[64];
        const int n = snprintf(line, sizeof(line), "os: closed %d file%s process %d left open\n", count,
                               count == 1 ? "" : "s", pid);
        sys::log().write(line, static_cast<size_t>(n));
    }
}

} // namespace

// The table, in large memory, the first time there is some; and the system
// is told of every process that ends.
void installed()
{
    if (g_open == nullptr && g_kit.memory != nullptr) {
        void *room = g_kit.memory->allocate(kMaxOpen * sizeof(Open));
        if (room != nullptr) {
            g_open = new (room) Open[kMaxOpen];
        }
    }
    proc::table().on_exit(&closed_for, nullptr);
}

int open_files(proc::Pid pid)
{
    int count = 0;
    for (int i = 0; g_open != nullptr && i < kMaxOpen; i++) {
        count += g_open[i].used && (pid == proc::kNone || g_open[i].owner == pid);
    }
    return count;
}

Error open(const char *path, ports::OpenMode mode, Handle &file, const job::Job *job)
{
    file = kNoFile;
    Open *o = claim(job);
    if (o == nullptr) {
        return Error::TooManyOpen;
    }
    const Error e = g_kit.storage->open(path, mode, o->save.file);
    if (e != Error::Ok) {
        give_back(*o);
        return e;
    }
    file = handle_of(*o);
    return Error::Ok;
}

Error read(Handle file, void *data, size_t size, size_t &done)
{
    done = 0;
    Open *o = held(file);
    return o != nullptr ? g_kit.storage->read(o->save.file, data, size, done) : Error::Io;
}

Error write(Handle file, const void *data, size_t size)
{
    Open *o = held(file);
    return o != nullptr ? g_kit.storage->write(o->save.file, data, size) : Error::Io;
}

Error seek(Handle file, uint32_t offset)
{
    Open *o = held(file);
    return o != nullptr ? g_kit.storage->seek(o->save.file, offset) : Error::Io;
}

Error close(Handle file)
{
    Open *o = held(file);
    if (o == nullptr) {
        return Error::Io;
    }
    const Error e = o->saving ? g_kit.storage->finish_save(o->save, false) : g_kit.storage->close(o->save.file);
    give_back(*o);
    return e;
}

Error begin_save(const char *path, Handle &save, const job::Job *job)
{
    save = kNoFile;
    Open *o = claim(job);
    if (o == nullptr) {
        return Error::TooManyOpen;
    }
    const Error e = g_kit.storage->begin_save(path, o->save);
    if (e != Error::Ok) {
        give_back(*o);
        return e;
    }
    o->saving = true;
    save = handle_of(*o);
    return Error::Ok;
}

Error finish_save(Handle save, bool keep)
{
    Open *o = held(save);
    if (o == nullptr || !o->saving) {
        return Error::Io;
    }
    const Error e = g_kit.storage->finish_save(o->save, keep);
    give_back(*o);
    return e;
}

Error read_file(const char *path, void *data, size_t capacity, size_t &size)
{
    return g_kit.storage->read_file(path, data, capacity, size);
}
Error write_file(const char *path, const void *data, size_t size)
{
    return g_kit.storage->write_file(path, data, size);
}

} // namespace file

/* ---- Settings ------------------------------------------------------------------ */

bool load_settings(const char *program, fs::Ini &ini)
{
    char path[fs::kMaxPath];
    settings_path(program, path, sizeof(path));
    return ini.load(*g_kit.storage, path);
}

bool save_settings(const char *program, const fs::Ini &ini)
{
    g_kit.storage->make_folder("C:\\.config"); // already there is fine
    char path[fs::kMaxPath];
    settings_path(program, path, sizeof(path));
    return ini.save(*g_kit.storage, path) == ports::FsError::Ok;
}

/* ---- The machine ---------------------------------------------------------------- */

void restart() { g_kit.system->reboot(); }
void stats(ports::SystemStats &out) { g_kit.system->stats(out); }
bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity)
{
    return g_kit.system->fact(which, value, text, capacity);
}
uint32_t busy_us() { return g_kit.wm->busy_us(); }

} // namespace os
