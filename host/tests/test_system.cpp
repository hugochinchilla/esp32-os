// The system programs: the Task Manager on Ctrl+Alt+Del, Shut Down with its
// power-down screen, and the Defragmenter's made-up disk.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "../../programs/defragmenter/defrag.hpp"
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

    shell::start_program("Defragmenter");
    CHECK(strcmp(title(), "Defragmenter") == 0);
    // The window, and the pretend disk in it, exist only while it is open.
    const auto disk = []() -> const shell::DefragModel & { return shell::defragmenter_disk(); };
    CHECK(disk().placed() == 0 && disk().moves() == 0);
    wait(10 * shell::kDefragStepUs);
    CHECK(disk().moves() == 0); // nothing moves until Start
    key(wm::key::Enter);      // Start
    for (int i = 0; i < 40; i++) {
        wait(shell::kDefragStepUs);
    }
    CHECK(disk().moves() == 10);
    CHECK(no_stale_pixels());
    // Pause (the same button) holds it; Resume goes on.
    letter('p', kAlt);
    const uint32_t held = disk().moves();
    wait(20 * shell::kDefragStepUs);
    CHECK(disk().moves() == held);
    letter('r', kAlt);
    for (int i = 0; i < 8; i++) {
        wait(shell::kDefragStepUs);
    }
    CHECK(disk().moves() == held + 2);
    // Closing ends the program: its memory goes back, and nothing runs.
    const int blocks = memory.in_use();
    key(wm::key::Escape);
    CHECK(find("Defragmenter") == nullptr);
    CHECK(memory.in_use() == blocks - 11); // the window, its nine controls and its arguments
    CHECK(proc::table().find_by_name("Defragmenter") == proc::kNone);
    wait(20 * shell::kDefragStepUs);
    CHECK(memory.in_use() == blocks - 11);
    // Opened again it starts from the scrambled disk.
    shell::start_program("Defragmenter");
    CHECK(disk().moves() == 0 && !disk().done());
    key(wm::key::Enter); // Start
    for (int i = 0; i < 100000 && !disk().done(); i++) {
        wait(shell::kDefragStepUs);
    }
    wait(shell::kDefragStepUs);
    CHECK(disk().done() && disk().tidy());
    CHECK(no_stale_pixels());
    // Scramble makes a new mess; Start after the end does as well.
    letter('c', kAlt);
    CHECK(!disk().done() && disk().moves() == 0);
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
    shell::start_program("Welcome");
    shell::open_in_editor("C:\\Documents\\notes.txt");
    shell::start_program("Widget gallery");
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

    // End Task asks first (TERM): the Editor, with unsaved changes, puts
    // its question up in front and stays. Nothing was saved or lost.
    ctrl_alt_del();
    key(wm::key::Home);
    key(wm::key::Down); // Welcome, Editor, Widget gallery
    letter('e', kAlt);
    CHECK(find("notes.txt* - Editor") != nullptr && strcmp(title(), "Editor") == 0);
    CHECK(desktop.window_count() == 5 && no_stale_pixels());
    // Cancel: the Editor goes on. Force is not offered behind the user's
    // back, however long it takes: only once the Task Manager is in front.
    key(wm::key::Escape);
    CHECK(strcmp(title(), "notes.txt* - Editor") == 0);
    wait(7000000);
    CHECK(strcmp(title(), "notes.txt* - Editor") == 0 && desktop.window_count() == 4);
    ctrl_alt_del();
    CHECK(strcmp(title(), "End Task") == 0 && desktop.window_count() == 5);
    CHECK(no_stale_pixels());
    // Cancel there too: the program stays, and the offer is not repeated.
    key(wm::key::Escape);
    wait(7000000);
    CHECK(strcmp(title(), "Task Manager") == 0 && find("notes.txt* - Editor") != nullptr);
    // End Task once more sends TERM again, and the Editor asks again. This
    // time the question is left open and the Task Manager brought back:
    // before the few seconds are up nothing is offered, after them it is.
    // (There is one message box, so the offer takes the question's place.)
    letter('e', kAlt);
    CHECK(strcmp(title(), "Editor") == 0);
    ctrl_alt_del();
    wait(3000000);
    CHECK(strcmp(title(), "Task Manager") == 0);
    wait(3000000);
    CHECK(strcmp(title(), "End Task") == 0);
    // End Now (KILL): the Editor goes, unsaved changes and all, and the
    // Task Manager is in front with the list up to date.
    key(wm::key::Enter);
    wait(10000);
    CHECK(find("notes.txt* - Editor") == nullptr && desktop.window_count() == 3);
    CHECK(strcmp(title(), "Task Manager") == 0);
    char saved[4096];
    size_t n = 0;
    storage.read_file("C:\\Documents\\notes.txt", saved, sizeof(saved), n);
    CHECK(std::string(saved, n).find("unsaved") == std::string::npos);
    CHECK(no_stale_pixels());
    wait(7000000);
    CHECK(strcmp(title(), "Task Manager") == 0 && desktop.window_count() == 3); // nothing left to offer

    // A program with a dialog open: End Task brings the dialog forward, and
    // the program stays. End Task on it a second time offers force at once;
    // End Now takes the dialog with the program.
    shell::open_in_editor("C:\\Documents\\notes.txt");
    letter('f', kAlt);
    letter('a'); // Save As
    CHECK(strcmp(title(), "Save As") == 0);
    ctrl_alt_del();
    CHECK(strcmp(title(), "Task Manager") == 0);
    key(wm::key::End); // Welcome, Widget gallery, Editor
    letter('e', kAlt);
    CHECK(strcmp(title(), "Save As") == 0 && find("notes.txt - Editor") != nullptr);
    ctrl_alt_del();
    letter('e', kAlt);
    CHECK(strcmp(title(), "End Task") == 0);
    letter('e', kAlt); // End Now, by its letter
    wait(10000);
    CHECK(find("Save As") == nullptr && find("notes.txt - Editor") == nullptr);
    CHECK(desktop.window_count() == 3 && strcmp(title(), "Task Manager") == 0);
    CHECK(no_stale_pixels());

    // A program with nothing to ask closes at once, as its close button
    // would make it, and force is never offered.
    key(wm::key::End); // Welcome, Widget gallery
    letter('e', kAlt);
    CHECK(find("Widget gallery") == nullptr && desktop.window_count() == 2);
    wait(7000000);
    CHECK(strcmp(title(), "Task Manager") == 0 && desktop.window_count() == 2);
    shell::start_program("Widget gallery");

    // A window opened behind its back shows up within a second.
    shell::start_program("About Chinchilla");
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

