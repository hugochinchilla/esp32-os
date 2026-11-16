// Start of the OS firmware. It brings up the screen, runs the window manager
// with the demo windows from the core, and answers the debug protocol on the
// serial port.
//
// This file holds the ESP32 adapters: the pieces that connect the portable
// core (../../core) to the hardware.
#include <cstdarg>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "core/dbg/protocol.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "core/ports.hpp"
#include "core/video/pointer.hpp"
#include "vga.h"

namespace {

const char *TAG = "os";
const uart_port_t kDebugUart = UART_NUM_0;

class UartSink : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override
    {
        uart_write_bytes(kDebugUart, data, size);
    }
};

class RtosLock : public ports::Lock {
public:
    RtosLock() : mutex_(xSemaphoreCreateMutex()) {}
    void lock() override { xSemaphoreTake(mutex_, portMAX_DELAY); }
    bool try_lock() override
    {
        // A task that already holds the lock is logging from inside a send;
        // that text has to be dropped.
        if (xSemaphoreGetMutexHolder(mutex_) == xTaskGetCurrentTaskHandle()) {
            return false;
        }
        return xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE;
    }
    void unlock() override { xSemaphoreGive(mutex_); }

private:
    SemaphoreHandle_t mutex_;
};

class VgaDisplay : public ports::Display {
public:
    int width() const override { return VGA_WIDTH; }
    int height() const override { return VGA_HEIGHT; }
    uint8_t *pixels() override { return vga_framebuffer(); }
};

// The pointer as the video interrupt sees it. Changes go to `pending`; the
// interrupt copies them to `active` when it starts a new frame, so the
// pointer never moves half way down the screen. All of it is in internal RAM.
portMUX_TYPE s_pointer_lock = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR video::PointerState s_pending;
DRAM_ATTR video::PointerState s_active;
DRAM_ATTR video::PointerShape s_shapes[2];

IRAM_ATTR void compose_line(uint8_t *pixels, int first, int count)
{
    if (first == 0) {
        portENTER_CRITICAL_ISR(&s_pointer_lock);
        s_active = s_pending;
        portEXIT_CRITICAL_ISR(&s_pointer_lock);
    }
    video::compose_pointer(pixels, first, count, VGA_WIDTH, s_active);
}

class VgaPointer : public ports::Pointer {
public:
    void set_position(int x, int y) override
    {
        portENTER_CRITICAL(&s_pointer_lock);
        s_pending.x = static_cast<int16_t>(x);
        s_pending.y = static_cast<int16_t>(y);
        portEXIT_CRITICAL(&s_pointer_lock);
    }
    void set_shape(const video::PointerShape &shape) override
    {
        // Two shape slots: the new shape goes into the one the screen is not
        // using. Wait until the screen has picked up the last change, so that
        // slot is really free.
        while (true) {
            portENTER_CRITICAL(&s_pointer_lock);
            bool settled = s_active.shape == s_pending.shape;
            portEXIT_CRITICAL(&s_pointer_lock);
            if (settled) {
                break;
            }
            vTaskDelay(1);
        }
        video::PointerShape *spare = s_pending.shape == &s_shapes[0] ? &s_shapes[1] : &s_shapes[0];
        *spare = shape;
        portENTER_CRITICAL(&s_pointer_lock);
        s_pending.shape = spare;
        portEXIT_CRITICAL(&s_pointer_lock);
    }
    void set_visible(bool visible) override
    {
        portENTER_CRITICAL(&s_pointer_lock);
        s_pending.visible = visible;
        portEXIT_CRITICAL(&s_pointer_lock);
    }
    video::PointerState state() override
    {
        // The latest state: the monitor shows it from the next frame on.
        portENTER_CRITICAL(&s_pointer_lock);
        video::PointerState copy = s_pending;
        portEXIT_CRITICAL(&s_pointer_lock);
        return copy;
    }
};

class EspSystem : public ports::System {
public:
    void stats(ports::SystemStats &out) override
    {
        out.uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        out.free_internal_ram = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        out.free_external_ram = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        out.frames_shown = vga_frame_count();
        out.scanout_us = vga_fill_us();
    }
    void reboot() override
    {
        uart_wait_tx_done(kDebugUart, pdMS_TO_TICKS(200));
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    }
};

class EspClock : public ports::Clock {
public:
    uint32_t micros() override { return static_cast<uint32_t>(esp_timer_get_time()); }
};

dbg::Protocol *s_protocol;

// Log text travels inside the protocol, so it cannot corrupt a screenshot
// that is being sent at the same moment.
int log_to_debug_link(const char *format, va_list args)
{
    char text[256];
    int size = vsnprintf(text, sizeof(text), format, args);
    if (size <= 0) {
        return size;
    }
    if (size >= static_cast<int>(sizeof(text))) {
        size = sizeof(text) - 1;
    }
    s_protocol->send_log(text, static_cast<size_t>(size));
    return size;
}

// The UI task: the window manager runs here, and input (for now injected
// through the debug link) arrives here as events, one at a time.
void ui_task(void *)
{
    static UartSink sink;
    static RtosLock lock;
    static VgaDisplay display;
    static VgaPointer pointer;
    static EspSystem system;
    static EspClock clock;
    static wm::Wm desktop(display, pointer, clock);
    static dbg::Protocol protocol(sink, lock, display, pointer, desktop, system, "esp32-os");
    protocol.set_desktop(&desktop);

    shell::start(desktop, system);
    desktop.start();
    const uint32_t first_paint_us = desktop.last_update_us();

    s_protocol = &protocol;
    esp_log_set_vprintf(log_to_debug_link);
    protocol.send_hello();
    ESP_LOGI(TAG, "debug link ready at %d baud", CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    ESP_LOGI(TAG, "first full paint took %u us", static_cast<unsigned>(first_paint_us));

    static uint8_t buffer[256];
    while (true) {
        int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
        if (got > 0) {
            protocol.feed(buffer, static_cast<size_t>(got));
        }
        desktop.update(); // anything a window asked to repaint on its own
    }
}

} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting video");
    vga_start();
    vga_set_line_hook(compose_line);

    // The console already set this port's speed; the driver adds buffered
    // reading and writing.
    ESP_ERROR_CHECK(uart_driver_install(kDebugUart, 4096, 0, 0, nullptr, 0));

    xTaskCreate(ui_task, "ui", 16384, nullptr, 5, nullptr);
}
