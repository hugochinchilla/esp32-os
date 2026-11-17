// A simulated board. It opens a pseudo-terminal, prints its path, and speaks
// the debug protocol on it, so the PC tool can be used and tested with no
// hardware:
//
//   ./sim                       prints e.g. /dev/pts/5
//   tools/dbg.py --port /dev/pts/5 screenshot out.png
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "core/dbg/protocol.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/init/init.hpp"
#include "core/msg/input.hpp"
#include "core/msg/queue.hpp"
#include "core/sh/console.hpp"
#include "core/shell/boot.hpp"
#include "core/sys/log.hpp"
#include "core/shell/shell.hpp"
#include "core/net/posix_tcp.hpp"
#include "core/net/rfb.hpp"
#include "core/net/services.hpp"
#include "core/net/telnet.hpp"
#include "core/net/update.hpp"
#include "core/net/web.hpp"
#include "core/net/wifi.hpp"
#include "core/proc/table.hpp"
#include "fake_wifi.hpp"
#include "folder_volume.hpp"
#include "core/wm/wm.hpp"
#include "host_adapters.hpp"
#include "thread_queue.hpp"

namespace {

// The message queue of the simulated board. Its loop sleeps in poll() on the
// serial line, so a post also writes a byte to a pipe that the same poll()
// watches: that is this loop's "wake the UI task".
class PipeQueue : public host::ThreadQueue {
public:
    PipeQueue()
    {
        if (pipe(ends_) != 0) {
            ends_[0] = ends_[1] = -1;
        }
        for (int end : ends_) {
            fcntl(end, F_SETFL, fcntl(end, F_GETFL) | O_NONBLOCK);
        }
    }
    bool post(const msg::Message &m, uint32_t wait_ms = msg::kNoWait) override
    {
        const bool posted = host::ThreadQueue::post(m, wait_ms);
        ring();
        return posted;
    }
    void wake() override
    {
        host::ThreadQueue::wake();
        ring();
    }
    int wake_fd() const { return ends_[0]; }
    void quiet()
    {
        char bytes[64];
        while (read(ends_[0], bytes, sizeof(bytes)) > 0) {
        }
    }

private:
    void ring()
    {
        const char byte = 1;
        if (write(ends_[1], &byte, 1) < 0) {
            // The pipe is full: the loop has plenty to wake it already.
        }
    }
    int ends_[2];
};

class FdSink : public ports::ByteSink {
public:
    explicit FdSink(int fd) : fd_(fd) {}
    void write(const uint8_t *data, size_t size) override
    {
        while (size > 0) {
            ssize_t done = ::write(fd_, data, size);
            if (done <= 0) {
                return;
            }
            data += done;
            size -= static_cast<size_t>(done);
        }
    }

private:
    int fd_;
};

} // namespace

