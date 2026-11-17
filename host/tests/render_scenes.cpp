// Renders the desktop through a scripted series of steps and writes each
// screen as a raw file (one RRRGGGBB byte per pixel, 640x480) into the folder
// given. tools/check_pictures.py compares them with the reference pictures
// in host/tests/pictures/.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/init/init.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/defrag.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
wm::Wm desktop(display, pointer, clock_);
std::string folder;
int failures = 0;

// `with_pointer`: the pointer mixed in, as the monitor shows it.
void save(const char *name, bool update = true, bool with_pointer = false)
{
    if (update) {
        desktop.update(); // paint what direct calls (not events) changed
    }
    std::vector<uint8_t> shown(display.pixels(), display.pixels() + 640 * 480);
    if (with_pointer) {
        video::compose_pointer(shown.data(), 0, 640 * 480, 640, pointer.state());
    }
    std::string path = folder + "/" + name + ".raw";
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr || fwrite(shown.data(), 1, 640 * 480, f) != 640 * 480) {
        printf("cannot write %s\n", path.c_str());
        failures++;
    }
    if (f != nullptr) {
        fclose(f);
    }
}

// Each event moves the clock on, so separate clicks are never a double click.
void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
}

void click(int x, int y)
{
    mouse(x, y, 0);
    mouse(x, y, 1);
    mouse(x, y, 0);
}

void double_click(int x, int y)
{
    mouse(x, y, 0);
    for (int i = 0; i < 2; i++) { // two quick clicks, 0.1 s apart
        clock_.now += 100000;
        desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 0});
        desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
    }
}

void key(uint16_t usage, bool alt = false)
{
    if (alt) {
        desktop.key(wm::key::LeftAlt, true);
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    if (alt) {
        desktop.key(wm::key::LeftAlt, false);
    }
}

wm::Window *find(const char *title)
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (std::string(desktop.window_at(z)->title()) == title) {
            return desktop.window_at(z);
        }
    }
    printf("no window called %s\n", title);
    exit(1);
}

// Lets time pass in small slices, as the UI loop ticks.
void wait(uint32_t us)
{
    for (uint32_t done = 0; done < us; done += 10000) {
        clock_.now += 10000;
        desktop.tick();
    }
}

// Plays Culebra towards the berry until `berries` have been eaten.
void play_culebra(int berries)
{
    using games::Snake;
    const Snake &g = shell::culebra_game();
    const uint16_t keys[4] = {wm::key::Up, wm::key::Right, wm::key::Down, wm::key::Left};
    for (int steps = 0; steps < 600 && g.berries() < berries; steps++) {
        const int dx = g.berry_col() - g.head_col(), dy = g.berry_row() - g.head_row();
        // Towards the berry along the row first, then the column; any
        // other way when those are blocked.
        const int order[4] = {dx > 0 ? 1 : 3, dy > 0 ? 2 : 0, dy > 0 ? 0 : 2, dx > 0 ? 3 : 1};
        const int first = dx != 0 ? 0 : 1;
        int want = order[first];
        for (int i = 0; i < 4; i++) {
            const int d = order[(first + i) & 3];
            const int col = g.head_col() + (d == 1) - (d == 3), row = g.head_row() + (d == 2) - (d == 0);
            if (col >= 0 && col < Snake::kCols && row >= 0 && row < Snake::kRows &&
                g.at(col, row).kind != Snake::Kind::Body) {
                want = d;
                break;
            }
        }
        key(keys[want]);
        wait(130000);
    }
    if (g.berries() < berries || g.state() != Snake::State::Playing) {
        printf("the Culebra scene did not play out\n");
        failures++;
    }
}

