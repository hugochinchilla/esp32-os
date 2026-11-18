// Painting writes each screen pixel at most once per update: with one screen
// buffer, a pixel painted twice (background, then text or icon on top) can
// be scanned out in between and flash. Counted over the whole shell for the
// things people do most: activating a window, moving one, opening menus,
// taskbar changes, dragging a file icon.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/gfx/canvas.hpp"
#include "core/desk/desk.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
static bool verbose = false;

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

struct Writes {
    std::vector<uint8_t> counts = std::vector<uint8_t>(640 * 480, 0);
    Writes() { gfx::Canvas::count_writes(counts.data()); }
    ~Writes() { gfx::Canvas::count_writes(nullptr); }
    void reset() { std::fill(counts.begin(), counts.end(), 0); }
    // Pixels written more than once, and where.
    int twice(gfx::Rect &where) const
    {
        int n = 0;
        where = {};
        for (int i = 0; i < 640 * 480; i++) {
            if (counts[i] > 1) {
                n++;
                where = gfx::bounding(where, {i % 640, i / 640, 1, 1});
            }
        }
        return n;
    }
} *writes;

// One step, then the check that nothing was painted twice.
void expect_once(const char *what)
{
    desktop.update();
    gfx::Rect r;
    const int n = writes->twice(r);
    if (n != 0) {
        printf("FAILED %s: %d pixels written twice, within x %d..%d, y %d..%d\n", what, n, r.x, r.right() - 1, r.y,
               r.bottom() - 1);
        failures++;
        if (const char *dir = getenv("PAINT_MAPS")) {
            // The screen, with pixels written twice in red, for looking at.
            char path[512];
            snprintf(path, sizeof(path), "%s/%02d.ppm", dir, failures);
            if (FILE *out = fopen(path, "wb")) {
                fprintf(out, "P6 640 480 255\n");
                for (int i = 0; i < 640 * 480; i++) {
                    const uint8_t v = display.pixels()[i];
                    const uint8_t grey = static_cast<uint8_t>(((v >> 5) * 36 + ((v >> 2) & 7) * 36 + (v & 3) * 85) / 3);
                    const uint8_t px[3] = {writes->counts[i] > 1 ? uint8_t(255) : grey,
                                           writes->counts[i] > 1 ? uint8_t(0) : grey,
                                           writes->counts[i] > 1 ? uint8_t(0) : grey};
                    fwrite(px, 1, 3, out);
                }
                fclose(out);
            }
        }
    } else if (verbose) {
        printf("ok %s\n", what);
    }
    writes->reset();
}

void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
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
    clock_.now += 100000;
}

void centre(const gfx::Rect &r, int &x, int &y)
{
    x = r.x + r.w / 2;
    y = r.y + r.h / 2;
}

} // namespace

