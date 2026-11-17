// The BIOS: the first of our two firmwares. After every reset the
// bootloader starts it (firmware/os/bootloader_components). It brings up
// the video, runs the self-test on its text screen and as plain text on the
// serial port, checks that there is a system to start, and restarts the
// chip into the OS, leaving a word in RTC memory that tells the bootloader
// so (firmware/common/boot_flag.h).
//
// No debug protocol here: the serial port carries the screen's text, and
// any byte received counts as a key.
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bootloader_common.h"
#include "driver/uart.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_private/esp_clk.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "boot_flag.h"
#include "core/bios/post.hpp"
#include "core/bios/text_screen.hpp"
#include "core/ports.hpp"
#include "core/shell/boot.hpp"
#include "vga.h"

namespace {

const uart_port_t kSerial = UART_NUM_0;

// Restarts the chip into the OS.
[[noreturn]] void start_os()
{
    const uint32_t word = CHINCHILLA_START_OS;
    memcpy(bootloader_common_get_rtc_retain_mem()->custom, &word, sizeof(word));
    // A missed refill breaks the picture's timing and can make a monitor
    // search for it again: said on serial when it happened.
    uint32_t missed = 0, worst_us = 0, line = 0;
    vga_missed_total(&missed, &worst_us, &line);
    if (missed != 0) {
        char text[96];
        const int n = snprintf(text, sizeof(text), "\r\nVideo: %u refills missed, the worst by %u us\r\n",
                               static_cast<unsigned>(missed), static_cast<unsigned>(worst_us));
        uart_write_bytes(kSerial, text, n);
    }
    uart_wait_tx_done(kSerial, pdMS_TO_TICKS(200));
    esp_restart();
    while (true) {
    }
}

// tools/dbg.py armed a one-shot fast start (a word in NVS, which the reset
// after flashing does not clear): no screens. The OS reads the same word
// and clears it.
bool fast_start_armed()
{
    nvs_handle_t h;
    if (nvs_flash_init() != ESP_OK || nvs_open("chinchilla", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    uint32_t word = 0;
    nvs_get_u32(h, "fast_next", &word);
    nvs_close(h);
    return word == shell::kFastBootMagic;
}

class VgaDisplay : public ports::Display {
public:
    int width() const override { return VGA_WIDTH; }
    int height() const override { return VGA_HEIGHT; }
    uint8_t *pixels() override { return vga_framebuffer(); }
};

// The facts the self-test asks for (the same answers as the OS gives).
class BiosSystem : public ports::System {
public:
    void stats(ports::SystemStats &out) override
    {
        out = {};
        out.uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
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
            snprintf(text, capacity, "serial console"); // no USB host yet
            return true;
        case ports::Fact::Sound:
            snprintf(text, capacity, "none");
            return true;
        }
        return false;
    }
    void reboot() override { esp_restart(); }
};

// Memory for the memory test: the largest free block, given back afterwards.
class BiosMemory : public ports::Memory {
public:
    void *allocate(size_t bytes) override { return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void *borrow(size_t max, size_t &got) override { return take(MALLOC_CAP_SPIRAM, 0, max, got); }
    // Less 16 KB kept for whatever else needs internal RAM meanwhile.
    void *borrow_internal(size_t max, size_t &got) override
    {
        return take(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, 16384, max, got);
    }
    void release(void *block) override { heap_caps_free(block); }

private:
    static void *take(uint32_t caps, size_t keep, size_t max, size_t &got)
    {
        got = heap_caps_get_largest_free_block(caps);
        got = got > keep ? got - keep : 0;
        got = got > max ? max : got;
        got &= ~static_cast<size_t>(3);
        void *p = got != 0 ? heap_caps_malloc(got, caps) : nullptr;
        if (p == nullptr) {
            got = 0;
        }
        return p;
    }
};

class EspClock : public ports::Clock {
public:
    uint32_t micros() override { return static_cast<uint32_t>(esp_timer_get_time()); }
};

uint32_t s_video_start_us;
bool s_key; // a byte arrived on serial

void video_task(void *done)
{
    s_video_start_us = static_cast<uint32_t>(esp_timer_get_time());
    vga_start();
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(done));
    vTaskDelete(nullptr);
}

void serial_out(const char *bytes, size_t n, void *) { uart_write_bytes(kSerial, bytes, n); }

// While the screen waits: any byte on serial is a key. `ticks`: how long to
// sleep for one.
void look_for_key(TickType_t ticks)
{
    uint8_t buffer[64];
    if (uart_read_bytes(kSerial, buffer, sizeof(buffer), ticks) > 0) {
        s_key = true;
    }
}
void idle(void *) { look_for_key(0); }
void nap(void *) { look_for_key(1); } // one tick asleep, so the idle tasks run

// Drive C: is not mounted here; its size comes from the partition table.
const char *drive_text()
{
    static char text[32];
    const esp_partition_t *p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, nullptr);
    if (p == nullptr) {
        return nullptr;
    }
    char size[16];
    shell::format_bytes(size, sizeof(size), p->size);
    snprintf(text, sizeof(text), "%s flash disk", size);
    return text;
}

bool image_ok(const esp_partition_t *p, uint32_t &length)
{
    const esp_partition_pos_t pos = {p->address, p->size};
    esp_image_metadata_t data;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &data) != ESP_OK) {
        return false;
    }
    length = data.image_len;
    return true;
}

