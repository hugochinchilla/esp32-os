# 2026-11-16: Architecture review, and the plan to turn Chinchilla into more of an OS

This entry records a backlog-grooming conversation between the owner (Hugo) and
an agent, the review the agent wrote in answer, the twelve design choices put to
the owner, and what he chose. Nothing was built in this session; the work it
leads to is tracked in [`docs/BACKLOG.md`](../BACKLOG.md), and the outcomes are
in [`docs/DECISIONS.md`](../DECISIONS.md) under "OS architecture".

Code references are to `main` at commit `dd43f75`.

## 1. What the owner asked

The owner speaks Spanish and asked for answers in English. His message, in his
own words:

> Vamos a hacer un poco de grooming de backlog, ¿vale? [...] me gustaría hacer
> una lista de deuda técnica de lo que es el prototipo y qué cosas faltan para
> que esto se considere un intento serio de sistema operativo. [...]
>
> Tenemos por un lado el monitor de procesos, que comentaste por ahí que hay una
> forma más correcta de monitorizar el uso de recursos de las aplicaciones, que
> ahora se están midiendo desde el GUI. No entiendo muy bien qué significa eso y
> que hacerlo de otra forma requería cambios en el firmware. Quiero que me lo
> expliques más en detalle. No sé si es viable tener prioridades en los procesos
> o si eso implica meter mano al scheduler de FreeRTOS y no es una cosa que por
> defecto esté soportada. Guardar cambios a disco en segundo plano sería algo
> ideal para que las aplicaciones no se congelen mientras se escriben. Pero una
> aplicación, por ejemplo, no puede salir sin esperar a que el guardado en
> segundo plano complete. O sea, debería hacer algo como un fork y siempre que
> quiera cerrar una aplicación debería esperar a que todos los forks existentes
> salgan primero, se completen. También me gustaría que en algún momento, cuando
> se quiere finalizar una tarea, la tarea se finalice mandándole un evento, tipo
> el kill en Linux, y que haya diferentes señales, ¿no? Terminación forzosa,
> sugerir un acabado, algo tipo un SIGTERM. Estaría bien añadir una shell con
> comandos tipo Linux, ls, cd. No sé si llegaríamos a tener un editor de texto
> en la consola. De momento con un comando edit, nombre de fichero, y que se
> abra la ventana del editor gráfico puede ser suficiente. Más adelante eso no
> será suficiente porque me gustaría poder conectarme a la shell por telnet
> desde una máquina remota. También me gustaría que la terminal de consola de la
> máquina se comportara como una consola en un sistema operativo Linux, que la
> verdad que no lo conozco muy bien porque nunca las he usado, pero creo que te
> conectan a una TTY. Entonces pues estaría interesante si hubiera un subproceso
> de TTY con la consola. Sé que eso va en contra de toda la mecánica que tenemos
> ahora de depuración montada. Para solucionarlo, lo que me imagino es que a
> través de esta TTY podríamos invocar el proceso de debugger que abre el
> proceso de comunicación que hay ahora, que permite transferir capturas de
> pantalla y comandos remotos para el diagnóstico remoto que están usando los
> agentes para trabajar. El proceso este TTY, por ejemplo, sería un proceso que
> se debería ver en la lista de procesos del gestor de procesos. Debería haber
> protección de procesos que no deberían poder ser finalizados por el usuario,
> ¿no? Por ejemplo, no se debería poder matar la TTY de la consola, ya que
> rompería la capacidad de los agentes de comunicarse con la aplicación. [...]
> No entiendo muy bien cómo está funcionando ahora mismo el sistema operativo a
> nivel de diseño. [...] en Linux pues hay un PID número 1 y todo es hijo de él.
> Yo lo que percibo cuando arranca la máquina es que hay cosas que arrancan como
> de golpe. En plan, todo el entorno gráfico arranca instantáneamente como una
> unidad. En una computadora de los 90 yo lo que veía es que primero cargaba el
> escritorio vacío y luego se arrancaba la aplicación de la que gestiona la
> barra de abajo, la que tiene el menú de inicio y los selectores de ventana y
> aparecían los iconos en el escritorio. Daba la impresión de que eran como
> diferentes capas de procesos que se iban apilando unas encima de otras. No sé
> si eso es algo que tiene sentido o no, solo quiero contemplarlo. Me gustaría
> un comando tipo top en el CLI, que sería como el gestor de tareas, pero en
> visualización de consola. Me gustaría que los procesos tuvieran un PID de
> proceso como en Linux, de forma que yo pudiera hacer un kill de los procesos,
> con la opción de si es un kill forzado o sugerir a la aplicación que se
> cierre. No sé si muchas de estas cosas son features de diseño moderno de
> sistemas operativos y no eran comunes en los 90 y a lo mejor implican un
> cambio radical del sistema operativo como está escrito ahora. Entonces todo lo
> que te digo son como ideas que quiero que evaluemos y quiero que me expliques
> cómo están ahora, qué cambios necesitarían hacerse para soportar unas
> características más modernas y en general ¿qué cosas crees tú que faltan por
> añadir? Esto sigue siendo un juguete, es un hobby, pero me está sirviendo para
> aprender y entonces quiero seguir estirándolo para aprender más cosas.

