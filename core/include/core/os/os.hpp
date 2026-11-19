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
#include "core/input/bluetooth.hpp"
#include "core/job/job.hpp"
#include "core/net/wifi.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
#include "core/sh/terminal_session.hpp"
#include "core/sound/sound.hpp"
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
// False while the process is in a call that has taken longer than the
// system waits (a program on a task of its own that hangs, or is slow).
bool responding(proc::Pid pid);
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

/* ---- Background jobs ---------------------------------------------------------- */

// Work on a task of its own, for the process asking, while the desktop and
// the program's own window go on (core/job/job.hpp has the whole promise:
// what `work` may touch, and that the process is not ended before its jobs
// have). `work` runs on the new task; `done` runs afterwards where the
// program's events run. The program's object is destroyed when its window
// closes, job or no job: what the work reads must be memory that outlives
// the window (a block of its own, given back by `done`). False: the job
// could not be started, and nothing was called. Where there are no tasks
// the work is done before this returns (job::Job::own_task()).
bool start_job(const job::Spec &spec);
// From a job's own task only: gives the processor away for about `ms`.
// Work that computes for seconds calls it a few times a second, or the
// tasks below it never run. A job cannot ask the time (now_us() is the UI
// task's): the program sets a timer and tells its job when time is up.
void job_rest(uint32_t ms);

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
// One more carried by the drag just begun, when several are picked: let go,
// they are dropped one after the other, that of begin_drag() first. False:
// no memory for it.
bool add_to_drag(const char *path);
// While a Dropped event is answered: how many more files of the same drag
// will be dropped here after this one. What can wait for the last (looking
// at the folder again, saying what went wrong) waits for 0.
int drops_to_come();
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
// The look of windows, buttons and the taskbar: a place in the table of
// themes (core/gfx/theme.hpp, where gfx::theme() tells the one in use).
// Everything on the screen is painted again.
void set_theme(int index);
// The screen's modes (backlog item 68): how many there are, each one's
// size, and the one in use. set_video_mode() changes to another one: the
// picture goes black while the monitor finds the new signal, every window
// is brought onto the new screen and everything is painted again. It
// happens once the event that asked has been answered, though video_mode()
// says the new one at once. False: there is no such mode.
int video_mode_count();
ports::VideoMode video_mode_at(int index);
int video_mode();
bool set_video_mode(int index);
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
// Starts Shut Down (shell::power_down): every program is asked to close,
// the system waits for those that ask a question first and ends what is
// still open after 20 seconds, then the picture fades to the screen that
// says the power can go. Returns at once; a program that says no calls it
// off. False: this machine has no Shut Down.
bool shut_down();
// Starts one of the system's own programs by its name ("About Chinchilla"),
// or brings it forward if it runs. False: there is none of that name.
bool launch(const char *program);

/* ---- The network ---------------------------------------------------------------- */

// The Wi-Fi service: the networks in range, connecting, the time zone.
// nullptr: this board has none. A program asks each time and does not keep
// the pointer.
net::Manager *network();
// The Bluetooth service: the keyboards and mice connected, remembered and
// in range. nullptr: this machine has none. Asked for each time, not kept.
bt::Manager *bluetooth();

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
// A run of entries from the first-th on, in one pass over the folder: what
// a listing asks. `count` fewer than `capacity`: the folder ends there.
Error entries(const char *folder, int first, fs::DirEntry *out, int capacity, int &count);
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

/* ---- Files on the clipboard --------------------------------------------------- */

// Copy and Cut put files and folders on the clipboard by their full paths;
// Paste asks which, and whether they were cut (they are then moved, and
// whoever pasted clears the clipboard). The clipboard is the one the text
// uses: it holds text or files, never both (os::ui::clipboard_size() is of
// the text).
namespace clipboard {

// Empties the clipboard and puts these there: each is `folder` with a name
// of `names`. False if not all of them fitted (the clipboard is then empty).
bool set_files(const char *folder, const char *const *names, int count, bool cut);
int file_count();                                  // 0: no files there
bool file(int index, char *out, size_t capacity);  // a full path
bool files_cut();
void clear();

} // namespace clipboard

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

/* ---- Sound --------------------------------------------------------------------- */

// One of the system's sounds (core/sound/sound.hpp), from its start. It
// returns at once; a new one takes the place of the one playing. Nothing
// on a machine that has no sound output or no recordings.
void play_sound(sound::Id which);
// The beeper: a square wave, one pitch at a time (ports::Sound::beep). It
// returns at once, and a new call replaces what the beeper was playing.
void beep(const ports::Beep *steps, int count);
// Whether anything is still to be heard.
bool sound_playing();

/* ---- For the system, not for programs ------------------------------------------ */

struct Kit {
    wm::Wm *wm = nullptr;
    ports::System *system = nullptr;
    fs::Storage *storage = nullptr;
    ports::Memory *memory = nullptr;
    bool (*shut_down)() = nullptr; // starts Shut Down: programs close, the picture fades, the last screen
    bool (*launch)(const char *program) = nullptr; // a row of the program table, by name
    sh::TerminalSession *(*open_session)(ports::ByteSink &device, const char *&why) = nullptr;
};
// What the calls work with. The desktop hands it in when it starts.
void install(const Kit &kit);
const Kit &installed_kit(); // for the other files of core/os
// The Wi-Fi service, when the board has one (before or after install()).
void install_network(net::Manager *wifi);
// The Bluetooth service, when the machine has one (before or after install()).
void install_bluetooth(bt::Manager *bluetooth);
// The sound output, when the machine has one (before or after install()).
// Without one, or with nullptr, the calls reach sound::silence().
void install_sound(ports::Sound *output);

} // namespace os
