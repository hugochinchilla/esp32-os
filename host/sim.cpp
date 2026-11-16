// A simulated board. It opens a pseudo-terminal, prints its path, and speaks
// the debug protocol on it, so the PC tool can be used and tested with no
// hardware:
//
//   ./sim                       prints e.g. /dev/pts/5
//   tools/dbg.py --port /dev/pts/5 screenshot out.png
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include "core/dbg/protocol.hpp"
#include "core/shell/shell.hpp"
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

int main()
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

    shell::start(screen, system);
    screen.start();

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
            usleep(10000);
        }
        screen.tick();
    }
    return 0;
}