(The message was dictated; a few transcription slips are corrected above:
"lscd" to "ls, cd", "TTI" to "TTY", "FreeRTOs", "electores", "estaqueando". The
part about a Start menu bug is left out: another thread fixed it.)

In short, he asked for: an explanation of how the OS works today; why Task
Manager's figures are "measured from the GUI"; and an evaluation of process
priorities, background saves with a parent waiting for its children, PIDs, kill
signals, protected processes, a Linux-like shell with `top`, telnet, a TTY on
the serial console that can start the debug protocol, and an init process with
a tree and a layered start-up. Then: what else is missing.

## 2. The review, as delivered

### Short version

Chinchilla today is one program with one worker. Every app, the taskbar, the
Start menu, the debug link and the VNC server take turns on a single FreeRTOS
task called `ui`. That is the Windows 3.1 and classic Mac OS design, known as
cooperative multitasking. It explains the three things the owner noticed: the
desktop appears all at once, Task Manager can only report "the desktop" as one
lump, and a slow save freezes the screen.

Almost everything on the list can be built without touching the FreeRTOS
scheduler and without a rewrite. The ideas look like ten features, but they
rest on the same three missing pieces:

1. **A process table.** A small list kept by the OS: PID, name, parent, state,
   flags, and what each process owns (windows, timers, background jobs,
   memory). PIDs, `kill`, `top`, protected processes and the init tree are all
   views of this one table.
2. **Safe ways to work off the UI task.** A message queue into the desktop, and
   a lock on the file system. Background saves, the shell, telnet and the
   serial console all need other tasks to touch files and windows safely.
3. **A terminal (TTY) object.** One text channel with three possible ends: the
   serial port, a telnet socket, or a terminal window. The shell is written
   once and runs on any of them.

The one thing this chip cannot give is memory isolation between programs. All
code shares one address space, so a forced kill can never be as clean as on
Linux. The closest historical relative is AmigaOS (1985): real preemptive tasks
with priorities, one shared memory space, no protection. That is a respectable
design to aim for, and it was a real 90s desktop OS.

### How Chinchilla works today

**The layers.** FreeRTOS is the real kernel. It owns the scheduler, the two
processor cores, and the tasks. A FreeRTOS task is what Linux would call a
thread: its own stack and priority, but shared memory with everything else.
Chinchilla's own code sits on top and currently uses exactly one long-lived
task.

| Task | What it does | Priority | Lives |
| --- | --- | --- | --- |
| `ui` | Everything Chinchilla does: debug link, input, timers, all painting, all apps, VNC and web server | 5 | Forever |
| `video` | Starts the VGA output on core 1, then exits. The video interrupt stays on core 1 | 10 | A moment at start-up |
| `scanlog` | Records video timing counters for the first 20 seconds | 1 | 20 seconds |
| ESP-IDF's own | Wi-Fi, the network stack (core 0), software timers, mDNS, one idle task per core | Mostly above `ui` | Forever |

Source: `firmware/os/main/main.cpp`, `app_main` and `ui_task`.

**What an "app" is.** An app is not a process. It is a C++ window object
compiled into the firmware, plus one row in a table: name, icon, and a function
that opens its window (`core/include/core/shell/shell.hpp`). The Start menu and
the desktop icons just call that function.