// The fade, a step at a time, for as long as `stage` says.
template <class F> int steps_while(F stage)
{
    int steps = 0;
    while (stage() && steps < 100) {
        wait(60000);
        steps++;
    }
    return steps;
}

bool plain_desktop()
{
    // The plain desktop repeats every 8 pixels both ways.
    const uint8_t *px = display.pixels();
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            if (px[y * 640 + x] != px[(y & 7) * 640 + (x & 7)]) {
                return false;
            }
        }
    }
    return true;
}

int pixels_of(gfx::Colour c)
{
    const uint8_t *px = display.pixels();
    int n = 0;
    for (int i = 0; i < 640 * 480; i++) {
        n += px[i] == c;
    }
    return n;
}

void test_power_down()
{
    close_all();
    shell::start_program("Welcome");
    shell::open_in_editor("C:\\Documents\\notes.txt");
    desktop.text("keep me?");

    // Shut Down asks every program to close. Welcome goes; the Editor has
    // unsaved changes and asks, and Shut Down waits for the answer.
    shell::shut_down_dialog();
    key(wm::key::Enter); // Shut Down is the default button
    wait(1000);
    CHECK(shell::shutting_down() && !shell::powered_down() && !desktop.taken_over());
    CHECK(strcmp(title(), "Editor") == 0); // "Save changes?"
    CHECK(find("Shut Down Chinchilla") == nullptr && find("Welcome") == nullptr);
    CHECK(desktop.window_count() == 2);
    // It waits, and choosing Shut Down again changes nothing.
    wait(3000000);
    CHECK(shell::power_down() && shell::shutting_down() && desktop.window_count() == 2);
    CHECK(strcmp(title(), "Editor") == 0);
    // Cancel: the Editor stays, and Shut Down is off.
    key(wm::key::Escape);
    wait(100000);
    CHECK(!shell::shutting_down() && !shell::powered_down() && !desktop.taken_over());
    CHECK(desktop.window_count() == 1 && find("notes.txt* - Editor") != nullptr);
    wait(30000000); // and stays off: nothing is ended later
    CHECK(desktop.window_count() == 1 && !desktop.taken_over());

    // Again, by Alt+S this time, and No to the question: the Editor closes
    // without saving and the rest follows by itself.
    shell::start_program("Welcome");
    shell::shut_down_dialog();
    letter('s', kAlt);
    wait(1000);
    CHECK(shell::shutting_down() && strcmp(title(), "Editor") == 0);
    CHECK(pointer.state().visible);
    letter('n', kAlt);
    CHECK(desktop.window_count() == 0 && !desktop.taken_over());
    const int taskbar_grey = pixels_of(gfx::kGrey);
    CHECK(taskbar_grey > 10000 && !plain_desktop());
    wait(100000);
    // The picture is kept and fades: the icons and the taskbar into the
    // plain desktop, in seven steps at most.
    CHECK(desktop.taken_over() && shell::shutting_down() && !shell::powered_down());
    CHECK(desktop.window_count() == 0);
    CHECK(!pointer.state().visible);
    CHECK(pixels_of(gfx::kGrey) == taskbar_grey); // nothing has moved yet
    wait(60000);
    CHECK(pixels_of(gfx::kGrey) == 0 && !plain_desktop()); // one step on
    // Ctrl+Alt+Del and the rest of the keyboard do nothing meanwhile.
    ctrl_alt_del();
    key(wm::key::Enter);
    CHECK(desktop.window_count() == 0 && !system_.reboot_requested);
    const int to_plain = steps_while([] { return !plain_desktop(); });
    CHECK(to_plain >= 3 && to_plain <= 6);
    CHECK(pixels_of(gfx::kBlack) == 0);
    // It rests there a moment, then goes to black.
    const std::string plain = screen();
    wait(60000);
    wait(60000);
    CHECK(plain == screen());
    const int to_black = steps_while([] { return pixels_of(gfx::kBlack) != 640 * 480; });
    CHECK(to_black >= 5 && to_black <= 12); // the rest, then as many steps as the desktop's colour is from black
    CHECK(shell::shutting_down());
    // Out of the black comes the last screen, in seven steps again.
    int last = 0, rising = 0;
    while (!shell::powered_down() && rising < 20) {
        wait(60000);
        rising++;
        const int lit = 640 * 480 - pixels_of(gfx::kBlack);
        CHECK(lit >= last);
        last = lit;
    }
    CHECK(rising >= 7 && rising <= 10); // the steps left of the fade to black, and its own seven
    CHECK(shell::powered_down() && !shell::shutting_down() && desktop.taken_over());
    wait(60000);
    const uint8_t *px = display.pixels();
    CHECK(px[0] == gfx::kBlack && px[479 * 640] == gfx::kBlack && px[479 * 640 + 639] == gfx::kBlack); // full black, no taskbar
    CHECK(pixels_of(gfx::kWhite) > 5000); // the mascot and the sentence
    CHECK(pixels_of(gfx::rgb(0, 0, 1)) == 0); // the night sky's blue is gone

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
