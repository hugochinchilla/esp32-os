/*
 * VGA output through the LCD peripheral. The settings of the first mode are
 * the ones proven with firmware/vga-test on the bench: 25.000 MHz pixel
 * clock and bounce buffers (see docs/DECISIONS.md, "Found on the bench").
 * The second mode, 800x600, is backlog item 68. The third, 320x240, is for a
 * program that has the whole screen (Doom, backlog item 81): to the monitor
 * it is 640x480 still, see `repeat` below.
 *
 * We own the single framebuffer and fill the bounce buffers ourselves, so a
 * line hook (the mouse pointer) can be mixed in on the way out.
 *
 * A mode may ask for the frame's third piece to be copied ahead of time,
 * in the vertical blank (`ahead` below, docs/decisions/scanout-ahead.md).
 */
#include "vga.h"

#include <stddef.h>
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

/* The modes. 640x480 is the one proven on the bench first; its 25.000 MHz is
 * 160 MHz / 6.4. 800x600 at 60 Hz is the standard's timing as it stands:
 * 40 MHz is 160 MHz / 4, a whole division. Both syncs idle high and pulse
 * low in the first, idle low and pulse high in the second, as the standards
 * say (a monitor may tell the modes apart by it). */
typedef struct {
    uint16_t width, height;
    uint32_t pixel_clock_hz;
    uint16_t h_front, h_sync, h_back;
    uint16_t v_front, v_sync, v_back;
    bool sync_high;
    /* Copy the frame's third piece to internal RAM in the vertical blank.
     * The first refill inside a frame comes a whole blank (about 1 ms) after
     * the cache was asked to fetch its piece, and under load the cache has
     * let it go by then, so the copy runs at the speed of the external
     * RAM's bus. At 800x600 that takes most of the 264 us between refills,
     * the fetch for the next piece starts late, and the refills of lines 30
     * to 50 are not ready in time. 640x480 has 320 us for 6,400 bytes and
     * missed nothing, so it is left as it was. */
    bool ahead;
    /* How many times each line of the picture is sent (1, or 2). A monitor
     * takes no signal of 240 lines: it wants the 525 lines of 640x480, 31.5
     * kHz of them. So the coarse mode sends that very signal with the pixel
     * clock halved, which makes every pixel twice as wide and is nothing
     * the monitor can see, and sends each line twice, which the refill
     * does as it copies. It is what a VGA card did with the 320x200 of the
     * games of its day. `height` is the picture's; the signal has `repeat`
     * times as many lines. */
    uint8_t repeat;
} vga_mode_t;
static const vga_mode_t s_modes[] = {
    {640, 480, 25000000, 16, 96, 48, 10, 2, 33, false, false, 1}, /* 800 x 525 in all, 59.52 Hz */
    {800, 600, 40000000, 40, 128, 88, 1, 4, 23, true, true, 1},   /* 1056 x 628 in all, 60.32 Hz */
    {320, 240, 12500000, 8, 48, 24, 10, 2, 33, false, false, 2},  /* 400 x 525 in all, 59.52 Hz */
};
#define MODE_COUNT ((int)(sizeof(s_modes) / sizeof(s_modes[0])))
#define BOUNCE_LINES 10

static esp_lcd_panel_handle_t s_panel;
static uint8_t *s_framebuffer;
/* One bounce buffer of picture in internal RAM, for a mode with `ahead`:
 * the frame's third piece, copied when the second was handed over. Only the
 * interrupt touches the two once the panel runs. */
static uint8_t *s_ahead;
static bool s_ahead_ready;
/* The mode in use, as the interrupt needs it: in internal RAM, not read
 * from the table (which is constant data, behind the cache). */
