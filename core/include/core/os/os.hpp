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
#include "core/ports.hpp"
#include "core/proc/table.hpp"
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
// The hourglass while slow work runs on the UI task:
//     const os::Busy busy;
class Busy {
public:
    Busy();
    ~Busy();
    Busy(const Busy &) = delete;
    Busy &operator=(const Busy &) = delete;
};

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

} // namespace file

/* ---- Settings ------------------------------------------------------------------ */

// A program's own settings file, C:\.config\<program>.ini. load: false when
// there is none (the defaults apply). save: makes C:\.config if needed.
bool load_settings(const char *program, fs::Ini &ini);
bool save_settings(const char *program, const fs::Ini &ini);

/* ---- The machine ---------------------------------------------------------------- */

void restart();

/* ---- For the system, not for programs ------------------------------------------ */

struct Kit {
    wm::Wm *wm = nullptr;
    ports::System *system = nullptr;
    fs::Storage *storage = nullptr;
    ports::Memory *memory = nullptr;
};
// What the calls work with. The desktop hands it in when it starts.
void install(const Kit &kit);

} // namespace os
