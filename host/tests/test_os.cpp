// The OS calls (core/os/os.hpp): whose a block of memory is, the files, a
// process and a scope, and that the programs moved to them give back what
// they asked for.
#include <cstdio>
#include <cstring>

#include "core/fs/memory_volume.hpp"
#include "core/os/os.hpp"
#include "core/proc/table.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
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

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

void test_memory()
{
    proc::Table &t = proc::table();
    const uint32_t blocks = os::blocks_out(), bytes = os::bytes_out();
    const proc::Pid a = os::spawn("a");
    CHECK(a != proc::kNone && t.find(a)->parent == os::self());
    void *mine = os::allocate(1000);
    void *its = nullptr;
    {
        const os::As as(a);
        CHECK(os::self() == a);
        its = os::allocate(300);
        // Zeroed, whatever was there before.
        bool zero = its != nullptr;
        for (int i = 0; zero && i < 300; i++) {
            zero = static_cast<uint8_t *>(its)[i] == 0;
        }
        CHECK(zero);
    }
    CHECK(os::self() != a);
    CHECK(mine != nullptr && its != nullptr);
    CHECK(t.find(a)->memory == 300);
    CHECK(os::blocks_out() == blocks + 2 && os::bytes_out() == bytes + 1300);
    // Given back by someone else: still taken off the owner's count.
    os::release(its);
    CHECK(t.find(a)->memory == 0);
    // The owner gone before its block: nothing breaks.
    void *late = nullptr;
    {
        const os::As as(a);
        late = os::memory().allocate(64);
    }
    t.exit(a);
    os::memory().release(late);
    os::release(mine);
    os::release(nullptr);
    CHECK(os::blocks_out() == blocks && os::bytes_out() == bytes);
    CHECK(os::find("a") == proc::kNone && os::find("desktop") != proc::kNone);
}

