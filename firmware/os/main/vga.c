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

/* Runs in the video interrupt, from internal RAM: short, no logging, no
 * locks, no heap. pos_px is where in the frame this buffer starts. */
static IRAM_ATTR bool on_bounce_empty(esp_lcd_panel_handle_t panel, void *bounce_buf, int pos_px,
                                      int len_bytes, void *context)
{
    (void)panel;
    (void)context;
    uint32_t start = esp_cpu_get_cycle_count();
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
    s_fill_cycles += esp_cpu_get_cycle_count() - start;
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
