/*
 * VGA output through the LCD peripheral. The settings are the ones proven
 * with firmware/vga-test on the bench: 25.000 MHz pixel clock and bounce
 * buffers (see docs/DECISIONS.md, "Found on the bench").
 *
 * We own the single framebuffer and fill the bounce buffers ourselves, so a
 * line hook (the mouse pointer) can be mixed in on the way out.
 */
#include "vga.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp32s3/rom/cache.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

/* Must match docs/hardware/README.md. Data lines D7..D0 carry RRRGGGBB. */
#define PIN_R2 4
#define PIN_R1 5
#define PIN_R0 6
#define PIN_G2 7
#define PIN_G1 15
#define PIN_G0 16
#define PIN_B1 17
#define PIN_B0 18
#define PIN_B_MIRROR 8 /* blue's third resistor: repeats D1, blue's top bit */
#define PIN_HSYNC 9
#define PIN_VSYNC 10

#define PIXEL_CLOCK_HZ 25000000
#define BOUNCE_PIXELS (10 * VGA_WIDTH)
#define FRAME_BYTES (VGA_WIDTH * VGA_HEIGHT)

static esp_lcd_panel_handle_t s_panel;
static uint8_t *s_framebuffer;
static volatile uint32_t s_frames;
static vga_line_hook_t s_line_hook;
static volatile uint64_t s_fill_cycles;

/* Scanout timing, measured in the interrupt (CPU cycles at 240 MHz). In the
 * visible part of a frame the DMA empties one bounce buffer every 10 lines
 * (320 us) like clockwork, so each refill has a deadline: it must be done
 * before the DMA comes back to that buffer, one period after it was handed
 * over. Lateness is how long after its slot (counted from the frame's first
 * refill) a refill started. */
#define CYCLES_PER_US (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
#define PERIOD_CYCLES (10 * 800 * CYCLES_PER_US / 25)
static volatile vga_scan_stats_t s_scan;
static volatile uint32_t s_missed_total, s_missed_worst_cycles, s_missed_pos;
static uint32_t s_last_start;
static int s_last_pos = -1;

/* Runs in the video interrupt, from internal RAM: short, no logging, no
 * locks, no heap. pos_px is where in the frame this buffer starts. */
static IRAM_ATTR bool on_bounce_empty(esp_lcd_panel_handle_t panel, void *bounce_buf, int pos_px,
                                      int len_bytes, void *context)
{
    (void)panel;
    (void)context;
    uint32_t start = esp_cpu_get_cycle_count();
    /* Time since the previous refill, when both are in the same stretch of
     * picture (not across the vertical blank, where the timing is different). */
    const bool steady = s_last_pos >= 0 && pos_px == s_last_pos + len_bytes && pos_px > 2 * len_bytes; /* the first refills come around the vertical blank */
    if (s_last_pos >= 0 && pos_px == s_last_pos + len_bytes && start - s_last_start > 2 * PERIOD_CYCLES) {
        s_scan.last_gap_pos = (uint32_t)pos_px;
    }
    const uint32_t interval = start - s_last_start;
    s_last_start = start;
    s_last_pos = pos_px;
    if (steady) {
        const uint32_t us = interval / CYCLES_PER_US;
        s_scan.histogram[us < 200 ? 0 : us < 300 ? 1 : us < 340 ? 2 : us < 400 ? 3 : us < 640 ? 4 : 5]++;
    }
    const int32_t late = steady ? (int32_t)(interval - PERIOD_CYCLES) : 0;
    memcpy(bounce_buf, s_framebuffer + pos_px, len_bytes);
    vga_line_hook_t hook = s_line_hook;
    if (hook) {
        hook(bounce_buf, pos_px, len_bytes);
    }
    /* Start fetching the next piece from PSRAM, as the driver does when it
     * owns the framebuffer. */
    int next = pos_px + len_bytes;
    if (next >= FRAME_BYTES) {
        next = 0;
    }
    Cache_Start_DCache_Preload((uint32_t)(s_framebuffer + next), len_bytes, 0);
    const uint32_t end = esp_cpu_get_cycle_count();
    const uint32_t fill = end - start;
    s_fill_cycles += fill;
    s_scan.refills++;
    if (fill > s_scan.max_fill_cycles) {
        s_scan.max_fill_cycles = fill;
    }
    if (steady) {
        if (late > (int32_t)s_scan.max_late_cycles) {
            s_scan.max_late_cycles = (uint32_t)late;
        }
        if (late > (int32_t)(PERIOD_CYCLES / 10)) {
            s_scan.late++; /* more than 32 us after its slot */
        }
        if (late + (int32_t)fill > (int32_t)PERIOD_CYCLES) {
            s_scan.missed++; /* not done when the DMA needed it: stale lines shown */
            s_missed_total++;
            s_missed_pos = (uint32_t)pos_px;
            if (late > 0 && (uint32_t)late > s_missed_worst_cycles) {
                s_missed_worst_cycles = (uint32_t)late;
            }
        }
    }
    return false;
}

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                               const esp_lcd_rgb_panel_event_data_t *event, void *context)
{
    (void)panel;
    (void)event;
    (void)context;
    s_frames++;
    return false;
}

