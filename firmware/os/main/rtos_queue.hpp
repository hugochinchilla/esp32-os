// The message queue into the UI task on the board: a FreeRTOS queue for the
// messages and a binary semaphore that the UI task sleeps on. A post gives
// the semaphore; so does the serial port's interrupt when bytes arrive
// (wake_from_isr), which is how one sleep waits for both.
//
// Both are created in memory of this object, with no heap: 32 messages of
// 20 bytes and two kernel headers, about 0.8 KB of internal RAM. It must be
// internal: a queue is used with interrupts off, and external RAM cannot be
// reached while the flash is being written.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_attr.h"

#include "core/msg/queue.hpp"

class RtosQueue : public msg::Queue {
public:
    static constexpr int kDepth = 32;

    RtosQueue()
        : queue_(xQueueCreateStatic(kDepth, sizeof(msg::Message), storage_, &queue_buffer_)),
          wake_(xSemaphoreCreateBinaryStatic(&wake_buffer_))
    {
    }

    bool post(const msg::Message &m, uint32_t wait_ms = msg::kNoWait) override
    {
        const TickType_t ticks = wait_ms == msg::kForever ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);
        if (xQueueSend(queue_, &m, ticks) != pdTRUE) {
            refused_ = refused_ + 1; // a statistic; two tasks at once may lose a count
            return false;
        }
        xSemaphoreGive(wake_);
        return true;
    }

    void wake() override { xSemaphoreGive(wake_); }

    // From an interrupt handler. `woken` as FreeRTOS wants it.
    void IRAM_ATTR wake_from_isr(BaseType_t *woken) { xSemaphoreGiveFromISR(wake_, woken); }

    bool take(msg::Message &out) override
    {
        const UBaseType_t waiting = uxQueueMessagesWaiting(queue_);
        most_ = waiting > most_ ? waiting : most_;
        return xQueueReceive(queue_, &out, 0) == pdTRUE;
    }

    // The kernel counts time in ticks (10 ms): the sleep ends at the first
    // tick at or after max_us, or one earlier, never a whole tick late.
    bool sleep(uint32_t max_us) override
    {
        if (max_us == 0) {
            return xSemaphoreTake(wake_, 0) == pdTRUE;
        }
        const uint32_t tick_us = portTICK_PERIOD_MS * 1000;
        const uint32_t ticks = max_us > 0xFFFFFFFF - tick_us ? max_us / tick_us : (max_us + tick_us - 1) / tick_us;
        return xSemaphoreTake(wake_, static_cast<TickType_t>(ticks)) == pdTRUE;
    }

    // Posts that found the queue full, and the most messages seen waiting.
    uint32_t refused() const { return refused_; }
    uint32_t most_waiting() const { return most_; }

private:
    StaticQueue_t queue_buffer_;
    StaticSemaphore_t wake_buffer_;
    uint8_t storage_[kDepth * sizeof(msg::Message)];
    QueueHandle_t queue_;
    SemaphoreHandle_t wake_;
    volatile uint32_t refused_ = 0;
    uint32_t most_ = 0;
};
