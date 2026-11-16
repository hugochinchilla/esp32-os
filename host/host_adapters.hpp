// Adapters that let the core run on a PC: a screen in ordinary memory and
// do-nothing versions of the rest.
#pragma once

#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "core/ports.hpp"

namespace host {

class MemoryDisplay : public ports::Display {
public:
    MemoryDisplay(int width, int height)
        : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height)
    {
    }
    int width() const override { return width_; }
    int height() const override { return height_; }
    uint8_t *pixels() override { return pixels_.data(); }

private:
    int width_, height_;
    std::vector<uint8_t> pixels_;
};

// Keeps the pointer's state; there is no monitor to send it to.
class MemoryPointer : public ports::Pointer {
public:
    void set_position(int x, int y) override
    {
        state_.x = static_cast<int16_t>(x);
        state_.y = static_cast<int16_t>(y);
    }
    void set_shape(const video::PointerShape &shape) override
    {
        shape_ = shape;
        state_.shape = &shape_;
    }
    void set_visible(bool visible) override { state_.visible = visible; }
    video::PointerState state() override { return state_; }

private:
    video::PointerShape shape_ = {};
    video::PointerState state_ = {};
};

class SteadyClock : public ports::Clock {
public:
    uint32_t micros() override
    {
        using namespace std::chrono;
        return static_cast<uint32_t>(
            duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
    }
};

// A clock that only moves when told to, so pictures and timings in tests
// do not depend on the speed of the computer.
class FixedClock : public ports::Clock {
public:
    uint32_t micros() override { return now; }
    uint32_t now = 0;
};

// Large buffers from the ordinary heap. ponytail: never freed; they live
// as long as the program, as on the board.
class HostMemory : public ports::Memory {
public:
    void *allocate(size_t bytes) override { return std::calloc(1, bytes); }
};

class NoLock : public ports::Lock {
public:
    void lock() override {}
    bool try_lock() override { return true; }
    void unlock() override {}
};

class HostSystem : public ports::System {
public:
    void stats(ports::SystemStats &out) override
    {
        using namespace std::chrono;
        out.uptime_ms = static_cast<uint32_t>(
            duration_cast<milliseconds>(steady_clock::now() - start_).count());
        out.free_internal_ram = 0;
        out.free_external_ram = 0;
        out.frames_shown = out.uptime_ms * 60 / 1000;
    }
    void reboot() override { reboot_requested = true; }

    bool reboot_requested = false;

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

} // namespace host
