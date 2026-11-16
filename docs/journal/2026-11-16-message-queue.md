# The message queue: a way into the UI task, and a loop that sleeps

2026-11-16. Backlog item 8. Branch `claude/project-thread-2jwyaw`.

## 1. What was asked

The architecture review of the same day
([journal](2026-11-16-architecture-review.md)) found that ten of the owner's
ideas rest on three missing pieces. The second one was "safe ways to work off
the UI task: a message queue into the desktop, and a lock on the file system".
The owner approved the backlog whole:

> de la lista final de tareas que sugieres, en las que me das opciones y tú
> respondes ya con una opción sugerida, apruebo todas tal y como están, con sus
> sugerencias. [...] Empieza poquito a poco, no intentes hacer todo a la vez,
> paraleliza algunas cosas si se pueden, pero prioriza hacer las cosas dejando
> un buen detalle del trabajo hecho y de las decisiones tomadas

Item 8 reads: "Message queue into the UI task. The loop sleeps until there is
work". The review's table of smaller improvements had the second half as a
line of its own: "The UI loop wakes every 10 ms and polls the serial port, VNC
and timers. Sleep until a message or the next timer is due. Comes naturally
with the message queue."

Nothing on the screen changes. This is plumbing for what comes next: saves on
a worker task (item 10), the console (12), init (18), apps on their own tasks
(26). The file-system lock (item 9) is being built in another thread at the
same time.

## 2. What was found

**Everything belongs to one task.** Windows, widgets, timers, menus and the
process table are plain data with no locks. That is safe because only the
`ui` task ever touches them. It is also why a save freezes the desktop: the
only task allowed to touch the Editor's window is the one busy writing the
file. The moment a second task exists (a save job, a shell on the serial
port), it needs some way to say "the save is done, clear the star in the
title" without touching the window itself.

**The loop polled.** The end of `ui_task` in `firmware/os/main/main.cpp` was:

```cpp
while (true) {
    int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
    if (got > 0) {
        protocol.feed(buffer, static_cast<size_t>(got));
    }
    desktop.tick(); // timers (caret, key repeat, clock), then any painting
    remote.poll();  // the VNC viewer: its keys and mouse in, changed screen out
    ...
}
```

Three things about it that are easy to miss:

- `uart_read_bytes` with a 256-byte buffer and a 10 ms limit does not return
  when the first byte arrives. It waits for 256 bytes or for the limit. A
  debug command is a few dozen bytes, so every command sat in the driver's
  buffer until the limit ran out.
- The kernel's clock ticks 100 times a second (`CONFIG_FREERTOS_HZ` is at its
  default). "10 ms" means "until the next tick", so the loop ran once per
  tick: 100 rounds a second, whether or not there was anything to do.
- The network is polled too. The VNC server and the web server never wait;
  each round asks the sockets "anything new?". Nothing in the socket calls
  can wake a task that is sleeping on something else.

**What needs the loop by the clock.** The window manager keeps up to 16
timers. The ones that run on an idle desktop are the caret (every 500 ms),
the taskbar clock (every second), the Wi-Fi manager's check (every 250 ms)
and the Desktop folder's check (every 2 s). So an idle desktop has work about
7 times a second, not 100. The games, the Defragmenter, Task Manager and the
picture decoder add their own timers while they are open, and the screensaver
draws 30 frames a second while it shows.

**Internal RAM is tight**: about 102 KB free with Wi-Fi up
([journal](2026-11-16-internal-ram.md)). A queue is used with interrupts
switched off for a moment, and external RAM cannot be reached while the flash
is being written, so the queue has to be internal.

## 3. The options considered

The owner approved the item; these are the choices inside it. The first two
shape what later work builds on, so they are put to the owner (section 4).
The rest are small and written down for him to overrule.

### 3.1 What a message is

