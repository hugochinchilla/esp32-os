// The Shut Down dialog: Shut Down, Restart or Cancel. A program of its own;
// the closing of every program and the screen that follows are the
// system's (power.cpp), asked for through os::shut_down().
//
// The first program written with the thin classes (core/app/app.hpp) over
// windows and controls by number: its window and buttons are the system's,
// and it holds their numbers.
#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

class ShutDownDialog : public app::Window {
public:
    ShutDownDialog()
        : question_(label({56, 16, 240, 14}, "What do you want the computer to do?")),
          detail_(label({56, 34, 240, 14}, "Shut Down gets it ready to be unplugged.")),
          shut_down_(button({312 - 3 * 75 - 2 * 6 - 10, 122 - 23 - 10, 75, 23}, "&Shut Down")),
          restart_(button({312 - 2 * 75 - 6 - 10, 122 - 23 - 10, 75, 23}, "&Restart")),
          cancel_(button({312 - 75 - 10, 122 - 23 - 10, 75, 23}, "Cancel"))
    {
        set_default(shut_down_);
    }

    void command(app::Control which) override
    {
        if (which == shut_down_) {
            close();
            os::shut_down();
        } else if (which == restart_) {
            os::restart();
        } else if (which == cancel_) {
            close();
        }
    }

    void paint(gfx::Canvas &c) override { c.blit(gfx::mascot_32(), 12, 12); } // the picture over the face

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('r' - 'a')) {
            restart_.press();
            return true;
        }
        return false;
    }

private:
    app::Label question_, detail_;
    app::Button shut_down_, restart_, cancel_;
};

app::Number g_dialog = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void shut_down_dialog()
{
    const AsProgram as("Shut Down");
    if (app::bring_forward(g_dialog)) {
        return;
    }
    app::Look look;
    look.title = "Shut Down Chinchilla";
    look.width = 320;
    look.height = 150;
    look.icon = &gfx::power_icon_16();
    look.flags = app::kDialog | app::kWholeDesktop | app::kPicture;
    g_dialog = app::open<ShutDownDialog>(look);
    if (g_dialog == app::kNone) {
        no_memory_for("Shut Down");
    }
}

} // namespace shell
