# doomgeneric

The engine of Doom (id Software, 1993; source released 1997) as
[doomgeneric](https://github.com/ozkl/doomgeneric) has it: Chocolate Doom's
code with the parts that belong to one machine taken out. It is under the
GNU General Public License, version 2 (`LICENSE`). It is the engine of
`programs/doom` (backlog item 81) and of nothing else: it is not linked
into the system, only into the program file `doom.app`, which is under the
same licence as a whole.

Taken from commit `e257a09527062bfb986b6e5e0780839b6cbd5b71` of
`https://github.com/ozkl/doomgeneric`, folder `doomgeneric/`.

**No file here is changed.** What differs from the original is which files
are here:

- Left out, because `programs/doom/port.c` takes their place: `i_system.c`,
  `i_timer.c`, `i_video.c`, `i_input.c`, `doomgeneric.c`, `doomgeneric.h`.
- Left out, because they belong to other machines or to sound, which the
  port does not have: every `doomgeneric_*.c`, `i_sdl*.c`, `i_allegro*.c`,
  `i_cdmus.c`, `i_endoom.c`, `i_scale.c`, `mus2mid.*`, `gusconf.*`,
  `icon.c`, the makefiles and the Visual Studio project.

The engine is compiled with `programs/doom/port.h` put in front of every
file, which gives the C library's names (files, memory, `printf`) to
functions of the port. `programs/doom/engine.txt` lists the files that are
compiled.

No game data is here or anywhere in this repository. The game needs a WAD
file, which the player copies to `C:\Programs`.
