// The start-up self-test: the memory test finds a real address fault, and a
// check that fails is counted and shown, and the boot goes on.
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "core/gfx/canvas.hpp"
#include "core/shell/boot.hpp"
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

} // namespace

int main()
{
    test_memory_fault();
    test_failed_check();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
