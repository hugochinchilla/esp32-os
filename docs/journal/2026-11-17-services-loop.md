# The services loop: the network gets a task of its own

2026-11-17. Backlog item 8b. Branch `claude/project-thread-bn0qlc`. Built and
tested on the PC, compiled and linked for the board; **nothing here has run
on the board yet**.

## 1. What was asked

When the message queue was being built (item 8), the owner looked at the
arrangement and questioned it
([journal](2026-11-16-message-queue.md)):

> It seems odd having the network be monitored by the ui. If I were to make
> the board detect if a monitor is not plugged in and skip the ui completely
> from running, who checks the network? Maybe there is an in the middle
> ground, where it is not a dedicated thread but not inside the loop from the
> ui, do we have another candidate loop for such tasks?

The answer then was that no such loop existed, and that the middle ground was
one to make: a single services task shared by every network service, asleep
on all their sockets, the way Unix's `inetd` listened for many services in
one process. He chose it on a card as its own backlog item, after the queue.

This entry is that item. It was built without him at the keyboard, on this
instruction:

> ve haciendo, no pares hasta terminar el backlog, dejo a tu criterio el
> orden de las tareas y el paralelismo, no tengas más de 4 cosas activas a
> la vez

So the smaller choices below were made by the agent. Each is marked, and each
is his to change.

## 2. What was found