void test_files()
{
    using os::file::Error;
    CHECK(os::file::has_drive('C') && !os::file::has_drive('Q'));
    char letters[4];
    CHECK(os::file::drives(letters, 4) == 1 && letters[0] == 'C');
    fs::VolumeInfo info;
    CHECK(os::file::drive_info('C', info) == Error::Ok);

    CHECK(os::file::make_folder("C:\\os") == Error::Ok);
    CHECK(os::file::write_file("C:\\os\\a.txt", "hello", 5) == Error::Ok);
    fs::DirEntry e;
    CHECK(os::file::stat("C:\\os\\a.txt", e) == Error::Ok && e.size == 5 && !e.folder);
    CHECK(os::file::entry("C:\\os", 0, e) == Error::Ok && strcmp(e.name, "a.txt") == 0);
    CHECK(os::file::entry("C:\\os", 1, e) != Error::Ok);

    // An open file is a handle, and the system knows whose.
    using os::file::Handle;
    Handle f = os::file::kNoFile;
    char text[16] = {};
    size_t got = 0;
    CHECK(os::file::open_files() == 0);
    CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, f) == Error::Ok && f != os::file::kNoFile);
    CHECK(os::file::open_files(os::self()) == 1 && os::file::open_files() == 1);
    CHECK(os::file::read(f, text, sizeof(text), got) == Error::Ok && got == 5 && memcmp(text, "hello", 5) == 0);
    CHECK(os::file::close(f) == Error::Ok && os::file::open_files() == 0);
    // A handle that was closed is refused, also once its slot is in use
    // again; so are numbers that were never handles.
    CHECK(os::file::read(f, text, sizeof(text), got) == Error::Io && got == 0);
    CHECK(os::file::close(f) == Error::Io);
    Handle again = os::file::kNoFile;
    CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, again) == Error::Ok && again != f);
    CHECK(os::file::read(f, text, sizeof(text), got) == Error::Io);
    CHECK(os::file::close(again) == Error::Ok);
    CHECK(os::file::close(0) == Error::Io && os::file::close(-5) == Error::Io && os::file::close(77) == Error::Io);
    // A file that is not there takes no slot.
    CHECK(os::file::open("C:\\os\\none.txt", ports::OpenMode::Read, f) == Error::NotFound && f == os::file::kNoFile);
    CHECK(os::file::open_files() == 0);

    // A save given up leaves the old file; one kept replaces it.
    Handle save = os::file::kNoFile;
    CHECK(os::file::begin_save("C:\\os\\a.txt", save) == Error::Ok);
    CHECK(os::file::write(save, "bye", 3) == Error::Ok);
    CHECK(os::file::finish_save(save, false) == Error::Ok && os::file::open_files() == 0);
    CHECK(os::file::read_file("C:\\os\\a.txt", text, sizeof(text), got) == Error::Ok && got == 5);
    CHECK(os::file::begin_save("C:\\os\\a.txt", save) == Error::Ok);
    CHECK(os::file::write(save, "bye", 3) == Error::Ok);
    CHECK(os::file::finish_save(save) == Error::Ok);
    CHECK(os::file::read_file("C:\\os\\a.txt", text, sizeof(text), got) == Error::Ok && got == 3);
    CHECK(os::file::finish_save(save) == Error::Io); // it was closed by the first

    // One process may hold kMaxOpenEach, the machine kMaxOpen.
    {
        proc::Table &t = proc::table();
        const proc::Pid a = os::spawn("holder"), b = os::spawn("other"), c = os::spawn("third");
        const proc::Pid d = os::spawn("fourth");
        Handle held[os::file::kMaxOpen + 1] = {};
        int n = 0;
        for (const proc::Pid pid : {a, b, d}) {
            const os::As as(pid);
            for (int i = 0; i < os::file::kMaxOpenEach; i++) {
                CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, held[n++]) == Error::Ok);
            }
            Handle more;
            CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, more) == Error::TooManyOpen);
        }
        CHECK(os::file::open_files(a) == os::file::kMaxOpenEach && os::file::open_files() == os::file::kMaxOpen);
        {
            const os::As as(c);
            Handle more;
            CHECK(os::file::open("C:\\os\\a.txt", ports::OpenMode::Read, more) == Error::TooManyOpen);
        }
        // A process that ends: what it left open is closed for it, and its
        // handles are dead. The other's are as they were.
        CHECK(t.exit(a));
        CHECK(os::file::open_files(a) == 0 && os::file::open_files() == 2 * os::file::kMaxOpenEach);
        CHECK(os::file::read(held[0], text, sizeof(text), got) == Error::Io);
        CHECK(os::file::read(held[os::file::kMaxOpenEach], text, sizeof(text), got) == Error::Ok && got == 3);
        {
            // One that ends in the middle of a save: the new text is thrown
            // away, the old file stays, and no temporary file is left.
            const os::As as(c);
            CHECK(os::file::begin_save("C:\\os\\a.txt", save) == Error::Ok);
            CHECK(os::file::write(save, "half", 4) == Error::Ok);
        }
        CHECK(t.exit(c) && t.exit(b) && t.exit(d) && os::file::open_files() == 0);
        CHECK(os::file::read_file("C:\\os\\a.txt", text, sizeof(text), got) == Error::Ok && got == 3);
        fs::DirEntry only;
        CHECK(os::file::entry("C:\\os", 0, only) == Error::Ok && strcmp(only.name, "a.txt") == 0);
        CHECK(os::file::entry("C:\\os", 1, only) != Error::Ok);
    }

    CHECK(os::file::rename("C:\\os\\a.txt", "C:\\os\\b.txt") == Error::Ok);
    CHECK(os::file::remove("C:\\os") == Error::NotEmpty);
    CHECK(os::file::remove("C:\\os\\b.txt") == Error::Ok);
    CHECK(os::file::remove("C:\\os") == Error::Ok);

    // Settings: none, then what was saved.
    fs::Ini ini;
    CHECK(!os::load_settings("ostest", ini));
    ini.set("a", "b", "c");
    CHECK(os::save_settings("ostest", ini));
    fs::Ini back;
    char value[8] = {};
    CHECK(os::load_settings("ostest", back) && back.get("a", "b", value, sizeof(value)) && strcmp(value, "c") == 0);
    CHECK(os::file::stat("C:\\.config\\ostest.ini", e) == Error::Ok);
}

void test_windows_and_time()
{
    const gfx::Rect area = os::work_area();
    CHECK(area.w == 640 && area.h == 480 - wm::Wm::kTaskbarHeight);
    const gfx::Rect r = os::centred(200, 100);
    CHECK(r.x == 220 && r.w == 200 && r.h == 100 && r.y == (area.h - 100) / 2);
    CHECK(!os::menu_open());

    // A timer belongs to whoever asked, and goes with it.
    static int fired = 0;
    const proc::Pid p = os::spawn("ticker");
    int id = 0;
    {
        const os::As as(p);
        id = os::start_timer(1000, 1000, [](void *) { fired++; }, nullptr);
    }
    CHECK(id != 0);
    const uint64_t before = os::now_us();
    wait(1500);
    CHECK(fired == 1 && os::now_us() == before + 1500);
    os::stop_timer(id);
    wait(5000);
    CHECK(fired == 1);
    proc::table().exit(p);
}

