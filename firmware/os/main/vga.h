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

#ifdef __cplusplus
}
#endif