| Option | What it is | Who did it this way | For | Against |
| --- | --- | --- | --- | --- |
| A. Numbered messages to windows | `post(window, WM_SAVED, a, b)`. Every window gets one function with a big `switch` on the number | Windows (`PostMessage`), classic Mac OS events | The 90s way, and the most familiar picture. Messages can be listed and logged by name | Every window class has to grow a message function, and today none has one: windows here are C++ objects with one method per event (`key`, `mouse`, `paint`). Message numbers need a registry. A job posts to a window, but jobs belong to processes |
| B. "Run this function on the UI task" | `post(function, context)`. The loop calls the function | AmigaOS (a message is a pointer; what it means is between sender and receiver), every modern UI toolkit (`dispatch_async`, `Invoke`, `runOnUiThread`) | Smallest. No numbers, no registry, nothing to add to the windows | Nothing stops a message from arriving after its program has closed. The function then touches a window that is gone. On this OS, with no memory protection, that is a crash of the whole machine |
| **C. A function, addressed to a process** (chosen, to confirm) | `post(pid, function, context, a, b)`. The loop checks that the process is still alive, runs the function as that process, and drops the message if it is not | The process table makes it possible; closest to B | As small as B. The message that outlives its program is dropped, not delivered. Whatever the function creates (a dialog, a timer) is recorded as the process's own, because the process table's "current process" is set for the call. The backlog's jobs "owned by a process" get the address they need | Still no names for messages: a log can count them, not describe them. `context` can still point at something the process freed while alive; the PID check does not see that |

C is what was built. It costs 20 bytes per message: the function, the
context, two 32-bit values and the PID.

### 3.2 How the network gets its turn

| Option | For | Against |
| --- | --- | --- |
| Keep 100 rounds a second whenever Wi-Fi is on | No change in behaviour | The loop never really sleeps on a board with Wi-Fi, which is every board |
| **Look by the clock, at a pace that fits** (chosen, to confirm): every 10 ms while a viewer is connected or a web page is being sent, every 50 ms while only waiting for someone to connect, never when no port is open | No RAM, no new task, no change to the socket code, which the PC shares. A viewer is served exactly as before. A new viewer or browser is noticed at most 50 ms later than before, once | Still polling: 20 rounds a second on an idle board with Wi-Fi, where 7 would do |
| A watcher task that sleeps in `select()` on the sockets and wakes the UI task | The loop becomes fully event-driven | A task needs a stack: about 3 KB of internal RAM for something that saves a few thousandths of one processor. The listeners would have to hand their socket numbers out through the portable interfaces. And the VNC server is going to become a task of its own anyway (items 18 and 26), which removes the problem at the root |
| lwIP's callback API instead of sockets | No polling, no task | The same VNC and web code runs on the PC through POSIX sockets. This would split it in two |

### 3.3 How serial bytes wake the loop

| Option | For | Against |
| --- | --- | --- |
| The serial driver's event queue, joined with the message queue in a FreeRTOS "queue set" | The documented way to wait on two queues | The driver's queue costs heap, the set costs more, and a queue set has a rule that bites: every item must be taken through the set, or the set and its queues disagree for ever |
| A small task that blocks on the serial port and forwards | Obvious | A stack of 2 KB or more, and one more task switch per command |
| **The driver's notification hook** (chosen) | The driver calls a function from its interrupt when bytes land in its buffer (`uart_set_select_notif_callback`). The function gives a semaphore. No RAM beyond the semaphore, no task | The hook exists for `select()` on serial ports. Using it means nothing else may use `select()` on this port; nothing does |

### 3.4 What the loop sleeps on

| Option | For | Against |
| --- | --- | --- |
| The message queue itself, with a time limit | One object | Only messages wake it. Serial bytes would have to be wrapped in a message from an interrupt |
| A task notification | Free: every task has one | FreeRTOS's stream buffers use the same notification slot. A library that uses one on the UI task would eat our wake-ups, or we theirs |
| **A binary semaphore next to the queue** (chosen) | 80 bytes. Posting a message gives it; so does the serial interrupt. A give that comes before the sleep is remembered, so there is no gap in which a wake can be lost | One more kernel object |

### 3.5 Smaller ones

- **When the queue is full.** The sender chooses: `post(..., wait_ms)` waits
  for room that long (not at all by default, or for ever) and says whether it
  got in. A job's "I have finished" waits; a progress tick that nobody will
  miss does not. Dropping silently, or always blocking, were the other
  options; one loses news that matters and the other can freeze a task that
  only wanted to say something unimportant.