// ./sim [folder [vnc-port [web-port [telnet-port [update-port]]]]]: the folder, if given, is drive D:;
// with a port, the remote screen (VNC) is offered on it, and with a second
// one the viewer page for a browser (http://localhost:web-port/). Both to
// this computer only.
int main(int argc, char **argv)
{
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (fd < 0 || grantpt(fd) != 0 || unlockpt(fd) != 0) {
        perror("pseudo-terminal");
        return 1;
    }
    // Never wait for bytes: the timers (clock, caret) keep running.
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    // Raw bytes both ways: no echo, no line editing, no newline changes.
    termios tio;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tcsetattr(fd, TCSANOW, &tio);

    printf("%s\n", ptsname(fd));
    fflush(stdout);

    host::MemoryDisplay display(640, 480);
    static host::ThreadLock lock; // the console's session writes from a thread of its own
    host::HostSystem system;
    system.pretend_tasks = true; // tools/dbg.py tasks has a list to show
    host::MemoryPointer pointer;
    host::SteadyClock clock;
    static wm::Wm screen(display, pointer, clock);
    FdSink sink(fd);
    dbg::Protocol protocol(sink, lock, display, pointer, screen, system, "sim");
    protocol.set_desktop(&screen);
    // The log is kept for `dmesg`, as on the board.
    static char log_room[16 * 1024];
    static host::ThreadLock log_lock;
    sys::log().set_room(log_room, sizeof(log_room), &log_lock);

    // C: a RAM disk, as on the board. D: a folder of the PC, if given.
    static host::HostMemory memory;
    static fs::Storage storage;
    constexpr size_t kRamDisk = 2 * 1024 * 1024;
    static fs::MemoryVolume ram_disk(memory.allocate(kRamDisk), kRamDisk, "RAM disk");
    storage.mount('C', ram_disk);
    system.real_clock = true; // the PC knows the time: files get dates
    ram_disk.set_clock(&system);
    fs::add_samples(storage, 'C');
    static host::FolderVolume *pc = nullptr;
    if (argc > 1) {
        pc = new host::FolderVolume(argv[1]);
        storage.mount('D', *pc);
    }
    protocol.set_storage(&storage);
    // The start-up screens, as on the board (the same memory test, over
    // ordinary memory of the PC).
    // While the start-up screens show, the debug link is answered, as on
    // the board.
    static int link_fd = fd;
    static dbg::Protocol *link = &protocol;
    static shell::BootIo boot_io{display, system, memory, clock, &storage,
                                 [](void *) {
                                     uint8_t bytes[256];
                                     const ssize_t got = read(link_fd, bytes, sizeof(bytes));
                                     if (got > 0) {
                                         link->feed(bytes, static_cast<size_t>(got));
                                     }
                                 },
                                 nullptr, nullptr};
    shell::run_self_test(boot_io);
    shell::show_splash(boot_io);
    protocol.set_boot_screens(
        [](int which, void *) {
            if (which == 0) {
                shell::run_self_test(boot_io, false);
            } else if (which == 3) {
                sys::CrashRecord sample; // the PC has no crashes of its own to show
                sys::crash_sample(sample);
                shell::show_crash_screen(boot_io, sample, true, false);
            } else {
                shell::show_splash(boot_io, false);
            }
        },
        nullptr);
    // A pretend radio with a few networks, so the Network panel and the
    // wifi commands can be tried with no hardware.
    static host::FakeWifi radio;
    static host::MemoryKeyStore settings;
    radio.add("Casa Chinchilla", -48, "semillas-y-heno");
    radio.add("Vecinos 2.4G", -74, "no-la-sabemos");
    radio.add("Cafe & Churros", -63, "");
    radio.utc = time(nullptr);
    radio.has_time = true;
    static net::Manager wifi(radio, settings);
    shell::set_network(&wifi);
    protocol.set_network(&wifi);

    // The remote screen, for a VNC viewer on this computer. As on the board
    // it runs on a task of its own, the services loop: the viewer's keys and
    // mouse come to the desktop as messages.
    static PipeQueue queue;
    msg::set_queue(&queue);
    static msg::Inbox inbox(queue);
    static msg::PostedInput remote_input(screen);
    static rfb::Server remote(display, pointer, remote_input, clock, memory);
    static host::ThreadLock damage_lock;
    remote.set_lock(&damage_lock);
    static net::PosixTcpListener vnc_port;
    screen.set_damage_listener([](const gfx::Rect &r, void *) { remote.damage(r); }, nullptr);
    if (argc > 2) {
        if (vnc_port.listen(atoi(argv[2]), true)) {
            remote.set_listener(&vnc_port);
        } else {
            fprintf(stderr, "cannot listen on port %s\n", argv[2]);
        }
    }
    static net::PosixTcpPool web_port;
    static net::WebGate web;
    if (argc > 3) {
        if (web_port.listen(atoi(argv[3]), true)) {
            web.set_port(&web_port);
            web.set_page(net::viewer_page(), net::viewer_page_size());
            web.set_second_page("/novnc", net::novnc_page(), net::novnc_page_size());
            remote.set_listener(&web, 1);
        } else {
            fprintf(stderr, "cannot listen on port %s\n", argv[3]);
        }
    }

    // A real board first prints start-up text that is not part of the
    // protocol, then says hello.
    const char boot[] = "boot text outside the protocol\r\n";
    sink.write(reinterpret_cast<const uint8_t *>(boot), sizeof(boot) - 1);
    protocol.send_hello();
    protocol.send_log("simulated board ready\n", 22);

    // The services loop, as on the board: a thread asleep in select() on
    // the remote screen's sockets. Only when a port was asked for.
    static net::PosixWait sockets;
    static net::Services services(sockets, clock);
    services.add(remote);
    // The command shell for `telnet localhost telnet-port`, as the board
    // offers on port 23 when its settings file says so.
    static net::PosixTcpListener telnet_port;
    if (argc > 4 && !telnet_port.listen(atoi(argv[4]), true)) {
        fprintf(stderr, "cannot listen on port %s\n", argv[4]);
    }

    // Updates over the network (tools/ota.py localhost --port update-port):
    // the image goes to memory, and to the file CHINCHILLA_UPDATE_OUT names
    // if there is one; a restart ends this program.
    static net::PosixTcpListener update_port;
    if (argc > 5 && !update_port.listen(atoi(argv[5]), true)) {
        fprintf(stderr, "cannot listen on port %s\n", argv[5]);
    }

    // init, as on the board: PID 1, which starts the desktop and the network
    // services from C:\.config\startup.txt (the RAM disk is new at every
    // run, so the file is the usual one unless D: has put another there).
    struct Parts {
        ports::System *system;
        dbg::Protocol *protocol;
        bool any_port;
    };
    static Parts parts = {&system, &protocol,
                          vnc_port.listening() || web_port.listening() || telnet_port.listening() ||
                              update_port.listening()};
    static init::Init first(proc::table());
    first.set_log(
        [](const char *line, void *) {
            char text[200];
            const int n = snprintf(text, sizeof(text), "%s\n", line);
            parts.protocol->send_log(text, static_cast<size_t>(n < static_cast<int>(sizeof(text)) ? n : sizeof(text) - 1));
        },
        nullptr);
    first.set_run(
        [](const char *what, char *why, size_t capacity, void *) { return shell::run_text(what, "C:\\", why, capacity); },
        nullptr);
    first.add(
        "desktop",
        [](void *) {
            shell::start(screen, *parts.system, storage, memory);
            shell::open_demo_windows(); // what the debug-tool tests drive
            parts.protocol->set_counters(shell::picture_timings(), shell::kPictureTimings);
            screen.start();
            return true;
        },
        nullptr, true);
    // The services loop is a thread asleep in select() on the sockets. Only
    // when a port was asked for.
    first.add(
        "inetd",
        [](void *) {
            if (!parts.any_port) {
                // Nothing to listen on is not a line of the file gone
                // wrong: the default file has this line, and most runs of
                // the simulator ask for no port.
                return true;
            }
            if (telnet_port.listening() && first.started("desktop")) {
                static host::ThreadLock telnet_lock;
                static host::ThreadEvent telnet_typed, telnet_room, telnet_answered;
                static host::ThreadTasks telnet_tasks;
                static sh::QueueCall telnet_ui(queue, telnet_answered);
                static net::TelnetKit kit;
                kit.lock = &telnet_lock;
                kit.typed = &telnet_typed;
                kit.room = &telnet_room;
                kit.tasks = &telnet_tasks;
                const proc::Pid telnetd = proc::table().spawn_service("telnetd");
                static net::Telnet telnet(shell::session_environment(&telnet_ui, telnetd), kit);
                telnet.set_listener(&telnet_port);
                services.add(telnet);
            }
            if (update_port.listening()) {
                static host::MemoryUpdater updater;
                static host::SteadyClock update_clock;
                static net::Update update(updater, update_clock);
                update.set_listener(&update_port);
                update.set_log(
                    [](const char *line, void *) {
                        char text[200];
                        const int n = snprintf(text, sizeof(text), "%s\n", line);
                        parts.protocol->send_log(text, static_cast<size_t>(n));
                    },
                    nullptr);
                update.on_written(
                    [](bool restart, void *) {
                        if (const char *out = getenv("CHINCHILLA_UPDATE_OUT")) {
                            if (FILE *f = fopen(out, "wb")) {
                                fwrite(updater.installed.data(), 1, updater.installed.size(), f);
                                fclose(f);
                            }
                        }
                        if (restart) {
                            parts.system->reboot();
                            queue.wake(); // the loop looks at once
                        }
                    },
                    nullptr);
                services.add(update);
            }
            static proc::Pid inetd_pid, telnetd_pid;
            inetd_pid = proc::table().spawn_service("inetd");
            telnetd_pid = proc::table().find_by_name("telnetd");
            // Stoppable, as on the board: End Task or kill on inetd.
            services.set_longest_sleep(200000);
            proc::table().set_stop(
                inetd_pid, [](proc::Pid, proc::Signal, void *) { services.request_stop(); }, nullptr);
            std::thread([] {
                while (services.round()) {
                }
                vnc_port.stop();
                web_port.stop();
                telnet_port.stop();
                update_port.stop();
                msg::post(
                    proc::kNone,
                    [](const msg::Message &) {
                        proc::table().exit(inetd_pid);
                        proc::table().exit(telnetd_pid);
                    },
                    nullptr, 0, 0, 1000);
            }).detach();
            return true;
        },
        nullptr);
    // The command shell on the "serial port" (the pseudo-terminal), sharing
    // it with the debug link as on the board.
    first.add(
        "console",
        [](void *) {
            if (!first.started("desktop")) {
                return false;
            }
            class Out : public ports::ByteSink {
            public:
                void write(const uint8_t *data, size_t size) override { parts.protocol->send_text(data, size); }
            };
            static host::ThreadLock console_lock;
            static host::ThreadEvent console_typed, console_answered;
            static host::ThreadTasks console_tasks;
            static sh::QueueCall console_ui(queue, console_answered);
            static Out out;
            static sh::ConsoleKit kit;
            kit.lock = &console_lock;
            kit.typed = &console_typed;
            kit.tasks = &console_tasks;
            const proc::Pid pid = proc::table().spawn_service("console", proc::kProtected);
            static sh::Console console(shell::session_environment(&console_ui, pid), kit, out);
            if (!console.start()) {
                return false;
            }
            static host::SteadyClock idle_clock;
            parts.protocol->set_console(&console.typed(), &idle_clock);
            return true;
        },
        nullptr);
    // CHINCHILLA_STARTUP: a start-up file for this run, lines separated by
    // semicolons ("start desktop;start console"), since the RAM disk is new
    // every time.
    if (const char *wanted = getenv("CHINCHILLA_STARTUP")) {
        std::string text(wanted);
        for (char &c : text) {
            c = c == ';' ? '\n' : c;
        }
        text += "\n";
        storage.make_folder("C:\\.config");
        storage.write_file(init::kPath, text.data(), text.size());
    }
    shell::startup_trouble(first.boot(storage));

    // The loop, as on the board (firmware/os/main/main.cpp): take what has
    // arrived (serial bytes, messages from the services thread), let the
    // desktop work, then sleep until serial bytes or a message come or the
    // desktop's next timer is due.
    uint8_t buffer[512];
    while (!system.reboot_requested) {
        const ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got > 0) {
            protocol.feed(buffer, static_cast<size_t>(got));
        } else {
            protocol.line_idle();
        }
        queue.quiet();
        const bool more = inbox.deliver();
        screen.tick();
        if (more || got == static_cast<ssize_t>(sizeof(buffer))) {
            continue; // there may be more
        }
        uint32_t sleep_us = screen.idle_us();
        sleep_us = sleep_us > 1000000 ? 1000000 : sleep_us;
        sleep_us = sleep_us < 1000 ? 1000 : sleep_us;
        pollfd waits[2] = {{fd, POLLIN, 0}, {queue.wake_fd(), POLLIN, 0}};
        if (poll(waits, 2, static_cast<int>((sleep_us + 999) / 1000)) > 0 && waits[1].revents == 0 &&
            (waits[0].revents & POLLIN) == 0) {
            // Nobody has the other end open yet (or any more): poll() says
            // so at once, every time.
            usleep(sleep_us < 10000 ? sleep_us : 10000);
        }
    }
    // The services thread is still running, on objects of this function:
    // leave without taking them apart under it.
    fflush(nullptr);
    _exit(0);
}
