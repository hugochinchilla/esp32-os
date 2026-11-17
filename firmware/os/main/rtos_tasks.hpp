// Tasks for background jobs, on FreeRTOS: the adapter for ports::Tasks
// (core/job/job.hpp). A job gets a task of its own for as long as its work
// takes; the stack comes from internal memory when the job starts and goes
// back when it ends.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "core/ports.hpp"

class RtosTasks : public ports::Tasks {
public:
    // One below the UI task (5): a job never keeps the desktop from an
    // event, and it runs whenever the desktop is waiting for one. The three
    // priority classes are backlog item 11.
    static constexpr UBaseType_t kPriority = 4;
    // ponytail: at most this many starting at the same moment; jobs are
    // started by the UI task one at a time, so one would do.
    static constexpr int kStarting = 4;

    bool start(const char *name, ports::TaskFn fn, void *context, uint32_t stack_bytes) override
    {
        // What the new task needs to know is kept here until it has read
        // it, since start() returns before the task first runs.
        Start *slot = nullptr;
        for (Start &s : starting_) {
            if (s.fn == nullptr) {
                slot = &s;
                break;
            }
        }
        if (slot == nullptr) {
            return false;
        }
        slot->fn = fn;
        slot->context = context;
        if (xTaskCreate(&RtosTasks::run, name, stack_bytes, slot, kPriority, nullptr) != pdPASS) {
            slot->fn = nullptr;
            return false;
        }
        return true;
    }

private:
    struct Start {
        ports::TaskFn fn = nullptr;
        void *context = nullptr;
    };

    static void run(void *arg)
    {
        Start *slot = static_cast<Start *>(arg);
        const ports::TaskFn fn = slot->fn;
        void *const context = slot->context;
        slot->fn = nullptr; // free for the next start()
        fn(context);
        vTaskDelete(nullptr); // the idle task gives the stack back
    }

    Start starting_[kStarting];
};

// Something a task sleeps on (ports::Event): a binary semaphore. A set()
// before the wait() is kept, and several are one.
class RtosEvent : public ports::Event {
public:
    RtosEvent() : semaphore_(xSemaphoreCreateBinary()) {}
    void set() override { xSemaphoreGive(semaphore_); }
    bool wait(uint32_t max_ms) override
    {
        const TickType_t ticks = max_ms == 0xFFFFFFFF ? portMAX_DELAY : pdMS_TO_TICKS(max_ms);
        return xSemaphoreTake(semaphore_, ticks) == pdTRUE;
    }

private:
    SemaphoreHandle_t semaphore_;
};

// A plain lock (ports::Lock) for what two tasks share for a moment: a
// Terminal's TTY and grid.
class RtosMutex : public ports::Lock {
public:
    RtosMutex() : mutex_(xSemaphoreCreateMutex()) {}
    void lock() override { xSemaphoreTake(mutex_, portMAX_DELAY); }
    bool try_lock() override
    {
        if (xSemaphoreGetMutexHolder(mutex_) == xTaskGetCurrentTaskHandle()) {
            return false;
        }
        return xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    void unlock() override { xSemaphoreGive(mutex_); }

private:
    SemaphoreHandle_t mutex_;
};
