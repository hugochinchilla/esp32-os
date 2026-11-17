// Shared between the shell's source files.
#pragma once

#include <new>
#include <utility>

#include "core/fs/fs.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
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
    explicit AsProgram(const char *program) : scope_(&proc::table(), process_for(program)) {}

private:
    proc::Scope scope_;
};

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

// The icon in the taskbar's tray (the network's signal). Kept while there
// is no taskbar and shown when there is one again.
void set_tray_icon(const gfx::Bitmap *icon, void (*click)(void *context), void *context);
// The taskbar's process (desktop.cpp): starts it if it is not running, as
// after it ended too often to be restarted. False if it runs.
bool start_taskbar_again();

// Starts following the network, if there is one (network.cpp).
void start_network();

// Centres a window of the given size in the work area.
gfx::Rect centred(const wm::Wm &wm, int w, int h);

// Cactus Patch (cactus_patch.cpp): its Start menu entry.
void open_cactus_patch();

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
// The Delete command: asks, then moves to the Trash (on C:) or deletes for
// good (in the Trash, on other drives).
void delete_asking(const char *path);
// Dragged to the Trash: moved without asking on C:, asks elsewhere.
void drop_in_trash(const char *path);
void open_trash();
// After files moved or went away: the desktop, the File manager windows
// and the Trash window read their folders again.
void files_changed();
void refresh_folder_windows(); // file_manager.cpp

} // namespace shell
