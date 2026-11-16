// Adapters that let the core run on a PC: a screen in ordinary memory and
// do-nothing versions of the rest.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
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
    // The start-up memory test borrows from a pretend 4 MB of free memory,
    // in blocks of at most 1.5 MB, so it behaves the same on every PC.
    void *borrow(size_t max, size_t &got) override
    {
        const size_t left = kLendable - lent_;
        got = std::min(std::min(max, left), kBlock);
        void *p = got != 0 ? std::malloc(got) : nullptr;
        if (p == nullptr) {
            got = 0;
            return nullptr;
        }
        lent_ += got;
        blocks_.push_back({p, got});
        return p;
    }
    // The internal RAM test: a pretend 64 KB.
    void *borrow_internal(size_t max, size_t &got) override
    {
        if (internal_lent_) {
            got = 0;
            return nullptr;
        }
        internal_lent_ = true;
        got = std::min(max, static_cast<size_t>(64u << 10));
        void *p = std::malloc(got);
        blocks_.push_back({p, got});
        return p;
    }
    void release(void *block) override
    {
        for (size_t i = 0; i < blocks_.size(); i++) {
            if (blocks_[i].first == block) {
                std::free(block);
                blocks_.erase(blocks_.begin() + static_cast<long>(i));
                return; // what was lent stays counted: the test borrows once
            }
        }
    }

private:
    static constexpr size_t kLendable = 4u << 20, kBlock = 1536u << 10;
    size_t lent_ = 0;
    bool internal_lent_ = false;
    std::vector<std::pair<void *, size_t>> blocks_;
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
        out.free_internal_ram = free_internal_ram;
        out.free_external_ram = free_external_ram;
        out.scanout_us = scanout_us;
        out.frames_shown = out.uptime_ms * 60 / 1000;
    }
    void reboot() override { reboot_requested = true; }
    // Plausible facts, so the start-up screens look as on the board.
    bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity) override
    {
        value = 0;
        switch (which) {
        case ports::Fact::Processor:
            snprintf(text, capacity, "ESP32-S3 dual core, 240 MHz (simulated)");
            return true;
        case ports::Fact::InternalRam: value = 400u << 10; return true;
        case ports::Fact::ExternalRam: value = 8u << 20; return true;
        case ports::Fact::Flash: value = 16u << 20; return true;
        case ports::Fact::Video: value = 25000000; return true;
        case ports::Fact::Input: snprintf(text, capacity, "serial debug link"); return true;
        case ports::Fact::Sound: snprintf(text, capacity, "none"); return true;
        }
        return false;
    }

    bool reboot_requested = false;
    // Plausible figures for the task manager (the board's after start-up);
    // tests change them.
    uint32_t free_internal_ram = 311u << 10;
    uint32_t free_external_ram = 5900u << 10;
    uint32_t scanout_us = 0; // the PC sends no video

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

} // namespace host