- **32 messages deep**, 640 bytes. Deeper costs internal RAM for a queue that
  empties 100 times a second under load. The high-water mark is reported
  (section 5), so this is a guess that can be checked.
- **At most 16 messages per round**, then timers and painting, then the rest
  without sleeping. A task that posts without pause cannot stop the caret.
- **A longest sleep of one second.** If a wake were ever lost, the desktop
  would stall for a second instead of for ever. An idle desktop wakes more
  often than that anyway.
- **A shortest sleep of one tick** unless the round stopped because there was
  more to take. A timer that is always due cannot turn the loop into a spin
  that starves every task below it.
- **Timers stay as exact as before**, which is one tick (10 ms). Making them
  exact to the microsecond (a hardware timer that wakes the loop) is possible
  now and was left out: nothing needs it yet.

## 4. What was chosen, by whom, and why

Built with the options marked above. 3.1 and 3.2 were put to the owner as
they are the ones later work leans on; the entry is updated with his answer.

## 5. What was built

**The port and the dispatcher** (`core/include/core/msg/queue.hpp`,
`core/src/msg/queue.cpp`). No ESP-IDF in it.

```cpp
struct Message {
    Handler fn;      // called on the UI task
    void *context;
    uint32_t a, b;
    proc::Pid to;    // dropped unless this process is alive; 0: the OS itself
};

class Queue {                       // the port
    virtual bool post(const Message &m, uint32_t wait_ms = kNoWait) = 0;  // any task
    virtual void wake() = 0;                                              // any task
    virtual bool take(Message &out) = 0;                                  // UI task
    virtual bool sleep(uint32_t max_us) = 0;                              // UI task
};
```

`msg::Inbox::deliver()` is the UI task's side: take a message, check the PID
against the process table, set the current process, call the function.
`msg::post(pid, fn, context, a, b)` posts to the machine's one queue from
anywhere.

**The FreeRTOS adapter** (`firmware/os/main/rtos_queue.hpp`): a static
FreeRTOS queue and a static binary semaphore inside one object. About 0.8 KB
of internal RAM, none from the heap.

**The PC adapter** (`host/thread_queue.hpp`): the same port over a mutex and
two condition variables, so the tests post from real threads.

**The window manager says how long it can be left alone**
(`Wm::idle_us()`): the time to its next timer, to the screensaver's next
frame, or to the moment the screensaver should start; zero if something is
waiting to be painted. The screensaver interface gained `next_frame_us()`.

**The remote screen says how often it wants a turn**
(`rfb::Server::poll_interval_us()`): 10 ms, 50 ms or never, as in 3.2. A
listener can say it is busy (`net::Listener::busy()`); the web server does
while a request is open.

**The loop** in `main.cpp`, in full:

```cpp
while (true) {
    bool more = false;
    for (int reads = 0;; reads++) {          // serial bytes, without waiting
        if (reads == kSerialReads) { more = true; break; }
        const int got = uart_read_bytes(kDebugUart, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        protocol.feed(buffer, static_cast<size_t>(got));
    }
    more |= inbox.deliver();                 // what other tasks asked for
    desktop.tick();                          // timers, then painting
    remote.poll();                           // the VNC viewer
    ...
    uint32_t sleep_us = 0;
    if (!more) {
        sleep_us = min(desktop.idle_us(), remote.poll_interval_us(), one second), at least one tick
    }
    s_queue->sleep(sleep_us);                // a message or serial bytes end it early
}
```

**The first real message.** The `scanlog` task (the one that samples the
video counters for the first 20 seconds) now ends by posting a message, and
the UI task logs it: "start-up timeline complete: 200 samples (a message from
the scanlog task, run on the ui task)". It is there so that every start of
the board shows a message crossing from one task to another.

**Figures for the board turn.** `tools/dbg.py memory` gained two lines: how
many rounds the loop has made, how many sleeps were ended early, and the
queue's counters (delivered, dropped, refused, most waiting).

**The simulator** (`host/sim.cpp`) has the same loop, with `poll()` on its
serial line as the sleep.

### How it was tested

