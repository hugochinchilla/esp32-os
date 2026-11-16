# OS firmware

The start of the real firmware. For now it brings up the screen, shows a demo
screen (a mouse pointer, three boxes for the mouse buttons, a grid that lights
up for each key held down) and answers the debug protocol on the serial port.

Built with ESP-IDF v5.5 and run on the board on 2026-11-16: the demo screen,
screenshots, injected keys and mouse all work over the debug link.

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
tools/dbg.py mouse move 100 100
tools/dbg.py mouse click
tools/dbg.py key down a
tools/dbg.py stats
```

The pointer should move on the monitor, and `shot.png` should match what the
monitor shows.

## Layout

| File | Contents |
| --- | --- |
| `main/main.cpp` | Start-up and the ESP32 adapters: serial port, lock, screen, system |
| `main/vga.c` | VGA signal, with the settings proven by `firmware/vga-test` |
| `../../core/` | Everything portable: the protocol and the demo screen |