// The system the bootloader will start next: the slot the update record
// names, or the other one if that one was on trial and did not confirm
// itself (the bootloader goes back then). Its image is read and checked.
bool boot_device(char *text, size_t capacity, void *)
{
    const esp_partition_t *p = esp_ota_get_boot_partition();
    if (p == nullptr) {
        snprintf(text, capacity, "none");
        return false;
    }
    const char *note = "";
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(p, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        const esp_partition_t *other = esp_ota_get_next_update_partition(p);
        if (other != nullptr && other != p) {
            note = p->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ? " (ota_0 failed its trial)"
                                                                 : " (ota_1 failed its trial)";
            p = other;
        }
    }
    uint32_t length = 0;
    if (!image_ok(p, length)) {
        snprintf(text, capacity, "%s, not a system", p->label);
        return false;
    }
    char size[16];
    shell::format_bytes(size, sizeof(size), length);
    snprintf(text, capacity, "%s, %s, image OK%s", p->label, size, note);
    return true;
}

// The bottom line, as BIOSes had: the build's date, the chip and its flash
// and RAM in megabytes, e.g. "17/11/2026-ESP32S3-N16R8-CHINCHILLA".
const char *identity()
{
    static char text[64];
    static const char *const months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = __DATE__; // "Nov 17 2026"
    const char *m = strstr(months, (const char[]){date[0], date[1], date[2], 0});
    const int month = m != nullptr ? static_cast<int>(m - months) / 3 + 1 : 0;
    uint32_t flash = 0;
    esp_flash_get_size(nullptr, &flash);
    snprintf(text, sizeof(text), "%c%c/%02d/%s-ESP32S3-N%uR%u-CHINCHILLA", date[4] == ' ' ? '0' : date[4], date[5],
             month, date + 7, static_cast<unsigned>(flash >> 20), static_cast<unsigned>(esp_psram_get_size() >> 20));
    return text;
}

// The self-test and the handover. A task of its own with the stack the OS
// gives the same self-test (16 KB): the main task's 3.5 KB overflowed on the
// board in the memory test (docs/journal/2026-11-17-bios-second-stage.md).
void bios_task(void *)
{
    static VgaDisplay display;
    static BiosSystem system;
    static BiosMemory memory;
    static EspClock clock;
    static bios::TextScreen screen;
    screen.set_out(&serial_out, nullptr);

    static shell::BootIo io{display, system, memory, clock, nullptr, &idle, nullptr, nullptr};
    io.video_start_us = s_video_start_us;
    // The first screen has done the waiting for the monitor: the self-test
    // starts at once after it. (Settings come with step 3, in NVS.)
    io.monitor_wait_ms = 0;
    io.pace_percent = shell::kSelfTestPace;
    io.hold_s = shell::kSelfTestHoldS;
    io.skip = [](void *) { return s_key; };
    io.nap = &nap;

    static bios::Post post{io, screen};
    post.id = identity();
    post.drive = drive_text();
    post.boot_device = &boot_device;
    // The first screen while the monitor wakes, then the self-test.
    bios::run_splash(post);
    bios::run_post(post);

    if (!post.bootable) {
        // Nothing to start: the screen says so. Stay here; flashing a
        // system resets the chip.
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    start_os();
}

} // namespace

extern "C" void app_main(void)
{
    // The speed is already set by the console; the driver adds buffered
    // reading and writing.
    ESP_ERROR_CHECK(uart_driver_install(kSerial, 2048, 2048, 0, nullptr, 0));

    if (fast_start_armed()) {
        static const char line[] = "\r\nRodent BIOS: fast start asked for, no self-test\r\n";
        uart_write_bytes(kSerial, line, sizeof(line) - 1);
        start_os();
    }

    // The video from core 1, as in the OS.
    static StaticSemaphore_t done_buffer;
    SemaphoreHandle_t done = xSemaphoreCreateBinaryStatic(&done_buffer);
    xTaskCreatePinnedToCore(video_task, "video", 4096, done, 10, nullptr, 1);
    xSemaphoreTake(done, portMAX_DELAY);

    xTaskCreate(bios_task, "bios", 16384, nullptr, 5, nullptr);
}