void centre_of(const gfx::Rect &r, int &x, int &y)
{
    x = r.x + r.w / 2;
    y = r.y + r.h / 2;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: render_scenes <folder>\n");
        return 2;
    }
    folder = argv[1];
    int x, y;

    static host::HostMemory memory;
    static fs::Storage storage;
    // The same size as on the board, so "KB free" matches too.
    static fs::MemoryVolume ram_disk(memory.allocate(2 << 20), 2 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    // The machine knows the time (17 November 2026, 14:05 in Spain), so the
    // sample files have a date.
    system_.utc = 1794920709;
    ram_disk.set_clock(&system_);
    fs::add_samples(storage, 'C');
    // The start-up screens (the clock stands still: the memory test takes
    // all 4 MB the PC adapter lends, and nothing waits).
    shell::BootIo boot{display, system_, memory, clock_, &storage, nullptr, nullptr, nullptr};
    if (shell::run_self_test(boot, false) != 0) {
        printf("self-test failed on the PC\n");
        failures++;
    }
    save("00a-boot-self-test", false);
    shell::show_splash(boot, false);
    save("00b-boot-splash", false);
    // The error screen at the start after a crash: a program that hung, and
    // one that used a bad pointer.
    {
        sys::CrashRecord r;
        sys::crash_begin(r, sys::CrashKind::Hang);
        r.pid = 7;
        r.uptime_ms = 754000;
        r.waited_ms = 15400;
        r.core = 1;
        sys::crash_text(r.program, sizeof(r.program), "Editor");
        sys::crash_text(r.task, sizeof(r.task), "ui");
        sys::crash_seal(r);
        boot.crash_hold_s = 15;
        gfx::RectList all;
        all.set({0, 0, 640, 480});
        gfx::Canvas c(display.pixels(), 640, 480, all, {0, 0, 640, 480});
        shell::draw_crash_screen(c, 640, 480, r, true, 15);
        save("00d-crash-hang", false);
        sys::crash_begin(r, sys::CrashKind::Panic);
        r.pid = 12;
        r.uptime_ms = 3723000;
        r.pc = 0x4200ABCD;
        r.trace_count = 8;
        for (int i = 0; i < 8; i++) {
            r.trace[i] = 0x42001000u + static_cast<uint32_t>(i) * 0x111;
        }
        sys::crash_text(r.program, sizeof(r.program), "Cactus Patch");
        sys::crash_text(r.task, sizeof(r.task), "ui");
        sys::crash_text(r.reason, sizeof(r.reason), "StoreProhibited");
        sys::crash_seal(r);
        shell::draw_crash_screen(c, 640, 480, r, true, 9);
        save("00e-crash-panic", false);
    }

    static init::Init first(proc::table()); // PID 1, as on the board; the desktop is its child
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    save("00c-desktop-at-start"); // no windows; welcome.txt on the desktop
    shell::open_demo_windows();
    desktop.set_animations(false); // the caret stays on: pictures do not depend on timing
    save("01-desktop");

    click(150, 200); // into the Welcome window: it comes to the front
    save("02-welcome-focused");

    // Drag the About window by the part of its title bar that shows: an
    // outline while moving, the window itself on release.
    mouse(520, 200, 0);
    mouse(520, 200, 1);
    mouse(480, 150, 1);
    mouse(430, 110, 1);
    save("03-dragging");
    mouse(430, 110, 0);
    save("04-dropped");

    // Maximise the Welcome window with its title button.
    wm::Window *welcome = find("Welcome to Chinchilla");
    gfx::Rect max = desktop.title_button(*welcome, wm::Part::Maximize);
    click(max.x + 5, max.y + 5);
    save("05-maximized");

    // Restore it, close About with Enter (its OK button), minimise Welcome.
    max = desktop.title_button(*welcome, wm::Part::Maximize);
    click(max.x + 5, max.y + 5);
    desktop.activate(*find("About Chinchilla"));
    key(wm::key::Enter);
    gfx::Rect min = desktop.title_button(*welcome, wm::Part::Minimize);
    click(min.x + 5, min.y + 5);
    save("06-about-closed-welcome-minimized");

    // Bring it back from the taskbar.
    gfx::Rect button = desktop.taskbar_button(0);
    click(button.x + 20, button.y + 10);
    save("07-restored-from-taskbar");

    // Start menu, with Programs open.
    centre_of(desktop.taskbar_button(-1), x, y);
    click(x, y);
    centre_of(desktop.popup_item(0, 0), x, y);
    mouse(x, y, 0);
    clock_.now += wm::Wm::kSubmenuDelayUs; // Programs opens after a short rest
    desktop.tick();
    save("08-start-menu-programs");

    // Launch the Widget gallery from Programs, type into its text field.
    // Found by name, so a new program in the table does not move it.
    int gallery = 0;
    while (std::string(shell::app(gallery).name) != "Widget gallery") {
        gallery++;
    }
    centre_of(desktop.popup_item(1, gallery), x, y);
    click(x, y);
    key(wm::key::End);
    desktop.text(" año");
    save("09-widget-gallery");

    // Its Options menu with the Size submenu, opened with the keyboard.
    key(0x12, true); // Alt+O
    key(wm::key::Down);
    key(wm::key::Right);
    save("10-gallery-menu");
    key(wm::key::Escape);
    key(wm::key::Escape);

    // Resize the gallery by its bottom right corner.
    const gfx::Rect wf = find("Widget gallery")->frame();
    mouse(wf.right() - 2, wf.bottom() - 2, 0);
    mouse(wf.right() - 2, wf.bottom() - 2, 1);
    mouse(wf.right() + 60, wf.bottom() + 40, 1);
    save("11-resizing");
    mouse(wf.right() + 60, wf.bottom() + 40, 0);
    save("12-resized");

    // A desktop icon: double-click Network for its message box.
    centre_of(desktop.desktop_icon_rect(2), x, y);
    double_click(x, y);
    save("13-icon-message-box");
    key(wm::key::Enter);

    // Shut Down dialog from the Start menu.
    centre_of(desktop.taskbar_button(-1), x, y);
    click(x, y);
    centre_of(desktop.popup_item(0, 4), x, y);
    click(x, y);
    save("14-shut-down");
    key(wm::key::Escape);

    // Close everything, then My Board from its icon: the drives.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    centre_of(desktop.desktop_icon_rect(0), x, y);
    double_click(x, y);
    save("15-my-board");

    // Into C:, then Documents (keyboard: select and Enter).
    key(wm::key::Home);
    key(wm::key::Enter);
    save("16-drive-c");
    wm::Window *fm = desktop.active();
    key(wm::key::Home);
    key(wm::key::Enter); // Documents is the first folder
    save("17-docs");

    // Open notes.txt with the keyboard: Desktop, español.txt, notes.txt, todo.txt.
    key(wm::key::Home);
    key(wm::key::Right);
    key(wm::key::Right);
    save("17b-file-chosen"); // the status bar: its size and when it was written
    key(wm::key::Enter);
    key(wm::key::End, false);
    desktop.text(" (añadido)");
    save("18-editor");

    // Save As dialog from the File menu (Alt+F, A).
    key(0x09, true);
    key(0x04);
    save("19-save-as");
    key(wm::key::Escape);

    // Closing with changes asks first.
    key(wm::key::F4, true);
    save("20-save-changes");
    key(wm::key::Escape); // Cancel

    // Rename in the folder window: F2, on español.txt.
    desktop.activate(*fm);
    key(wm::key::Home);
    key(wm::key::Right);
    key(0x3B); // F2
    save("21-rename");
    key(wm::key::Escape);

    // Delete asks first.
    key(wm::key::Delete);
    save("22-delete");
    key(wm::key::Escape);

    // Pictures, all from the keyboard so the board can replay it. First a
    // damaged copy of landscape.jpg (its first half), as tools put it there.
    static uint8_t bytes[16384];
    size_t size = 0;
    storage.read_file("C:\\pictures\\landscape.jpg", bytes, sizeof(bytes), size);
    storage.write_file("C:\\pictures\\broken.jpg", bytes, size / 2);

    // Up to C:, into pictures: blue marble.png, broken.jpg, earthrise.jpg,
    // landscape.jpg, mascot.bmp, test card.png.
    key(wm::key::Backspace);
    key(wm::key::Home);
    key(wm::key::Right); // Documents, pictures
    key(wm::key::Enter);
    save("23-pictures-folder");

    // Open landscape.jpg (the fourth) in the Picture viewer, decoded to the end.
    key(wm::key::Home);
    for (int i = 0; i < 3; i++) {
        key(wm::key::Right);
    }
    key(wm::key::Enter);
    shell::finish_picture();
    save("24-viewer-landscape");

    // The View menu (Alt+V) with Dithering open (D), then Bayer 4x4
    // and no dither from it.
    key(0x19, true);
    key(0x07); // D
    save("25-viewer-menu");
    key(0x21); // 4: Bayer 4x4
    shell::finish_picture();
    save("26-viewer-bayer4");
    key(0x19, true);
    key(0x07);
    key(0x11); // N: None
    shell::finish_picture();
    save("27-viewer-no-dither");

    // View > Options (O): the dialog, Blue noise picked from the list.
    key(0x19, true);
    key(0x12);
    key(wm::key::Home);
    for (int i = 0; i < 4; i++) {
        key(wm::key::Down);
    }
    save("27a-viewer-options");
    key(wm::key::Enter);
    shell::finish_picture();
    save("27b-viewer-blue-noise");
    key(0x19, true);
    key(0x07);
    key(0x09); // F: Floyd-Steinberg (the default) again
    shell::finish_picture();

    // Smaller window: fitted, then actual size with scroll bars.
    const gfx::Rect vf = find("landscape.jpg - Picture viewer")->frame();
    mouse(vf.right() - 2, vf.bottom() - 2, 0);
    mouse(vf.right() - 2, vf.bottom() - 2, 1);
    mouse(vf.right() - 162, vf.bottom() - 122, 1);
    mouse(vf.right() - 162, vf.bottom() - 122, 0);
    shell::finish_picture();
    save("28-viewer-fitted-small");
    key(0x19, true);
    key(0x04); // A: Actual size
    shell::finish_picture();
    save("29-viewer-actual-size");

    // File > Open (Alt+F, O): the dialog, then the mascot (4-bit BMP).
    key(0x09, true);
    key(0x12);
    save("30-viewer-open-dialog");
    desktop.text("mascot.bmp");
    key(wm::key::Enter);
    shell::finish_picture();
    save("31-viewer-mascot");

    // Back to fitting, and the test card (PNG).
    key(0x19, true);
    key(0x09); // F: Fit to window
    key(0x09, true);
    key(0x12);
    desktop.text("test card.png");
    key(wm::key::Enter);
    shell::finish_picture();
    save("32-viewer-test-card");

    // The damaged picture: what was decoded shows, and an error box.
    key(0x09, true);
    key(0x12);
    desktop.text("broken.jpg");
    key(wm::key::Enter);
    shell::finish_picture();
    save("33-viewer-damaged");
    key(wm::key::Enter);

    // Ctrl+Alt+Del: the Task Manager, with the second program picked. The
    // clock moves on a second so the figures are a real sample.
    desktop.key(wm::key::LeftCtrl, true);
    desktop.key(wm::key::LeftAlt, true);
    desktop.key(wm::key::Delete, true);
    desktop.key(wm::key::Delete, false);
    desktop.key(wm::key::LeftAlt, false);
    desktop.key(wm::key::LeftCtrl, false);
    clock_.now += 1000000;
    desktop.tick();
    key(wm::key::Home);
    key(wm::key::Down);
    save("34-task-manager");
    // "Show all processes": the whole table, system processes included.
    key(wm::key::A, true);
    save("34b-task-manager-all");
    // A protected process picked (the desktop): End Task is greyed out.
    key(wm::key::A + 15, true); // Alt+P: the list
    key(wm::key::Home);
    save("34c-task-manager-protected");
    key(wm::key::A, true);
    key(wm::key::Escape);

    // Culebra, the snake game: a new game, a few berries in, its menu,
    // paused, and the end against a wall.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    shell::launch(*shell::find_app("Culebra"));
    save("39-culebra-new-game");
    play_culebra(8);
    save("40-culebra-playing");
    key(0x0A, true); // Alt+G
    key(0x16);       // S: Speed
    save("41-culebra-menu");
    key(wm::key::Escape);
    key(wm::key::Escape);
    wait(130000); // the step that was due finds the game was left: paused
    save("42-culebra-paused");
    key(wm::key::Space);
    key(wm::key::Up);
    wait(130000 * 20);
    save("43-culebra-game-over");
    key(wm::key::F4, true);

    // The Terminal: as it opens, after a few commands, and the list of
    // commands.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    shell::launch(*shell::find_app("Terminal"));
    save("44-terminal");
    desktop.text("dir");
    key(wm::key::Enter);
    desktop.text("cd Documents");
    key(wm::key::Enter);
    desktop.text("type notes.txt");
    key(wm::key::Enter);
    desktop.text("ps");
    key(wm::key::Enter);
    desktop.text("echo \xC2\xA1hola, se\xC3\xB1or!");
    save("45-terminal-commands");
    key(wm::key::Enter);
    desktop.text("help");
    key(wm::key::Enter);
    save("46-terminal-help");
    key(wm::key::F4, true);

    // Start > Run...: a program of its own, with a name typed.
    shell::run_dialog();
    desktop.text("file manager");
    save("47-run");
    key(wm::key::Escape);

    // The Defragmenter: its made-up disk, part of the way, and tidy.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    shell::open_defragmenter();
    save("35-defragmenter");
    key(wm::key::Enter); // Start
    for (int i = 0; i < 601; i++) {
        clock_.now += shell::kDefragStepUs;
        desktop.tick();
    }
    save("36-defragmenter-running");
    for (int i = 0; i < 20000 && !shell::defragmenter_disk().done(); i++) {
        clock_.now += shell::kDefragStepUs;
        desktop.tick();
    }
    clock_.now += shell::kDefragStepUs;
    desktop.tick();
    save("37-defragmenter-done");

    // The screensaver. Its dialog from Start > Settings (its last entry), then a preview of each animation: 150 frames, five seconds.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    centre_of(desktop.taskbar_button(-1), x, y);
    click(x, y);
    centre_of(desktop.popup_item(0, 1), x, y);
    mouse(x, y, 0);
    clock_.now += wm::Wm::kSubmenuDelayUs;
    desktop.tick();
    save("37a-settings-menu");
    centre_of(desktop.popup_item(1, 3), x, y); // Screen Saver...
    click(x, y);
    save("37b-screen-saver-dialog");
    const char *const savers[3] = {"37c-saver-starfield", "37d-saver-chinchillas", "37e-saver-curves"};
    for (int i = 0; i < 3; i++) {
        key(wm::key::Home); // the list: (None), then the three
        for (int n = 0; n <= i; n++) {
            key(wm::key::Down);
        }
        clock_.now += 1000000;          // the clock is the seed of the run
        key(0x13, true);                // Alt+P: Preview
        for (int frame = 0; frame < 150; frame++) {
            desktop.tick();
            clock_.now += 33334;
        }
        save(savers[i], false);
        key(wm::key::Escape); // wakes the screen; the dialog stays
    }
    save("37f-after-screen-saver");
    key(wm::key::Escape); // closes the dialog

    // The desktop's background. A right click on the desktop gives a menu
    // with Display...; there a colour and a pattern, shown by Apply, taken
    // back by Escape, kept by OK.
    {
        const auto check = [&](bool ok, const char *what) {
            if (!ok) {
                printf("Display: %s\n", what);
                failures++;
            }
        };
        mouse(400, 300, 0);
        mouse(400, 300, 2);
        mouse(400, 300, 0);
        check(desktop.menu_open(), "a right click on the desktop opens its menu");
        save("37p-desktop-menu");
        centre_of(desktop.popup_item(0, 0), x, y);
        click(x, y);
        check(desktop.active() != nullptr && strcmp(desktop.active()->title(), "Display") == 0, "the dialog opens");
        save("37q-display-dialog");
        key(wm::key::Right);            // Teal
        key(0x13, true);                // Alt+P: the patterns
        for (int i = 0; i < 4; i++) {
            key(wm::key::Right);        // Bricks
        }
        save("37r-display-teal-bricks");
        check(desktop.backdrop().colour.r == 58, "nothing changes before Apply");
        key(0x04, true);                // Alt+A: Apply
        check(desktop.backdrop().colour.g == 128 && desktop.backdrop().pattern[0] == 0xFF, "Apply shows it");
        save("37s-display-applied");
        key(wm::key::Escape);
        check(desktop.backdrop().colour.r == 58 && desktop.backdrop().pattern[0] == 0, "Escape takes it back");
        // Kept by OK, in desktop.ini.
        shell::display_dialog();
        for (int i = 0; i < 9; i++) {
            key(wm::key::Right);        // Sand
        }
        key(0x13, true);
        key(wm::key::Down);
        key(wm::key::Right);            // Diamonds
        key(wm::key::Enter);
        check(desktop.backdrop().colour.r == 208 && desktop.backdrop().pattern[0] == 0x10, "OK keeps it");
        fs::Ini ini;
        char value[32] = "";
        check(shell::load_settings("desktop", ini) && ini.get("background", "colour", value, sizeof(value)) &&
                  strcmp(value, "sand") == 0,
              "the colour is in desktop.ini");
        check(ini.get("background", "pattern", value, sizeof(value)) && strcmp(value, "diamonds") == 0,
              "the pattern is in desktop.ini");
        save("37t-desktop-sand-diamonds");
        // Back to how it was, for the pictures that follow.
        shell::display_dialog();
        for (int i = 0; i < 9; i++) {
            key(wm::key::Left);
        }
        key(0x13, true);
        key(wm::key::Up);
        key(wm::key::Left);
        key(wm::key::Enter);
        check(desktop.backdrop().colour.r == 58 && desktop.backdrop().pattern[0] == 0, "and back");
    }

    // The Trash. Empty at first, from its desktop icon.
    const auto there = [&](const char *path) {
        ports::DirEntry e;
        return storage.stat(path, e) == ports::FsError::Ok;
    };
    const auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            printf("Trash: %s\n", what);
            failures++;
        }
    };
    centre_of(desktop.desktop_icon_rect(3), x, y);
    double_click(x, y);
    save("37g-trash-empty");
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    // Delete in the File manager asks, then moves the file to the Trash,
    // whose icon fills.
    shell::open_folder("C:\\Documents");
    key(wm::key::Home); // Desktop, then espa\xc3\xb1ol.txt
    key(wm::key::Right);
    key(wm::key::Delete);
    save("37h-move-to-trash");
    key(wm::key::Enter); // Yes
    expect(!there("C:\\Documents\\espa\xc3\xb1ol.txt") && there("C:\\.trash\\espa\xc3\xb1ol.txt"), "Delete did not move the file");
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    centre_of(desktop.desktop_icon_rect(3), x, y);
    double_click(x, y);
    key(wm::key::Home);
    save("37i-trash-full"); // with where it came from
    // File > Restore puts it back.
    key(0x09, true); // Alt+F
    key(0x15);       // R
    expect(there("C:\\Documents\\espa\xc3\xb1ol.txt") && !there("C:\\.trash\\espa\xc3\xb1ol.txt"), "Restore did not put it back");
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    // A desktop file dragged onto the Trash: an outline follows the pointer
    // and the Trash is highlighted; let go, the file is in the Trash.
    int tx, ty;
    centre_of(desktop.desktop_icon_rect(4), x, y); // welcome.txt
    centre_of(desktop.desktop_icon_rect(3), tx, ty);
    mouse(x, y, 0);
    mouse(x, y, 1);
    mouse(x + 10, y - 10, 1);
    mouse(tx, ty, 1);
    save("37j-dragging-to-trash");
    mouse(tx, ty, 0);
    save("37k-dropped-in-trash");
    expect(!there("C:\\Documents\\Desktop\\welcome.txt") && there("C:\\.trash\\welcome.txt"), "the drop did not trash welcome.txt");
    // In the File manager: readme.txt dragged onto the Documents folder moves there.
    shell::open_folder("C:\\");
    {
        wm::Window *w = desktop.active();
        const gfx::Rect c = w->client();
        // Documents, pictures, system, readme.txt.
        const int fx = c.x + 2 + 10 + 38, fy = c.y + 64 + 2 + 8 + 16;
        mouse(fx + 3 * 76, fy, 0);
        mouse(fx + 3 * 76, fy, 1);
        mouse(fx + 3 * 76 - 10, fy + 10, 1);
        mouse(fx, fy, 1);
        save("37l-dragging-to-folder");
        mouse(fx, fy, 0);
        expect(!there("C:\\readme.txt") && there("C:\\Documents\\readme.txt"), "the drop did not move readme.txt");
    }
    // Into Documents: Desktop, espa\xc3\xb1ol.txt, notes.txt, readme.txt, todo.txt.
    // readme.txt dragged out with Escape pressed on the way: nothing moves;
    // dragged onto the empty desktop: it goes into C:\Documents\Desktop.
    shell::open_folder("C:\\Documents");
    for (int pass = 0; pass < 2; pass++) {
        wm::Window *w = desktop.active();
        const gfx::Rect c = w->client();
        const int fx = c.x + 2 + 10 + 38 + 3 * 76, fy = c.y + 64 + 2 + 8 + 16;
        mouse(fx, fy, 0);
        mouse(fx, fy, 1);
        mouse(fx + 10, fy + 10, 1);
        mouse(560, 420, 1);
        if (pass == 0) {
            key(wm::key::Escape);
        }
        mouse(560, 420, 0);
        if (pass == 0) {
            expect(there("C:\\Documents\\readme.txt"), "Escape did not cancel the drag");
        } else {
            expect(!there("C:\\Documents\\readme.txt") && there("C:\\Documents\\Desktop\\readme.txt"),
                   "the drop on the desktop did not move readme.txt");
        }
        desktop.activate(*w);
    }
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    // Empty Trash asks first, saying how many.
    centre_of(desktop.desktop_icon_rect(3), x, y);
    double_click(x, y);
    {
        // The Empty Trash button in the toolbar (File > Empty Trash does the same).
        const gfx::Rect c = desktop.active()->client();
        click(c.x + 4 + 36, c.y + 17);
    }
    save("37m-empty-trash");
    key(wm::key::Enter); // Yes
    expect(!there("C:\\.trash\\welcome.txt"), "Empty Trash left welcome.txt");
    save("37n-trash-emptied");
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    // The same name deleted twice: the second is numbered in the Trash.
    shell::open_folder("C:\\Documents");
    for (int i = 0; i < 2; i++) {
        storage.write_file("C:\\Documents\\a.txt", "a", 1);
        key(0x3E); // F5
        key(wm::key::Home); // Desktop, then a.txt
        key(wm::key::Right);
        key(wm::key::Delete);
        key(wm::key::Enter); // Yes
    }
    expect(there("C:\\.trash\\a.txt") && there("C:\\.trash\\a (2).txt"), "the second a.txt was not numbered");
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }

    // The Editor in the middle of a save: nothing is painted meanwhile, but
    // the pointer, mixed in on the way to the monitor, is the hourglass.
    shell::open_in_editor("C:\\Documents\\notes.txt");
    mouse(330, 250, 0);
    {
        const wm::Wait saving(desktop);
        save("39-editor-saving-hourglass", true, true);
    }
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }

    // End Task on a program with unsaved work sends TERM, its own close
    // button: the Editor asks. Left unanswered, with the Task Manager back
    // in front after the few seconds, force is offered; End Now is KILL.
    {
        // (Here at the end: these scenes move the clock on by seconds.)
        const int windows = desktop.window_count();
        shell::open_in_editor("C:\\Documents\\notes.txt");
        desktop.text("not saved yet");
        desktop.key(wm::key::LeftCtrl, true);
        desktop.key(wm::key::LeftAlt, true);
        desktop.key(wm::key::Delete, true);
        desktop.key(wm::key::Delete, false);
        desktop.key(wm::key::LeftAlt, false);
        desktop.key(wm::key::LeftCtrl, false);
        clock_.now += 10000;
        desktop.tick();
        key(wm::key::End);
        key(wm::key::A + 4, true); // Alt+E
        save("34d-end-task-asks");
        desktop.key(wm::key::LeftCtrl, true);
        desktop.key(wm::key::LeftAlt, true);
        desktop.key(wm::key::Delete, true);
        desktop.key(wm::key::Delete, false);
        desktop.key(wm::key::LeftAlt, false);
        desktop.key(wm::key::LeftCtrl, false);
        clock_.now += 6000000;
        desktop.tick();
        save("34e-end-task-force");
        key(wm::key::Enter); // End Now
        clock_.now += 10000;
        desktop.tick();
        if (desktop.window_count() != windows + 1) { // the Task Manager stays
            printf("End Now left the Editor open\n");
            failures++;
        }
    }
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }

    // Shut Down from the Start menu: Enter picks Shut Down, and the screen
    // says the power can go.
    centre_of(desktop.taskbar_button(-1), x, y);
    click(x, y);
    centre_of(desktop.popup_item(0, 4), x, y);
    click(x, y);
    key(wm::key::Enter);
    if (!shell::powered_down()) {
        printf("Shut Down did not reach the power-down screen\n");
        failures++;
    }
    save("38-power-down");

    return failures == 0 ? 0 : 1;
}
