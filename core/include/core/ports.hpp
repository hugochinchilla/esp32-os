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

// Guards the debug link when several tasks send on it.
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
    virtual FsError open(const char *path, OpenMode mode, int &file) = 0;
    virtual FsError read(int file, void *data, size_t size, size_t &done) = 0;
    virtual FsError write(int file, const void *data, size_t size) = 0;
    virtual FsError seek(int file, uint32_t offset) = 0;
    virtual FsError close(int file) = 0;

protected:
    ~Volume() = default;
};

// Large, long-lived memory (external RAM on the board), asked for once at
// start-up or on first use, never per event. nullptr when there is none.
class Memory {
public:
    virtual void *allocate(size_t bytes) = 0;

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

class System {
public:
    virtual void stats(SystemStats &out) = 0;
    virtual void reboot() = 0;

protected:
    ~System() = default;
};

} // namespace ports
