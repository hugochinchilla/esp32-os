// Renders the desktop through a scripted series of steps and writes each
// screen as a raw file (one RRRGGGBB byte per pixel, 640x480) into the folder
// given. tools/check_pictures.py compares them with the reference pictures
// in host/tests/pictures/.
#include <cstdio>
#include <cstdlib>
#include <string>

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

void save(const char *name)
{
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

    shell::start(desktop, system_);
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
    centre_of(desktop.popup_item(1, 2), x, y);
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

    // A desktop icon: select Documents, double-click it for a message box.
    centre_of(desktop.desktop_icon_rect(1), x, y);
    double_click(x, y);
    save("13-icon-message-box");
    key(wm::key::Enter);

    // Shut Down dialog from the Start menu.
    centre_of(desktop.taskbar_button(-1), x, y);
    click(x, y);
    centre_of(desktop.popup_item(0, 4), x, y);
    click(x, y);
    save("14-shut-down");

    return failures == 0 ? 0 : 1;
}
