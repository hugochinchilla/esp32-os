// The piezo beeper on GPIO14 (docs/hardware/README.md), for the BIOS and the
// OS alike: ports::System::beep on the board. The chip's LED PWM makes the
// square wave in hardware, and a one-shot timer (esp_timer, a task of its
// own) goes from step to step, so the tune keeps time whatever the caller
// is busy with. The beeper sits between the pin and ground: low is silent.
// A fading note (ports::Beep::fade_ms) has its pulse narrowed every 20 ms.
#pragma once

#include <cmath>

#include "core/ports.hpp"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace beeper {

constexpr int kPin = 14;
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr uint32_t kHalf = 512; // of the 10 bits: a square wave
constexpr int64_t kFadeTickUs = 20000; // a fading note changes this often
constexpr float kFadeFloor = 0.03f;    // and is never quieter than this

struct State {
    ports::Beep steps[ports::kBeepSteps];
    int count = 0, next = 0;
    ports::Beep now = {};         // the step playing
    int64_t start_us = 0, end_us = 0;
    bool playing = false;
    esp_timer_handle_t timer = nullptr;
    SemaphoreHandle_t lock = nullptr;
    StaticSemaphore_t lock_buffer;
    bool ready = false;
};
inline State g;

// Silent is the pin held low: the beeper is between the pin and ground
// (docs/hardware/README.md), so low leaves nothing across it. The pin
// stays the PWM's from begin() on; the level is the channel's idle level,
// which a restart of the processors does not change. Two tries on the
// board got this wrong (docs/journal/2026-11-18-beeper-never-stopped.md).
inline void silence() { ledc_stop(kMode, kChannel, 0); }

// The pulse's width for a loudness of 0 to 1: the fundamental of a pulse
// goes as the sine of its width, so the width is the arcsine.
inline uint32_t width(float level)
{
    const float d = asinf(level > 1 ? 1 : (level < kFadeFloor ? kFadeFloor : level)) / 3.14159265f;
    const uint32_t w = static_cast<uint32_t>(d * 1024 + 0.5f);
    return w < 1 ? 1 : w;
}

// With the lock held: what the pin should be doing at this moment, and the
// timer set for the next change.
inline void advance()
{
    int64_t t = esp_timer_get_time();
    if (!g.playing || t >= g.end_us - 500) {
        // The next step, or silence after the last.
        if (g.next >= g.count) {
            g.count = g.next = 0;
            g.playing = false;
            silence();
            return;
        }
        g.now = g.steps[g.next++];
        g.playing = true;
        g.start_us = t;
        g.end_us = t + static_cast<int64_t>(g.now.ms) * 1000;
        if (g.now.hz == 0 || ledc_set_freq(kMode, kTimer, g.now.hz) != ESP_OK) {
            g.now.hz = 0;
            silence();
        }
    }
    int64_t wait = g.end_us - t;
    if (g.now.hz != 0) {
        const float top = g.now.level != 0 ? g.now.level / 255.0f : 1.0f;
        uint32_t duty = g.now.level != 0 ? width(top) : kHalf;
        if (g.now.fade_ms != 0) {
            duty = width(top * expf(-static_cast<float>(t - g.start_us) / (g.now.fade_ms * 1000.0f)));
            wait = wait > kFadeTickUs ? kFadeTickUs : wait;
        }
        ledc_set_duty(kMode, kChannel, duty);
        ledc_update_duty(kMode, kChannel);
    }
    const esp_err_t err = esp_timer_start_once(g.timer, static_cast<uint64_t>(wait < 1000 ? 1000 : wait));
    if (err != ESP_OK) {
        // Nothing would end the note: better none.
        ESP_LOGE("beeper", "timer not started (%s): silent", esp_err_to_name(err));
        g.count = g.next = 0;
        g.playing = false;
        silence();
    }
}

// Once, before the first play(). False: the beeper stays silent.
inline bool begin()
{
    if (g.ready) {
        return true;
    }
    ledc_timer_config_t timer = {};
    timer.speed_mode = kMode;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = kTimer;
    timer.freq_hz = 1000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t channel = {};
    channel.gpio_num = kPin;
    channel.speed_mode = kMode;
    channel.channel = kChannel;
    channel.timer_sel = kTimer;
    channel.duty = 0;
    if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&channel) != ESP_OK) {
        return false;
    }
    silence();
    // A restart in the middle of a tune would leave the tone on for ever.
    esp_register_shutdown_handler([]() { ledc_stop(kMode, kChannel, 0); });
    g.lock = xSemaphoreCreateMutexStatic(&g.lock_buffer);
    esp_timer_create_args_t args = {};
    args.callback = [](void *) {
        xSemaphoreTake(g.lock, portMAX_DELAY);
        // A timer that fired just as play() replaced the tune finds the
        // new one's timer already set: that one does the work.
        if (g.playing && !esp_timer_is_active(g.timer)) {
            advance();
        }
        xSemaphoreGive(g.lock);
    };
    args.name = "beeper";
    g.ready = esp_timer_create(&args, &g.timer) == ESP_OK;
    ESP_LOGI("beeper", "GPIO%d: %s", kPin, g.ready ? "ready" : "no timer, silent");
    return g.ready;
}

// ports::System::beep.
inline void play(const ports::Beep *steps, int count)
{
    if (!g.ready) {
        return;
    }
    xSemaphoreTake(g.lock, portMAX_DELAY);
    esp_timer_stop(g.timer);
    g.count = count < 0 ? 0 : (count > ports::kBeepSteps ? ports::kBeepSteps : count);
    for (int i = 0; i < g.count; i++) {
        g.steps[i] = steps[i];
    }
    g.next = 0;
    g.playing = false;
    ESP_LOGD("beeper", "%d step%s", g.count, g.count == 1 ? "" : "s");
    advance();
    xSemaphoreGive(g.lock);
}

} // namespace beeper