static int s_mode;
static int s_modes_used = 1;     /* vga_start(modes) */
static size_t s_picture_bytes;   /* the memory behind s_framebuffer */
static int s_width = 640, s_height = 480;
static int s_frame_bytes = 640 * 480; /* of the signal: the picture's, times s_repeat */
static int s_repeat = 1;
static uint32_t s_period_cycles;
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
#define PERIOD_CYCLES s_period_cycles /* 320 us at 640x480, 264 us at 800x600 */
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
    if (s_ahead_ready && pos_px == 2 * len_bytes) {
        memcpy(bounce_buf, s_ahead, len_bytes); /* copied in the blank, see below */
    } else if (s_repeat == 2) {
        /* Each line of the picture twice: the first copy from the picture,
         * the second from the first, which is in internal RAM. A bounce
         * buffer holds an even number of lines and starts on an even one. */
        const int width = s_width;
        const uint8_t *from = s_framebuffer + pos_px / 2;
        uint8_t *to = bounce_buf;
        for (int done = 0; done < len_bytes; done += 2 * width) {
            memcpy(to, from, (size_t)width);
            memcpy(to + width, to, (size_t)width);
            from += width;
            to += 2 * width;
        }
    } else {
        memcpy(bounce_buf, s_framebuffer + pos_px, len_bytes);
    }
    s_ahead_ready = false;
    vga_line_hook_t hook = s_repeat == 1 ? s_line_hook : NULL; /* no pointer on a coarse picture */
    if (hook) {
        hook(bounce_buf, pos_px, len_bytes);
    }
    /* Start fetching the next piece from PSRAM, as the driver does when it
     * owns the framebuffer. */
    int next = pos_px + len_bytes;
    if (next >= s_frame_bytes) {
        next = 0;
    }
    /* The frame's second piece is asked for when the last lines of the
     * frame before have gone out. Nothing more is asked for until the blank
     * is over and the first piece has been shown (about 1 ms), so this is
     * the time to copy the third, however slow the external RAM is now. It
     * is not timed as a refill: nothing waits for it. */
    const bool ahead = s_ahead != NULL && pos_px == len_bytes && s_frame_bytes >= 3 * len_bytes;
    if (!ahead) {
        Cache_Start_DCache_Preload((uint32_t)(s_framebuffer + next / s_repeat), len_bytes / s_repeat, 0);
    }
    const uint32_t end = esp_cpu_get_cycle_count();
    const uint32_t fill = end - start;
    s_fill_cycles += fill;
    s_scan.refills++;
    if (fill > s_scan.max_fill_cycles) {
        s_scan.max_fill_cycles = fill;
    }
    const unsigned place = (unsigned)pos_px / (unsigned)len_bytes;
    if (place < VGA_SCAN_PLACES && fill > s_scan.max_fill_at[place]) {
        s_scan.max_fill_at[place] = fill;
    }
    if (ahead) {
        memcpy(s_ahead, s_framebuffer + next, len_bytes);
        s_ahead_ready = true;
        const uint32_t took = esp_cpu_get_cycle_count() - end;
        s_fill_cycles += took;
        if (took > s_scan.max_ahead_cycles) {
            s_scan.max_ahead_cycles = took;
        }
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

static uint32_t frame_us(const vga_mode_t *m)
{
    const uint64_t h = m->width + m->h_front + m->h_sync + m->h_back;
    const uint64_t v = m->height * m->repeat + m->v_front + m->v_sync + m->v_back;
    return (uint32_t)(h * v * 1000000 / m->pixel_clock_hz);
}

/* Sets the panel up in a mode and starts it. The picture's memory is there
 * already; no panel exists. */
static void start_mode(int index)
{
    const vga_mode_t *m = &s_modes[index];
    s_mode = index;
    s_width = m->width;
    s_height = m->height;
    s_repeat = m->repeat;
    s_frame_bytes = m->width * m->height * m->repeat;
    /* One bounce buffer of picture with its share of the blanking. */
    s_period_cycles = (uint32_t)((uint64_t)BOUNCE_LINES * (m->width + m->h_front + m->h_sync + m->h_back) *
                                 (CYCLES_PER_US * 1000000ULL) / m->pixel_clock_hz);
    s_last_pos = -1;
    memset(s_framebuffer, 0, s_picture_bytes);
    /* A new mode has new timings: what was measured in the last one would
     * only mislead. */
    memset((void *)&s_scan, 0, sizeof(s_scan));
    /* Without the memory the mode still works, as it did before. */
    s_ahead_ready = false;
    s_ahead = m->ahead ? heap_caps_malloc((size_t)BOUNCE_LINES * m->width, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                       : NULL;

    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = m->pixel_clock_hz,
            .h_res = m->width,
            .v_res = m->height * m->repeat,
            .hsync_pulse_width = m->h_sync,
            .hsync_back_porch = m->h_back,
            .hsync_front_porch = m->h_front,
            .vsync_pulse_width = m->v_sync,
            .vsync_back_porch = m->v_back,
            .vsync_front_porch = m->v_front,
            .flags = {
                .hsync_idle_low = m->sync_high,
                .vsync_idle_low = m->sync_high,
            },
        },
        .data_width = 8,
        .bits_per_pixel = 8,
        .num_fbs = 0,
        .bounce_buffer_size_px = BOUNCE_LINES * m->width,
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

void vga_start(int modes)
{
    /* The one picture in memory, large enough for the largest mode that
     * will be used, so that its address never changes and a task that is
     * reading it while the mode changes reads memory that is still there.
     * Allocated before the panel, because the panel starts filling bounce
     * buffers as soon as it is set up. */
    s_modes_used = modes < 1 ? 1 : (modes > MODE_COUNT ? MODE_COUNT : modes);
    for (int i = 0; i < s_modes_used; i++) {
        const size_t bytes = (size_t)s_modes[i].width * s_modes[i].height;
        s_picture_bytes = bytes > s_picture_bytes ? bytes : s_picture_bytes;
    }
    s_framebuffer = heap_caps_aligned_alloc(64, s_picture_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_ERROR_CHECK(s_framebuffer ? ESP_OK : ESP_ERR_NO_MEM);
    start_mode(0);
}

void vga_set_mode(int index)
{
    if (index < 0 || (index >= s_modes_used && index != VGA_MODE_COARSE) || index == s_mode) {
        return;
    }
    /* The panel goes with its interrupt, its DMA channel and its two bounce
     * buffers (internal RAM), and a new one is made: the buffers are 10
     * lines of the mode each. */
    ESP_ERROR_CHECK(esp_lcd_panel_del(s_panel));
    s_panel = NULL;
    heap_caps_free(s_ahead); /* the interrupt went with the panel */
    s_ahead = NULL;
    start_mode(index);
}

int vga_mode_count(void)
{
    return s_modes_used;
}

void vga_mode_info(int index, vga_mode_info_t *out)
{
    const vga_mode_t *m = &s_modes[index >= 0 && index < MODE_COUNT ? index : 0];
    out->width = m->width;
    out->height = m->height;
    out->pixel_clock_hz = m->pixel_clock_hz;
    out->frame_us = frame_us(m);
}

int vga_mode(void)
{
    return s_mode;
}

int vga_width(void)
{
    return s_width;
}

int vga_height(void)
{
    return s_height;
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
    *line = s_missed_pos / (uint32_t)s_width;
}

void vga_scan_stats(vga_scan_stats_t *out, bool reset)
{
    *out = s_scan;
    if (reset) {
        s_scan.refills = s_scan.late = s_scan.missed = 0;
        s_scan.max_late_cycles = s_scan.max_fill_cycles = s_scan.max_ahead_cycles = 0;
        for (int i = 0; i < VGA_SCAN_PLACES; i++) {
            s_scan.max_fill_at[i] = 0;
        }
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
    return s_modes[s_mode].pixel_clock_hz;
}

uint32_t vga_frame_count(void)
{
    return s_frames;
}
