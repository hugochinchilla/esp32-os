# Chinchilla on a PC: where the screenshots come from, and an idea

2026-11-17. A conversation; nothing was built.

## What was asked

After a run of screenshots of work that "has not run on the board", the
owner:

> ¿Cómo generas todas estas capturas cuando dices que esto no se ha
> compilado para la placa ni se ha probado nunca en ella? ¿Estamos
> ejecutando una especie de máquina virtual en mi PC con el sistema
> operativo?

## What was found

No virtual machine. Everything that is the system (window manager, desktop,
programs, icons, drawing) is in `core/`, which may not include any ESP-IDF
header and only writes 256-colour pixels into 640x480 bytes of memory. The
hardware (VGA, USB, flash, Wi-Fi) is behind small adapters in `firmware/`.
So the same `core/` also builds as an ordinary Linux program.
`host/tests/render_scenes.cpp` starts the desktop in memory, feeds it made-up
clicks and keys, and saves the memory as PNG files: the exact pixels the
board would send to the monitor. That runs in the agent's cloud machine, not
on the owner's PC.

What it cannot show: that the firmware compiles for the ESP32, the colours
through the resistors and a real monitor, speed, real USB devices, RAM and
flash. That is what the board turn is for.

`host/sim.cpp` is a simulated board that speaks the debug protocol; it has
no window of its own.

## The idea

The owner's next message:

> ¿Significa esto que podríamos hacer como una aplicación de escritorio para
> Linux en la que yo puedo ejecutar como si hubiera una máquina virtual con
> Chinchilla OS y probar las aplicaciones y compartirlo con mis amigos?

Yes. Two ways were put to him:

1. **A desktop program for Linux, with SDL2.** A window, the real mouse and
   keyboard, sound, a folder as C:. The quickest. Friends need Linux and the
   program.
2. **In the browser, with WebAssembly.** A link that opens anywhere with
   nothing to install; better for sharing and for the videos. More work: C:
   kept in the browser, a page to publish.

They share nearly everything, so the first leaves the second half done.
Neither is an emulator of the ESP32: it is the same system running straight
on the PC, far faster than the board, with no real Wi-Fi and no memory
limits. Good for trying and showing programs, not for measuring.

## What was chosen

The owner: "Haremos la opción de escritorio primero y luego la de web, pero
como tareas nuevas en el backlog, no como parte de este hilo." The
recommendation was the same order. Backlog items 32 and 33.
