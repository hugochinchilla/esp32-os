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
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "core/dbg/protocol.hpp"
#include "littlefs_volume.hpp"
#include "core/net/posix_tcp.hpp"
#include "core/net/rfb.hpp"
#include "core/net/web.hpp"
#include "core/net/wifi.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "core/ports.hpp"
#include "core/video/pointer.hpp"
#include "vga.h"
#include "wifi.hpp"

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
    size_t memory_report(char *out, size_t capacity) override
    {
        size_t n = 0;
        auto add = [&](const char *format, auto... args) {
            if (n < capacity) {
                const int k = snprintf(out + n, capacity - n, format, args...);
                n += k > 0 ? static_cast<size_t>(k) : 0;
                n = n < capacity ? n : capacity - 1;
            }
        };
        const struct {
            const char *name;
            uint32_t caps;
        } heaps[] = {{"internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT}, {"dma", MALLOC_CAP_DMA},
                     {"psram", MALLOC_CAP_SPIRAM}};
        for (const auto &h : heaps) {
            add("%-8s total %7u free %7u lowest %7u largest %7u\n", h.name,
                static_cast<unsigned>(heap_caps_get_total_size(h.caps)),
                static_cast<unsigned>(heap_caps_get_free_size(h.caps)),
                static_cast<unsigned>(heap_caps_get_minimum_free_size(h.caps)),
                static_cast<unsigned>(heap_caps_get_largest_free_block(h.caps)));
        }
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
        static TaskStatus_t tasks[24];
        const UBaseType_t count = uxTaskGetSystemState(tasks, 24, nullptr);
        add("task stacks (unused bytes, where):\n");
        for (UBaseType_t i = 0; i < count; i++) {
            const bool in_psram = esp_ptr_external_ram(tasks[i].pxStackBase);
            add("  %-14s %5u %s\n", tasks[i].pcTaskName, static_cast<unsigned>(tasks[i].usStackHighWaterMark),
                in_psram ? "psram" : "internal");
        }
#endif
        return n;
    }
    bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity) override
    {
        value = 0;
        switch (which) {
        case ports::Fact::Processor: {
            esp_chip_info_t info;
            esp_chip_info(&info);
            const char *model = info.model == CHIP_ESP32S3 ? "ESP32-S3" : "an unexpected chip";
            snprintf(text, capacity, "%s, %d core%s, rev %d.%d, %d MHz", model, info.cores,
                     info.cores == 1 ? "" : "s", info.revision / 100, info.revision % 100,
                     esp_clk_cpu_freq() / 1000000);
            return info.model == CHIP_ESP32S3;
        }
        case ports::Fact::InternalRam:
            value = static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
            return value > 0;
        case ports::Fact::ExternalRam:
            value = static_cast<uint32_t>(esp_psram_get_size());
            return value > 0;
        case ports::Fact::Flash: {
            uint32_t size = 0;
            const bool ok = esp_flash_get_size(nullptr, &size) == ESP_OK;
            value = size;
            return ok && size > 0;
        }
        case ports::Fact::Video:
            value = vga_pixel_clock_hz();
            return true;
        case ports::Fact::Input:
            snprintf(text, capacity, "Serial debug link (no USB yet)");
            return true;
        case ports::Fact::Sound:
            snprintf(text, capacity, "None");
            return true;
        }
        return false;
    }
    void reboot() override
    {
        uart_wait_tx_done(kDebugUart, pdMS_TO_TICKS(200));
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    }
};

// Large buffers in external RAM (PSRAM), asked for once.
class EspMemory : public ports::Memory {
public:
    void *allocate(size_t bytes) override
    {
        return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    // The memory test: the largest free block, taken from the heap so
    // nothing in use is touched, and given back afterwards.
    void *borrow(size_t max, size_t &got) override
    {
        got = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        got = got > max ? max : got;
        got &= ~static_cast<size_t>(3);
        void *p = got != 0 ? heap_caps_malloc(got, MALLOC_CAP_SPIRAM) : nullptr;
        if (p == nullptr) {
            got = 0;
        }
        return p;
    }
    void release(void *block) override { heap_caps_free(block); }
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

// While the start-up screens show: answer the debug link.
void boot_idle(void *context)
{
    static uint8_t buffer[256];
    const int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), 0);
    if (got > 0) {
        static_cast<dbg::Protocol *>(context)->feed(buffer, static_cast<size_t>(got));
    }
}

void boot_log(const char *line, void *) { ESP_LOGI(TAG, "%s", line); }