void vga_start(void)
{
    /* The one picture in memory. Allocated before the panel, because the
     * panel starts filling bounce buffers as soon as it is set up. */
    s_framebuffer = heap_caps_aligned_alloc(64, FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_framebuffer ? ESP_OK : ESP_ERR_NO_MEM);
    memset(s_framebuffer, 0, FRAME_BYTES);

    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = PIXEL_CLOCK_HZ,
            .h_res = VGA_WIDTH,
            .v_res = VGA_HEIGHT,
            .hsync_pulse_width = 96,
            .hsync_back_porch = 48,
            .hsync_front_porch = 16,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 33,
            .vsync_front_porch = 10,
            /* Both syncs idle high and pulse low, as 640x480 expects. */
            .flags = {
                .hsync_idle_low = 0,
                .vsync_idle_low = 0,
            },
        },
        .data_width = 8,
        .bits_per_pixel = 8,
        .num_fbs = 0,
        .bounce_buffer_size_px = BOUNCE_PIXELS,
        .hsync_gpio_num = PIN_HSYNC,
        .vsync_gpio_num = PIN_VSYNC,
        .de_gpio_num = -1,
        .pclk_gpio_num = -1,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            PIN_B0, PIN_B1, PIN_G0, PIN_G1, PIN_G2, PIN_R0, PIN_R1, PIN_R2,
            -1, -1, -1, -1, -1, -1, -1, -1,
        },
        .flags = {
            .no_fb = 1, /* we own the framebuffer, see on_bounce_empty */
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&config, &s_panel));

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_vsync = on_vsync,
        .on_bounce_empty = on_bounce_empty,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(s_panel, &callbacks, NULL));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* Blue's low pin repeats blue's top bit (data line D1). */
    gpio_config_t mirror = {
        .pin_bit_mask = 1ULL << PIN_B_MIRROR,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&mirror));
    esp_rom_gpio_connect_out_signal(PIN_B_MIRROR, LCD_DATA_OUT1_IDX, false, false);
}

void vga_set_line_hook(vga_line_hook_t hook)
{
    s_line_hook = hook;
}

void vga_missed_total(uint32_t *missed, uint32_t *worst_us, uint32_t *line)
{
    *missed = s_missed_total;
    *worst_us = s_missed_worst_cycles / CYCLES_PER_US;
    s_missed_worst_cycles = 0; /* a miss landing between these two lines loses only its lateness */
    *line = s_missed_pos / VGA_WIDTH;
}

void vga_scan_stats(vga_scan_stats_t *out, bool reset)
{
    *out = s_scan;
    if (reset) {
        s_scan.refills = s_scan.late = s_scan.missed = 0;
        s_scan.max_late_cycles = s_scan.max_fill_cycles = 0;
        for (int i = 0; i < 6; i++) {
            s_scan.histogram[i] = 0;
        }
    }
}

uint32_t vga_fill_us(void)
{
    return (uint32_t)(s_fill_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ));
}

uint8_t *vga_framebuffer(void)
{
    return s_framebuffer;
}

uint32_t vga_pixel_clock_hz(void)
{
    return PIXEL_CLOCK_HZ;
}

uint32_t vga_frame_count(void)
{
    return s_frames;
}
