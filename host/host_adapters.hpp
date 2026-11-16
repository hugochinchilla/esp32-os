// Adapters that let the core run on a PC: a screen in ordinary memory and
// do-nothing versions of the rest.
#pragma once

#include <chrono>
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
