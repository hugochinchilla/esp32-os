// Start of the OS firmware. For now it brings up the screen, shows the demo
// screen from the core, and answers the debug protocol on the serial port.
//
// This file holds the ESP32 adapters: the pieces that connect the portable
// core (../../core) to the hardware.
#include <cstdarg>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "core/dbg/protocol.hpp"
#include "core/demo/demo_screen.hpp"
#include "core/ports.hpp"
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

class EspSystem : public ports::System {
public:
    void stats(ports::SystemStats &out) override
    {
        out.uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        out.free_internal_ram = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        out.free_external_ram = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        out.frames_shown = vga_frame_count();
    }
    void reboot() override
    {
        uart_wait_tx_done(kDebugUart, pdMS_TO_TICKS(200));
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    }
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

void debug_task(void *)
{
    static UartSink sink;
    static RtosLock lock;
    static VgaDisplay display;
    static EspSystem system;
    static demo::DemoScreen screen(display);
    static dbg::Protocol protocol(sink, lock, display, screen, system, "esp32-os");

    screen.draw();

    s_protocol = &protocol;
    esp_log_set_vprintf(log_to_debug_link);
    protocol.send_hello();
    ESP_LOGI(TAG, "debug link ready at %d baud", CONFIG_ESP_CONSOLE_UART_BAUDRATE);

    static uint8_t buffer[256];
    while (true) {
        int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
        if (got > 0) {
            protocol.feed(buffer, static_cast<size_t>(got));
        }
    }
}

} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "starting video");
    vga_start();

    // The console already set this port's speed; the driver adds buffered
    // reading and writing.
    ESP_ERROR_CHECK(uart_driver_install(kDebugUart, 4096, 0, 0, nullptr, 0));

    xTaskCreate(debug_task, "debug", 8192, nullptr, 5, nullptr);
}
