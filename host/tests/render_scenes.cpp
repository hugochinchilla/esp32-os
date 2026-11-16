// Renders the desktop through a scripted series of steps and writes each
// screen as a raw file (one RRRGGGBB byte per pixel, 640x480) into the folder
// given. tools/check_pictures.py compares them with the reference pictures
// in host/tests/pictures/.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/shell/boot.hpp"
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

void save(const char *name, bool update = true)
{
    if (update) {
        desktop.update(); // paint what direct calls (not events) changed
    }
    std::string path = folder + "/" + name + ".raw";
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr || fwrite(display.pixels(), 1, 640 * 480, f) != 640 * 480) {
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

    shell::start(desktop, system_, storage, memory);
    desktop.start();
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
    centre_of(desktop.popup_item(1, 5), x, y);
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

    // Into C:, then docs (keyboard: select and Enter).
    key(wm::key::Home);
    key(wm::key::Enter);
    save("16-drive-c");
    wm::Window *fm = desktop.active();
    key(wm::key::Home);
    key(wm::key::Enter); // docs is the first folder
    save("17-docs");

    // Open notes.txt with the keyboard: español.txt, notes.txt, todo.txt.
    key(wm::key::Home);
    key(wm::key::Right);
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

    // Rename in the folder window: F2.
    desktop.activate(*fm);
    key(wm::key::Home);
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
    key(wm::key::Right);
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

    // The screensaver. Its dialog from Start > Settings (the third entry of
    // each), then a preview of each animation: 150 frames, five seconds.
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    centre_of(desktop.taskbar_button(-1), x, y);
    click(x, y);
    centre_of(desktop.popup_item(0, 1), x, y);
    mouse(x, y, 0);
    clock_.now += wm::Wm::kSubmenuDelayUs;
    desktop.tick();
    save("34-settings-menu");
    centre_of(desktop.popup_item(1, 2), x, y);
    click(x, y);
    save("35-screen-saver-dialog");
    const char *const savers[3] = {"36-saver-starfield", "37-saver-chinchillas", "38-saver-curves"};
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
    save("39-after-screen-saver");

    return failures == 0 ? 0 : 1;
}
