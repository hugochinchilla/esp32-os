// Shared between the shell's source files.
#pragma once

#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>

#include "core/fs/fs.hpp"
#include "core/image/image.hpp"
#include "core/job/job.hpp"
#include "core/os/os.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
#include "core/shell/files.hpp"
#include "core/sh/terminal_session.hpp"
#include "core/wm/wm.hpp"

namespace shell {

wm::Wm &manager();
ports::System &system_port();
fs::Storage &storage();

// The process of a program, by its name: the one running, or a new one whose
// parent is the current process (the desktop, or the program that opened
// it). If the table is full, the current process stands in.
proc::Pid process_for(const char *program);
// For a program's way in (launch(), open_in_editor()...): what it opens from
// here to the end of the block is its own, whoever called.
class AsProgram {
public:
    explicit AsProgram(const char *program) : as_(process_for(program)) {}

private:
    os::As as_;
};

// A program's way in when someone else may be the one calling (the desktop
// opening a folder, the File manager opening a file in the Editor, the
// answer to a question coming back): the code runs as the program and, if
// the program has a task of its own, on that task (backlog item 26). The
// callable is handed over by copy, so it holds what it needs by value: a
// path goes in as a PathArg, not as a pointer into the caller. False if the
// program did not come back in time.
//   in_program("Picture viewer", [p] { ... p.get() ... });
// `program` names the row of the program table that says whether there is
// a task and how much stack; nullptr for a process that is not a program's
// own (it runs where its process runs).
bool enter_program(proc::Pid pid, const char *program, proc::CallFn fn, void *context, size_t size);
template <class F> bool in_process(proc::Pid pid, const char *program, F f)
{
    static_assert(std::is_trivially_copyable<F>::value && sizeof(F) <= proc::kCallRoom, "handed over by copy");
    return enter_program(
        pid, program, [](void *context) { (*static_cast<F *>(context))(); }, &f, sizeof(F));
}
template <class F> bool in_program(const char *program, F f)
{
    return in_process(process_for(program), program, f);
}
// A path, or none, held by value.
struct PathArg {
    explicit PathArg(const char *path) : given(path != nullptr)
    {
        snprintf(text, sizeof(text), "%s", given ? path : "");
    }
    const char *get() const { return given ? text : nullptr; }
    char text[fs::kMaxPath];
    bool given;
};

// The open window with this number, nullptr if it has closed. Only for the
// dialogs that are still window classes of the system's kind and must name
// the window they are over; it goes when they are written by number.
wm::Window *window_numbered(int number);
// A window that already has a dialog over it gets that one brought forward
// (true): whoever was about to open another does not.
bool has_dialog(int number);

// Opens a window if it is not open, otherwise brings it to the front.
void show(wm::Window &w);

/* ---- Programs that live only while they are open ----------------------------
 *
 * A program's window, with its widgets and buffers inside it, is made when
 * the program opens and destroyed once it has closed: the window manager
 * calls back at its next safe point (wm::Window::set_dispose), the object's
 * destructor runs and its memory goes back. Nothing of a closed program
 * stays but its code and, for a program that runs once at a time, one
 * pointer. docs/journal/2026-11-17-program-lifecycle.md has the story.
 *
 * The memory is the Memory port's: external RAM on the board (where these
 * objects already lived as fixed ones), zeroed. A window's destructor gives
 * back what the window asked for beyond itself (text, pictures, lists). */

// Zeroed memory for a program, nullptr when there is none; and giving it
// back (nullptr is fine).
void *program_memory(size_t bytes);
void free_program_memory(void *block);
// The same as a Memory port, for code that takes one (the picture decoder).
ports::Memory &program_port();
// "There is not enough memory to open <program>."
void no_memory_for(const char *program);

// The one window of its kind, for a program or dialog that runs once at a
// time: nullptr while it does not exist.
template <class T> T *&the()
{
    static T *window = nullptr;
    return window;
}

template <class T> void destroy_window(wm::Window &w)
{
    T *window = static_cast<T *>(&w);
    if (the<T>() == window) {
        the<T>() = nullptr;
    }
    window->~T();
    free_program_memory(window);
}

// A new window of class T, destroyed by itself after it closes. nullptr
// when there is no memory for it. If it is never opened (the window manager
// was full), give it to discard(), which has it destroyed the same way.
template <class T, class... Args> T *make_window(Args &&...args)
{
    void *place = program_memory(sizeof(T));
    if (place == nullptr) {
        return nullptr;
    }
    T *window = new (place) T(std::forward<Args>(args)...);
    window->set_dispose(&destroy_window<T>);
    return window;
}

template <class T> void discard(T *window)
{
    if (window != nullptr) {
        manager().discard(*window); // nothing happens to an open one
    }
}

// The one window of its kind: the one that exists, or a new one. `program`
// names it in the message when there is no memory (nullptr comes back).
template <class T, class... Args> T *one(const char *program, Args &&...args)
{
    if (the<T>() == nullptr) {
        the<T>() = make_window<T>(std::forward<Args>(args)...);
        if (the<T>() == nullptr) {
            no_memory_for(program);
        }
    }
    return the<T>();
}

// Shows the one window of a simple program: made if need be, opened or
// brought to the front. Use after AsProgram, so the window is the program's.
template <class T> void open_one(const char *program)
{
    if (T *window = one<T>(program)) {
        show(*window);
        if (!window->open()) {
            discard(window); // the window manager is full
        }
    }
}

// An icon in the taskbar's tray: the network's signal next to the clock,
// Bluetooth left of it. Kept while there is no taskbar and shown when
// there is one again.
constexpr int kTrayNetwork = 0, kTrayBluetooth = 1;
void set_tray_icon(int place, const gfx::Bitmap *icon, void (*click)(void *context), void *context);
// The taskbar's process (desktop.cpp): starts it if it is not running, as
// after it ended too often to be restarted. False if it runs.
bool start_taskbar_again();

// What os::accept_open does (loaded.cpp): the program asking takes one more
// thing to open while it runs.
void accept_open(os::OpenFn opened);
// Brings forward the window of `program` that shows `path`
// (wm::Window::document), if there is one.
bool show_document(const char *program, const char *path);

// A terminal session for the process asking (sessions.cpp): what
// os::open_session does.
sh::TerminalSession *open_session(ports::ByteSink &device, const char *&why);

// Starts following the network, if there is one (network.cpp).
void start_network();
// Starts following the Bluetooth keyboards and mice, if there is a radio
// for them (bluetooth_service.cpp).
void start_bluetooth();

// Centres a window of the given size in the work area.
gfx::Rect centred(const wm::Wm &wm, int w, int h);


// Puts the desktop background in place with its settings (display.cpp).
void install_display();
// The desktop's picture (wallpaper.cpp; backlog item 87). install_wallpaper()
// before install_display(), which names the picture the settings hold.
void install_wallpaper();
// What os::set_wallpaper does: the file to show over the desktop's colour,
// "" for none. The UI task. The picture comes when a background job has
// read it; the colour stays if it cannot be shown.
void set_wallpaper(const char *path, os::Wallpaper how);
// A picture in the screen's colours, one byte per pixel, from the Memory
// port: give it back with release_fitted().
struct FittedPicture {
    uint8_t *pixels = nullptr;
    int width = 0, height = 0;
};
// The size of a width x height picture inside max_w x max_h: made smaller
// to fit, keeping its shape, never larger.
void fit_inside(int width, int height, int max_w, int max_h, int &w, int &h);
// Reads a picture file and fits it so, or with `stretch` makes it max_w x
// max_h whatever its shape and size, then makes it `part` / `of` of that
// size (1, 1 for the desktop; less for Display's small screen).
// `background`: behind the clear parts of a PNG or GIF. Any task; `job`, if
// the caller is one, lets the others run meanwhile. Seconds of work for a
// photograph on the board.
image::Error decode_fitted(const char *path, int max_w, int max_h, bool stretch, int part, int of,
                           const uint8_t background[3], const job::Job *job, FittedPicture &out);
void release_fitted(FittedPicture &picture);
// The file beside `path` made for a screen of this size, if `path` is named
// for a screen: "C:\pictures\Mir 800x600.jpg" and 640 x 480 give
// "C:\pictures\Mir 640x480.jpg". False if it is not named so.
bool wallpaper_for_screen(const char *path, int width, int height, char *out, int capacity);
// The file the desktop shows for `path` on such a screen: that one if it is
// on the drive, else `path` itself.
void wallpaper_file(const char *path, int width, int height, char *out, int capacity);
// Puts the screensaver in place with its settings (screensaver.cpp).
void install_screen_saver();

// Ctrl+Alt+Del (power.cpp): the Task Manager, or a restart from the
// power-down screen.
void attention_key(void *context);

// Dragged to the Trash: moved without asking on C:, asks elsewhere.
void drop_in_trash(const char *path);
void open_trash();
void open_about();   // about.cpp
void open_welcome(); // welcome.cpp
// The folders that changed, as fs::Storage's notices told (desktop.cpp).
using FolderKeys = os::file::Changes;
// What os::file::watch does (loaded.cpp): the program asking is told of
// the folders that change from now on; tell_watchers() tells them all,
// each as itself.
void watch_files(os::file::ChangedFn changed);
void tell_watchers(const FolderKeys &changed);
// The same for the desktop's picture: it is looked for again if its folder
// is among them.
void wallpaper_files_changed(const FolderKeys &changed);

} // namespace shell
