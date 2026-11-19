/* VGA output, one byte per pixel (RRRGGGBB), in one of two modes: 640x480 at
 * 60 Hz (the one it starts in) and 800x600 at 60 Hz.
 * Wiring and resistor values: docs/hardware/README.md */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGA_MODES 2 /* all of those a screen can be set to */
/* A third, 320x240, for a program that has the whole screen: the signal of
 * 640x480 with every pixel twice as wide and every line sent twice. It is
 * not counted by vga_mode_count(); vga_set_mode() takes it at any time and
 * needs no more memory than the picture already has. */
#define VGA_MODE_COARSE 2

typedef struct {
    uint16_t width, height;
    uint32_t pixel_clock_hz;
    uint32_t frame_us; /* one frame with its blanking */
} vga_mode_info_t;

/* Starts the video signal in mode 0. `modes`: how many of the modes will
 * be used (1: only the first, as the BIOS does); the picture's memory is
 * sized once, for the largest of them. Stops the program if it cannot. Call
 * it from a task tied to the core the video interrupt is to run on. */
void vga_start(int modes);

/* The modes; index 0 is 640x480. */
int vga_mode_count(void);
void vga_mode_info(int index, vga_mode_info_t *out);
int vga_mode(void);
/* Stops the signal and starts it again in another mode; the picture is
 * black afterwards. Nothing may be drawing meanwhile. Like vga_start(), from
 * a task tied to the video's core. Stops the program if it cannot. */
void vga_set_mode(int index);
/* The size of the mode in use. */
int vga_width(void);
int vga_height(void);

/* The picture being shown: vga_width() * vga_height() bytes, rows top to
 * bottom. Writing to it changes the screen. The address is the same in
 * every mode. */
uint8_t *vga_framebuffer(void);

/* Number of frames sent to the monitor since start. */
uint32_t vga_frame_count(void);

/* Scanout timing since the last reset or change of mode (see vga.c). */
#define VGA_SCAN_PLACES 8
typedef struct {
    uint32_t refills;         /* bounce buffers filled */
    uint32_t late;            /* refills started more than 32 us after their slot */
    uint32_t missed;          /* refills not finished when the DMA needed them */
    uint32_t max_late_cycles; /* worst start after the slot */
    uint32_t max_fill_cycles; /* longest refill */
    uint32_t histogram[6];    /* refill intervals: <200, <300, <340, <400, <640, more us */
    uint32_t last_gap_pos;    /* where the last long gap between refills was */
    uint32_t max_fill_at[VGA_SCAN_PLACES]; /* longest refill of each of the frame's first pieces, from the top */
    uint32_t max_ahead_cycles; /* longest copy made in the vertical blank (0: the mode makes none) */
} vga_scan_stats_t;
void vga_scan_stats(vga_scan_stats_t *out, bool reset);

/* Missed refills since start, never reset (vga_scan_stats can be). worst_us
 * is the worst lateness of a miss since the last call (then cleared); line is
 * the screen line where the most recent miss was. */
void vga_missed_total(uint32_t *missed, uint32_t *worst_us, uint32_t *line);

/* The pixel clock the panel was set up with, in Hz. */
uint32_t vga_pixel_clock_hz(void);

/* Called for every piece of picture on its way to the monitor, after it was
 * copied from the framebuffer: `pixels` holds `count` pixels starting at
 * pixel number `first` of the frame (first == 0 starts a new frame). Runs in
 * the video interrupt: must be in IRAM, short, no logging, locks or heap. */
typedef void (*vga_line_hook_t)(uint8_t *pixels, int first, int count);
void vga_set_line_hook(vga_line_hook_t hook);

/* Time spent filling video lines since start, in microseconds (wraps). */
uint32_t vga_fill_us(void);

#ifdef __cplusplus
}
#endif
