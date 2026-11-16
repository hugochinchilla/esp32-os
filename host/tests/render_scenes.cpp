// Renders the demo desktop through a scripted series of steps and writes
// each screen as a raw file (one RRRGGGBB byte per pixel, 640x480) into the
// folder given. tools/check_pictures.py compares them with the reference
// pictures in host/tests/pictures/.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/shell/demo_desktop.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
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

void mouse(int x, int y, uint8_t buttons)
{
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
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

void click(int x, int y)
{
    mouse(x, y, 0);
    mouse(x, y, 1);
    mouse(x, y, 0);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: render_scenes <folder>\n");
        return 2;
    }
    folder = argv[1];

    shell::open_demo_windows(desktop);
    desktop.start();
    save("01-desktop");

    click(100, 200); // into the Welcome window: it comes to the front
    save("02-welcome-focused");

    // Drag the About window by the part of its title bar that shows: an
    // outline while moving, the window itself on release.
    mouse(490, 180, 0);
    mouse(490, 180, 1);
    mouse(450, 140, 1);
    mouse(400, 90, 1);
    save("03-dragging");
    mouse(400, 90, 0);
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
    desktop.key(0x28, true);
    desktop.key(0x28, false);
    gfx::Rect min = desktop.title_button(*welcome, wm::Part::Minimize);
    click(min.x + 5, min.y + 5);
    save("06-about-closed-welcome-minimized");

    // Bring it back from the taskbar.
    gfx::Rect button = desktop.taskbar_button(0);
    click(button.x + 20, button.y + 10);
    save("07-restored-from-taskbar");

    return failures == 0 ? 0 : 1;
}