How it worked until now (`firmware/os/main/main.cpp`, the UI task's loop):

- Each round of the UI loop called `remote.poll()`: the VNC server took new
  viewers, read their keys and mouse, encoded changed screen for up to 4 ms
  and sent what fitted. The web page for browsers was served from inside the
  same call.
- Nothing could wake the loop for a socket, so it came back by the clock:
  every 10 ms with a viewer connected or a page half sent, every 50 ms while
  only waiting for someone to connect. That is 20 wake-ups a second on a
  board nobody is connected to, for as long as it is on.
- A viewer's key went straight from the VNC server into the window manager.
  That was safe only because both ran on the same task.
- The kernel's tick is 10 ms, so with a viewer the server got 4 ms of
  encoding and then at least one tick of sleep: under a third of the time,
  however much screen was waiting to go.

Three things in the VNC server touched what belongs to the UI task, and had
to be found before moving it: the input it injects, the grid of "what was
painted" that the window manager fills in, and the pointer's position and
shape.

## 3. The options considered

### Where the network runs

Decided by the owner on the card of 2026-11-16; listed here so the story is
whole.

| Option | What it costs |
| --- | --- |
| A. Stay in the UI loop | Nothing. No UI, no network; 20 wake-ups a second for ever |
| B. A task per service (`vncd`, `httpd`, later `telnetd`) | A stack each, 3 to 4 KB of internal RAM per service |
| **C. One services task for all of them, asleep in `select()`** | One stack of 4 KB. The services must never wait, which they already did not |

C was chosen.

### How the services task knows what to sleep on (the agent's choice)

`select()` needs socket numbers, and the portable core may not include
socket headers in its interfaces.

| Option | What it costs |
| --- | --- |
| A. The firmware collects the sockets itself | The firmware would have to know that a web request half read wants "readable" and one half answered wants "writable": protocol knowledge in the wrong place, and written twice (board and simulator) |
| **B. Each service says what it waits for** | Two small virtual calls: a stream has a `handle()`, and a service has `watch()`, which adds handles to a list and returns the longest sleep it accepts |
| C. Keep polling by the clock, only on another task | No new interface, but the board still wakes 20 times a second for nothing. It would move the oddity, not remove it |

B was chosen. The loop itself (`net::Services`) is 40 lines and knows nothing
about VNC. A stream or listener that is not a socket (the fakes in the tests)
answers "cannot be watched", and the old clock pace applies to it, so nothing
that worked before stops working.

### How the screen's changes cross from one task to the other (the agent's choice)

The window manager reports each rectangle it paints (`damage()`), on the UI
task. The server reads and empties that grid on the services task.

| Option | What it costs |
| --- | --- |
| **A. A lock around the grid** | A FreeRTOS mutex taken for a few microseconds per painted rectangle and once per update |
| B. Atomic operations on each cell | No lock, but 8-bit atomics on this chip go through a library that turns interrupts off, and the code is harder to read |
| C. `damage()` as a message to the services task | A second queue, and a painted rectangle would be 20 bytes of message instead of a few bytes set in a grid |

A was chosen. It is the same `ports::Lock` the file system uses.

### What the viewer sees of a screen that is being painted (the agent's choice)

The services task reads the screen's pixels while the UI task may be painting
them.

| Option | What it costs |
| --- | --- |
| **A. No lock on the pixels; a block caught half painted is sent again** | A viewer can, for one update, see a block half painted. It heals itself because the window manager reports a rectangle after painting it, so the block is marked again |
| B. A lock held by the UI task while it paints and by the server while it reads a block | The picture is always whole, but painting can wait for the network task, and the lock goes into the window manager, which every other branch is editing |

A was chosen, and it has one condition: damage must be reported after the
pixels are written. `Wm::update()` already did. The dotted outline shown
while moving a window reported first and drew second; that order was swapped
(`Wm::xor_outline()`), four lines.

### How fresh damage wakes the services task (the agent's choice)

With a viewer waiting for the screen to change, the change comes from the UI
task, and a task asleep in `select()` only wakes for sockets.

| Option | What it costs |
| --- | --- |
| **A. While a viewer waits for an update, look every 10 ms** | 100 short wake-ups a second, only while someone is connected and waiting. The same pace the UI loop had |
| B. An event the UI task can signal and `select()` can wait on (ESP-IDF's `eventfd`) | Updates leave at once instead of within 10 ms, and no clock at all. A new ESP-IDF piece that cannot be tried without the board, and a different one on the PC |
| C. A UDP packet to itself over the loopback | Works with plain sockets, but every wake goes through the network stack's task |

A was chosen for now: it adds no mechanism that only the board can test. B is
the better end state and is noted under "what is left".

### The row in the process table (the agent's choice)

The rule since 2026-11-16 is that only real tasks are listed, and that a
service gets its row in the change that makes it a task. The names planned
were `vncd` and `httpd`, but there is one task, not two.

| Option | What it costs |
| --- | --- |
| **A. One row, `inetd`** | The name says what it is: one process listening for several services |
| B. Two rows, `vncd` and `httpd` | Two rows for one task: exactly what the rule forbids |
| C. One row, `netd` | That name was planned for the Wi-Fi manager, which is still part of the desktop |

A was chosen. The FreeRTOS task has the same name, so `tools/dbg.py tasks`
and Task Manager's "Show all processes" agree.

## 4. What was built

- **`core/net/services.hpp`**: `net::Service` (`poll()`, `watch()`),
  `net::Wait` (the sleep, a port) and `net::Services`, the loop. One round:
  every service's turn, then each says what it waits for, then one sleep on
  all of it.
- **`net::PosixWait`** in `core/net/posix_tcp.hpp`: the sleep as `select()`,
  the same code on the PC and on the board (lwIP offers the same call).
- **`rfb::Server` is a `net::Service`.** Its `watch()`: with no viewer, the
  listening sockets and no time limit. With a viewer, its socket for reading,
  and for writing only when bytes are waiting to go. No limit while the
  viewer has what it asked for; 10 ms while it waits for the screen to
  change; straight back when an update is half encoded.
- **`net::WebGate::watch()`**: each browser connection for reading while its
  request arrives and for writing while its answer leaves. The listening
  socket of the pool is only watched while a place is free, or the sleep
  would end again and again for a browser nobody can let in.
- **`msg::PostedInput`** (`core/msg/input.hpp`): an input sink that turns
  each key, mouse event and piece of typed text into a message for the UI
  task. A key fits in one of the message's two values, a mouse event in both,
  text goes seven bytes at a time, never cut inside a character. If the
  queue is full it waits up to a second, then the event is lost and counted.
- **In the VNC server**: the damage grid has a lock; the pointer's shape is
  copied before it is sent and the copy is what is remembered as sent, so a
  shape caught half changed is followed by a good one; buttons a departing
  viewer held are released where that viewer last had the pointer, instead
  of asking the screen where the pointer is now.
- **A rest.** A service with work left comes straight back without sleeping.
  After 40 ms of that the loop sleeps 10 ms whatever happens, so the tasks
  below it and the kernel's idle task (which a watchdog looks at) always get
  the processor. Before, encoding got 4 ms in every 10 at best; now it can
  get 40 in every 50.
- **The firmware** (`firmware/os/main/main.cpp`): a task `inetd`, 4 KB of
  stack in internal RAM, priority 4 (the UI task has 5), started after the
  desktop and only if a port could be opened. The UI loop no longer mentions
  the network. `tools/dbg.py memory` has a new line for the services loop:
  rounds, sleeps, how many the network ended, forced rests, input lost.
- **The simulator** (`host/sim.cpp`) does the same with a thread, so the
  existing VNC test (`tools/test_vnc_sim.py`) now drives a viewer through two
  threads and the message queue.

Nothing in this change touches the process table from the services task. The
`inetd` row is made by the UI task when it starts the task. What a later
service needs from the table (telnet starting a shell) goes through
`msg::post()`.

## 5. How it was tested

On the PC, 23 test programs, all passing with the address and
undefined-behaviour checkers on. The new one is `host/tests/test_services.cpp`:

- **The loop**: turns, the shortest wish wins, 10 or 11 turns of 4 ms and
  then a rest that nothing can end early.
- **Input as messages**: negative movements, the wheel, `ñ` and `€` cut
  between characters, a full queue losing exactly the surplus.
- **Real sockets**: nobody connected, the sleep is on two listening sockets
  with no limit, and a 20 ms look finds nothing. A connection ends a
  5-second sleep at once. A browser's request and its answer.
- **Two tasks.** One thread paints random rectangles into the screen and
  reports each, as the UI task does, for at least half a second. A second
  runs the services loop. A third is a small VNC viewer on a real socket
  that also sends a key down, a key up and a pointer move with every
  request. At the end the viewer's picture must equal the screen byte for
  byte, and every input event must have arrived on the painting thread, in
  order. One run: 143 updates, 8,477 rectangles, 423 input events. Before
  and after the viewer, the services thread must not go round once in
  300 ms: it is asleep.

The same test under the thread checker (`-DHOST_TSAN=ON`, a new option in
`host/CMakeLists.txt`) reports one data race: the server reading screen
pixels while the other thread writes them, which is option A above, chosen
knowingly. With the lock taken out it reports seven, all in the damage grid.

Firmware: compiled and linked in the cloud container with ESP-IDF v5.5
(`tools/setup-cloud-idf.sh`), no errors; the image is 1,347,888 bytes. It
has not run.

The stack was estimated from the compiler's frame sizes on the PC (the
deepest of our own paths is about 1.2 KB: answering a WebSocket handshake;
a log line adds 256 bytes of text and the C library's formatting). That is
an estimate from another processor, not a measurement.

Not tested: anything on the board. See the next section.

## 6. What was learned

- The old code had the damage reported before the outline was drawn. On one
  task the order did not matter. It was the only place, and a second task
  would have left stale dots on a viewer's screen now and then.
- "Sleeps on all sockets" is not quite all: a full pool of browser
  connections must stop watching its door, and a socket is only watched for
  writing while there is something to write. Each of those, done wrong, is a
  loop that never sleeps and that no test of behaviour notices, because
  everything still works. The two-task test checks the round counter for
  that reason.
- The viewer test released a held button at (0, 0) because the server asked
  the screen for the pointer. It now releases it where the viewer had it.
  One expected string in `test_rfb.cpp` changed for this.

## 7. What is left

For the board turn, in order of what could go wrong:

1. `tools/dbg.py tasks`: `inetd` exists and its free stack is above 1 KB
   after a viewer has connected, a browser has loaded the page and a log
   line has been printed from it ("VNC viewer connected"). If not, raise
   `kServicesStack`.
2. Idle: with nobody connected, `tools/dbg.py memory` twice, a minute apart.
   The services loop's rounds must not move, and the UI loop's rounds per
   second must be lower than before (it no longer wakes 20 times a second
   for the network).
3. A VNC viewer and the browser page: picture right, keys and mouse work,
   the pointer shape follows (arrow, resize arrows, hourglass).
4. Times against main (`docs/journal/2026-11-16-vnc-connect.md`): connect
   and handshake was 272 ms median; a full raw screen with
   `tools/rfb_client.py` was 10.7 s. Both should be the same or better.
5. Drag a window with a viewer connected: no stale outline dots or half
   painted blocks left standing on the viewer.
6. The VGA picture: late and missed refills as before, with a viewer
   connected and a full-screen update under way. The services task encodes
   on whichever core is free; the video interrupt is on core 1.
7. Free internal RAM: about 4 KB less (the stack) plus the task's control
   block.

Later:

- Wake the services task from the UI task when damage arrives (ESP-IDF's
  `eventfd`), so updates leave at once and the 10 ms look goes away.
- The telnet listener (item 17) is the next service: `services.add()`.
- Ending `inetd` from Task Manager answers "not supported" until services
  have stop functions (item 5b).
- A board with no monitor that skips the UI (the owner's question) can now
  keep its network: nothing in the services task needs the desktop, except
  that the VNC server would have nothing to show.
