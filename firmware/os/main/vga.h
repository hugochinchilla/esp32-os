/* VGA output, 640x480 at 60 Hz, one byte per pixel (RRRGGGBB).
 * Wiring and resistor values: docs/hardware/README.md */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGA_WIDTH 640
#define VGA_HEIGHT 480

/* Starts the video signal. Stops the program if it cannot. */
void vga_start(void);

/* The picture being shown: VGA_WIDTH * VGA_HEIGHT bytes, rows top to bottom.
 * Writing to it changes the screen. */
uint8_t *vga_framebuffer(void);

/* Number of frames sent to the monitor since start. */
uint32_t vga_frame_count(void);

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