// What the Task Manager asks: the list, a window's process, a signal.
void test_process_list()
{
    const int before = os::process_count();
    const proc::Pid p = os::spawn("listed");
    CHECK(os::process_count() == before + 1 && os::process_at(before).pid == p);
    CHECK(os::alive(p) && os::process(p) != nullptr && strcmp(os::process(p)->name, "listed") == 0);
    CHECK(os::main_window(p) == nullptr);
    os::refresh_figures();
    // The desktop is protected; one that is not there is not there; a
    // window program without windows is ended at the next safe point.
    CHECK(os::signal(os::find("desktop"), proc::Signal::Kill) == proc::Sent::NotPermitted);
    CHECK(os::signal(30000, proc::Signal::Term) == proc::Sent::NoSuchProcess);
    CHECK(os::signal(p, proc::Signal::Kill) == proc::Sent::Ok);
    wait(10000);
    wait(10000);
    CHECK(!os::alive(p) && os::process(p) == nullptr && os::process_count() == before);

    // A program with a window: the window is its main one, it is in front,
    // nothing blocks it, and Term closes it.
    shell::open_culebra();
    wait(10000);
    wm::Window *w = desktop.active();
    CHECK(w != nullptr);
    if (w != nullptr) {
        const proc::Pid owner = w->owner();
        CHECK(os::main_window(owner) == w && os::in_front(*w) && os::blocker(*w) == nullptr);
        CHECK(os::signal(owner, proc::Signal::Term) == proc::Sent::Ok);
        wait(10000);
        wait(10000);
        CHECK(!os::alive(owner));
    }

    ports::SystemStats stats = {};
    os::stats(stats);
    uint32_t value = 0;
    char text[64] = "";
    os::fact(ports::Fact::Processor, value, text, sizeof(text));
    CHECK(&os::file::storage() == &storage);
}

// Every program opened and closed: the memory out comes back to where it
// was, so nothing a program asked the OS for outlives it.
void test_programs_give_back()
{
    const uint32_t blocks = os::blocks_out(), bytes = os::bytes_out();
    int opened = 0;
    for (int i = 0; i < shell::app_count(); i++) {
        const shell::App &app = shell::app(i);
        wm::Window *before = desktop.active();
        shell::launch(app);
        wait(10000);
        wm::Window *w = desktop.active();
        if (w == nullptr || w == before) {
            continue;
        }
        opened++;
        CHECK(os::blocks_out() > blocks);
        desktop.close(*w);
        // A program that asks before closing has a box open now: the last
        // button is "do not ask again", whatever it says.
        for (int tries = 0; tries < 4 && desktop.active() != before && desktop.active() != nullptr; tries++) {
            wait(10000);
            if (desktop.active() != nullptr && desktop.active() != before) {
                desktop.close(*desktop.active());
            }
        }
        wait(10000);
        if (os::blocks_out() != blocks) {
            printf("%s left %u blocks, %u bytes\n", app.name, static_cast<unsigned>(os::blocks_out() - blocks),
                   static_cast<unsigned>(os::bytes_out() - bytes));
        }
        CHECK(os::blocks_out() == blocks && os::bytes_out() == bytes);
    }
    CHECK(opened >= 5);
}

// What the settings tools and Shut Down ask: the background, the
// screensaver, the network, the way down.
void test_desktop_calls()
{
    wm::Wm::Backdrop b = desktop.backdrop();
    b.colour = {1, 2, 3};
    b.pattern[0] = 0x55;
    os::set_backdrop(b);
    CHECK(desktop.backdrop().colour.g == 2 && desktop.backdrop().pattern[0] == 0x55);

    // No screensaver set: a preview shows one, and a key ends it.
    os::set_screen_saver(-1, 60);
    os::preview_screen_saver(99); // there is no such animation
    CHECK(!desktop.screen_saver_active());
    os::preview_screen_saver(0);
    CHECK(desktop.screen_saver_active());
    for (int i = 0; i < 10; i++) {
        wait(40000);
    }
    desktop.key(wm::key::Escape, true);
    desktop.key(wm::key::Escape, false);
    wait(10000);
    CHECK(!desktop.screen_saver_active());
    CHECK(os::last_saver_run().frames > 0 && os::last_saver_run().shown_us > 0);
    // One set: it starts by itself after the wait, and not when taken away.
    os::set_screen_saver(1, 2);
    wait(2100000);
    CHECK(desktop.screen_saver_active());
    desktop.key(wm::key::Escape, true);
    desktop.key(wm::key::Escape, false);
    os::set_screen_saver(1, 0);
    wait(3000000);
    CHECK(!desktop.screen_saver_active());

    // The Display and Screen Saver tools keep nothing while they are closed.
    const uint32_t blocks = os::blocks_out();
    shell::display_dialog();
    shell::screen_saver_dialog();
    wait(10000);
    CHECK(os::blocks_out() == blocks + 2);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
        wait(10000);
    }
    CHECK(os::blocks_out() == blocks);
    // Closing Display put back what the settings file says, not what was
    // set behind its back.
    CHECK(desktop.backdrop().pattern[0] != 0x55);

    CHECK(os::network() == nullptr); // this machine has no radio

    // Shut Down with nothing open: the screen is taken (what stops it is in
    // test_system). Last: there is no way back from it.
    CHECK(os::shut_down() && desktop.taken_over());
}

} // namespace

int main()
{
    static fs::MemoryVolume disk(memory.allocate(1 << 18), 1 << 18, "RAM disk");
    storage.mount('C', disk);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    const proc::Scope as(&proc::table(), proc::table().find_by_name("desktop"));
    wait(10000);

    test_memory();
    test_files();
    test_windows_and_time();
    test_process_list();
    test_programs_give_back();
    test_desktop_calls();

    if (failures == 0) {
        printf("os: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
