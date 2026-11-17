// Start of the OS firmware. It brings up the screen, runs the window manager
// with the demo windows from the core, and answers the debug protocol on the
// serial port.
//
// This file holds the ESP32 adapters: the pieces that connect the portable
// core (../../core) to the hardware.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/uart_select.h"
#include "esp_attr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_memory_utils.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "core/dbg/protocol.hpp"
#include "core/fs/ini.hpp"
#include "littlefs_volume.hpp"
#include "rtos_queue.hpp"
#include "core/msg/input.hpp"
#include "core/msg/queue.hpp"
#include "core/net/posix_tcp.hpp"
#include "core/net/rfb.hpp"
#include "core/net/services.hpp"
#include "core/net/web.hpp"
#include "core/net/wifi.hpp"
#include "core/proc/table.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "core/ports.hpp"
#include "core/video/pointer.hpp"
#include "vga.h"
#include "wifi.hpp"

// tools/dbg.py reboot --fast arms a one-shot fast start: a word in NVS
// (RTC memory was tried first, but the reset after flashing clears it on
// this board; NVS survives that, and flashing does not erase it).
constexpr const char *kFastNextKey = "fast_next";

void arm_fast_next()
{
    nvs_handle_t h;
    if (nvs_flash_init() == ESP_OK && nvs_open("chinchilla", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, kFastNextKey, shell::kFastBootMagic);
        nvs_commit(h);
        nvs_close(h);
    }
}

