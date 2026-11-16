// The start-up self-test: the memory test finds a real address fault, and a
// check that fails is counted and shown, and the boot goes on.
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/gfx/canvas.hpp"
#include "core/image/image.hpp"
#include "core/shell/boot.hpp"
#include "../folder_volume.hpp"
#include "../host_adapters.hpp"

namespace {

int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                  \
        }                                                                \
    } while (0)

constexpr size_t kChunk = 128 * 1024;

// Lends one block of four chunks whose second and third chunks are the same
// memory (an address fault: writing one changes the other).
class AliasedMemory : public ports::Memory {
public:
    void *allocate(size_t) override { return nullptr; }
    void *borrow(size_t, size_t &got) override
    {
        got = 0;
        if (lent_) {
            return nullptr;
        }
        const int fd = memfd_create("alias", 0);
        if (fd < 0 || ftruncate(fd, 3 * kChunk) != 0) {
            return nullptr;
        }
        auto *base = static_cast<uint8_t *>(mmap(nullptr, 4 * kChunk, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        const size_t offsets[4] = {0, kChunk, kChunk, 2 * kChunk};
        for (int i = 0; i < 4; i++) {
            mmap(base + i * kChunk, kChunk, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, offsets[i]);
        }
        close(fd);
        lent_ = true;
        block_ = base;
        got = 4 * kChunk;
        return base;
    }
    void release(void *block) override { munmap(block, 4 * kChunk); }
    uint8_t *block_ = nullptr;

private:
    bool lent_ = false;
};

// A system whose flash cannot be read.
class NoFlashSystem : public host::HostSystem {
public:
    bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity) override
    {
        if (which == ports::Fact::Flash) {
            value = 0;
            return false;
        }
        return HostSystem::fact(which, value, text, capacity);
    }
};

void test_memory_fault()
{
    AliasedMemory memory;
    host::FixedClock clock;
    const shell::MemoryTestResult r = shell::test_memory(memory, clock, 1000000, nullptr, nullptr);
    CHECK(!r.ok);
    CHECK(r.bad_address == reinterpret_cast<uintptr_t>(memory.block_ + kChunk)); // the first word of chunk 1
    CHECK(r.wrote != r.read);

    host::HostMemory good;
    const shell::MemoryTestResult g = shell::test_memory(good, clock, 1000000, nullptr, nullptr);
    CHECK(g.ok && g.tested_kb == 4096);
}

void test_failed_check()
{
    host::MemoryDisplay display(640, 480);
    NoFlashSystem system;
    host::HostMemory memory;
    host::FixedClock clock;
    shell::BootIo io{display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    // Flash fails, and so do the drives (none given).
    CHECK(shell::run_self_test(io, false) == 2);
    // The status column shows red [FAIL] on the Flash line (the fifth).
    const int y = 84 + 4 * 18 + 6;
    bool red = false;
    for (int x = 560; x < 600; x++) {
        red = red || display.pixels()[y * 640 + x] == gfx::rgb(7, 1, 0);
    }
    CHECK(red);
}

// The splash is the approved drawing, pixel for pixel: the PNG in
// docs/look/boot/, read with our own decoder.
struct Rows : image::RowSink {
    uint8_t *out;
    void row(int y, const uint8_t *rgb) override
    {
        for (int x = 0; x < 640; x++) {
            out[y * 640 + x] = gfx::nearest({rgb[x * 3], rgb[x * 3 + 1], rgb[x * 3 + 2]});
        }
    }
};

void test_splash_matches_approved(const char *look_folder)
{
    static host::FolderVolume volume(look_folder);
    static fs::Storage storage;
    storage.mount('L', volume);
    host::HostMemory memory;
    static uint8_t approved[640 * 480];
    image::Decoder d;
    image::Info info;
    CHECK(d.open(storage, "L:\\boot-e-colour-ribbon.png", memory, info) == image::Error::None);
    CHECK(info.width == 640 && info.height == 480);
    Rows rows;
    rows.out = approved;
    while (!d.done()) {
        if (d.step(rows, 16) != image::Error::None) {
            CHECK(false);
            break;
        }
    }
    d.close();

    host::MemoryDisplay display(640, 480);
    host::HostSystem system;
    host::FixedClock clock;
    shell::BootIo io{display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    shell::show_splash(io, false);
    int differ = 0;
    for (int i = 0; i < 640 * 480; i++) {
        differ += display.pixels()[i] != approved[i];
    }
    if (differ != 0) {
        printf("the splash differs from the approved drawing in %d pixels\n", differ);
    }
    CHECK(differ == 0);
}

// A clock that moves on 1 ms each time it is read.
class SteppingClock : public ports::Clock {
public:
    uint32_t micros() override { return now += 1000; }
    uint32_t now = 0;
};

// The self-test waits for the monitor; a key skips the waiting and the
// splash; the fast start draws nothing and logs its checks.
void test_waiting_and_skipping()
{
    host::MemoryDisplay display(640, 480);
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    shell::BootIo io{display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    io.video_start_us = 0;
    io.monitor_wait_ms = 4000;
    shell::run_self_test(io, true);
    CHECK(clock.now >= 4000000 && clock.now < 4100000 && !io.skipped);
    clock.now = 0;
    shell::show_splash(io, true);
    CHECK(clock.now >= io.splash_ms * 1000 && clock.now < io.splash_ms * 1000 + 100000);

    // A key during the wait: the self-test ends early, the splash is not shown.
    static int polls = 0;
    shell::BootIo skip_io{display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    skip_io.video_start_us = clock.now;
    skip_io.monitor_wait_ms = 4000;
    skip_io.skip = [](void *) { return ++polls > 50; };
    const uint32_t start = clock.now;
    shell::run_self_test(skip_io, true);
    CHECK(skip_io.skipped && clock.now - start < 2000000);
    std::vector<uint8_t> before(display.pixels(), display.pixels() + 640 * 480);
    shell::show_splash(skip_io, true);
    CHECK(memcmp(before.data(), display.pixels(), before.size()) == 0); // nothing drawn

    // Fast start: nothing drawn, every check logged, the failure counted.
    NoFlashSystem broken;
    static int lines = 0, failed_lines = 0;
    shell::BootIo quiet{display, broken, memory, clock, nullptr, nullptr, nullptr,
                        [](const char *line, void *) {
                            lines++;
                            failed_lines += strstr(line, "FAILED") != nullptr;
                        }};
    CHECK(shell::run_quiet_checks(quiet) == 1);
    CHECK(lines == 8 && failed_lines == 1); // 7 checks and "memory test skipped"
    CHECK(memcmp(before.data(), display.pixels(), before.size()) == 0);
}

} // namespace

int main(int argc, char **argv)
{
    test_memory_fault();
    test_failed_check();
    test_waiting_and_skipping();
    if (argc > 1) {
        test_splash_matches_approved(argv[1]);
    } else {
        printf("no docs/look/boot folder given: splash not compared\n");
        failures++;
    }
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
