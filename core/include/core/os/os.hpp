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
#include "core/job/job.hpp"
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
    ~As();
    As(const As &) = delete;
    As &operator=(const As &) = delete;

private:
    proc::Pid before_;
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
// Another process's window is a number, never its object (backlog item 37):
// the number is given when a window opens and not given again, so one that
// has closed is simply not there. kNoWindow: none.
using WindowId = int;
constexpr WindowId kNoWindow = 0;
// The main window of a process.
WindowId main_window(proc::Pid pid);
// Its title ("" once it has closed) and its icon (nullptr: none, or closed).
const char *window_title(WindowId window);
const gfx::Bitmap *window_icon(WindowId window);
// Brings it to the front, or the dialog that holds it. False: it has closed.
bool bring_forward(WindowId window);
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

// An open file is a number, a handle, and the system keeps the table of
// which process holds which (backlog item 37, first step). What a process
// leaves open when it ends is closed for it, and a save it had not finished
// is thrown away, the old file staying as it was. A handle that is closed,
// or was never given, is refused (Error::Io).
using Handle = int;
constexpr Handle kNoFile = 0;
// ponytail: how many files may be open through these calls at once, in the
// whole machine and by one process. No program holds more than one today,
// and a drive itself keeps six open at most.
constexpr int kMaxOpen = 6;
constexpr int kMaxOpenEach = 2;

// `job`: from a background job, which runs on another task, the job says
// whose the file is. On the UI task leave it out: it is the one asking.
// Error::TooManyOpen when the process, or the machine, has its share.
Error open(const char *path, ports::OpenMode mode, Handle &file, const job::Job *job = nullptr);
Error read(Handle file, void *data, size_t size, size_t &done);
Error write(Handle file, const void *data, size_t size);
// The place the next read() starts at, in bytes from the start of the file.
Error seek(Handle file, uint32_t offset);
Error close(Handle file);
// Saving without putting the old file at risk (fs::Storage::begin_save):
// write() to the handle, then finish_save(), which closes it. keep false
// throws the new text away.
Error begin_save(const char *path, Handle &save, const job::Job *job = nullptr);
Error finish_save(Handle save, bool keep = true);
// How many files a process holds open; every process for proc::kNone.
int open_files(proc::Pid pid = proc::kNone);
// Whole files at once; write_file saves the safe way too.
Error read_file(const char *path, void *data, size_t capacity, size_t &size);
Error write_file(const char *path, const void *data, size_t size);

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
const Kit &installed_kit(); // for the other files of core/os
// The Wi-Fi service, when the board has one (before or after install()).
void install_network(net::Manager *wifi);

} // namespace os
