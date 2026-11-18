// Adapters that let the core run on a PC: a screen in ordinary memory and
// do-nothing versions of the rest.
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <memory>
#include <pthread.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <cstdint>
#include <vector>

#include "core/ports.hpp"
#include "core/proc/table.hpp"

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

// Large buffers from the ordinary heap. Tests count the blocks out
// (in_use()) to see that a closed program has given everything back.
class HostMemory : public ports::Memory {
public:
    void *allocate(size_t bytes) override
    {
        void *p = std::calloc(1, bytes);
        if (p != nullptr) {
            sizes_[p] = bytes;
            bytes_ += bytes;
        }
        return p;
    }
    int in_use() const { return static_cast<int>(sizes_.size()); }
    size_t bytes_in_use() const { return bytes_; }
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
        const auto found = sizes_.find(block);
        if (found != sizes_.end()) {
            bytes_ -= found->second;
            sizes_.erase(found);
            std::free(block);
        }
    }

private:
    static constexpr size_t kLendable = 4u << 20, kBlock = 1536u << 10;
    size_t lent_ = 0;
    bool internal_lent_ = false;
    std::vector<std::pair<void *, size_t>> blocks_;
    std::map<void *, size_t> sizes_;
    size_t bytes_ = 0;
};

class NoLock : public ports::Lock {
public:
    void lock() override {}
    bool try_lock() override { return true; }
    void unlock() override {}
};

// A real lock for tests with several threads, as the FreeRTOS mutex is on
// the board.
class ThreadLock : public ports::Lock {
public:
    void lock() override
    {
        mutex_.lock();
        owner_ = std::this_thread::get_id();
    }
    bool try_lock() override
    {
        if (owner_.load() == std::this_thread::get_id() || !mutex_.try_lock()) {
            return false;
        }
        owner_ = std::this_thread::get_id();
        return true;
    }
    void unlock() override
    {
        owner_ = std::thread::id();
        mutex_.unlock();
    }

private:
    std::mutex mutex_;
    std::atomic<std::thread::id> owner_{};
};

// A real event for tests with several threads, as a FreeRTOS binary
// semaphore is on the board.
class ThreadEvent : public ports::Event {
public:
    void set() override
    {
        {
            const std::lock_guard<std::mutex> hold(mutex_);
            set_ = true;
        }
        changed_.notify_one();
    }
    bool wait(uint32_t max_ms) override
    {
        std::unique_lock<std::mutex> hold(mutex_);
        const auto is_set = [this] { return set_; };
        if (max_ms == 0xFFFFFFFF) {
            changed_.wait(hold, is_set);
        } else if (!changed_.wait_for(hold, std::chrono::milliseconds(max_ms), is_set)) {
            return false;
        }
        set_ = false;
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool set_ = false;
};

// Tasks as threads of the PC. For the tests it can hold the tasks back:
// while held, a started task waits, as a job that has not got far yet, until
// release() lets it run. join() waits for every task that was let run.
class ThreadTasks : public ports::Tasks {
public:
    ~ThreadTasks()
    {
        release();
        join();
    }
    ports::TaskId start(const char *, ports::TaskFn fn, void *context, uint32_t, ports::Priority priority) override
    {
        if (refuse) {
            return nullptr;
        }
        started++;
        last_priority = priority;
        records_.push_back(std::make_unique<Record>());
        Record *r = records_.back().get();
        r->fn = fn;
        r->context = context;
        if (!held_) {
            run(*r);
        }
        return r;
    }
    void end(ports::TaskId) override { ended++; }
    void set_priority(ports::TaskId, ports::Priority priority) override { last_priority = priority; }
    // Live while the thread runs; what it had used at its end afterwards.
    uint64_t run_time_us(ports::TaskId task) override
    {
        Record &r = *static_cast<Record *>(task);
        if (!r.over && r.thread.joinable()) {
            clockid_t clock;
            timespec t = {};
            if (pthread_getcpuclockid(r.thread.native_handle(), &clock) == 0 && clock_gettime(clock, &t) == 0) {
                return micros(t);
            }
        }
        return r.used;
    }
    void hold() { held_ = true; }
    void release()
    {
        held_ = false;
        for (auto &r : records_) {
            if (!r->started) {
                run(*r);
            }
        }
    }
    void join()
    {
        for (auto &r : records_) {
            if (r->thread.joinable()) {
                r->thread.join();
            }
        }
    }
    // release() and join(): the held tasks run to their end.
    void finish()
    {
        release();
        join();
    }