// Read once at start-up; the flash is written only when it was armed.
bool take_fast_next()
{
    nvs_handle_t h;
    if (nvs_flash_init() != ESP_OK || nvs_open("chinchilla", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    uint32_t word = 0;
    nvs_get_u32(h, kFastNextKey, &word);
    const bool armed = shell::take_one_shot_fast_boot(word);
    if (armed) {
        nvs_erase_key(h, kFastNextKey);
        nvs_commit(h);
    }
    nvs_close(h);
    return armed;
}

namespace {

const char *TAG = "os";
const uart_port_t kDebugUart = UART_NUM_0;

// The scanout counters every 100 ms from start-up (scan_timeline_task).
struct ScanSample {
    uint32_t ms, late, missed, max_late_us;
};
EXT_RAM_BSS_ATTR ScanSample s_scan_samples[200]; // written by the scanlog task
volatile int s_scan_count = 0;

// The message queue into the UI task, made in app_main before any task
// that could post to it. The UI loop also sleeps on it.
RtosQueue *s_queue;

// The services task's stack, in bytes. The deepest path is a log line sent
// from inside a round (256 bytes of text and newlib's formatting).
constexpr uint32_t kServicesStack = 4096;

// The UI loop, counted for tools/dbg.py memory: how often it went round,
// and how many of its sleeps were ended by a message or by serial bytes
// (the others ran out: a timer was due).
struct LoopStats {
    uint32_t rounds, woken, no_sleep, delivered, dropped;
};
LoopStats s_loop;

// The services task (inetd): the remote screen and its web page, asleep in
// select() on all their sockets. Its counters for tools/dbg.py memory.
net::Services *s_services;
msg::PostedInput *s_remote_input;

// The serial driver's interrupt has put bytes in its buffer: wake the UI
// task. (The driver offers this call for select(); nothing else here uses
// select() on the serial port.)
IRAM_ATTR void serial_wake(uart_port_t, uart_select_notif_t what, BaseType_t *woken)
{
    if (what == UART_SELECT_READ_NOTIF) {
        s_queue->wake_from_isr(woken);
    }
}

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
    // Every FreeRTOS task. The kernel adds up each task's processor time
    // at every task switch (the run-time statistics settings in
    // sdkconfig.defaults); without them there is no list.
    int tasks(ports::TaskInfo *out, int capacity, ports::TaskTotals &totals) override
    {
        totals = {};
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_USE_TRACE_FACILITY
        // The kernel gives all the tasks or none, so there is room for a
        // few more than exist now. The list is in external RAM for the
        // length of this call; nothing stays allocated.
        const UBaseType_t room = uxTaskGetNumberOfTasks() + 4;
        auto *list = static_cast<TaskStatus_t *>(
            heap_caps_malloc(room * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (list == nullptr) {
            list = static_cast<TaskStatus_t *>(malloc(room * sizeof(TaskStatus_t)));
        }
        if (list == nullptr) {
            return 0;
        }
        configRUN_TIME_COUNTER_TYPE clock = 0;
        const UBaseType_t count = uxTaskGetSystemState(list, room, &clock);
        int written = 0;
        for (UBaseType_t i = 0; i < count && written < capacity; i++) {
            const TaskStatus_t &from = list[i];
            ports::TaskInfo &t = out[written++];
            t = {};
            snprintf(t.name, sizeof(t.name), "%s", from.pcTaskName);
            t.id = static_cast<uint16_t>(from.xTaskNumber);
            switch (from.eCurrentState) {
            case eRunning: t.state = ports::TaskState::Running; break;
            case eReady: t.state = ports::TaskState::Ready; break;
            case eBlocked: t.state = ports::TaskState::Blocked; break;
            case eSuspended: t.state = ports::TaskState::Suspended; break;
            case eDeleted: t.state = ports::TaskState::Deleted; break;
            default: t.state = ports::TaskState::Unknown; break;
            }
            t.priority = static_cast<uint8_t>(from.uxCurrentPriority);
#if configTASKLIST_INCLUDE_COREID
            t.core = from.xCoreID == tskNO_AFFINITY ? -1 : static_cast<int8_t>(from.xCoreID);
#else
            t.core = -1;
#endif
            t.stack_external = esp_ptr_external_ram(from.pxStackBase);
            t.stack_free = static_cast<uint32_t>(from.usStackHighWaterMark); // bytes on this chip
            t.run_time_us = from.ulRunTimeCounter;
        }
        totals.clock_us = clock;
        totals.cores = portNUM_PROCESSORS;
        totals.left_out = static_cast<uint8_t>(count - static_cast<UBaseType_t>(written));
        heap_caps_free(list);
        return written;
#else
        (void)out;
        (void)capacity;
        return 0;
#endif
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
        vga_scan_stats_t scan;
        vga_scan_stats(&scan, false);
        add("scanout since last reset: refills %u late %u missed %u worst late %u us longest fill %u us\n"
            "  intervals <200 %u, <300 %u, <340 %u, <400 %u, <640 %u, more %u\n",
            static_cast<unsigned>(scan.refills), static_cast<unsigned>(scan.late), static_cast<unsigned>(scan.missed),
            static_cast<unsigned>(scan.max_late_cycles / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
            static_cast<unsigned>(scan.max_fill_cycles / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
            static_cast<unsigned>(scan.histogram[0]), static_cast<unsigned>(scan.histogram[1]),
            static_cast<unsigned>(scan.histogram[2]), static_cast<unsigned>(scan.histogram[3]),
            static_cast<unsigned>(scan.histogram[4]), static_cast<unsigned>(scan.histogram[5]));
        add("  last long gap before the refill at pixel %u\n", static_cast<unsigned>(scan.last_gap_pos));
        {
            uint32_t late = 0, missed = 0, worst = 0;
            int shown = 0;
            for (int i = 0; i < s_scan_count; i++) {
                late += s_scan_samples[i].late;
                missed += s_scan_samples[i].missed;
                worst = s_scan_samples[i].max_late_us > worst ? s_scan_samples[i].max_late_us : worst;
            }
            add("start-up timeline (%d x 100 ms): late %u, missed %u, worst late %u us\n", s_scan_count,
                static_cast<unsigned>(late), static_cast<unsigned>(missed), static_cast<unsigned>(worst));
            for (int i = 0; i < s_scan_count && shown < 12; i++) {
                if (s_scan_samples[i].late || s_scan_samples[i].missed) {
                    add("  at %u ms: late %u missed %u worst %u us\n", static_cast<unsigned>(s_scan_samples[i].ms),
                        static_cast<unsigned>(s_scan_samples[i].late), static_cast<unsigned>(s_scan_samples[i].missed),
                        static_cast<unsigned>(s_scan_samples[i].max_late_us));
                    shown++;
                }
            }
        }
        add("ui loop: %u rounds, %u sleeps ended by a message or serial bytes, %u rounds without sleep\n"
            "  messages: %u delivered, %u dropped (their process had ended), %u refused (queue full), "
            "at most %u of %d waiting\n",
            static_cast<unsigned>(s_loop.rounds), static_cast<unsigned>(s_loop.woken),
            static_cast<unsigned>(s_loop.no_sleep), static_cast<unsigned>(s_loop.delivered),
            static_cast<unsigned>(s_loop.dropped), static_cast<unsigned>(s_queue->refused()),
            static_cast<unsigned>(s_queue->most_waiting()), RtosQueue::kDepth);
        if (s_services != nullptr) {
            const net::Services::Stats &v = s_services->stats();
            add("services loop (inetd): %u rounds, %u sleeps (%u ended by the network, %u forced rests), "
                "%u keys or mouse events lost (queue full)\n",
                static_cast<unsigned>(v.rounds), static_cast<unsigned>(v.sleeps), static_cast<unsigned>(v.woken),
                static_cast<unsigned>(v.rests), static_cast<unsigned>(s_remote_input->lost()));
        } else {
            add("services loop (inetd): not started (no radio, or no port could be opened)\n");
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
            snprintf(text, capacity, "%s %s core, %d MHz", model, info.cores == 2 ? "dual" : "single",
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
            snprintf(text, capacity, "serial debug link"); // no USB host yet
            return true;
        case ports::Fact::Sound:
            snprintf(text, capacity, "none");
            return true;
        }
        return false;
    }
    void arm_fast_boot() override { arm_fast_next(); }
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
    // Internal RAM for its test: the largest free block, less 16 KB kept for
    // whatever else needs internal RAM meanwhile.
    void *borrow_internal(size_t max, size_t &got) override
    {
        got = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        got = got > 16384 ? got - 16384 : 0;
        got = got > max ? max : got;
        got &= ~static_cast<size_t>(3);
        void *p = got != 0 ? heap_caps_malloc(got, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : nullptr;
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
uint32_t s_video_start_us; // when vga_start() began sending the picture

// Input goes to the desktop; while the start-up screens show, a key press or
// a mouse button only asks to skip them (pointer movement does nothing).
class BootInput : public ports::InputSink {
public:
    explicit BootInput(ports::InputSink &desktop) : desktop_(desktop) {}
    void key(uint16_t usage, bool pressed) override
    {
        if (booting) {
            skip |= pressed;
            return;
        }
        desktop_.key(usage, pressed);
    }
    void mouse(const ports::MouseEvent &event) override
    {
        if (booting) {
            skip |= event.buttons != 0;
            return;
        }
        desktop_.mouse(event);
    }
    void text(const char *utf8) override
    {
        if (booting) {
            skip = true;
            return;
        }
        desktop_.text(utf8);
    }
    bool booting = true;
    bool skip = false;

private:
    ports::InputSink &desktop_;
};

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

// The scanout counters every 100 ms from start-up to 20 s, kept for
// tools/dbg.py memory (Wi-Fi's own log lines have the same clock).

void scan_timeline_task(void *)
{
    vga_scan_stats_t s;
    vga_scan_stats(&s, true);
    TickType_t wake = xTaskGetTickCount();
    while (s_scan_count < 200) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
        vga_scan_stats(&s, true);
        s_scan_samples[s_scan_count] = {static_cast<uint32_t>(esp_timer_get_time() / 1000), s.late, s.missed,
                                        s.max_late_cycles / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ};
        s_scan_count = s_scan_count + 1;
    }
    // The first message of every start: this task hands the news to the UI
    // task, which logs it. It shows on the board that a message crosses
    // from one task to another.
    msg::post(
        proc::kNone,
        [](const msg::Message &m) {
            ESP_LOGI(TAG, "start-up timeline complete: %u samples (a message from the scanlog task, run on the %s task)",
                     static_cast<unsigned>(m.a), pcTaskGetName(nullptr));
        },
        nullptr, static_cast<uint32_t>(s_scan_count), 0, 1000);
    vTaskDelete(nullptr);
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

// The services task, "inetd": every network service in one loop that sleeps
// in select() on all their sockets (core/net/services.hpp). It never touches
// the desktop or the process table: a viewer's keys and mouse are posted to
// the UI task as messages.
struct ServicesTask {
    net::Services *services;
    rfb::Server *remote;
};

void services_task(void *context)
{
    const ServicesTask &my = *static_cast<ServicesTask *>(context);
    bool viewer = false;
    while (true) {
        my.services->round();
        const rfb::Server::Stats &v = my.remote->stats();
        if (v.connected != viewer) {
            viewer = v.connected;
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
    static wm::Wm desktop(display, pointer, clock); // internal: every event and repaint goes through it
    static BootInput input(desktop);
    static EXT_RAM_BSS_ATTR dbg::Protocol protocol(sink, lock, display, pointer, input, system, "esp32-os");
    protocol.set_desktop(&desktop);
    // The debug link has no row in the process table: it runs on this task.
    // A service gets a row when it is a task of its own (the owner's rule,
    // 2026-11-16), as the network services have now: `inetd`, further down.
    // The debug link answers from here on, also during the start-up screens.
    s_protocol = &protocol;
    esp_log_set_vprintf(log_to_debug_link);
    protocol.send_hello();
    const int64_t boot_start = esp_timer_get_time();

    // C: LittleFS on the flash; files stay across restarts. A new drive
    // gets the sample files.
    static EspMemory memory;
    static fs::Storage storage;
    // One task uses the file system today; the lock is in place for the
    // first one that joins it (background saves, the shell).
    static RtosLock storage_lock;
    storage.set_lock(&storage_lock);
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
    shell::BootSettings boot = shell::boot_settings(storage);
    if (take_fast_next()) {
        boot.fastboot = true;
        ESP_LOGI(TAG, "one-shot fast boot (armed by tools/dbg.py; boot.ini unchanged)");
    }
    boot_io.video_start_us = s_video_start_us;
    boot_io.monitor_wait_ms = boot.monitor_wait_ms;
    boot_io.splash_ms = boot.splash_ms;
    boot_io.pace_percent = boot.pace_percent;
    boot_io.hold_s = boot.hold_s;
    boot_io.skip = [](void *) { return input.skip; };
    pointer.set_visible(false);
    const int64_t test_start = esp_timer_get_time();
    const int failed = boot.fastboot ? shell::run_quiet_checks(boot_io) : shell::run_self_test(boot_io);
    const int64_t splash_start = esp_timer_get_time();
    if (!boot.fastboot) {
        shell::show_splash(boot_io);
    }
    const int64_t splash_end = esp_timer_get_time();
    protocol.set_boot_screens(&show_boot_screen, &boot_io);
    input.booting = false;
    if (boot_io.skipped) {
        ESP_LOGI(TAG, "start-up screens skipped by a key or button");
    }
    pointer.set_visible(true);

    // Wi-Fi, after the start-up screens so their timings stay as they
    // were. If the radio does not start, the Network panel says so.
    const int64_t wifi_start = esp_timer_get_time();
    // C:\.config\network.ini [wifi]: enabled (false: the radio is not
    // started) and tx_power_dbm, 2 to 20, default 2: the radio's transmit
    // bursts put lines on the analogue picture, and at 2 dBm they are gone
    // (docs/DECISIONS.md).
    static EXT_RAM_BSS_ATTR fs::Ini network;
    network.load(storage, "C:\\.config\\network.ini");
    const int tx_dbm = network.get_int("wifi", "tx_power_dbm", 2);
    const int8_t tx = static_cast<int8_t>((tx_dbm < 2 ? 2 : (tx_dbm > 20 ? 20 : tx_dbm)) * 4); // quarter dBm
    const bool radio = network.get_bool("wifi", "enabled", true) && wifi_begin();
    if (radio) {
        const esp_err_t err = esp_wifi_set_max_tx_power(tx);
        ESP_LOGI(TAG, "wifi transmit power %d dBm (%s)", tx / 4, esp_err_to_name(err));
    }
    static EXT_RAM_BSS_ATTR net::Manager wifi(wifi_port(), settings_store());
    shell::set_network(&wifi);
    protocol.set_network(&wifi);
    ESP_LOGI(TAG, "wifi %s in %lld ms", radio ? "started" : "did not start",
             (esp_timer_get_time() - wifi_start) / 1000);

    // The remote screen: a VNC viewer connects to the board's address, port
    // 5900. The port opens now and viewers reach it once Wi-Fi has joined.
    // It runs on the services task, so the viewer's keys and mouse come to
    // the desktop as messages, and the list of what was painted, which both
    // tasks use, has a lock.
    static msg::PostedInput remote_input(desktop);
    static EXT_RAM_BSS_ATTR rfb::Server remote(display, pointer, remote_input, clock, memory);
    static RtosLock damage_lock;
    remote.set_lock(&damage_lock);
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
    static EXT_RAM_BSS_ATTR net::WebGate web;
    if (radio && web_port.listen(80)) {
        web.set_port(&web_port);
        web.set_page(net::viewer_page(), net::viewer_page_size());
        web.set_second_page("/novnc", net::novnc_page(), net::novnc_page_size());
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

    // The network services, after the desktop: one task for all of them. Not
    // started when there is nothing to listen on. Its stack is in internal
    // RAM, like every task's here; priority just under this task's, so a
    // viewer that asks for a lot never holds the desktop back.
    if (vnc_port.listening() || web_port.listening()) {
        static net::PosixWait sockets;
        static net::Services services(sockets, clock);
        static ServicesTask context = {&services, &remote};
        services.add(remote);
        s_remote_input = &remote_input;
        s_services = &services;
        if (xTaskCreate(services_task, "inetd", kServicesStack, &context, 4, nullptr) == pdPASS) {
            proc::table().spawn_service("inetd");
            ESP_LOGI(TAG, "network services (inetd) started");
        } else {
            s_services = nullptr;
            ESP_LOGE(TAG, "network services: no memory for the task");
        }
    }

    ESP_LOGI(TAG, "debug link ready at %d baud", CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    ESP_LOGI(TAG, "%s: %d failed, %lld ms (on screen until %u ms after the video started); splash %lld ms; "
             "boot to desktop %lld ms, %lld ms after the video started",
             boot.fastboot ? "fast start" : "self-test", failed, (splash_start - test_start) / 1000,
             static_cast<unsigned>(boot.fastboot ? 0 : boot_io.monitor_wait_ms),
             (splash_end - splash_start) / 1000, (esp_timer_get_time() - boot_start) / 1000,
             (esp_timer_get_time() - s_video_start_us) / 1000);
    ESP_LOGI(TAG, "first full paint took %u us", static_cast<unsigned>(first_paint_us));

    // The loop. Each round takes everything that has arrived (serial bytes,
    // messages from other tasks), lets the desktop run its timers and paint,
    // and then sleeps until the next thing: a message or serial bytes wake
    // it at once; otherwise it sleeps until the desktop's next timer. The
    // network is not looked at here: that is the services task, and what a
    // viewer types arrives as messages.
    constexpr uint32_t kLongestSleepUs = 1000000; // a lost wake costs a second, not the desktop
    constexpr uint32_t kShortestSleepUs = 1000;   // the kernel makes it one tick: lower tasks always get a turn
    constexpr int kSerialReads = 16;              // 4 KB of serial bytes per round at most
    static msg::Inbox inbox(*s_queue);
    static uint8_t buffer[256];
    while (true) {
        bool more = false;
        for (int reads = 0;; reads++) {
            if (reads == kSerialReads) {
                more = true;
                break;
            }
            const int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), 0);
            if (got <= 0) {
                break;
            }
            protocol.feed(buffer, static_cast<size_t>(got));
        }
        more |= inbox.deliver(); // what other tasks asked the UI task to do
        desktop.tick();          // timers (caret, key repeat, clock), then any painting
        uint32_t sleep_us = 0;
        if (!more) {
            sleep_us = desktop.idle_us();
            sleep_us = sleep_us > kLongestSleepUs ? kLongestSleepUs : sleep_us;
            sleep_us = sleep_us < kShortestSleepUs ? kShortestSleepUs : sleep_us;
        }
        s_loop.rounds++;
        s_loop.no_sleep += more;
        s_loop.delivered = inbox.delivered();
        s_loop.dropped = inbox.dropped();
        s_loop.woken += s_queue->sleep(sleep_us);
    }
}

} // namespace

// Starts the video from core 1, so its interrupt runs there: Wi-Fi's task
// and interrupts are on core 0 and delayed the picture's refills by up to
// 0.7 ms (measured), enough to show stale lines now and then.
void video_task(void *done)
{
    s_video_start_us = static_cast<uint32_t>(esp_timer_get_time());
    vga_start();
    vga_set_line_hook(compose_line);
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(done));
    vTaskDelete(nullptr);
}

extern "C" void app_main(void)
{
    // The message queue first: every task made from here on may post.
    static RtosQueue queue;
    s_queue = &queue;
    msg::set_queue(&queue);

    ESP_LOGI(TAG, "starting video on core 1");
    static StaticSemaphore_t done_buffer;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&done_buffer);
    xTaskCreatePinnedToCore(video_task, "video", 4096, done, 10, nullptr, 1);
    xSemaphoreTake(done, portMAX_DELAY);

    // The console already set this port's speed; the driver adds buffered
    // reading and writing.
    ESP_ERROR_CHECK(uart_driver_install(kDebugUart, 4096, 0, 0, nullptr, 0));
    // Serial bytes wake the UI task instead of being looked for every 10 ms.
    uart_set_select_notif_callback(kDebugUart, &serial_wake);

    xTaskCreate(ui_task, "ui", 16384, nullptr, 5, nullptr);
    xTaskCreate(scan_timeline_task, "scanlog", 3072, nullptr, 1, nullptr);
}