int main(int argc, char **)
{
    verbose = argc > 1;
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    shell::open_demo_windows();
    shell::open_in_editor("C:\\Documents\\notes.txt");
    shell::open_folder("C:\\Documents");
    desktop.update();
    Writes w;
    writes = &w;
    int x, y;

    desktop.repaint_all();
    expect_once("a full repaint");

    // Activating a window behind others: a click on its title.
    wm::Window *bottom = desktop.window_at(0);
    const gfx::Rect f = bottom->frame();
    mouse(f.x + 30, f.y + 8, 0);
    mouse(f.x + 30, f.y + 8, 1);
    mouse(f.x + 30, f.y + 8, 0);
    expect_once("activating a window");

    // Moving it: the outline while dragging, then the drop.
    mouse(f.x + 30, f.y + 8, 1);
    expect_once("pressing on a title");
    mouse(f.x + 90, f.y + 60, 1);
    expect_once("dragging a window outline");
    mouse(f.x + 90, f.y + 60, 0);
    expect_once("dropping a window");

    // Menus: a menu bar menu, moving through it, closing it.
    key(0x09, true); // Alt+F on the active window
    expect_once("opening a menu");
    key(wm::key::Down);
    expect_once("moving through a menu");
    key(wm::key::Escape);
    expect_once("closing a menu");

    // The Start menu and its Programs submenu.
    centre(shell::taskbar()->button(-1), x, y);
    mouse(x, y, 1);
    mouse(x, y, 0);
    expect_once("opening the Start menu");
    centre(desktop.popup_item(0, 0), x, y);
    mouse(x, y, 0);
    clock_.now += wm::Wm::kSubmenuDelayUs;
    desktop.tick();
    expect_once("opening a submenu");
    key(wm::key::Escape);
    key(wm::key::Escape);
    expect_once("closing the Start menu");

    // The taskbar: minimising and restoring with its button.
    wm::Window *top = desktop.active();
    desktop.minimize(*top);
    expect_once("minimising a window");
    centre(shell::taskbar()->button(0), x, y);
    mouse(x, y, 1);
    expect_once("pressing a taskbar button");
    mouse(x, y, 0);
    expect_once("restoring from the taskbar");

    // The clock changing.
    clock_.now += 61000000;
    desktop.tick();
    expect_once("the taskbar clock");

    // Selecting a desktop icon, and dragging a file icon across windows.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    shell::open_in_editor("C:\\Documents\\notes.txt");
    desktop.update();
    writes->reset();
    centre(shell::desktop_icons()->icon_rect(4), x, y); // welcome.txt
    mouse(x, y, 0);
    mouse(x, y, 1);
    expect_once("selecting a desktop icon");
    mouse(x + 20, y, 1);
    expect_once("starting a file drag");
    for (int step = 1; step <= 10; step++) {
        mouse(x + step * 40, y - step * 20, 1);
        expect_once("dragging a file over windows");
    }
    key(wm::key::Escape);
    mouse(x + 400, y - 200, 0);
    expect_once("letting go of a file");

    // Every program, and the dialogs: opening each, and a full repaint.
    const auto check_window = [&](const char *name) {
        char what[96];
        snprintf(what, sizeof(what), "opening %s", name);
        expect_once(what);
        desktop.repaint_all();
        snprintf(what, sizeof(what), "repainting %s", name);
        expect_once(what);
        while (desktop.active() != nullptr) {
            desktop.close(*desktop.active());
        }
        desktop.update();
        writes->reset();
    };
    for (int i = 0; i < shell::app_count(); i++) {
        shell::launch(shell::app(i));
        check_window(shell::app(i).name);
    }
    shell::open_folder(nullptr);
    check_window("My Board");
    shell::open_picture("C:\\pictures\\mascot.bmp");
    shell::finish_picture();
    check_window("a picture");
    shell::message_box("Message", "A message box with an icon\nand two lines.");
    check_window("a message box");
    static const char *const yes_no[] = {"&Yes", "&No"};
    shell::ask("Question", "Are you sure?", yes_no, 2, [](int, void *) {}, nullptr);
    check_window("a question");
    shell::open_in_editor("C:\\Documents\\notes.txt");
    shell::save_file_dialog(desktop.active()->id(), "C:\\Documents", "notes.txt", [](const char *, void *) {}, nullptr);
    check_window("the Save As dialog");
    shell::shut_down_dialog();
    check_window("the Shut Down dialog");
    shell::screen_saver_dialog();
    check_window("the screen saver dialog");
    shell::open_task_manager();
    check_window("the Task Manager");

    // The same in every other theme (backlog item 47): the flat edges, title
    // bars and taskbar write each pixel once too.
    for (int theme = 1; theme < gfx::theme_count(); theme++) {
        desktop.set_theme(theme);
        desktop.update();
        w.reset();
        desktop.repaint_all();
        char what[64];
        snprintf(what, sizeof(what), "a full repaint in the %s theme", gfx::theme_at(theme).name);
        expect_once(what);
        w.reset();
    }
    desktop.set_theme(0);

    writes = nullptr;
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
