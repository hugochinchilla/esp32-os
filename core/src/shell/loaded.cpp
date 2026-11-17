// Programs that are not part of the firmware (backlog item 27): a file on a
// drive, compiled by itself, that the Loader port makes ready to run. Here
// it gets a process of its own, is entered once and stays in memory until
// that process has ended.
//
// docs/journal/2026-11-17-loaded-programs.md has the story.
#include <cstdio>
#include <cstring>

#include "core/os/os.hpp"
#include "core/prog/prog.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

ports::Loader *g_loader = nullptr;

// What is loaded now, and whose it is.
struct Loaded {
    proc::Pid pid = proc::kNone;
    void *module = nullptr;
    bool inside = false; // its way in has not returned yet
    bool gone = false;   // its process has ended: to be taken out of memory
};
constexpr int kMaxLoaded = 8;
Loaded g_loaded[kMaxLoaded];
bool g_sweep_asked = false;

// Out of memory go the programs whose process has ended. Not from inside
// the end of the process: its last window is destroyed after that, and the
// window's own memory is given back by the program's code.
void sweep(void *)
{
    g_sweep_asked = false;
    for (Loaded &l : g_loaded) {
        if (l.module != nullptr && l.gone && !l.inside) {
            g_loader->unload(l.module);
            l = Loaded();
        }
    }
}

void ask_sweep()
{
    if (g_sweep_asked) {
        return;
    }
    const os::As as_desktop(os::find("desktop"));
    g_sweep_asked = os::start_timer(0, 0, &sweep, nullptr) != 0;
}

void ended(proc::Pid pid, void *)
{
    for (Loaded &l : g_loaded) {
        if (l.module != nullptr && l.pid == pid) {
            l.gone = true;
            ask_sweep();
        }
    }
}

// "C:\Games\hello.app" is the process "hello".
void process_name(const char *path, char *out, size_t capacity)
{
    snprintf(out, capacity, "%s", fs::path::name(path));
    if (char *dot = strrchr(out, '.')) {
        *dot = 0;
    }
}

bool fail(char *why, size_t capacity, const char *text)
{
    snprintf(why, capacity, "%s", text);
    return false;
}

} // namespace

void set_loader(ports::Loader *loader)
{
    g_loader = loader;
    if (loader != nullptr) {
        proc::table().on_exit(&ended, nullptr);
    }
}

bool is_program_file(const char *path)
{
    char ext[8];
    fs::path::extension(path, ext, sizeof(ext));
    return strcmp(ext, "app") == 0;
}

bool run_file(const char *path, char *why, size_t capacity)
{
    const prog::InCall in_call;
    if (g_loader == nullptr) {
        return fail(why, capacity, "this machine cannot load programs");
    }
    Loaded *place = nullptr;
    for (Loaded &l : g_loaded) {
        if (l.module == nullptr) {
            place = &l;
            break;
        }
    }
    if (place == nullptr) {
        return fail(why, capacity, "too many loaded programs are running");
    }
    fs::DirEntry entry;
    const ports::FsError found = os::file::stat(path, entry);
    if (found != ports::FsError::Ok || entry.folder) {
        return fail(why, capacity, fs::error_text(found != ports::FsError::Ok ? found : ports::FsError::IsFolder));
    }
    if (entry.size == 0 || entry.size > kMaxProgramBytes) {
        return fail(why, capacity, entry.size == 0 ? "the file is empty" : "the file is too large to be a program");
    }
    // The file whole, only while it is made ready: what runs is a copy the
    // loader keeps.
    void *image = os::allocate(entry.size);
    if (image == nullptr) {
        return fail(why, capacity, "there is not enough memory");
    }
    size_t size = 0;
    const os::file::Error read = os::file::read_file(path, image, entry.size, size);
    const char *reason = "it is not a program for this machine";
    void *module = read == ports::FsError::Ok ? g_loader->load(static_cast<const uint8_t *>(image), size, reason)
                                              : nullptr;
    os::release(image);
    if (read != ports::FsError::Ok) {
        return fail(why, capacity, fs::error_text(read));
    }
    if (module == nullptr) {
        return fail(why, capacity, reason);
    }
    char name[proc::Process::kNameSize];
    process_name(path, name, sizeof(name));
    const proc::Pid pid = os::spawn(name);
    if (pid == proc::kNone) {
        g_loader->unload(module);
        return fail(why, capacity, "too many programs are running");
    }
    place->pid = pid;
    place->module = module;
    place->gone = false;
    place->inside = true;
    // Its way in, as itself. It opens its window and returns; a program
    // that opens none ends at once and is swept away.
    struct Enter {
        void *module;
        int result;
    } enter = {module, 0};
    in_process(pid, nullptr, [&enter] { enter.result = g_loader->enter(enter.module); });
    place->inside = false;
    if (place->gone) {
        ask_sweep();
    }
    if (enter.result != 0) {
        os::signal(pid, proc::Signal::Kill);
        snprintf(why, capacity, "the program gave up as it started (%d)", enter.result);
        return false;
    }
    return true;
}

} // namespace shell