On the PC, with the address and undefined-behaviour checkers on
(`host/tests/test_msg.cpp`, test `msg`):

- Order, the 16-per-round limit, the full queue (refused at once, and after
  a 30 ms wait).
- A message to a process that ends before delivery is dropped and its
  function never runs; a message to a live process runs as that process.
- Sleep: lasts as long as asked when nothing happens; a wake given before
  the sleep ends it at once and is used up by it; a wake from another thread
  ends a 5 s sleep after 30 ms.
- Four threads post 2000 messages each, faster than the loop takes them.
  All 8000 arrive, each thread's in its own order, all run on the "UI"
  thread, never more than 32 waiting.
- `Wm::idle_us()` to the microsecond with a clock the test moves by hand. A
  loop that sleeps exactly as long as allowed calls a 70 ms timer 142 times
  in 10 s, the same as looking every 10 ms, in 171 rounds instead of 1000.
  The screensaver starts on time and draws 30 frames a second.
- The remote screen's three paces.

All 22 PC tests pass, including the two that drive the simulator through
`tools/dbg.py` and through a VNC client.

Measured on the simulator, old loop against new, with `tools/dbg.py`
connected and the desktop idle (PC figures: they show the mechanism, not the
board):

| | Ping, median | Ping, worst | Rounds a second, idle |
| --- | --- | --- | --- |
| Before, no network | 10.24 ms | 10.65 ms | 98 |
| After, no network | 0.08 ms | 0.20 ms | 6 |
| Before, VNC and web ports open | 2.20 ms | 2.82 ms | 462 |
| After, VNC and web ports open | 0.08 ms | 0.56 ms | 22 |

(The old simulator looked every 2 ms when a VNC port was open; the board
always looked every 10 ms.)

### What was not tested

The firmware was compiled and linked in the cloud container (section 6). It
has **not run**: the FreeRTOS adapter, the serial wake and the new loop have
never executed on a board. That is the board turn, section 7.

## 6. What was learned

- A read with a time limit is not "wait for data". `uart_read_bytes(buf, 256,
  10 ms)` waits for *all 256* bytes or the limit. Every debug command paid
  up to 10 ms for it, and nobody had noticed because 10 ms is small. On the
  simulator the ping went from 10.2 ms to 0.08 ms.
- "Sleep until the next timer" is only as fine as the kernel's tick. With
  100 ticks a second, asking for 3 ms gets "until the next tick", somewhere
  between 0 and 10 ms. The loop rounds up and looks again, which gives the
  same accuracy as before with one extra round now and then.
- A wake-up has to be something that is remembered. If bytes arrive between
  "is there anything?" and "sleep", a signal that only wakes a task already
  asleep is lost, and the desktop sleeps on top of a waiting command. A
  semaphore that stays given until taken closes that gap. This is the
  classic lost wake-up, and the reason the loop does not use "notify the
  task if it is waiting".
- Addressing a message to a process, not to an object, is what makes
  "the program closed while its job was running" safe without reference
  counting. It only works because the process table came first.
- Polling the network 100 times a second cost far less than it sounds (a
  few thousandths of a processor). The gain from this change is latency and
  the mechanism, not processor time. The idle share in `dbg.py tasks` should
  move by about one percent of one core, and that is fine.

## 7. What is left

- **The board turn.** Compare before and after on the board: frame rate
  (`dbg.py stats`, `frames_shown` over a known time), `dbg.py ping`, a VNC
  session (typing and dragging a window feel the same; a viewer connects as
  fast as before), the idle share in `dbg.py tasks`, free internal RAM in
  `dbg.py memory` (about 0.8 KB less), and the video counters (late and
  missed refills unchanged). The log should show the scanlog message 20 s
  after start. `dbg.py memory` should show a few rounds a second idle, not
  100.
- The process table still has no lock. Messages do not need one: the PID is
  checked on the UI task. The first item that *starts* a process from
  another task does (noted in the process table's entry).
- Wi-Fi events still reach the Network panel through a 250 ms timer that
  asks. They could be messages now.
- Backlog items 10 (jobs), 12 (TTY), 18 (init) and 26 (apps on their own
  tasks) build on this.
