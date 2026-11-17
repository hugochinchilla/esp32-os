// The OS calls: what a program may ask of the system, in one place.
//
// A program is a window class with its widgets (core/wm) and the calls in
// this file. It does not reach the window manager, the drives, the process
// table or the hardware ports by themselves: it asks here, and here is
// where the system knows who is asking (the current process) and can count,
// refuse or limit. docs/os-calls.md has the list with its reasons, and
// tools/check_os_calls.py the programs that keep to it already (backlog
// item 20: they move one by one).
//
// Every call is for the UI task unless it says otherwise, as the window
// manager's are. The file calls may also be made from a background job
// (core/job): the drives have their own lock.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/fs/ini.hpp"
#include "core/net/wifi.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
#include "core/sh/terminal_session.hpp"
#include "core/wm/wm.hpp"

namespace os {

/* ---- The process ------------------------------------------------------------ */

// Who is asking: the process the UI task is working for now.
proc::Pid self();
// A new process, child of the one asking; proc::kNone when the table is
// full. Windowed: it ends when its last window closes.
proc::Pid spawn(const char *name);
// The process with this name (proc::kNone: there is none).
proc::Pid find(const char *name);
// From here to the end of the block the one asking is `pid`: what is opened
// or asked for is that process's. For a program's way in, and for a dialog
// that starts something on behalf of the desktop.
class As {
public:
    explicit As(proc::Pid pid);

private:
    proc::Scope scope_;
};

// The process list, for the tools that show it or act on it (the Task
// Manager). Rows in order of creation; a row is valid until the next event.
int process_count();
const proc::Process &process_at(int index);
const proc::Process *process(proc::Pid pid); // nullptr: there is none
bool alive(proc::Pid pid);
// Processor time and memory of jobs still running, brought up to now.
void refresh_figures();
// Asks a process to close (Term) or ends it (Kill). The answer says if it
// may be signalled at all.
proc::Sent signal(proc::Pid pid, proc::Signal signal);

/* ---- Time --------------------------------------------------------------------- */

uint64_t now_us(); // since start-up; does not wrap
// A timer of the process asking, stopped by the system when the process
// ends. A period of 0 fires once. 0: there is no room.
int start_timer(uint32_t delay_us, uint32_t period_us, wm::Wm::TimerFn fn, void *context);
void stop_timer(int id);

/* ---- Memory ------------------------------------------------------------------- */

// Zeroed memory, counted as the asking process's (what Task Manager and top
// show); nullptr when there is none. release(nullptr) is fine.
void *allocate(size_t bytes);
void release(void *block);
// The same two calls as a Memory port, for code that takes one.
ports::Memory &memory();
// How many blocks are out and how many bytes, over all processes.
uint32_t blocks_out();
uint32_t bytes_out();

/* ---- Windows ------------------------------------------------------------------ */

// Opens a window as the asking process's, or brings it to the front if it
// is open.
void show(wm::Window &window);
void activate(wm::Window &window);
void move(wm::Window &window, const gfx::Rect &frame);
gfx::Rect work_area();          // the screen without the taskbar
gfx::Rect centred(int w, int h); // a frame of that size in the middle of it
bool menu_open();
void open_menu(wm::Menu &menu, int x, int y); // a free-standing menu at (x, y)
// The dialog that keeps `window` from being used now; nullptr: none.
wm::Window *blocker(const wm::Window &window);
bool in_front(const wm::Window &window); // it is the window in use
// The text cursor of the window in use has moved: it is shown at once, and
// its blink starts again.
void caret_moved();
// The main window of a process; nullptr: it has none.
wm::Window *main_window(proc::Pid pid);
// A file or folder starts being dragged (the File manager).
void begin_drag(const char *path);
// The hourglass while slow work runs on the UI task:
//     const os::Busy busy;
class Busy {
public:
    Busy();
    ~Busy();
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;
};

/* ---- The desktop --------------------------------------------------------------- */

// What the settings tools set (Display, Screen Saver) and Shut Down asks.

// The desktop's background: a colour and an 8x8 pattern over it.
void set_backdrop(const wm::Wm::Backdrop &backdrop);
// The screensaver: which animation (a place in core/saver's list; -1: none)
// and after how many seconds without a key or the mouse.
void set_screen_saver(int animation, uint32_t wait_seconds);
// Shows an animation now, until a key or the mouse. It stays the one in
// place: set_screen_saver() puts the chosen one back.
void preview_screen_saver(int animation);
// How the last run went, a preview or the real thing.
struct SaverRun {
    uint32_t frames;     // drawn
    uint64_t shown_us;   // how long it showed
    uint32_t slowest_us; // the slowest frame
};
SaverRun last_saver_run();
// Asks every program to close and, if all did, shows the screen that says
// the power can go. False: one of them has a question open, and it is
// brought forward.
bool shut_down();

/* ---- The network ---------------------------------------------------------------- */

// The Wi-Fi service: the networks in range, connecting, the time zone.
// nullptr: this board has none. A program asks each time and does not keep
// the pointer.
net::Manager *network();

/* ---- Terminal sessions ----------------------------------------------------------- */

// A command shell on a TTY of its own, as a new process `sh`, child of the
// one asking (core/sh/terminal_session.hpp). The one asking is its device:
// what the terminal shows is written to `device`, and the keys go in with
// the session's typed(). nullptr: it could not be started, and `why` says
// why in words for a message box.
//
// The session is the system's. The device hangs it up when it goes
// (hang_up()) and from then on does not use the pointer; if the session
// ends first it says so (on_ended()), and the pointer is dead after that.
sh::TerminalSession *open_session(ports::ByteSink &device, const char *&why);

/* ---- Files --------------------------------------------------------------------- */

namespace file {

using Error = ports::FsError;

bool has_drive(char letter);
int drives(char *letters, int capacity);
Error drive_info(char letter, fs::VolumeInfo &out);
Error stat(const char *path, fs::DirEntry &out);
Error entry(const char *folder, int index, fs::DirEntry &out);
Error make_folder(const char *path);
Error remove(const char *path);
Error rename(const char *from, const char *to); // same drive only

Error open(const char *path, ports::OpenMode mode, fs::File &file);
Error read(fs::File &file, void *data, size_t size, size_t &done);
Error write(fs::File &file, const void *data, size_t size);
Error close(fs::File &file);
// Saving without putting the old file at risk (fs::Storage::begin_save).
Error begin_save(const char *path, fs::Save &save);
Error finish_save(fs::Save &save, bool keep = true);
// Whole files at once; write_file saves the safe way too.
Error read_file(const char *path, void *data, size_t capacity, size_t &size);
Error write_file(const char *path, const void *data, size_t size);

// The drives themselves, for library code that reads a file on its own
// (the picture decoder). A program passes it on and does not keep it.
const fs::Storage &storage();

} // namespace file

/* ---- Settings ------------------------------------------------------------------ */

// A program's own settings file, C:\.config\<program>.ini. load: false when
// there is none (the defaults apply). save: makes C:\.config if needed.
bool load_settings(const char *program, fs::Ini &ini);
bool save_settings(const char *program, const fs::Ini &ini);

/* ---- The machine ---------------------------------------------------------------- */

void restart();
// Figures for the tools that show them: memory free, tasks; and facts that
// do not change (the processor, the RAM fitted).
void stats(ports::SystemStats &out);
bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity);
// How long the UI task has worked since start-up, in µs (wraps).
uint32_t busy_us();

/* ---- For the system, not for programs ------------------------------------------ */

struct Kit {
    wm::Wm *wm = nullptr;
    ports::System *system = nullptr;
    fs::Storage *storage = nullptr;
    ports::Memory *memory = nullptr;
    bool (*shut_down)() = nullptr; // closes every program and takes the screen
    sh::TerminalSession *(*open_session)(ports::ByteSink &device, const char *&why) = nullptr;
};
// What the calls work with. The desktop hands it in when it starts.
void install(const Kit &kit);
// The Wi-Fi service, when the board has one (before or after install()).
void install_network(net::Manager *wifi);

} // namespace os
