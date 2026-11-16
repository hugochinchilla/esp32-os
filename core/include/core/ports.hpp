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

protected:
    ~InputSink() = default;
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