An app never "runs" on its own. The window manager calls into it when something
happens: a key, a click, a paint request, or a timer it asked for. The app must
return quickly, because nothing else happens until it does. Long work is cut
into slices by hand. The Picture viewer decodes for up to 8 ms every 15 ms from
a timer. The Editor does not slice its save, which is why a 293 KB save holds
the whole desktop for 3.3 seconds.

Consequences of this model:

- Each app exists once, as a fixed object. There cannot be two Editors.
- There is no PID, no owner, and no list of what an app holds. "Programs
  running" in Task Manager is really the list of windows that have a taskbar
  button.
- Apps take their large memory on first use and keep it. The viewer keeps about
  2.3 MB after it closes, the Editor 0.6 MB.
- End Task simply closes the window without asking to save. That is safe only
  because nothing is ever freed.
- If an app loops forever inside one call, everything stops, including Task
  Manager and the debug link. Nothing can rescue it.

**Start-up, step by step.**

1. `app_main` starts the video on core 1, installs the serial driver, and
   creates the `ui` task.
2. The `ui` task brings up the debug link and mounts C:.
3. The self-test screen runs with its countdown, then the optional splash.
4. Wi-Fi starts. The VNC port, the web port and the `chinchilla.local` name are
   opened.
5. `shell::start` fills the Start menu from the program table, sets the desktop
   icons, reads the Desktop folder and starts a few timers.
6. `desktop.start()` paints the whole screen once. That takes about 29 ms.
7. The loop runs forever: read serial bytes, feed the debug protocol, run
   timers and painting, serve the VNC viewer. It waits at most 10 ms per round.

So the desktop appears as a unit because it is a unit. The wallpaper, icons,
taskbar, Start button and clock are all parts of one object (`wm::Wm`) drawn in
one paint. On a 90s PC they were separate programs loaded from a slow disk one
after another, which is the layering the owner remembers.

**How Task Manager measures.** "Measured from the GUI" means the desktop times
itself with a stopwatch. Each time the UI loop handles an event, a timer or a
repaint, it adds the elapsed microseconds to a counter (`Wm::busy_us`). Task
Manager reads that counter once a second and shows it as "Desktop". "Video out"
is the same trick inside the video interrupt.

What this cannot see:

- Which program used the time. All apps are one lump.
- Wi-Fi, the network stack, or anything else that is not the UI loop.
- The second core, or how idle the machine really is.
- Memory per program. It shows free memory for the whole machine only.

The firmware-level alternative is to ask FreeRTOS itself. With a build setting
switched on, the kernel notes the clock at every task switch and keeps a
running total per task. One call (`uxTaskGetSystemState`, already used by
`dbg.py memory` for stacks) then returns every task's name, state, priority,
processor time and unused stack. ESP-IDF can also tag each heap allocation with
the task that made it, at a cost of a few bytes per allocation. That is the
"change in the firmware": two or three lines in `sdkconfig.defaults` and a new
function in the System port. It is small.

The catch is that FreeRTOS counts per task, and all apps share one task. Kernel
statistics alone would say "ui used 12 %", not "Culebra used 12 %". Per-program
figures need one of two things: charge the stopwatch to whichever program the
window manager is calling at that moment, or give programs their own tasks.

### The design question underneath the ideas

Most of the ideas depend on one choice: what is a process in Chinchilla? There
are three workable answers.

| Model | How it works | Who did this | Cost here |
| --- | --- | --- | --- |
| A. Cooperative only | Keep one task. A process is a row in a table that owns windows and timers | Windows 3.1, Mac OS up to 9 | Small. But a stuck app still freezes everything, and the shell and telnet must be written as hand-sliced state machines |
| B. One task per app | Every app gets a FreeRTOS task and its own message queue. The window manager becomes a server | Windows 95 (32-bit apps), AmigaOS, Unix with X | Very large. The window manager and every widget assume one task. Each task also needs 4 to 8 KB of scarce internal RAM for its stack |
| C. Hybrid | Window code stays on the UI task. A process may also own real tasks: background jobs, shell sessions, services | Close to how Windows 95 treated 16-bit apps next to 32-bit ones | Medium, and it can be built in steps |

Recommended: C. It gives PIDs, signals, `top`, the shell, telnet, the TTY and
background saves, and it keeps the tested window code as it is. Individual apps
can move to their own task later, one at a time, once the message queue exists.

