/*
 * VGA wiring test for the esp32-os adapter board.
 *
 * Shows a test card at 640x480, 60 Hz, alternating every 10 seconds between
 * the 64-colour and the 256-colour version. Keys on the serial console
 * (UART0, 115200 baud) switch modes by hand; press 'h' for the list.
 *
 * Pin map and resistor values: docs/hardware/README.md
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "soc/gpio_sig_map.h"

static const char *TAG = "vga-test";

/* ---- Wiring (must match docs/hardware/README.md) ------------------------ */

/* One pixel is one byte, RRRGGGBB, on data lines D7..D0. */
#define PIN_R2 4  /* D7, red top bit,      510 ohm */
#define PIN_R1 5  /* D6, red middle bit,   1k      */
#define PIN_R0 6  /* D5, red low bit,      2k      */
#define PIN_G2 7  /* D4, green top bit,    510 ohm */
#define PIN_G1 15 /* D3, green middle bit, 1k      */
#define PIN_G0 16 /* D2, green low bit,    2k      */
#define PIN_B1 17 /* D1, blue top bit,     510 ohm */
#define PIN_B0 18 /* D0, blue middle bit,  1k      */
#define PIN_B_MIRROR 8 /* blue low pin, 2k: repeats D1 (blue top bit) */
#define PIN_HSYNC 9
#define PIN_VSYNC 10

/* ---- Video timing: 640x480 at 60 Hz ------------------------------------- */

#define H_RES 640
#define V_RES 480

/* Line-buffer size for the driver's bounce-buffer mode, in pixels.
 * 0 sends the framebuffer from PSRAM directly; on the bench that left the
 * picture displaced sideways after every redraw until the output was
 * restarted, so the test uses bounce buffers. */
#define VGA_BOUNCE_PX (10 * H_RES)

/* Pixel clocks to try with the 'c' key. 25.175 MHz is the VGA standard, but
 * on the bench it gave shifting pixels and 25.000 MHz was clean, so that is
 * the default. */
static const uint32_t s_pclk_options[] = {25000000, 25175000, 24000000, 26666666};
#define NUM_PCLK (sizeof(s_pclk_options) / sizeof(s_pclk_options[0]))

/* ---- State --------------------------------------------------------------- */

typedef enum { MODE_64, MODE_256 } color_mode_t;

static esp_lcd_panel_handle_t s_panel;
static color_mode_t s_mode = MODE_64;
static bool s_auto = true;
static unsigned s_pclk_index;

/* ---- Colour helpers ------------------------------------------------------ */

/* r and g are 0..7, b is 0..3. */
static inline uint8_t rgb332(unsigned r, unsigned g, unsigned b)
{
    return (uint8_t)((r << 5) | (g << 2) | b);
}

/* Restrict a colour to the 64-colour set: 2 bits per colour, with the low
 * pin of red and green repeating the top bit, the same way the blue mirror
 * pin does in hardware. All three colours then have the levels 0, 2, 5, 7. */
static inline uint8_t to_64_colours(uint8_t c)
{
    unsigned r2 = (c >> 6) & 3;
    unsigned g2 = (c >> 3) & 3;
    unsigned b2 = c & 3;
    return rgb332((r2 << 1) | (r2 >> 1), (g2 << 1) | (g2 >> 1), b2);
}

/* The four greys that are exactly neutral on this DAC. */
static inline uint8_t grey4(unsigned level)
{
    static const uint8_t rg[4] = {0, 2, 5, 7};
    return rgb332(rg[level], rg[level], level);
}

#define BLACK rgb332(0, 0, 0)
#define WHITE rgb332(7, 7, 3)

/* ---- Test card ----------------------------------------------------------- */

/* 5x7 digits for the mode label. */
static const uint8_t s_glyph_2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
static const uint8_t s_glyph_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
static const uint8_t s_glyph_5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
static const uint8_t s_glyph_6[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};

static bool label_pixel(int x, int y, color_mode_t mode)
{
    const uint8_t *glyphs[3];
    int count;
    if (mode == MODE_64) {
        glyphs[0] = s_glyph_6;
        glyphs[1] = s_glyph_4;
        count = 2;
    } else {
        glyphs[0] = s_glyph_2;
        glyphs[1] = s_glyph_5;
        glyphs[2] = s_glyph_6;
        count = 3;
    }
    const int scale = 8, advance = 6 * scale;
    if (x < 0 || y < 0 || y >= 7 * scale) {
        return false;
    }
    int index = x / advance;
    int col = (x % advance) / scale;
    if (index >= count || col >= 5) {
        return false;
    }
    return (glyphs[index][y / scale] >> (4 - col)) & 1;
}

