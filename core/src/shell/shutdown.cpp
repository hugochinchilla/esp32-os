// The Shut Down dialog: Shut Down, Restart or Cancel. A program of its own;
// the closing of every program and the screen that follows are the
// system's (power.cpp), asked for through os::shut_down().
//
// The first program written against windows and controls by number
// (core/os/ui.hpp): it has no window class and no button objects, only the
// numbers the system gave it and one function that is told what happens.
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/os/ui.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

namespace ui = os::ui;

// What the dialog remembers while it is open. It comes with the window and
// goes with it.
struct ShutDown {
    ui::Control shut_down, restart, cancel;
};

ui::Window g_dialog = ui::kNoWindow; // the one there may be; a number, dead once it has closed

bool events(ui::Window window, const ui::Event &event, void *state)
{
    const auto &d = *static_cast<ShutDown *>(state);
    switch (event.what) {
    case ui::What::Command:
        if (event.control == d.shut_down) {
            ui::close(window);
            os::shut_down();
        } else if (event.control == d.restart) {
            os::restart();
        } else if (event.control == d.cancel) {
            ui::close(window);
        }
        return true;
    case ui::What::Key:
        if (event.key->pressed && event.key->usage == wm::key::Escape) {
            ui::close(window);
            return true;
        }
        if (event.key->pressed && (event.key->mods & wm::kAlt) && event.key->usage == wm::key::A + ('r' - 'a')) {
            ui::press(window, d.restart);
            return true;
        }
        return false;
    case ui::What::Paint:
        event.canvas->blit(gfx::mascot_32(), 12, 12);
        return true;
    case ui::What::Closed:
        return true;
    }
    return false;
}

} // namespace

void shut_down_dialog()
{
    const AsProgram as("Shut Down");
    if (!ui::alive(g_dialog)) {
        ui::Look look;
        look.title = "Shut Down Chinchilla";
        look.width = 320;
        look.height = 150;
        look.icon = &gfx::power_icon_16();
        look.flags = ui::kDialog | ui::kWholeDesktop | ui::kPicture;
        g_dialog = ui::create(look, &events, sizeof(ShutDown));
        if (g_dialog == ui::kNoWindow) {
            no_memory_for("Shut Down");
            return;
        }
        auto &d = *static_cast<ShutDown *>(ui::state(g_dialog));
        const int y = 122 - 23 - 10;
        ui::label(g_dialog, {56, 16, 240, 14}, "What do you want the computer to do?");
        ui::label(g_dialog, {56, 34, 240, 14}, "Shut Down gets it ready to be unplugged.");
        d.shut_down = ui::button(g_dialog, {312 - 3 * 75 - 2 * 6 - 10, y, 75, 23}, "&Shut Down");
        d.restart = ui::button(g_dialog, {312 - 2 * 75 - 6 - 10, y, 75, 23}, "&Restart");
        d.cancel = ui::button(g_dialog, {312 - 75 - 10, y, 75, 23}, "Cancel");
        ui::set_default(g_dialog, d.shut_down);
    }
    ui::show(g_dialog);
}

} // namespace shell
