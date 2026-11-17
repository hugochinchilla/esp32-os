// The OS calls (core/os/os.hpp): each one a few lines over the window
// manager, the drives, the process table or a port. The point is not what
// they do but that programs come through here.
#include "core/os/os.hpp"

#include <cstdio>

#include "core/saver/saver.hpp"

namespace os {

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

void install(const Kit &kit) { g_kit = kit; }
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

Error open(const char *path, ports::OpenMode mode, fs::File &file) { return g_kit.storage->open(path, mode, file); }
Error read(fs::File &file, void *data, size_t size, size_t &done)
{
    return g_kit.storage->read(file, data, size, done);
}
Error write(fs::File &file, const void *data, size_t size) { return g_kit.storage->write(file, data, size); }
Error close(fs::File &file) { return g_kit.storage->close(file); }
Error begin_save(const char *path, fs::Save &save) { return g_kit.storage->begin_save(path, save); }
Error finish_save(fs::Save &save, bool keep) { return g_kit.storage->finish_save(save, keep); }
Error read_file(const char *path, void *data, size_t capacity, size_t &size)
{
    return g_kit.storage->read_file(path, data, capacity, size);
}
Error write_file(const char *path, const void *data, size_t size)
{
    return g_kit.storage->write_file(path, data, size);
}

const fs::Storage &storage() { return *g_kit.storage; }

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
