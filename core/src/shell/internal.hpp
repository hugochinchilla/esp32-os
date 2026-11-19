// Shared between the shell's source files.
#pragma once

#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>

#include "core/fs/fs.hpp"
#include "core/os/os.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
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
// Puts the screensaver in place with its settings (screensaver.cpp).
void install_screen_saver();

// Ctrl+Alt+Del (power.cpp): the Task Manager, or a restart from the
// power-down screen.
void attention_key(void *context);

// The Trash (trash.cpp): C:\.trash, with C:\.config\trash.ini remembering
// where each entry came from.
constexpr const char *kTrashFolder = "C:\\.trash";
bool in_trash(const char *path);  // C:\.trash or inside it
bool trashable(const char *path); // on C:, not a drive, not in the Trash
ports::FsError to_trash(const char *path);
ports::FsError remove_tree(const char *path, int depth = 0); // for good
int trash_count();
ports::FsError empty_trash();
bool trash_origin(const char *name, char *out, int capacity);
ports::FsError restore_from_trash(const char *name);
// Drag and drop between folders (same drive); into the Trash trashes.
ports::FsError move_to_folder(const char *path, const char *folder);
// A name for `name` not taken in `folder`: itself, else "name (2).ext"...
bool free_name_in(const char *folder, const char *name, bool is_folder, char *out, int capacity);
// What a file command acts on (backlog items 54 and 55): entries of one
// folder, by name. The names are the caller's and need only last the call.
struct Selection {
    const char *folder;
    const char *const *names;
    int count;
};
// The Delete command: asks once, for all of them, then moves them to the
// Trash (on C:) or deletes them for good (in the Trash, on other drives).
void delete_asking(const char *path);
void delete_asking(const Selection &s);
// Copy and Cut (file_ops.cpp): onto the clipboard, as files.
void copy_files(const Selection &s, bool cut);
// Whether Paste would do anything in `folder`: there are files on the
// clipboard, and it is a folder that takes them (not My Board, not the
// Trash).
bool can_paste(const char *folder);
// Paste: what is on the clipboard is copied into `folder`, or moved there
// if it was cut, by a job of the asker's; a name taken there is numbered,
// nothing is replaced. `window` shows the hourglass meanwhile
// (os::kNoWindow: none does). It says what went wrong, if anything.
void paste_files(const char *folder, int window);
// Dragged to the Trash: moved without asking on C:, asks elsewhere.
void drop_in_trash(const char *path);
void open_trash();
void open_about();   // about.cpp
void open_welcome(); // welcome.cpp
// The folders that changed, as fs::Storage's notices told (desktop.cpp):
// their keys (fs::path::key), or `all` when there were more than fit.
struct FolderKeys {
    uint32_t key[8] = {};
    int count = 0;
    bool all = false;
    // True if `folder` may be one of them.
    bool has(const char *folder) const
    {
        uint32_t k = fs::path::key(folder);
        k = k != 0 ? k : 1;
        for (int i = 0; i < count; i++) {
            if (key[i] == k) {
                return true;
            }
        }
        return all;
    }
};
// The File manager's windows on those folders, or inside them, read their
// folders again (file_manager.cpp). Nobody calls this after moving or
// deleting a file: the notice does.
void refresh_folder_windows(const FolderKeys &changed);

} // namespace shell