    bool refuse = false; // "no memory for the stack"
    int started = 0;
    int ended = 0; // end() calls: one for every task, once its job is over
    ports::Priority last_priority = ports::Priority::Normal;

private:
    struct Record {
        ports::TaskFn fn = nullptr;
        void *context = nullptr;
        bool started = false;
        std::atomic<bool> over{false};
        std::atomic<uint64_t> used{0};
        std::thread thread;
    };
    static uint64_t micros(const timespec &t)
    {
        return static_cast<uint64_t>(t.tv_sec) * 1000000u + static_cast<uint64_t>(t.tv_nsec) / 1000u;
    }
    static void run(Record &r)
    {
        r.started = true;
        r.thread = std::thread([&r] {
            r.fn(r.context);
            timespec t = {};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
            r.used = micros(t);
            r.over = true;
        });
    }
    bool held_ = false;
    // Kept to the end: a TaskId is a pointer to one of these.
    std::vector<std::unique_ptr<Record>> records_;
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
    // The time the tests say it is (`utc`), or the PC's own (`real_clock`);
    // neither: not known, as on the board before the network has told it.
    bool utc_time(int64_t &seconds) override
    {
        seconds = real_clock ? static_cast<int64_t>(time(nullptr)) : utc;
        return seconds != 0;
    }
    // The PC does not fail on purpose: it notes what was asked, and for
    // which process.
    void fault(ports::Fault kind) override
    {
        fault_requested = static_cast<int>(kind);
        fault_pid = proc::table().current();
    }
    // The kernel's tasks: `task_list` as it is (tests fill it), or with
    // `pretend_tasks` a made-up list that moves with the clock, so the
    // simulator has something to show. Neither: not available.
    int tasks(ports::TaskInfo *out, int capacity, ports::TaskTotals &totals) override
    {
        totals = {};
        if (pretend_tasks) {
            using namespace std::chrono;
            const uint64_t now = static_cast<uint64_t>(
                duration_cast<microseconds>(steady_clock::now() - start_).count());
            // name, priority, core, unused stack, share of a core in 1/1000
            static const struct {
                const char *name;
                uint8_t priority;
                int8_t core;
                uint32_t stack_free;
                uint32_t share;
            } pretend[] = {{"ui", 5, -1, 9200, 60},     {"IDLE0", 0, 0, 700, 930},  {"IDLE1", 0, 1, 700, 890},
                           {"wifi", 23, 0, 3600, 15},   {"tiT", 18, 0, 1900, 5},    {"esp_timer", 22, 0, 3100, 2},
                           {"ipc0", 24, 0, 500, 0},     {"ipc1", 24, 1, 500, 0}};
            task_list.clear();
            uint16_t id = 1;
            for (const auto &p : pretend) {
                ports::TaskInfo t = {};
                snprintf(t.name, sizeof(t.name), "%s", p.name);
                t.id = id++;
                t.state = t.id == 1 ? ports::TaskState::Running
                                    : (p.priority == 0 ? ports::TaskState::Ready : ports::TaskState::Blocked);
                t.priority = p.priority;
                t.core = p.core;
                t.stack_free = p.stack_free;
                t.run_time_us = now * p.share / 1000;
                task_list.push_back(t);
            }
            task_clock_us = now;
        }
        int count = 0;
        for (const ports::TaskInfo &t : task_list) {
            if (count < capacity) {
                out[count++] = t;
            }
        }
        totals.clock_us = task_clock_us;
        totals.cores = 2;
        totals.left_out = static_cast<uint8_t>(task_list.size() - static_cast<size_t>(count));
        return count;
    }
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

    // The PC has no beeper: it keeps the last tune asked for, and how many.
    void beep(const ports::Beep *steps, int count) override
    {
        beeps.assign(steps, steps + count);
        beep_calls++;
    }
    std::vector<ports::Beep> beeps;
    int beep_calls = 0;

    bool reboot_requested = false;
    int64_t utc = 0;
    bool real_clock = false;
    int fault_requested = -1; // the last ports::Fault asked for
    proc::Pid fault_pid = proc::kNone;
    // Plausible figures for the task manager (the board's after start-up);
    // tests change them.
    uint32_t free_internal_ram = 311u << 10;
    uint32_t free_external_ram = 5900u << 10;
    uint32_t scanout_us = 0; // the PC sends no video
    std::vector<ports::TaskInfo> task_list;
    uint64_t task_clock_us = 0;
    bool pretend_tasks = false;

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// Where an update goes on the PC: memory. finish() wants what an ESP image
// starts with (0xE9), so tests can send something that is not one.
class MemoryUpdater : public ports::Updater {
public:
    bool begin(uint32_t size) override
    {
        begins++;
        if (size > room) {
            return false;
        }
        image.clear();
        open = true;
        return true;
    }
    bool write(const uint8_t *data, size_t size) override
    {
        if (!open || image.size() + size > fail_after) {
            return false;
        }
        image.insert(image.end(), data, data + size);
        return true;
    }
    bool finish() override
    {
        open = false;
        if (image.empty() || image[0] != 0xE9) {
            return false;
        }
        installed = image;
        finished++;
        return true;
    }
    void abandon() override
    {
        open = false;
        abandoned++;
    }

    size_t room = 2 * 1024 * 1024;
    size_t fail_after = static_cast<size_t>(-1); // write() fails past this many bytes
    std::vector<uint8_t> image, installed;
    bool open = false;
    int begins = 0, finished = 0, abandoned = 0;
};

} // namespace host