One budget to keep in mind: with Wi-Fi on, about 142 KB of internal RAM is
free. Task stacks normally live there. A dozen extra tasks is fine, fifty is
not.

### The ideas, one by one

Sizes: S is one branch in one sitting. M is a few sittings. L is a milestone of
its own. XL touches most of the code.

| Idea | In 90s systems? | Size | Recommendation |
| --- | --- | --- | --- |
| PIDs and a process table | Unix yes. Windows 95 had them inside but never showed them | M | Do first. Everything else hangs off it |
| Real per-process figures in Task Manager | Windows NT yes, Windows 95 no, Unix `top` yes | S to M | Do with the process table |
| Kill signals, polite and forced | Unix yes. Windows had "close" and "terminate", not signals | M | Do, with three signals to start |
| Protected processes | NT and Unix yes. Windows 95 let you kill anything | S | Do. It is a flag in the table |
| Process priorities | Yes: Amiga, NT, Windows 95, Unix `nice` | S | Do. FreeRTOS supports it as it is |
| Background saves, wait for children | Rare. Word had it. Notepad froze, like ours | M | Do, as "jobs" owned by a process |
| Shell with `ls`, `cd`, `edit`, `top` | Yes, every one of them | L | Do, in the portable core |
| Telnet into the shell | Yes, very much of the era | S after the shell | Do |
| Serial console as a TTY that can start the debug link | Unix yes | M | Do, but let both share the wire instead of switching modes |
| init as PID 1, a process tree, layered start-up | Unix yes. Windows had the layering without the tree | M for the tree, L for splitting the desktop | Do the tree. Split the desktop later |

**PIDs and the process table.** Today nothing records who owns what. The change
is to add a process table in the portable core and make every window, timer,
job and large memory block carry its owner's PID.

- A process gets a number that counts up from 1 and is not reused until it
  wraps.
- Each row holds: PID, parent PID, name, state, flags, processor time used,
  memory held, and its terminal if it has one.
- Launching a program from the Start menu creates a row. Closing its last
  window and finishing its jobs removes it.
- Services that already exist become rows too: the VNC server, the web server,
  the Wi-Fi manager, the debug link.

This is not invasive. The window manager gains one field per window and per
timer, and `launch()` gains a few lines. One limit stays for now: each app is a
single fixed object, so there is one process per program.

**Honest per-process figures.** Two sources, both to be used:

1. FreeRTOS run-time statistics, for the task view: `ui`, Wi-Fi, network, idle
   on each core, plus every job and shell session. This shows real idle time
   and what the radio costs. It is a firmware-only change.
2. Charging the stopwatch to a PID, for programs that share the UI task. The
   window manager already times every call it makes into an app. It only needs
   to add that time to the owner's row instead of one global counter. This
   gives "Culebra 3 %, Editor 0 %" with no new tasks.

Memory per process works the same way. Large blocks already go through one door
(the Memory port), so each can be tagged with the PID that asked. That also
makes it possible to give memory back when a program exits.

**Priorities.** The scheduler does not need touching. FreeRTOS has fixed
priorities built in (25 levels in ESP-IDF), and a task's priority can be
changed while it runs with one call.

One thing differs from Linux. Linux `nice` shares the processor: a low-priority
program runs slower. FreeRTOS priorities are strict: a higher-priority task
that never sleeps starves everything below it completely. Tasks of equal
priority take turns.

So the proposal is three classes (Low, Normal, High) mapped to a narrow band
that always sits below the system: video, Wi-Fi and the network stack stay on
top. Priority only means something for things that are real tasks, such as
jobs, shell sessions and services. For window code on the shared UI task it has
no effect until an app moves to its own task.

**Background saves and waiting for children.** What the owner described as a
fork is, on this chip, a thread. A real fork copies the whole memory space of a
process, which needs hardware Chinchilla does not have. The useful part of the
idea survives intact: a process starts a child job, and cannot exit until its
jobs have finished. Unix calls that `wait`.

1. The Editor asks the OS to run a job: "write this buffer to this path". The
   OS runs it on a worker task that belongs to the Editor's PID.
2. The rest of the desktop stays responsive. The Editor says "Saving...".
   Typing is either blocked for that window only, or the job works on a copy of
   the text (up to 512 KB of external RAM).
