// Programs that are not part of the firmware (backlog item 27): a file on a
// drive, compiled by itself, that the Loader port makes ready to run. Here
// it gets a process of its own, is entered once and stays in memory until
// that process has ended.
//
// docs/journal/2026-11-17-loaded-programs.md has the story.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/app/file.hpp"
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
    int32_t bytes = 0;               // counted as the process's memory
    os::OpenFn opened = nullptr;     // os::accept_open: it takes one more thing to open while it runs
    char *words = nullptr;           // its file's path and what it was asked to open: argv
    char *argv[3] = {};
    int result = 0;                  // what its way in returned
    std::atomic<bool> inside{false}; // its way in has not returned yet
    bool gone = false;               // its process has ended: to be taken out of memory
};
constexpr int kMaxLoaded = 16;
// The header of the file being made ready: too large for a program's stack,
// and one file is made ready at a time.
app::FileInfo g_info;
Loaded g_loaded[kMaxLoaded];
bool g_sweep_asked = false;
constexpr uint32_t kSweepAgainUs = 100 * 1000;

void ask_sweep(uint32_t delay_us = 0);

// Out of memory go the programs whose process has ended. Not from inside
// the end of the process: its last window is destroyed after that, and the
// window's own memory is given back by the program's code. And not while a
// task of the program is still there (one that ended while it was late
// leaves when it can): the sweep comes back for those.
void sweep(void *)
{
    g_sweep_asked = false;
    bool again = false;
    for (Loaded &l : g_loaded) {
        if (l.module == nullptr) {
            continue;
        }
        if (l.inside.load()) {
            again = true; // late in its way in
            continue;
        }
        if (!l.gone && l.result != 0) {
            // Its way in gave up after the caller had stopped waiting.
            os::signal(l.pid, proc::Signal::Kill);
            l.gone = true;
        }
        if (!l.gone) {
            continue;
        }
        if (prog::runner().task_left(l.pid)) {
            again = true;
            continue;
        }
        proc::table().add_memory(l.pid, -l.bytes);
        g_loader->unload(l.module);
        os::release(l.words);
        l.words = nullptr;
        l.opened = nullptr;
        l.pid = proc::kNone;
        l.module = nullptr;
        l.bytes = 0;
        l.result = 0;
        l.gone = false;
    }
    if (again) {
        ask_sweep(kSweepAgainUs);
    }
}

void ask_sweep(uint32_t delay_us)
{
    if (g_sweep_asked) {
        return;
    }
    const os::As as_desktop(os::find("desktop"));
    g_sweep_asked = os::start_timer(delay_us, 0, &sweep, nullptr) != 0;
}

void ended(proc::Pid pid, void *)
{
    for (Loaded &l : g_loaded) {
        if (l.module != nullptr && l.pid == pid) {
            l.gone = true;
            // Its arguments go with its process; its code leaves later.
            os::release(l.words);
            l.words = nullptr;
            l.opened = nullptr;
            ask_sweep();
        }
    }
}

