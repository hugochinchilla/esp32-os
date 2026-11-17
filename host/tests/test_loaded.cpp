// Programs loaded from files (backlog item 27): a program compiled apart
// from the system is read from the disk, joined to the OS calls by name,
// run as a process of its own and taken out of memory when that has ended.
// Here the program is a shared library of the PC (programs/hello) and the
// loader is the PC's; on the board it is an ELF file and Espressif's loader.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/os/os.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../dl_loader.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);
host::DlLoader loader;

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

void press(uint8_t usage)
{
    desktop.key(usage, true);
    desktop.key(usage, false);
}

const char *title() { return desktop.active() != nullptr ? desktop.active()->title() : ""; }

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_loaded hello.app\n");
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<char> program((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(!program.empty());

    static fs::MemoryVolume disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', disk);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    proc::Table &t = proc::table();
    const proc::Scope as(&t, t.find_by_name("desktop"));
    wait(10000);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    const int processes = t.count();
    const uint32_t blocks = os::blocks_out();
    char why[120] = "";

    // No loader: no file is run, and it says so.
    CHECK(storage.write_file("C:\\hello.app", program.data(), program.size()) == ports::FsError::Ok);
    CHECK(shell::is_program_file("C:\\hello.app") && !shell::is_program_file("C:\\hello.txt"));
    CHECK(!shell::run_file("C:\\hello.app", why, sizeof(why)) && strstr(why, "cannot load") != nullptr);
    shell::set_loader(&loader);

    // Not there, empty, not a program: each refused in words, nothing left.
    CHECK(!shell::run_file("C:\\nothing.app", why, sizeof(why)) && why[0] != 0);
    CHECK(storage.write_file("C:\\empty.app", "", 0) == ports::FsError::Ok);
    CHECK(!shell::run_file("C:\\empty.app", why, sizeof(why)) && strstr(why, "empty") != nullptr);
    CHECK(storage.write_file("C:\\text.app", "not a program", 13) == ports::FsError::Ok);
    CHECK(!shell::run_file("C:\\text.app", why, sizeof(why)) && strstr(why, "not a program") != nullptr);
    CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);

    // The program: a process named after the file, with a window of its own
    // that the program made through the OS calls.
    CHECK(shell::run_file("C:\\hello.app", why, sizeof(why)));
    wait(10000);
    const proc::Pid hello = t.find_by_name("hello");
    CHECK(loader.loaded == 1 && hello != proc::kNone && t.count() == processes + 1);
    CHECK(strcmp(title(), "Hello") == 0 && desktop.active()->owner() == hello);
    CHECK(t.find(hello) != nullptr && t.find(hello)->parent == t.find_by_name("desktop"));
    // Its code answers events: Enter presses its default button.
    press(wm::key::Enter);
    press(wm::key::Enter);
    wait(10000);
    CHECK(strcmp(title(), "Hello") == 0);

    // Closed: the process ends, and the program leaves memory after its
    // window has been destroyed.
    desktop.close(*desktop.active());
    wait(10000);
    wait(10000);
    CHECK(!t.alive(hello) && loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);

    // Twice at a time: two processes, two copies; and from the File
    // manager's way of opening a file by its kind.
    CHECK(shell::run_file("C:\\hello.app", why, sizeof(why)));
    CHECK(shell::open_file("C:\\hello.app"));
    wait(10000);
    CHECK(loader.loaded == 2 && t.count() == processes + 2);
    // Ended from outside (End Task): out of memory all the same.
    CHECK(os::signal(t.find_by_name("hello"), proc::Signal::Kill) == proc::Sent::Ok);
    wait(10000);
    wait(10000);
    CHECK(loader.loaded == 1);
    desktop.close(*desktop.active());
    wait(10000);
    wait(10000);
    CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);

    if (failures == 0) {
        printf("loaded: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
