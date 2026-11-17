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
#include "core/prog/prog.hpp"
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
    // A program of the loader's kind without this system's header in front
    // is not one either.
    CHECK(storage.write_file("C:\\bare.app", program.data() + sizeof(app::FileInfo),
                             program.size() - sizeof(app::FileInfo)) == ports::FsError::Ok);
    CHECK(!shell::run_file("C:\\bare.app", why, sizeof(why)) && strstr(why, "not a program") != nullptr);
    CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);

    // What the file says of itself, without loading it.
    static app::FileInfo info;
    CHECK(!shell::program_info("C:\\text.app", info) && !shell::program_info("C:\\bare.app", info));
    CHECK(shell::program_info("C:\\hello.app", info));
    CHECK(strcmp(info.title, "Hello") == 0 && info.stack == 0 && info.has16 == 1 && info.has32 == 0);
    CHECK(info.icon16[0] == info.clear && info.icon16[17] == gfx::kBlack);

    // The program: a process with the name the program gives itself, with a window of its own
    // that the program made through the OS calls.
    CHECK(shell::run_file("C:\\hello.app", why, sizeof(why)));
    wait(10000);
    const proc::Pid hello = t.find_by_name("Hello");
    CHECK(loader.loaded == 1 && hello != proc::kNone && t.count() == processes + 1);
    CHECK(strcmp(title(), "Hello") == 0 && desktop.active()->owner() == hello);
    CHECK(t.find(hello) != nullptr && t.find(hello)->parent == t.find_by_name("desktop"));
    // What it takes in memory is counted as its own: the code and its window.
    CHECK(t.find(hello) != nullptr && t.find(hello)->memory > loader.bytes(nullptr));
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
    CHECK(os::signal(t.find_by_name("Hello"), proc::Signal::Kill) == proc::Sent::Ok);
    wait(10000);
    wait(10000);
    CHECK(loader.loaded == 1);
    desktop.close(*desktop.active());
    wait(10000);
    wait(10000);
    CHECK(loader.loaded == 0 && t.count() == processes && os::blocks_out() == blocks);

    // Start > Programs: the program files of C:\Programs, after the
    // built-in ones, by the names and pictures they give themselves. Found
    // as the menu opens; gone from it when the file has gone.
    desktop.open_start_menu();
    const wm::Menu *programs = desktop.popup_menu(0)->items[0].submenu;
    const int built_in = programs->count;
    desktop.close_menus();
    CHECK(storage.make_folder("C:\\Programs") == ports::FsError::Ok);
    CHECK(storage.write_file("C:\\Programs\\hello.app", program.data(), program.size()) == ports::FsError::Ok);
    CHECK(storage.write_file("C:\\Programs\\notes.txt", "x", 1) == ports::FsError::Ok);
    CHECK(storage.write_file("C:\\Programs\\broken.app", "not a program", 13) == ports::FsError::Ok);
    for (int round = 0; round < 2; round++) { // the second time from what it remembers
        desktop.open_start_menu();
        CHECK(programs->count == built_in + 1);
        const wm::MenuItem &entry = programs->items[built_in];
        CHECK(strcmp(entry.label, "Hello") == 0 && entry.icon != nullptr && entry.icon->pixels[17] == gfx::kBlack);
        desktop.close_menus();
    }
    programs->items[built_in].action(programs->items[built_in].context);
    wait(10000);
    CHECK(loader.loaded == 1 && strcmp(title(), "Hello") == 0);
    desktop.close(*desktop.active());
    wait(10000);
    wait(10000);
    CHECK(loader.loaded == 0);
    CHECK(storage.remove("C:\\Programs\\hello.app") == ports::FsError::Ok);
    desktop.open_start_menu();
    CHECK(programs->count == built_in);
    desktop.close_menus();
    CHECK(t.count() == processes);

    // On a task of its own (backlog item 26), a real thread here: its way
    // in and its events run there, and the program is not taken out of
    // memory before its task has gone.
    static host::ThreadTasks tasks;
    static host::ThreadEvent events[2];
    static prog::Kit kit = {&events[0], &events[1]};
    prog::runner().set_tasks(&tasks, &kit, 1);
    CHECK(shell::run_file("C:\\hello.app", why, sizeof(why)));
    wait(10000);
    const proc::Pid threaded = t.find_by_name("Hello");
    CHECK(tasks.started == 1 && prog::runner().has_task(threaded) && strcmp(title(), "Hello") == 0);
    press(wm::key::Enter);
    wait(10000);
    desktop.close(*desktop.active());
    wait(10000);
    wait(10000);
    CHECK(!t.alive(threaded) && tasks.ended == 1 && prog::runner().tasks() == 0 && loader.loaded == 0);
    prog::runner().set_tasks(nullptr, nullptr, 0);

    if (failures == 0) {
        printf("loaded: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