// The process has the name the program gives itself; one that gives none
// has its file's: "C:\Games\hello.app" is "hello".
void process_name(const char *path, const app::FileInfo &info, char *out, size_t capacity)
{
    if (info.title[0] != 0) {
        // As much of it as a process's name holds.
        const int room = static_cast<int>(capacity) - 1;
        snprintf(out, capacity, "%.*s", room < app::FileInfo::kTitleSize - 1 ? room : app::FileInfo::kTitleSize - 1,
                 info.title);
        return;
    }
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

void accept_open(os::OpenFn opened)
{
    const proc::Pid pid = os::self();
    for (Loaded &l : g_loaded) {
        if (l.module != nullptr && l.pid == pid) {
            l.opened = opened;
        }
    }
}

bool show_document(const char *program, const char *path)
{
    if (path == nullptr || path[0] == 0) {
        return false;
    }
    wm::Wm &wm = manager();
    for (int z = wm.window_count() - 1; z >= 0; z--) {
        wm::Window *w = wm.window_at(z);
        if (w == nullptr || strcasecmp(w->document(), path) != 0) {
            continue;
        }
        const proc::Process *owner = os::process(w->owner());
        if (owner != nullptr && strcmp(owner->name, program) == 0) {
            os::show(*w);
            return true;
        }
    }
    return false;
}

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

bool program_info(const char *path, app::FileInfo &info)
{
    const prog::InCall in_call;
    os::file::Handle file = os::file::kNoFile;
    if (os::file::open(path, ports::OpenMode::Read, file) != ports::FsError::Ok) {
        return false;
    }
    size_t got = 0;
    const os::file::Error read = os::file::read(file, &info, sizeof(info), got);
    os::file::close(file);
    return read == ports::FsError::Ok && got == sizeof(info) && app::is_program(info);
}

bool run_file(const char *path, char *why, size_t capacity, const char *argument)
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
    // It begins with the header (core/app/file.hpp); the code is the rest.
    app::FileInfo &info = g_info;
    const bool ours = read == ports::FsError::Ok && size > sizeof(info);
    if (ours) {
        memcpy(&info, image, sizeof(info));
    }
    // What it is asked to open may be on the screen already, in a window
    // of this program: that window comes forward and nothing is loaded.
    if (ours && app::is_program(info)) {
        char name[proc::Process::kNameSize];
        process_name(path, info, name, sizeof(name));
        if (show_document(name, argument)) {
            os::release(image);
            return true;
        }
    }
    // One copy at a time unless the program says otherwise: opened again,
    // the one that runs comes forward, and is told what to open if it
    // takes that (os::accept_open).
    if (ours && app::is_program(info) && info.many == 0) {
        char name[proc::Process::kNameSize];
        process_name(path, info, name, sizeof(name));
        const proc::Pid running = proc::table().find_by_name(name);
        wm::Window *window = running != proc::kNone ? manager().main_window_of(running) : nullptr;
        if (window != nullptr) {
            os::release(image);
            os::activate(*window);
            os::OpenFn opened = nullptr;
            for (const Loaded &l : g_loaded) {
                if (l.module != nullptr && l.pid == running) {
                    opened = l.opened;
                }
            }
            if (opened != nullptr && argument != nullptr) {
                const PathArg words(argument);
                in_process(running, nullptr, [opened, words] { opened(words.get()); });
            }
            return true;
        }
    }
    void *module = ours && app::is_program(info)
                       ? g_loader->load(static_cast<const uint8_t *>(image) + sizeof(info), size - sizeof(info), reason)
                       : nullptr;
    os::release(image);
    if (read != ports::FsError::Ok) {
        return fail(why, capacity, fs::error_text(read));
    }
    if (module == nullptr) {
        return fail(why, capacity, reason);
    }
    char name[proc::Process::kNameSize];
    process_name(path, info, name, sizeof(name));
    // Its arguments, kept while it is loaded: the file, then what to open.
    const size_t path_size = strlen(path) + 1, argument_size = argument != nullptr ? strlen(argument) + 1 : 0;
    const proc::Pid pid = os::spawn(name);
    if (pid == proc::kNone) {
        g_loader->unload(module);
        return fail(why, capacity, "too many programs are running");
    }
    char *words = nullptr;
    {
        const os::As as_program(pid); // counted as the program's own
        words = static_cast<char *>(os::allocate(path_size + argument_size));
    }
    if (words == nullptr) {
        os::signal(pid, proc::Signal::Kill);
        g_loader->unload(module);
        return fail(why, capacity, "there is not enough memory");
    }
    memcpy(words, path, path_size);
    if (argument != nullptr) {
        memcpy(words + path_size, argument, argument_size);
    }
    place->words = words;
    place->argv[0] = words;
    place->argv[1] = argument != nullptr ? words + path_size : nullptr;
    place->argv[2] = nullptr;
    place->pid = pid;
    place->module = module;
    place->bytes = static_cast<int32_t>(g_loader->bytes(module));
    place->result = 0;
    place->gone = false;
    place->inside.store(true);
    // What the program takes in memory is its process's, as its windows are.
    proc::table().add_memory(pid, place->bytes);
    // A program has a task of its own where the machine has tasks (backlog
    // item 26), with the stack its header asks for. Without one (none here,
    // none left) it runs on the caller's.
    prog::runner().start(pid, info.stack);
    // Its way in, as itself. It opens its window and returns; a program
    // that opens none ends at once and is swept away. One that is late
    // coming back is left to it: the sweep sees how it went.
    in_process(pid, nullptr, [place] {
        place->result = g_loader->enter(place->module, place->argv[1] != nullptr ? 2 : 1, place->argv);
        place->inside.store(false);
    });
    if (place->inside.load()) {
        ask_sweep(kSweepAgainUs);
        return true;
    }
    if (place->gone) {
        ask_sweep();
    }
    const int result = place->result;
    place->result = 0;
    if (result != 0) {
        os::signal(pid, proc::Signal::Kill);
        snprintf(why, capacity, "the program gave up as it started (%d)", result);
        return false;
    }
    return true;
}

} // namespace shell