3. When the job ends, it posts a message to the UI task, and the Editor clears
   the star in its title.
4. Closing the window hides it at once, but the process stays in the table in a
   "closing" state until its jobs end. Shut Down waits for all jobs and shows
   which ones.

Two things must exist first: a lock on the file system (it is written for one
task today) and the message queue into the UI task. Both are small and both are
needed by the shell anyway.

One safety change goes with it. Saves should write to a temporary file and
rename it over the original at the end. Today a save empties the file first, so
a power cut or a kill in the middle loses the document.

Is it 90s? Barely. Word had an "allow background saves" option. Most programs
froze with an hourglass. A cheaper, more period-correct alternative is to slice
the save on a timer like the Picture viewer does, with a progress bar.

**Kill signals.** The polite half already exists: the close button asks the
program to close, and the Editor may answer with "save changes?". The forced
half exists too, as End Task. Turning them into signals means giving them
numbers and one delivery path. Three to start:

| Signal | Meaning | What the OS does |
| --- | --- | --- |
| TERM | Please close | Same path as the close button. The program may ask to save, or refuse |
| KILL | Close now | The program is not asked. The OS removes its windows and timers, cancels its jobs, frees its tagged memory |
| INT | Stop what you are doing | Ctrl+C in a terminal. Ends the running shell command |

HUP (your terminal went away) is worth adding with telnet. STOP and CONT (pause
and resume) are easy for window programs.

The risks of force-killing a FreeRTOS task are real, and they come from the
shared memory:

- **Held locks stay held.** If the task dies while holding the file system lock
  or the serial lock, the next task to want it waits forever.
- **Memory leaks.** FreeRTOS frees the task's stack and nothing else.
- **Open files and sockets stay open.** Nobody closes them.
- **Half-finished changes.** A task killed halfway through updating a shared
  list leaves that list broken for everyone.
- **C++ clean-up does not run.** Destructors are skipped.

Real kernels solve this in two ways, and both fit here:

1. **Kill takes effect at safe points.** The process is marked as dying. The
   next time it calls the OS (read, write, sleep, wait for a message), the call
   refuses and the task unwinds and exits by itself. Unix delivers signals this
   way too. A task is never destroyed while it is inside the OS holding a lock.
2. **The OS tracks what a process owns.** Windows, timers, files, sockets,
   memory blocks and jobs are all recorded against the PID, so the OS can
   release them itself when the process ends.

What remains is code that spins forever without ever calling the OS. A separate
task in that state can be deleted outright, accepting a small leak. A window
program stuck on the shared UI task cannot be rescued at all: the honest answer
is a watchdog that records the culprit and restarts the machine. That limit is
the best argument for moving apps to their own tasks one day.

**Protected processes.** A flag in the process table. Three flags are proposed:

- **system**: hidden in Task Manager's default view, shown in `top` and `ps`.
- **protected**: the user cannot signal it. Task Manager greys out End Task,
  and `kill` answers "operation not permitted".
- **respawn**: if it ever dies, init starts it again. Unix used this for the
  login prompt on each terminal.

Windows 95 had none of this, and killing the wrong thing took the machine down.
NT and Unix both protected their core processes.

**A Linux-like shell.** The shell would live in the portable core, so it can be
tested on the PC like everything else. It reads lines from a terminal and runs
commands that are built into it, the way COMMAND.COM and BusyBox do. It does
not need loadable programs.

- Files: `ls`, `cd`, `pwd`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `df`.
- Processes: `ps`, `top`, `kill`, `nice`, `free`, `uptime`.
- System: `date`, `reboot`, `wifi`, `dmesg`, `help`, `clear`.
- Desktop: `edit <file>` and a general `open <file>` that uses the same program
  the File manager would. `run <program>` starts any entry of the program
  table.

`edit` sends a message to the UI task, which opens the Editor window. From
telnet, the window opens on the board's screen, which can be watched over VNC.

Each shell session would be its own small task. That keeps the code simple: it
may wait for a key or a slow file without slicing anything. The greyed-out
Run... entry in the Start menu becomes a one-line front end to the same shell.

`top` is a shell command that redraws the screen once a second from the process
table, using standard terminal escape codes.

