# OS firmware

The start of the real firmware. It brings up the screen and runs the
desktop: window manager, taskbar and Start menu, desktop icons, and the
built-in programs (About Chinchilla, Editor, File manager, Welcome, Widget
gallery). Drive C: is a 2 MB RAM disk in external RAM, filled with a few
sample files at start-up and lost at restart, until a file system on the
flash is chosen. It answers the debug protocol on the serial port. Input is injected over the debug link until there is a USB keyboard and
mouse.

Built with ESP-IDF v5.5 and run on the board on 2026-11-16.

## Build and flash

```sh
get_idf
cd firmware/os
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash
```

The serial port runs at 921,600 baud. The chip's ROM and the bootloader
print their messages at 115,200 baud, so they look garbled in `idf.py
monitor`; the OS's own log lines are sent inside the debug protocol, so read
them with `tools/dbg.py monitor`.

The speed comes from `sdkconfig.defaults`. After changing that file, delete
`sdkconfig` and rebuild, or the old value is kept.

## Try it

From the repository root, with the monitor closed (only one program can use
the port):

```sh
tools/dbg.py info
tools/dbg.py screenshot shot.png
tools/dbg.py screenshot --no-pointer bare.png
tools/dbg.py windows
tools/dbg.py mouse move 400 200          # over About's title bar
tools/dbg.py mouse down
tools/dbg.py mouse move --buttons left 300 100
tools/dbg.py mouse up                    # dropped
tools/dbg.py key tap enter               # About's OK button
tools/dbg.py key tap gui                 # the Start menu
tools/dbg.py key tap esc
tools/dbg.py mouse doubleclick 38 103    # the Documents icon
tools/dbg.py text "año"                  # typed into the focused window
tools/dbg.py time set                    # the clock, from this computer
tools/dbg.py ls C:\\docs
tools/dbg.py put notes.txt C:\\docs\\notes.txt
tools/dbg.py get C:\\docs\\notes.txt copy.txt
tools/dbg.py repaint
tools/dbg.py stats
```

`shot.png` should match what the monitor shows. The UI runs on one task
(`ui_task` in `main/main.cpp`), which also serves the debug link.

## Layout

| File | Contents |
| --- | --- |
| `main/main.cpp` | Start-up and the ESP32 adapters: serial port, lock, screen, pointer, system |
| `main/vga.c` | VGA signal, with the settings proven by `firmware/vga-test`. Owns the one framebuffer and fills the video bounce buffers itself |
| `../../core/` | Everything portable: drawing (`gfx/`), the window manager and widgets (`wm/`), the demo windows (`shell/`), the protocol (`dbg/`) and the pointer (`video/`) |

## How the pointer is drawn

The pointer is never drawn into the framebuffer. The video interrupt copies
10 lines at a time from the framebuffer into a bounce buffer in internal RAM
and calls `video::compose_pointer()` to draw the pointer over them. A new
position or shape takes effect when the interrupt starts a new frame, so the
pointer never tears. `core/linker.lf` puts `compose_pointer()` in internal
RAM, as the interrupt requires. Filling the lines takes about 5 % of one
core; `tools/dbg.py stats` shows the total as `scanout_us`.