// The debug link asks for a start-up screen again (dbg.py boot).
void show_boot_screen(int which, void *context)
{
    auto &io = *static_cast<shell::BootIo *>(context);
    if (which == 0) {
        shell::run_self_test(io, false);
    } else {
        shell::show_splash(io, false);
    }
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
    // The debug link answers from here on, also during the start-up screens.
    s_protocol = &protocol;
    esp_log_set_vprintf(log_to_debug_link);
    protocol.send_hello();
    const int64_t boot_start = esp_timer_get_time();

    // C: LittleFS on the flash; files stay across restarts. A new drive
    // gets the sample files.
    static EspMemory memory;
    static fs::Storage storage;
    static LittleFsVolume flash;
    bool formatted = false;
    const int64_t mount_start = esp_timer_get_time();
    if (flash.mount(formatted)) {
        storage.mount('C', flash);
        ports::DirEntry first;
        if (formatted || storage.entry("C:\\", 0, first) == ports::FsError::NotFound) {
            const int64_t samples_start = esp_timer_get_time();
            fs::add_samples(storage, 'C');
            ESP_LOGI(TAG, "C: new, sample files written in %lld ms",
                     (esp_timer_get_time() - samples_start) / 1000);
        }
        ESP_LOGI(TAG, "C: mounted in %lld ms%s", (esp_timer_get_time() - mount_start) / 1000,
                 formatted ? " (formatted first)" : "");
    } else {
        ESP_LOGE(TAG, "C: the flash drive cannot be mounted or formatted");
    }
    protocol.set_storage(&storage);

    // Start-up screens: the real self-test, then the splash.
    static shell::BootIo boot_io{display, system, memory, clock, &storage, &boot_idle, &protocol, &boot_log};
    pointer.set_visible(false);
    const int64_t test_start = esp_timer_get_time();
    const int failed = shell::run_self_test(boot_io);
    const int64_t splash_start = esp_timer_get_time();
    shell::show_splash(boot_io);
    const int64_t splash_end = esp_timer_get_time();
    protocol.set_boot_screens(&show_boot_screen, &boot_io);
    pointer.set_visible(true);

    // Wi-Fi, after the start-up screens so their timings stay as they
    // were. If the radio does not start, the Network panel says so.
    const int64_t wifi_start = esp_timer_get_time();
    const bool radio = wifi_begin();
    static net::Manager wifi(wifi_port(), settings_store());
    shell::set_network(&wifi);
    protocol.set_network(&wifi);
    ESP_LOGI(TAG, "wifi %s in %lld ms", radio ? "started" : "did not start",
             (esp_timer_get_time() - wifi_start) / 1000);

    // The remote screen: a VNC viewer connects to the board's address, port
    // 5900. The port opens now and viewers reach it once Wi-Fi has joined.
    static rfb::Server remote(display, pointer, desktop, clock, memory);
    static net::PosixTcpListener vnc_port;
    desktop.set_damage_listener([](const gfx::Rect &r, void *) { remote.damage(r); }, nullptr);
    if (radio && vnc_port.listen(rfb::kPort)) {
        remote.set_listener(&vnc_port);
        ESP_LOGI(TAG, "remote screen (VNC) on port %d", rfb::kPort);
    } else {
        ESP_LOGW(TAG, "remote screen (VNC) not available");
    }
    // The same screen in a browser: http://<the board's address>/ gives a
    // viewer page, which talks VNC to the board through a WebSocket.
    static net::PosixTcpPool web_port;
    static net::WebGate web;
    if (radio && web_port.listen(80)) {
        web.set_port(&web_port);
        web.set_page(net::viewer_page(), net::viewer_page_size());
        remote.set_listener(&web, 1);
        ESP_LOGI(TAG, "remote screen for a browser on port 80");
    } else {
        ESP_LOGW(TAG, "remote screen for a browser not available");
    }
    // By name instead of by address: chinchilla.local, and viewers that
    // browse the network list the board.
    if (radio && wifi_announce("chinchilla", vnc_port.listening() ? rfb::kPort : 0, web_port.listening() ? 80 : 0)) {
        ESP_LOGI(TAG, "answers to chinchilla.local (mDNS)");
    }

    shell::start(desktop, system, storage, memory);
    protocol.set_counters(shell::picture_timings(), shell::kPictureTimings);
    desktop.start();
    const uint32_t first_paint_us = desktop.last_update_us();

    ESP_LOGI(TAG, "debug link ready at %d baud", CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    ESP_LOGI(TAG, "self-test: %d failed, %lld ms with its %u ms hold; splash %lld ms; boot to desktop %lld ms",
             failed, (splash_start - test_start) / 1000, static_cast<unsigned>(shell::kSelfTestHoldMs),
             (splash_end - splash_start) / 1000, (esp_timer_get_time() - boot_start) / 1000);
    ESP_LOGI(TAG, "first full paint took %u us", static_cast<unsigned>(first_paint_us));

    static uint8_t buffer[256];
    bool viewer = false;
    while (true) {
        int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
        if (got > 0) {
            protocol.feed(buffer, static_cast<size_t>(got));
        }
        desktop.tick(); // timers (caret, key repeat, clock), then any painting
        remote.poll();  // the VNC viewer: its keys and mouse in, changed screen out
        if (remote.stats().connected != viewer) {
            viewer = remote.stats().connected;
            const rfb::Server::Stats &v = remote.stats();
            if (viewer) {
                ESP_LOGI(TAG, "VNC viewer connected");
            } else {
                ESP_LOGI(TAG, "VNC viewer gone: %u updates, %u rectangles, %u bytes sent",
                         static_cast<unsigned>(v.updates), static_cast<unsigned>(v.rectangles),
                         static_cast<unsigned>(v.bytes_sent));
            }
        }
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