A text editor inside the console is not needed at first. When telnet makes it
necessary, a small full-screen editor in the style of `nano` is a medium job,
because the hard part (the text buffer) already exists in the Editor.

For a terminal window on the desktop, the missing piece is a terminal widget: a
grid of characters that understands the common escape codes. With the 6x12
font, 80 by 25 characters is 480 by 300 pixels.

**Telnet.** Once the shell and the TTY exist, telnet is small: listen on port
23, and for each connection create a terminal and start a shell session on it.
The protocol needs a few bytes of negotiation so the board echoes characters
and receives keys one at a time. The decision is access: telnet has no
encryption, and VNC and the web page ask for no password either.

**The serial console as a TTY, and keeping the agents' access.** On Linux, a
TTY is a kernel object that represents one text terminal. It does the echo,
line editing and Ctrl+C handling. A small program (`getty`) waits on each
terminal and starts a login shell there. Every program started from that shell
has it as its "controlling terminal", which is how Ctrl+C finds the right
process.

The owner's proposal was that the serial port becomes a text console, and a
command typed there starts the debug protocol. That works, but it has one
weakness: the agents' tool would depend on the shell being in a known, healthy
state. If a session is stuck inside `top`, or a half-typed line is waiting, the
tool cannot get in, and the board is unreachable exactly when debugging matters
most.

There is a way that needs no mode switch. Every debug frame on the wire begins
and ends with a zero byte, and the content between is encoded so it never
contains one (`docs/debug-protocol.md`). A person typing never sends a zero
byte. So the serial driver can sort incoming bytes by itself:

- Bytes between two zeros that pass the checksum go to the debug service.
- Everything else goes to the console TTY.

Both are then live at all times on the same wire. `tools/dbg.py` needs no
change: it already shows anything that is not a frame as plain text. Two
processes appear in the list, the console and the debug service, both
protected.

One detail to settle when it is built: log lines travel as frames today, so a
plain terminal program would show them as noise. The simple rule is to send
logs as plain text unless a debug tool has spoken in the last few seconds.

**init, the process tree and layered start-up.** With a process table, a tree
costs one extra field: the parent PID. A possible shape:

```
PID 1  init
  2    tty0        serial console        protected, respawn
  3    dbgd        debug service         protected, respawn
  4    netd        Wi-Fi and clock
  5    vncd        remote screen
  6    httpd       browser viewer
  7    telnetd
  12     sh        a telnet session
  8    desktop     window server         protected
  9    shell       taskbar, Start, icons respawn
  10     editor
  11     culebra
```

The video output, the file system and the Wi-Fi driver are not processes. They
are the kernel side, as on Linux.

init would read its list from a text file, in the spirit of Unix's `inittab` or
DOS's AUTOEXEC.BAT, for example `C:\.config\init.ini`.

The layered start-up can be made real instead of faked, in two steps:

1. **Reorder what already happens (small).** Today Wi-Fi, VNC, the web server
   and mDNS all start before the first paint. As services started by init after
   the desktop, the screen appears sooner and the Wi-Fi icon arrives in the
   tray a moment later, when its service really starts.
2. **Split the desktop into two programs (large).** The window server keeps the
   windows, the pointer and the wallpaper. A separate shell program owns the
   taskbar, Start menu and icons. Then the wallpaper appears first and the
   taskbar follows, as on Windows 95 where Explorer loaded after the system.
   Killing the shell would make the taskbar vanish and come back. This means
   cutting the 2,170-line window manager in two, so it comes last.

No artificial delays: the self-test screen set the tone, and what the screen
shows should be what the machine is really doing.

### What else is missing for a serious attempt

**Faults and limits**

| Gap | Today | What a serious version does | Size |
| --- | --- | --- | --- |
| Memory protection | None. Any program can overwrite any other, or the OS | Accept it, as the Amiga and Windows 3.1 did. Add cheap tripwires: stack overflow checks per task, heap checking in test builds, and per-process memory limits enforced where memory is handed out | S for tripwires, M for limits |
| Crash screen | A bad pointer in any app restarts the whole machine. The only trace is text on the serial port | Record the crash (which process, where, the call chain), restart, and show an error screen on the next start naming the culprit. Drawing at the moment of the crash is doubtful because the picture depends on an interrupt that a crash stops. ESP-IDF v5.5's hooks for this were not checked | M |
| Watchdog | If the UI loop hangs, nothing notices | A tiny high-priority supervisor task checks that the UI loop is alive, names the process that was running, and restarts | S |
| Atomic saves | A save empties the file, then writes. A power cut in between loses the document | Write to a temporary file, then rename | S |
| System log | Log lines go to the debug link and are gone | Keep the last few hundred lines in memory for `dmesg`, and optionally in a file | S |

