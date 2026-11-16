// The system programs: the Task Manager on Ctrl+Alt+Del, Shut Down with its
// power-down screen, and the Defragmenter's made-up disk.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/shell/defrag.hpp"
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

constexpr uint8_t kShift = 1, kCtrl = 2, kAlt = 4;

void key(uint16_t usage, uint8_t mods = 0)
{
    const uint16_t mod_keys[3] = {wm::key::LeftShift, wm::key::LeftCtrl, wm::key::LeftAlt};
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], true);
        }
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], false);
        }
    }
    clock_.now += 100000;
}

void letter(char c, uint8_t mods = 0) { key(static_cast<uint16_t>(wm::key::A + (c - 'a')), mods); }
void ctrl_alt_del() { key(wm::key::Delete, kCtrl | kAlt); }

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

const char *title() { return desktop.active() ? desktop.active()->title() : ""; }

wm::Window *find(const char *name)
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (strcmp(desktop.window_at(z)->title(), name) == 0) {
            return desktop.window_at(z);
        }
    }
    return nullptr;
}

std::string screen() { return std::string(reinterpret_cast<char *>(display.pixels()), 640 * 480); }

bool no_stale_pixels()
{
    const std::string before = screen();
    desktop.repaint_all();
    return before == screen();
}

void close_all()
{
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
}

/* ---- The Defragmenter's disk ------------------------------------------------------ */

void test_defrag_model()
{
    static shell::DefragModel disk, twin;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        disk.scramble(seed);
        twin.scramble(seed);
        // About 60 % full, in pieces; the same seed gives the same map.
        CHECK(disk.used() > shell::DefragModel::kCells / 2 && disk.used() <= shell::DefragModel::kCells * 6 / 10);
        CHECK(disk.file_count() > 10 && !disk.tidy() && !disk.done());
        int blocks[shell::DefragModel::kMaxFiles + 1] = {};
        bool same = true;
        for (int i = 0; i < shell::DefragModel::kCells; i++) {
            blocks[disk.file(i)]++;
            same = same && disk.file(i) == twin.file(i);
        }
        CHECK(same);
        // Stepping to the end: progress never goes back, no block is lost
        // or made up, at most a handful change per step, and it ends tidy.
        int steps = 0, last = 0;
        bool sane = true;
        while (disk.step()) {
            steps++;
            sane = sane && disk.placed() >= last && disk.changed_count() > 0 &&
                   disk.changed_count() <= shell::DefragModel::kMaxChanged && steps < 100000;
            last = disk.placed();
            if (!sane) {
                break;
            }
        }
        CHECK(sane);
        CHECK(disk.done() && disk.tidy() && disk.percent() == 100 && disk.current_file() == 0);
        CHECK(steps == static_cast<int>(disk.moves()) * 4);
        int after[shell::DefragModel::kMaxFiles + 1] = {};
        bool unmarked = true;
        for (int i = 0; i < shell::DefragModel::kCells; i++) {
            after[disk.file(i)]++;
            unmarked = unmarked && disk.mark(i) == shell::DefragModel::Mark::None;
        }
        CHECK(unmarked && memcmp(blocks, after, sizeof(blocks)) == 0);
        CHECK(!disk.step()); // and stays done
    }
    disk.scramble(1);
    twin.scramble(2);
    bool differ = false;
    for (int i = 0; i < shell::DefragModel::kCells; i++) {
        differ = differ || disk.file(i) != twin.file(i);
    }
    CHECK(differ);
}

