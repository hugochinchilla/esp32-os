// A simulated board. It opens a pseudo-terminal, prints its path, and speaks
// the debug protocol on it, so the PC tool can be used and tested with no
// hardware:
//
//   ./sim                       prints e.g. /dev/pts/5
//   tools/dbg.py --port /dev/pts/5 screenshot out.png
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "core/dbg/protocol.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "core/net/posix_tcp.hpp"
#include "core/net/rfb.hpp"
#include "core/net/wifi.hpp"
#include "fake_wifi.hpp"
#include "folder_volume.hpp"
#include "core/wm/wm.hpp"
#include "host_adapters.hpp"

namespace {

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

// ./sim [folder [vnc-port]]: the folder, if given, is drive D:; with a port,
// the remote screen (VNC) is offered on it, to this computer only.
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
    host::NoLock lock;
    host::HostSystem system;
    host::MemoryPointer pointer;
    host::SteadyClock clock;
    static wm::Wm screen(display, pointer, clock);
    FdSink sink(fd);
    dbg::Protocol protocol(sink, lock, display, pointer, screen, system, "sim");
    protocol.set_desktop(&screen);

    // C: a RAM disk, as on the board. D: a folder of the PC, if given.
    static host::HostMemory memory;
    static fs::Storage storage;
    constexpr size_t kRamDisk = 2 * 1024 * 1024;
    static fs::MemoryVolume ram_disk(memory.allocate(kRamDisk), kRamDisk, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    static host::FolderVolume *pc = nullptr;
    if (argc > 1) {
        pc = new host::FolderVolume(argv[1]);
        storage.mount('D', *pc);
    }
    protocol.set_storage(&storage);
    // The start-up screens, as on the board (the same memory test, over
    // ordinary memory of the PC).
    static shell::BootIo boot_io{display, system, memory, clock, &storage, nullptr, nullptr, nullptr};
    shell::run_self_test(boot_io);
    shell::show_splash(boot_io);
    protocol.set_boot_screens(
        [](int which, void *) {
            if (which == 0) {
                shell::run_self_test(boot_io, false);
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
    shell::start(screen, system, storage, memory);
    protocol.set_counters(shell::picture_timings(), shell::kPictureTimings);
    screen.start();

    // The remote screen, for a VNC viewer on this computer.
    static rfb::Server remote(display, pointer, screen, clock, memory);
    static net::PosixTcpListener vnc_port;
    screen.set_damage_listener([](const gfx::Rect &r, void *) { remote.damage(r); }, nullptr);
    if (argc > 2) {
        if (vnc_port.listen(atoi(argv[2]), true)) {
            remote.set_listener(&vnc_port);
        } else {
            fprintf(stderr, "cannot listen on port %s\n", argv[2]);
        }
    }

    // A real board first prints start-up text that is not part of the
    // protocol, then says hello.
    const char boot[] = "boot text outside the protocol\r\n";
    sink.write(reinterpret_cast<const uint8_t *>(boot), sizeof(boot) - 1);
    protocol.send_hello();
    protocol.send_log("simulated board ready\n", 22);

    uint8_t buffer[512];
    while (!system.reboot_requested) {
        ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got > 0) {
            protocol.feed(buffer, static_cast<size_t>(got));
        } else {
            // Nobody has the other end open yet (or any more).
            usleep(vnc_port.listening() ? 2000 : 10000);
        }
        screen.tick();
        remote.poll();
    }
    return 0;
}
