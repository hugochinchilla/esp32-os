// The start-up self-test: the memory test finds a real address fault, and a
// check that fails is counted and shown, and the boot goes on.
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
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
    // The drive line (line 8) and the summary (line 12) are red.
    auto red_in_line = [&](int index) {
        const int y = 24 + index * 18 + 8;
        for (int x = 24; x < 500; x++) {
            if (display.pixels()[y * 640 + x] == gfx::rgb(7, 1, 0)) {
                return true;
            }
        }
        return false;
    };
    CHECK(red_in_line(8) && red_in_line(12) && !red_in_line(3));
}

struct Rows : image::RowSink {
    uint8_t *out;
    void row(int y, const uint8_t *rgb) override
    {
        for (int x = 0; x < 640; x++) {
            out[y * 640 + x] = gfx::nearest({rgb[x * 3], rgb[x * 3 + 1], rgb[x * 3 + 2]});
        }
    }
};

// An approved drawing in docs/look/boot/, read with our own decoder.
void read_approved(const char *look_folder, const char *name, uint8_t *approved)
{
    static host::FolderVolume volume(look_folder);
    static fs::Storage storage;
    static bool mounted = false;
    if (!mounted) {
        storage.mount('L', volume);
        mounted = true;
    }
    host::HostMemory memory;
    image::Decoder d;
    image::Info info;
    char path[64];
    snprintf(path, sizeof(path), "L:\\%s", name);
    CHECK(d.open(storage, path, memory, info) == image::Error::None);
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
}

// The self-test screen, given the values the drawing shows, is the approved
// drawing pixel for pixel.
void test_self_test_matches_approved(const char *look_folder)
{
    static uint8_t approved[640 * 480];
    read_approved(look_folder, "boot-b-self-test.png", approved);
    host::MemoryDisplay display(640, 480);
    gfx::RectList clip;
    clip.set({0, 0, 640, 480});
    gfx::Canvas c(display.pixels(), 640, 480, clip, {0, 0, 640, 480});
    shell::draw_self_test(c, 640, 480,
                          {"v0.1", "ESP32-S3 dual core, 240 MHz", 8192, 512, "VGA 640x480, 256 colours", "2 MB RAM disk",
                           "serial debug link", "none"});
    int differ = 0, x0 = 640, y0 = 480, x1 = -1, y1 = -1;
    for (int i = 0; i < 640 * 480; i++) {
        if (display.pixels()[i] != approved[i]) {
            differ++;
            x0 = std::min(x0, i % 640); x1 = std::max(x1, i % 640);
            y0 = std::min(y0, i / 640); y1 = std::max(y1, i / 640);
        }
    }
    if (differ != 0) {
        printf("the self-test differs from the approved drawing in %d pixels (x %d-%d, y %d-%d)\n", differ, x0, x1,
               y0, y1);
    }
    CHECK(differ == 0);
}

// A clock that moves on 1 ms each time it is read.
class SteppingClock : public ports::Clock {
public:
    uint32_t micros() override { return now += 1000; }
    uint32_t now = 0;
};

// The self-test waits for the monitor; a key skips the waiting; the fast
// start draws nothing and logs its checks.
void test_waiting_and_skipping()
{
    host::MemoryDisplay display(640, 480);
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    shell::BootIo io{display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    io.video_start_us = 0;
    io.monitor_wait_ms = 4000;
    io.hold_s = 0;
    shell::run_self_test(io, true);
    CHECK(clock.now >= 4000000 + 6 * 120 * 1300 && clock.now < 9000000 && !io.skipped);
    // The countdown: five more seconds, "Booting in 1" last.
    io.hold_s = 5;
    clock.now = 0;
    io.monitor_wait_ms = 0;
    shell::run_self_test(io, true);
    CHECK(clock.now >= 5000000 && !io.skipped);

    // A key during the wait: the self-test ends early.
    static int polls = 0;
    shell::BootIo skip_io{display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    skip_io.video_start_us = clock.now;
    skip_io.monitor_wait_ms = 4000;
    skip_io.skip = [](void *) { return ++polls > 50; };
    const uint32_t start = clock.now;
    shell::run_self_test(skip_io, true);
    CHECK(skip_io.skipped && clock.now - start < 2000000);
    std::vector<uint8_t> before(display.pixels(), display.pixels() + 640 * 480);

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

// The one-shot fast boot: taken once when armed, then the starts are normal;
// anything else in the word (memory after a power cut) is not taken.
static void test_one_shot_fast_boot()
{
    uint32_t word = shell::kFastBootMagic;
    CHECK(shell::take_one_shot_fast_boot(word));
    CHECK(!shell::take_one_shot_fast_boot(word));
    word = 0xDEADBEEF;
    CHECK(!shell::take_one_shot_fast_boot(word) && word == 0);
}

int main(int argc, char **argv)
{
    test_one_shot_fast_boot();
    test_memory_fault();
    test_failed_check();
    test_waiting_and_skipping();
    if (argc > 1) {
        test_self_test_matches_approved(argv[1]);
    } else {
        printf("no docs/look/boot folder given: the self-test not compared\n");
        failures++;
    }
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
