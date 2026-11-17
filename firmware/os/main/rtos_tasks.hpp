// Tasks for background jobs, on FreeRTOS: the adapter for ports::Tasks
// (core/job/job.hpp). A job gets a task of its own for as long as its work
// takes; the stack comes from internal memory when the job starts and goes
// back when it ends.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "core/ports.hpp"

// The three classes as FreeRTOS priorities (docs/DECISIONS.md, "Priorities").
// The UI task has 5 and the video 10; Wi-Fi and the network stack are far
// above. Low and Normal never keep the desktop from an event; High takes
// turns with it. Services use the same numbers.
inline UBaseType_t rtos_priority(ports::Priority p)
{
    switch (p) {
    case ports::Priority::Low:
        return 3;
    case ports::Priority::High:
        return 5;
    case ports::Priority::Normal:
    default:
        return 4;
    }
}

class RtosTasks : public ports::Tasks {
public:
    // ponytail: at most this many starting at the same moment; jobs are
    // started by the UI task one at a time, so one would do.
    static constexpr int kStarting = 4;

    ports::TaskId start(const char *name, ports::TaskFn fn, void *context, uint32_t stack_bytes,
                        ports::Priority priority) override
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
            return nullptr;
        }
        slot->fn = fn;
        slot->context = context;
        TaskHandle_t task = nullptr;
        if (xTaskCreate(&RtosTasks::run, name, stack_bytes, slot, rtos_priority(priority), &task) != pdPASS) {
            slot->fn = nullptr;
            return nullptr;
        }
        return task;
    }

    // The task is asleep for good after its function (see run()), or about
    // to be: deleting it from here gives its stack back.
    // Not before the task has put itself to sleep. Deleted while it is
    // still on its way there on the other core, it is only marked, and its
    // own vTaskSuspend() then takes it off the list of tasks to clear away:
    // it stays for ever, with its stack (seen on the board, 2026-11-17).
    void end(ports::TaskId task) override
    {
        const TaskHandle_t handle = static_cast<TaskHandle_t>(task);
        while (eTaskGetState(handle) != eSuspended) {
            vTaskDelay(1);
        }
        vTaskDelete(handle);
    }
    void set_priority(ports::TaskId task, ports::Priority priority) override
    {
        vTaskPrioritySet(static_cast<TaskHandle_t>(task), rtos_priority(priority));
    }
    // A program that hangs: the caller has made sure it is not inside an OS
    // call, so it holds no lock.
    bool kill(ports::TaskId task) override
    {
        vTaskDelete(static_cast<TaskHandle_t>(task));
        return true;
    }

    // What the scheduler has counted for the task, in µs, as set up for the
    // task statistics (backlog item 1). For a task that is running this
    // moment it is up to the last time it was switched in: a little under.
    uint64_t run_time_us(ports::TaskId task) override
    {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_USE_TRACE_FACILITY
        TaskStatus_t status = {};
        // A state is given so that it is not looked up: only the counter is wanted.
        vTaskGetInfo(static_cast<TaskHandle_t>(task), &status, pdFALSE, eReady);
        return static_cast<uint64_t>(status.ulRunTimeCounter);
#else
        (void)task;
        return 0;
#endif
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
        // It does not delete itself: its figures are read once more, and
        // then whoever started it calls end().
        for (;;) {
            vTaskSuspend(nullptr);
        }
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
        // A wait shorter than a tick is a tick, not none: whoever waits a
        // few ms in a loop must sleep, not spin.
        TickType_t ticks = max_ms == 0xFFFFFFFF ? portMAX_DELAY : pdMS_TO_TICKS(max_ms);
        if (max_ms != 0 && ticks == 0) {
            ticks = 1;
        }
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