**The app model**

| Gap | Today | What a serious version does | Size |
| --- | --- | --- | --- |
| A system interface | Apps reach into the window manager, storage and hardware ports through global variables | One defined set of OS calls. This is where PID accounting, kill checks and limits attach. It is also the precondition for loadable apps | L, can be gradual |
| Program lifecycle | Apps are fixed objects that exist forever. One instance each. Memory is never returned | Programs are created at launch and destroyed at exit, with their memory. Two Editors become possible | L |
| Fixed limits | 16 windows, 16 timers, 24 widgets per window, 3 File manager windows | Fine for now. Worth a clear error when a limit is hit | S |
| Loadable apps | Everything is compiled into one firmware image. Deferred by the owner earlier | Three routes: native code loaded from disk, a script language, or a WebAssembly runtime. Only the last gives real isolation on this chip. None was evaluated on this board | XL |
| Event-driven loop | The UI loop wakes every 10 ms and polls the serial port, VNC and timers | Sleep until a message or the next timer is due. Comes naturally with the message queue | S after the queue |
| The window manager's size | One 2,170-line file holds windows, taskbar, Start menu, icons, drag and menus | Split into window server and desktop shell | L |

**Things a user would miss**

| Gap | Today | Size |
| --- | --- | --- |
| Real keyboard and mouse | Input arrives only over serial or VNC. USB host is waiting on hardware | L |
| Keyboard layouts | US layout only. A Spanish layout needs dead keys for accents | M |
| File dates | Files have a name and a size, no modified time | S to M |
| Control Panel, Run... | Both greyed out. The time zone is fixed in the code | M |
| Sound | Sounds are chosen and stored. No driver plays them | M |
| Updates over Wi-Fi | The flash layout reserved room for a second firmware slot. Nothing uses it | M |
| Users and passwords | None. Remote screen is open to the local network | A stance to choose |
| Pipes and redirection | Nothing like `ls > list.txt` | M |
| Automatic builds | The GitHub build file sits unused in `ci/` until the owner copies it to `.github/workflows/` | S |

What is already in good shape: the portable core with PC tests and pixel-exact
picture tests, the ports-and-adapters structure, and the debug link. Those are
what make the changes above safe to attempt.

## 3. The twelve choices, and what was decided

Each was put to the owner with options and a recommendation. He approved all
twelve as recommended (section 4), with one later remark that changes the
reading of number 5.