void test_defragmenter_window()
{
    const std::string disk_before = [] {
        char buf[8192];
        size_t n = 0;
        storage.read_file("C:\\readme.txt", buf, sizeof(buf), n);
        return std::string(buf, n);
    }();
    ports::VolumeInfo info_before = {}, info_after = {};
    storage.volume('C')->info(info_before);

    shell::open_defragmenter();
    CHECK(strcmp(title(), "Defragmenter") == 0);
    const shell::DefragModel &disk = shell::defragmenter_disk();
    CHECK(disk.placed() == 0 && disk.moves() == 0);
    wait(10 * shell::kDefragStepUs);
    CHECK(disk.moves() == 0); // nothing moves until Start
    key(wm::key::Enter);      // Start
    for (int i = 0; i < 40; i++) {
        wait(shell::kDefragStepUs);
    }
    CHECK(disk.moves() == 10);
    CHECK(no_stale_pixels());
    // Pause (the same button) holds it; Resume goes on.
    letter('p', kAlt);
    const uint32_t held = disk.moves();
    wait(20 * shell::kDefragStepUs);
    CHECK(disk.moves() == held);
    letter('r', kAlt);
    for (int i = 0; i < 8; i++) {
        wait(shell::kDefragStepUs);
    }
    CHECK(disk.moves() == held + 2);
    // Closing pauses too, and keeps the map.
    key(wm::key::Escape);
    CHECK(find("Defragmenter") == nullptr);
    const uint32_t closed_at = disk.moves();
    wait(20 * shell::kDefragStepUs);
    CHECK(disk.moves() == closed_at);
    shell::open_defragmenter();
    letter('r', kAlt);
    for (int i = 0; i < 100000 && !disk.done(); i++) {
        wait(shell::kDefragStepUs);
    }
    wait(shell::kDefragStepUs);
    CHECK(disk.done() && disk.tidy());
    CHECK(no_stale_pixels());
    // Scramble makes a new mess; Start after the end does as well.
    letter('c', kAlt);
    CHECK(!disk.done() && disk.moves() == 0);
    key(wm::key::Escape);

    // The show never touched the drive.
    storage.volume('C')->info(info_after);
    CHECK(info_before.free_bytes == info_after.free_bytes);
    char buf[8192];
    size_t n = 0;
    storage.read_file("C:\\readme.txt", buf, sizeof(buf), n);
    CHECK(disk_before == std::string(buf, n));
}

/* ---- The Task Manager ------------------------------------------------------------- */

void test_task_manager()
{
    close_all();
    shell::launch(*shell::find_app("Welcome"));
    shell::open_in_editor("C:\\docs\\notes.txt");
    shell::launch(*shell::find_app("Widget gallery"));
    CHECK(desktop.window_count() == 3);

    // Ctrl+Alt+Del opens it, whatever has the focus, and the key does not
    // reach the program (the Editor would have deleted a character).
    desktop.activate(*find("notes.txt - Editor"));
    ctrl_alt_del();
    CHECK(strcmp(title(), "Task Manager") == 0);
    CHECK(find("notes.txt - Editor") != nullptr); // title without *: unchanged
    CHECK(desktop.window_count() == 4);
    ctrl_alt_del(); // again: still one Task Manager
    CHECK(desktop.window_count() == 4 && strcmp(title(), "Task Manager") == 0);
    CHECK(no_stale_pixels());

    // The list is in taskbar order: Welcome, Editor, Widget gallery. Enter
    // (or Switch To) brings the chosen one forward.
    key(wm::key::Home);
    key(wm::key::Down);
    key(wm::key::Enter);
    CHECK(strcmp(title(), "notes.txt - Editor") == 0);
    desktop.text("unsaved");
    CHECK(strcmp(title(), "notes.txt* - Editor") == 0);

    // End Task closes it at once, unsaved changes and all, and the Task
    // Manager stays in front. The list follows: the next End Task closes
    // what moved up into that row.
    ctrl_alt_del();
    letter('e', kAlt);
    CHECK(find("notes.txt* - Editor") == nullptr && desktop.window_count() == 3);
    CHECK(strcmp(title(), "Task Manager") == 0);
    char saved[4096];
    size_t n = 0;
    storage.read_file("C:\\docs\\notes.txt", saved, sizeof(saved), n);
    CHECK(std::string(saved, n).find("unsaved") == std::string::npos);
    CHECK(no_stale_pixels());

    // A program with a dialog open: End Task takes the dialog with it.
    shell::open_in_editor("C:\\docs\\notes.txt");
    letter('f', kAlt);
    letter('a'); // Save As
    CHECK(strcmp(title(), "Save As") == 0);
    ctrl_alt_del();
    CHECK(strcmp(title(), "Task Manager") == 0);
    key(wm::key::End); // Welcome, Widget gallery, Editor
    letter('e', kAlt);
    CHECK(find("Save As") == nullptr && find("notes.txt - Editor") == nullptr);
    CHECK(desktop.window_count() == 3);

    // A window opened behind its back shows up within a second.
    shell::launch(*shell::find_app("About Chinchilla"));
    ctrl_alt_del();
    wait(1000000);
    key(wm::key::End);
    letter('s', kAlt); // Switch To
    CHECK(strcmp(title(), "About Chinchilla") == 0);
    key(wm::key::Enter); // its OK

    // The figures follow the machine: less free memory repaints the window.
    ctrl_alt_del();
    wait(1000000);
    const std::string before = screen();
    system_.free_internal_ram = 100u << 10;
    wait(1000000);
    CHECK(before != screen());
    CHECK(no_stale_pixels());

    // Closed, it stops sampling: nothing is painted any more.
    key(wm::key::Escape);
    CHECK(find("Task Manager") == nullptr);
    const std::string closed = screen();
    system_.free_internal_ram = 311u << 10;
    wait(1000000);
    CHECK(closed == screen());

    // While Shut Down holds the desktop, Ctrl+Alt+Del does nothing.
    shell::shut_down_dialog();
    ctrl_alt_del();
    CHECK(strcmp(title(), "Shut Down Chinchilla") == 0 && find("Task Manager") == nullptr);
    key(wm::key::Escape);
}