/* Colour of one pixel of the test card, before any 64-colour restriction. */
static uint8_t card_pixel(int x, int y, color_mode_t mode)
{
    /* One-pixel white frame: all four edges must be visible on the monitor. */
    if (x == 0 || y == 0 || x == H_RES - 1 || y == V_RES - 1) {
        return WHITE;
    }
    /* Eight colour bars. */
    if (y >= 8 && y < 128) {
        static const uint8_t bars[8][3] = {
            {7, 7, 3}, {7, 7, 0}, {0, 7, 3}, {0, 7, 0},
            {7, 0, 3}, {7, 0, 0}, {0, 0, 3}, {0, 0, 0},
        };
        int i = x / 80;
        return rgb332(bars[i][0], bars[i][1], bars[i][2]);
    }
    /* Red, green and blue ramps: every level of each colour. */
    if (y >= 136 && y < 184) {
        return rgb332(x / 80, 0, 0);
    }
    if (y >= 184 && y < 232) {
        return rgb332(0, x / 80, 0);
    }
    if (y >= 232 && y < 280) {
        return rgb332(0, 0, x / 160);
    }
    /* The four neutral greys. They must show no colour tint. */
    if (y >= 280 && y < 328) {
        return grey4(x / 160);
    }
    /* Every colour of the mode: 32 x 8 cells for 256, 16 x 4 cells for 64. */
    if (y >= 336 && y < 400) {
        if (mode == MODE_256) {
            return (uint8_t)((x / 20) + 32 * ((y - 336) / 8));
        }
        unsigned i = (unsigned)((x / 40) + 16 * ((y - 336) / 16));
        unsigned r2 = (i >> 4) & 3, g2 = (i >> 2) & 3, b2 = i & 3;
        return rgb332((r2 << 1) | (r2 >> 1), (g2 << 1) | (g2 >> 1), b2);
    }
    /* Bottom band: sharpness lines, checkerboard, mode label. */
    if (y >= 408 && y < 472) {
        if (x < 320) {
            return (x & 1) ? WHITE : BLACK; /* single-pixel vertical lines */
        }
        if (x < 480) {
            return (((x >> 3) ^ (y >> 3)) & 1) ? WHITE : BLACK;
        }
        return label_pixel(x - 488, y - 412, mode) ? WHITE : BLACK;
    }
    return BLACK;
}

static void draw_card(color_mode_t mode)
{
    static uint8_t line[H_RES];
    for (int y = 0; y < V_RES; y++) {
        for (int x = 0; x < H_RES; x++) {
            uint8_t c = card_pixel(x, y, mode);
            line[x] = (mode == MODE_64) ? to_64_colours(c) : c;
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, H_RES, y + 1, line);
    }
}

/* Whole screen white, for checking the signals with a multimeter. */
static void draw_white(void)
{
    static uint8_t line[H_RES];
    memset(line, WHITE, sizeof(line));
    for (int y = 0; y < V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, H_RES, y + 1, line);
    }
}

static void show_mode(color_mode_t mode)
{
    s_mode = mode;
    draw_card(mode);
    printf("mode: %s colours%s\n", mode == MODE_64 ? "64" : "256",
           s_auto ? " (auto, switches every 10 s)" : "");
}

/* ---- Video output -------------------------------------------------------- */

static void vga_start(void)
{
    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = s_pclk_options[0],
            .h_res = H_RES,
            .v_res = V_RES,
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
        .bounce_buffer_size_px = VGA_BOUNCE_PX,
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

/* ---- Serial console ------------------------------------------------------ */

static void print_help(void)
{
    printf("\nVGA test keys:\n"
           "  1  show the 64-colour card and stay there\n"
           "  2  show the 256-colour card and stay there\n"
           "  a  auto: switch between the two every 10 seconds\n"
           "  w  whole screen white and stay there (for multimeter checks)\n"
           "  c  try the next pixel clock\n"
           "  r  restart the video output\n"
           "  i  print status\n"
           "  h  this help\n\n");
}

static void print_status(void)
{
    printf("640x480, pixel clock %lu Hz, %s colours, auto %s, free PSRAM %u KB\n",
           (unsigned long)s_pclk_options[s_pclk_index],
           s_mode == MODE_64 ? "64" : "256", s_auto ? "on" : "off",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void handle_key(char key)
{
    switch (key) {
    case '1':
        s_auto = false;
        show_mode(MODE_64);
        break;
    case '2':
        s_auto = false;
        show_mode(MODE_256);
        break;
    case 'w':
        s_auto = false;
        draw_white();
        printf("white screen\n");
        break;
    case 'a':
        s_auto = true;
        printf("auto on\n");
        break;
    case 'c':
        s_pclk_index = (s_pclk_index + 1) % NUM_PCLK;
        esp_lcd_rgb_panel_set_pclk(s_panel, s_pclk_options[s_pclk_index]);
        print_status();
        break;
    case 'r':
        esp_lcd_rgb_panel_restart(s_panel);
        printf("video restarted\n");
        break;
    case 'i':
        print_status();
        break;
    case 'h':
    case '?':
        print_help();
        break;
    default:
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "starting VGA output");
    vga_start();

    /* Read single keys from the console UART. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));

    print_help();
    show_mode(MODE_64);
    print_status();

    int64_t last_switch = esp_timer_get_time();
    while (true) {
        uint8_t key;
        if (uart_read_bytes(UART_NUM_0, &key, 1, pdMS_TO_TICKS(100)) == 1) {
            handle_key((char)key);
            last_switch = esp_timer_get_time();
        }
        if (s_auto && esp_timer_get_time() - last_switch >= 10 * 1000000LL) {
            show_mode(s_mode == MODE_64 ? MODE_256 : MODE_64);
            last_switch = esp_timer_get_time();
        }
    }
}