| # | Question | Chosen | Options not taken, and why |
| --- | --- | --- | --- |
| 1 | What is a process? | **C, hybrid**: window code stays on the UI task; a process may also own real tasks | A (cooperative only): the shell and telnet would have to be hand-sliced state machines, and nothing could ever run in the background. B (one task per app): a rewrite of the window manager and every widget, and 4 to 8 KB of internal RAM per app; kept as a later step, app by app |
| 2 | Serial console and debug link on one port | **Both live at once**, sorted by the zero byte that frames debug packets | Mode switch from the shell (the owner's first idea): agents would be locked out whenever the shell is stuck. Console carried inside the debug protocol: safe, but a plain terminal program could not use the console |
| 3 | Shell flavour | **Linux names with DOS aliases** (`dir`, `type`, `del`) | Linux only: loses the 90s PC feel for no saving. DOS only: the owner asked for Linux-style commands |
| 4 | Paths in the shell | **Drive letters kept, both `\` and `/` accepted** | `C:\` only: awkward to type from a Linux terminal over telnet. A Unix root (`/c/Documents`): two names for every file, and the desktop shows drive letters |
| 5 | Who may use telnet? | **No password** (see the note below) | A password: the owner wants no users and no passwords anywhere |
| 6 | What End Task does | **Polite first, then offer force** if the program has not closed after a few seconds, as Windows 95 did | Force at once (today): loses unsaved work without a chance. Two separate buttons: more to explain, same result |
| 7 | Background saves | **Real jobs on a worker task** | Sliced on a timer with a progress bar: simpler and more period-correct, but the shell and telnet need the worker-task plumbing anyway |
| 8 | Typing while a save runs | **No: the window is locked and shows it is busy** | Saving a copy while typing continues: up to 512 KB more memory and more states to get wrong, for saves that take a few seconds |
| 9 | Protected processes | **Console, debug service, init and window server.** The taskbar restarts instead | Console and debug only: killing init or the window server leaves a dead machine. Also the taskbar: then the classic "kill it and it comes back" would be lost |
| 10 | Layered start-up | **Reorder now** (services start after the desktop), **split wallpaper from taskbar in phase 4** | Leave as is: the owner wants the layers. Split at once: it needs the process table and the OS-call layer first |
| 11 | After a crash | **Restart, then an error screen naming the program** | Silent restart (today): nothing is learned from a crash |
| 12 | Priority levels exposed | **Three classes: Low, Normal, High** | A Unix-style number range: FreeRTOS priorities are strict, not shares, so most numbers would either do nothing or starve something |

**Note on number 5.** The recommendation was "off by default, with a password,
and the same switch for VNC". The owner approved the list as it stood, and in
the same message said he wants no users and no passwords because security is
not a goal of this OS. The second, more specific statement is taken as his
intent: no password, for telnet or VNC. Whether telnet starts by default (like
VNC) or only when switched on in a settings file is asked again before the
telnet server is built.

## 4. The owner's answer

> Vale, me encanta el análisis que has hecho, he aprendido muchas cosas. Mira,
> de la lista final de tareas que sugieres, en las que me das opciones y tú
> respondes ya con una opción sugerida, apruebo todas tal y como están, con sus
> sugerencias. La de que el guardado bloquee durante un tiempo, incluso me hace
> ilusión para poder añadir el reloj de arena en el cursor, que es otra de esas
> cosas que no recordaba de esos tiempos, y en cuanto he leído sobre ello me ha
> hecho tener mucha nostalgia y quiero volver a verlo. Empieza poquito a poco,
> no intentes hacer todo a la vez, paraleliza algunas cosas si se pueden, pero
> prioriza hacer las cosas dejando un buen detalle del trabajo hecho y de las
> decisiones tomadas, y creando una documentación que incluya estos diálogos que
> tenemos y las conclusiones que sacamos, las opciones que se contemplaron, las
> que se tomaron, e incluso si hay, puede ser de valor añadir los motivos por
> los cuales no se tomaron otras decisiones. Estoy haciendo esto como un
> proyecto personal y a la vez lo estoy grabando para YouTube, entonces esa
> memoria luego me tiene que servir a mí para hablar sobre el proceso de
> creación de este sistema operativo y las cosas que he aprendido en el camino.
> Entonces dejar un registro escrito en el repositorio es de mucha ayuda. Como
> ideas sueltas así que me vienen ahora de lo que he leído en este documento,
> sí, no pretendo que haya usuarios ni contraseñas. No es un sistema operativo
> para uso real, es para uso recreativo, entonces la seguridad no es un
> problema. La falta de aislamiento de memoria la asumimos, hacemos como hacía
> Amiga OS en su día, e incluso me parece un entorno divertido con el que probar
> más adelante a escribir un virus y ver cómo funciona ese asunto. Adelante con
> el plan.

What follows from it:

- All twelve choices are decided as in section 3.
- **The hourglass.** A busy pointer, shown while a program makes the user wait
  (first of all a save), is wanted. It joins the backlog. The art must be our
  own.
- **Pace.** A little at a time, some pieces in parallel, and the written record
  comes before speed. This journal is that record.
- **No users, no passwords.** Security is not a goal: this is a recreational OS.
- **No memory isolation, on purpose**, as on AmigaOS. Writing a virus for it
  later, to see how one works, is on the list of ideas.

## 5. What happens next

The backlog is in [`docs/BACKLOG.md`](../BACKLOG.md). The first pieces, which
touch different files and can be built side by side:

- the process table with PIDs (item 2),
- FreeRTOS run-time statistics (item 1),
- atomic saves, with the hourglass pointer while a save blocks (items 6 and 6b).

Each gets its own branch and its own journal entry.
