// Welcome: a few lines of text in a window that can be resized. A program
// of its own, written with the thin classes over windows and controls by
// number (core/app/app.hpp).
#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

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

class WelcomeWindow : public app::Window {
public:
    WelcomeWindow() : text_(text_area({6, 6, 100, 100}, kWelcomeText))
    {
        file_items_[0] = {"&Close", &WelcomeWindow::menu_close, this, nullptr, nullptr, 0};
        help_items_[0] = {"&About Chinchilla", &WelcomeWindow::menu_about, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
    }

    void resized(int width, int height) override { text_.place({6, 6, width - 12, height - 12}); }

private:
    static void menu_close(void *context) { static_cast<WelcomeWindow *>(context)->close(); }
    static void menu_about(void *) { open_about(); }

    wm::MenuItem file_items_[1];
    wm::MenuItem help_items_[1];
    wm::Menu file_ = {file_items_, 1};
    wm::Menu help_ = {help_items_, 1};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 2};
    app::TextArea text_;
};

app::Number g_window = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void open_welcome()
{
    const AsProgram as("Welcome");
    if (app::bring_forward(g_window)) {
        return;
    }
    app::Look look;
    look.title = "Welcome to Chinchilla";
    look.x = 96;
    look.y = 30;
    look.width = 400;
    look.height = 280;
    look.icon = &gfx::welcome_icon_16();
    look.flags = app::kMenuBar;
    g_window = app::open<WelcomeWindow>(look);
    if (g_window == app::kNone) {
        no_memory_for("Welcome");
    }
}

} // namespace shell