// The desktop's share of the processor is time really spent: a clock that
// moves while the window manager works makes it count.
void test_busy_time()
{
    class MovingClock : public ports::Clock {
    public:
        uint32_t micros() override { return now += 50; }
        uint32_t now = 0;
    };
    static host::MemoryDisplay screen2(640, 480);
    static host::MemoryPointer pointer2;
    static MovingClock moving;
    static wm::Wm other(screen2, pointer2, moving);
    other.start();
    CHECK(other.busy_us() == 0);
    other.key(wm::key::A, true);
    const uint32_t after_key = other.busy_us();
    CHECK(after_key > 0 && after_key == other.last_event_us());
    other.mouse({true, 10, 10, 0, 0});
    const uint32_t after_mouse = other.busy_us();
    CHECK(after_mouse > after_key);
    other.tick();
    CHECK(other.busy_us() > after_mouse);
}

/* ---- Shut Down --------------------------------------------------------------------- */

void test_power_down()
{
    close_all();
    shell::launch(*shell::find_app("Welcome"));
    shell::open_in_editor("C:\\docs\\notes.txt");
    desktop.text("keep me?");

    // Unsaved changes call the shut-down off: the Editor's question shows.
    shell::shut_down_dialog();
    key(wm::key::Enter); // Shut Down is the default button
    CHECK(!shell::powered_down() && !desktop.taken_over());
    CHECK(strcmp(title(), "Editor") == 0);       // "Save changes?"
    CHECK(find("Shut Down Chinchilla") == nullptr);
    // While that question is open, Shut Down only brings it forward.
    CHECK(!shell::power_down());
    CHECK(strcmp(title(), "Editor") == 0);
    letter('n', kAlt); // No: the Editor closes without saving
    CHECK(desktop.window_count() == 0);

    // Now it goes through, by Alt+S this time.
    shell::launch(*shell::find_app("Welcome"));
    shell::shut_down_dialog();
    letter('s', kAlt);
    CHECK(shell::powered_down() && desktop.taken_over());
    CHECK(desktop.window_count() == 0);
    CHECK(!pointer.state().visible);
    const uint8_t *px = display.pixels();
    CHECK(px[479 * 640] == gfx::rgb(0, 0, 1) && px[479 * 640 + 639] == gfx::rgb(0, 0, 1)); // no taskbar
    int white = 0;
    for (int i = 0; i < 640 * 480; i++) {
        white += px[i] == gfx::kWhite;
    }
    CHECK(white > 5000); // the mascot and the sentence

    // The screen stays whatever happens: keys, text, the mouse, timers,
    // a repaint asked for by the debug link.
    const std::string shown = screen();
    key(wm::key::Enter);
    key(wm::key::Escape);
    key(wm::key::F4, kAlt);
    desktop.key(wm::key::LeftGui, true);
    desktop.key(wm::key::LeftGui, false);
    desktop.text("hello");
    desktop.mouse({true, 20, 470, 1, 0});
    desktop.mouse({true, 20, 470, 0, 0});
    wait(61000000);
    CHECK(shown == screen());
    CHECK(no_stale_pixels());
    CHECK(desktop.window_count() == 0 && !desktop.menu_open());
    CHECK(shell::power_down()); // already down

    // Ctrl+Alt+Del starts the system again.
    CHECK(!system_.reboot_requested);
    ctrl_alt_del();
    CHECK(system_.reboot_requested);
}

} // namespace

int main()
{
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    (void)kShift;
    test_defrag_model();
    test_defragmenter_window();
    test_task_manager();
    test_busy_time();
    test_power_down(); // last: there is no way back from it
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
