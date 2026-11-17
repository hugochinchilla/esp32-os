// Interfaces between the portable core and whatever it runs on.
// Adapters implement these for the ESP32, for the PC and for tests.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/video/pointer.hpp"

namespace ports {

// Somewhere to send bytes, such as a serial port.
class ByteSink {
public:
    virtual void write(const uint8_t *data, size_t size) = 0;

protected:
    ~ByteSink() = default;
};

// Guards something several tasks use, one task at a time: the debug link
// when several tasks send on it, the file system (fs::Storage). Not
// recursive: a task that holds it must not ask for it again.
class Lock {
public:
    virtual void lock() = 0;
    // Must return false instead of waiting forever, and false when the
    // calling task already holds the lock.
    virtual bool try_lock() = 0;
    virtual void unlock() = 0;

protected:
    ~Lock() = default;
};

// Something a task sleeps on until another task has work for it: a shell
// session waiting for a typed line (tty::Tty). A set() that comes before the
// wait() is not lost: that wait() returns at once. One sleeper at a time.
class Event {
public:
    // Any task. Wakes the sleeper, now or at its next wait().
    virtual void set() = 0;
    // Sleeps until a set() or until max_ms have gone by. True if it was set.
    virtual bool wait(uint32_t max_ms) = 0;

protected:
    ~Event() = default;
};

// Starts tasks: a FreeRTOS task on the board, a thread on the PC. Background
// jobs run on these (core/job/job.hpp). A machine without tasks, like the
// browser build, has no adapter and the work is done by the caller.
using TaskFn = void (*)(void *context);
// How a task stands against the desktop (docs/DECISIONS.md, "Priorities").
// Low: only when the desktop and the Normal tasks are waiting. Normal: just
// under the desktop, which is never kept from an event. High: level with the
// desktop, taking turns with it. All three are below the video, Wi-Fi and
// the network stack. Idle: level with the kernel's own idle tasks, for a
// program that has stopped answering: it may spin for ever, and the idle
// tasks must still get their turn (on the board they feed a watchdog).
enum class Priority : uint8_t { Low, Normal, High, Idle };
// What names a started task to the adapter that started it.
using TaskId = void *;
class Tasks {
public:
    // Any task. Runs fn(context) on a new task with a stack of its own.
    // Null if it could not be started (no memory for the stack), and then
    // fn is never called. The task and its stack are kept after fn returns,
    // so its figures can still be read, until the starter calls end().
    virtual TaskId start(const char *name, TaskFn fn, void *context, uint32_t stack_bytes, Priority priority) = 0;
    // Gives back a task whose fn has returned, or is at its last statement.
    // Once for every task started.
    virtual void end(TaskId task) = 0;
    // The processor time a task has used so far, in µs; 0 where it cannot
    // be told. Between start() and end() only.
    virtual uint64_t run_time_us(TaskId) { return 0; }
    // Changes how a task stands against the desktop (a program that has
    // stopped answering goes to Low until it answers again).
    virtual void set_priority(TaskId, Priority) {}
    // Takes away a task whose fn has not returned (a program that hangs).
    // True if it is gone and its stack given back: end() is then not called.
    // False where a task cannot be taken away (a thread of the PC): it is
    // left to return by itself. Never for a task that may hold a lock.
    virtual bool kill(TaskId) { return false; }

protected:
    ~Tasks() = default;
};

// The screen: one byte per pixel, RRRGGGBB, rows top to bottom, no padding.
class Display {
public:
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual uint8_t *pixels() = 0;

protected:
    ~Display() = default;
};

// The mouse pointer, mixed into the picture as it goes to the monitor. It is
// never drawn into Display::pixels().
class Pointer {
public:
    virtual void set_position(int x, int y) = 0;
    // The shape is copied.
    virtual void set_shape(const video::PointerShape &shape) = 0;
    virtual void set_visible(bool visible) = 0;
    // The pointer as the screen shows it, for screenshots. The shape it
    // points to stays valid until the next set_shape().
    virtual video::PointerState state() = 0;

protected:
    ~Pointer() = default;
};

// A free-running microsecond counter, for timing (wraps).
class Clock {
public:
    virtual uint32_t micros() = 0;

protected:
    ~Clock() = default;
};

// What the debug link can ask of the desktop.
class Desktop {
public:
    // The windows as JSON text; returns its length (at most capacity - 1).
    virtual size_t describe_windows(char *out, size_t capacity) = 0;
    // Paints the whole screen again; returns how long it took in µs.
    virtual uint32_t repaint_all() = 0;
    // The clock on the taskbar.
    virtual void set_time(int hours, int minutes, int seconds) = 0;
    virtual void get_time(int &hours, int &minutes, int &seconds) = 0;
    // Blinking off (caret always shown), for screenshots that compare.
    virtual void set_animations(bool on) = 0;

protected:
    ~Desktop() = default;
};

struct MouseEvent {
    bool absolute;   // true: x, y are a screen position. false: a movement.
    int16_t x;
    int16_t y;
    uint8_t buttons; // bit 0 left, bit 1 right, bit 2 middle
    int8_t wheel;
};

// Receives keyboard and mouse events, whatever their source.
class InputSink {
public:
    // usage is a USB HID keyboard usage code (0x04 is A, 0x28 is Enter).
    virtual void key(uint16_t usage, bool pressed) = 0;
    virtual void mouse(const MouseEvent &event) = 0;
    // Typed text (UTF-8), already turned into characters: from the debug
    // link now, from a keyboard layout later.
    virtual void text(const char * /*utf8*/) {}

protected:
    ~InputSink() = default;
};

/* ---- Storage ----------------------------------------------------------------- */

enum class FsError : uint8_t {
    Ok,
    NotFound,
    Exists,
    NotEmpty,
    NoSpace,
    BadName,
    IsFolder,
    NotFolder,
    TooManyOpen,
    InUse,
    Io,
};

struct DirEntry {
    char name[64];
    bool folder;
    uint32_t size;
    // When it was last written: seconds since 1970, UTC. 0: not known (the
    // machine did not know the time then, or the drive keeps no dates).
    int64_t modified = 0;
};

struct VolumeInfo {
    char label[24];
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool keeps_files; // false: everything is lost at restart (a RAM disk)
};

enum class OpenMode : uint8_t {
    Read,
    Write,  // create, or empty an existing file
    Update, // an existing file, read and written anywhere
};

// Where a new version of the system is written: on the board, the app slot
// of the flash that is not running (docs/journal/2026-11-17-updates.md).
// One update at a time; whoever calls stays on one task.
class Updater {
public:
    // Makes room for an image of this many bytes. False if there is no
    // second slot or the image is too big for it.
    virtual bool begin(uint32_t size) = 0;
    // The next bytes, in order.
    virtual bool write(const uint8_t *data, size_t size) = 0;
    // All of it has been written: checks that it is an image this machine
    // can start and makes it the one the next start uses. False, and nothing
    // changes, if it is not.
    virtual bool finish() = 0;
    // Gives up an update that was begun.
    virtual void abandon() = 0;

protected:
    ~Updater() = default;
};

// One drive. Paths are relative to its root, with '\' between names
// ("docs\notes.txt"; "" is the root). Names compare without regard to case.
class Volume {
public:
    virtual FsError info(VolumeInfo &out) = 0;
    // The index-th entry of a folder; NotFound past the last one.
    virtual FsError entry(const char *folder, int index, DirEntry &out) = 0;
    virtual FsError stat(const char *path, DirEntry &out) = 0;
    virtual FsError make_folder(const char *path) = 0;
    virtual FsError remove(const char *path) = 0; // a file or an empty folder
    virtual FsError rename(const char *from, const char *to) = 0;
    // Gives the file `from` the name `to` in one step, taking the place of a
    // file already there: after a power cut `to` is whole, either the old
    // file or the new one, never half of one. IsFolder if `to` is a folder.
    virtual FsError replace(const char *from, const char *to) = 0;
    virtual FsError open(const char *path, OpenMode mode, int &file) = 0;
    virtual FsError read(int file, void *data, size_t size, size_t &done) = 0;
    virtual FsError write(int file, const void *data, size_t size) = 0;
    virtual FsError seek(int file, uint32_t offset) = 0;
    virtual FsError close(int file) = 0;

protected:
    ~Volume() = default;
};

// Large memory (external RAM on the board), zeroed. nullptr when there is
// none. The OS's own buffers are asked for once at start-up and kept; a
// program's objects and buffers are asked for when it opens and given back
// with release() when it closes (docs/journal/2026-11-17-program-lifecycle.md).
// Never per event or per repaint. One task at a time: the UI task.
class Memory {
public:
    virtual void *allocate(size_t bytes) = 0;
    // For the start-up memory test only: the largest free block of external
    // RAM, at most `max` bytes, lent until release(). Nothing in use is ever
    // lent. nullptr (got = 0) when there is none.
    virtual void *borrow(size_t max, size_t &got)
    {
        (void)max;
        got = 0;
        return nullptr;
    }
    // The same for internal RAM (the start-up test of it); release() too.
    virtual void *borrow_internal(size_t max, size_t &got)
    {
        (void)max;
        got = 0;
        return nullptr;
    }
    // Gives back a block from allocate(), borrow() or borrow_internal().
    virtual void release(void *block) { (void)block; }

protected:
    ~Memory() = default;
};

struct SystemStats {
    uint32_t uptime_ms;
    uint32_t free_internal_ram;
    uint32_t free_external_ram;
    uint32_t frames_shown;
    uint32_t scanout_us; // time spent filling video lines since start (wraps)
};

// Facts about the machine for the start-up self-test. Each is read when
// asked; false when it could not be read.
enum class Fact : uint8_t {
    Processor,   // text: e.g. "ESP32-S3, 2 cores, rev 0.2, 240 MHz"
    InternalRam, // value: bytes
    ExternalRam, // value: bytes
    Flash,       // value: bytes
    Video,       // value: pixel clock in Hz
    Input,       // text: where keys and mouse come from
    Sound,       // text: the sound output
};

// One task as the kernel sees it (a FreeRTOS task on the board). Programs
// that share the UI task are not tasks: they all show up as "ui".
enum class TaskState : uint8_t {
    Running,   // on a processor at the moment it was asked
    Ready,     // wants a processor and is waiting its turn
    Blocked,   // waiting for something: a delay, a queue, a lock
    Suspended, // stopped until another task resumes it
    Deleted,   // ended, its memory not yet given back
    Unknown,
};

struct TaskInfo {
    char name[16];
    uint16_t id;          // the kernel's number for the task; it keeps it for life
    TaskState state;
    uint8_t priority;     // higher runs first
    int8_t core;          // the processor it is tied to; -1: either
    bool stack_external;  // its stack is in external RAM
    uint32_t stack_free;  // bytes of its stack never used so far
    uint64_t run_time_us; // processor time since start-up, interrupts that
                          // arrived while it ran included
};

struct TaskTotals {
    uint64_t clock_us; // the clock the run times are counted on, now
    uint8_t cores;     // processors; each can give clock_us of run time
    uint8_t left_out;  // tasks that did not fit in the list
};

// Faults made on purpose, to try the watchdog and the crash record
// (tools/dbg.py fault). None of them returns on the board.
enum class Fault : uint8_t {
    Hang = 0,       // the calling task spins for ever
    BadPointer = 1, // a write through a null pointer
    Abort = 2,      // abort()
    Slow = 3,       // the calling task spins for five seconds, then carries on
};

class System {
public:
    virtual void stats(SystemStats &out) = 0;
    // The date and time: seconds since 1970, UTC. False while the machine
    // does not know (the board has no clock of its own: it learns the time
    // from the network).
    virtual bool utc_time(int64_t &seconds)
    {
        seconds = 0;
        return false;
    }
    // Every task of the kernel, in no particular order; returns how many
    // were written (0: not available, as when the kernel does not keep
    // run-time statistics).
    virtual int tasks(TaskInfo *out, int capacity, TaskTotals &totals)
    {
        (void)out;
        (void)capacity;
        totals = {};
        return 0;
    }
    // A plain-text report of where memory goes (heaps, task stacks), for
    // the debug link; returns its length (0: not available).
    virtual size_t memory_report(char *out, size_t capacity)
    {
        (void)out;
        (void)capacity;
        return 0;
    }
    virtual bool fact(Fact which, uint32_t &value, char *text, size_t capacity)
    {
        (void)which;
        (void)text;
        (void)capacity;
        value = 0;
        return false;
    }
    virtual void reboot() = 0;
    // Makes the fault happen in the calling task, now.
    virtual void fault(Fault kind) { (void)kind; }
    // The next start is a fast one (no start-up screens), once.
    virtual void arm_fast_boot() {}

protected:
    ~System() = default;
};

} // namespace ports
