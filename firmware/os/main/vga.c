/*
 * VGA output through the LCD peripheral. The settings are the ones proven
 * with firmware/vga-test on the bench: 25.000 MHz pixel clock and bounce
 * buffers (see docs/DECISIONS.md, "Found on the bench").
 */
#include "vga.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
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

static esp_lcd_panel_handle_t s_panel;
static uint8_t *s_framebuffer;
static volatile uint32_t s_frames;

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
        .num_fbs = 1,
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
            .fb_in_psram = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&config, &s_panel));

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_vsync = on_vsync,
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

    void *framebuffer = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &framebuffer));
    s_framebuffer = framebuffer;
    memset(s_framebuffer, 0, VGA_WIDTH * VGA_HEIGHT);
}

uint8_t *vga_framebuffer(void)
{
    return s_framebuffer;
}

uint32_t vga_frame_count(void)
{
    return s_frames;
}
