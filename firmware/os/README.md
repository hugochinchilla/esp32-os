# OS firmware

The start of the real firmware. For now it brings up the screen, shows a demo
screen (a mouse pointer, three boxes for the mouse buttons, a grid that lights
up for each key held down) and answers the debug protocol on the serial port.

Not yet compiled against ESP-IDF or run on the board.

## Build and flash

```sh
get_idf
cd firmware/os
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

The serial port runs at 921,600 baud. `idf.py monitor` still works and shows
the boot messages as text, but the OS's own log lines are sent inside the
debug protocol; read them with `tools/dbg.py monitor`.

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
