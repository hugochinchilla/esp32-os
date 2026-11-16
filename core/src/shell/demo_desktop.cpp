#include "core/shell/demo_desktop.hpp"

#include "core/gfx/art.hpp"
#include "core/wm/widgets.hpp"

namespace shell {

namespace {

constexpr uint16_t kEnter = 0x28, kEscape = 0x29;

const char kWelcomeText[] =
    "Welcome to Chinchilla, a 90s-style desktop\n"
    "for the ESP32-S3.\n"
    "\n"
    "Try it:\n"
    "  - drag a window by its title bar\n"
    "  - click a window to bring it to the front\n"
    "  - minimise one and get it back from the\n"
    "    taskbar\n"
    "\n"
    "640 x 480, 256 colours, 60 Hz.\n"
    "Texto en español: año, niño, café, ¡hola!";

class WelcomeWindow : public wm::Window {
public:
    WelcomeWindow()
        : Window("Welcome to Chinchilla", {40, 30, 400, 280}, &gfx::text_icon_16()),
          text_({6, 6, 400 - 8 - 12, 280 - 28 - 12}, kWelcomeText)
    {
        add(text_);
    }

private:
    wm::TextArea text_;
};

class AboutWindow : public wm::Window {
public:
    AboutWindow()
        : Window("About Chinchilla", {250, 170, 300, 160}, &gfx::mascot_16()),
          name_({56, 14, 200, 14}, "Chinchilla", true),
          version_({56, 34, 220, 14}, "Version 0.1"),
          line_({56, 52, 220, 14}, "A 90s desktop for the ESP32-S3."),
          ok_({292 - 75 - 10, 132 - 23 - 10, 75, 23}, "OK", &AboutWindow::ok, this)
    {
        add(name_);
        add(version_);
        add(line_);
        add(ok_);
    }

    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit2x(gfx::mascot_16(), 12, 10);
    }

    void key(uint16_t usage, bool pressed) override
    {
        if (pressed && (usage == kEnter || usage == kEscape)) {
            ok_.click();
        }
    }

private:
    static void ok(wm::Button &, void *context)
    {
        static_cast<AboutWindow *>(context)->close();
    }

    wm::Label name_, version_, line_;
    wm::Button ok_;
};

} // namespace

void open_demo_windows(wm::Wm &wm)
{
    static WelcomeWindow welcome;
    static AboutWindow about;
    wm.add(welcome);
    wm.add(about);
}

} // namespace shell
