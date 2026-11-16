# The picture shook while the flash was written

## What was asked

With the first LittleFS build the owner wrote:

> the screen has started to rattle, can you grab it on webcam and diagnose?

## What was found

On the webcam, every other frame had bands shifted sideways for the whole
time a file was being written: the change from frame to frame reached 38.8
against 0.4 when still. Two debug-link frames were lost at the same time.

The cause: writing the flash turns the CPU cache off. The video picture is a
single framebuffer in PSRAM, and every 320 us an interrupt refills a bounce
buffer from it for the LCD peripheral's DMA. With the cache off that refill
cannot read PSRAM, so lines came out late and shifted.

## Options considered

- Pausing the video, or blanking it, during writes: visible every save.
- Copying the framebuffer to internal RAM: 300 KB, which internal RAM does not
  have.
- Espressif's advice for RGB panels with a PSRAM framebuffer: run the app's
  code and constants from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`) and keep the
  LCD interrupt in internal RAM (`CONFIG_LCD_RGB_ISR_IRAM_SAFE`), so nothing
  the refill needs lives behind the flash cache.

## What was chosen and why

The last: no visible cost, and it is the documented way.

## What was built and tested

Two lines in `firmware/os/sdkconfig.defaults`. On the board: 0 shaken frames
while writing, 59.5 frames a second, no lost frames, and debug-link writes
2.6 times faster (the flash no longer stalls the code). It costs about 610 KB
of external RAM for the code and constants; a full repaint still took
27.5 ms.

## What was learned

The owner then saw a different, smaller disturbance later the same day ("It
may be something you do"): the agent had been restarting the board for
tests without saying so. Since then every restart or flash is announced
first. The remaining lines turned out to be the radio; see
[the Wi-Fi scanlines](2026-11-16-wifi-scanlines.md).

## What is left

Nothing.
