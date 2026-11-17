// The Shut Down dialog: Shut Down, Restart or Cancel. A program of its own;
// the closing of every program and the screen that follows are the
// system's (power.cpp), asked for through os::shut_down().
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

class ShutDownDialog : public wm::Window {
public:
    ShutDownDialog()
        : Window("Shut Down Chinchilla", {0, 0, 320, 150}, &gfx::power_icon_16()),
          question_({56, 16, 240, 14}, "What do you want the computer to do?"),
          detail_({56, 34, 240, 14}, "Shut Down gets it ready to be unplugged."),
          shut_down_({312 - 3 * 75 - 2 * 6 - 10, 122 - 23 - 10, 75, 23}, "&Shut Down", &ShutDownDialog::shut_down, this),
          restart_({312 - 2 * 75 - 6 - 10, 122 - 23 - 10, 75, 23}, "&Restart", &ShutDownDialog::restart, this),
          cancel_({312 - 75 - 10, 122 - 23 - 10, 75, 23}, "Cancel", &ShutDownDialog::cancel, this)
    {
        set_dialog(true);
        set_modal(nullptr, true); // the whole desktop waits for an answer
        set_frame(os::centred(frame().w, frame().h));
        add(question_);
        add(detail_);
        add(shut_down_);
        add(restart_);
        add(cancel_);
        shut_down_.set_default(true);
        set_default(&shut_down_);
    }

    bool layered() const override { return true; } // the picture over the face
    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit(gfx::mascot_32(), 12, 12);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('r' - 'a')) {
            restart_.click();
            return true;
        }
        return Window::key(k);
    }

private:
    static void shut_down(wm::Button &, void *context)
    {
        static_cast<ShutDownDialog *>(context)->close();
        os::shut_down();
    }
    static void restart(wm::Button &, void *) { os::restart(); }
    static void cancel(wm::Button &, void *context) { static_cast<ShutDownDialog *>(context)->close(); }

    wm::Label question_, detail_;
    wm::Button shut_down_, restart_, cancel_;
};

} // namespace

void shut_down_dialog()
{
    const AsProgram as("Shut Down");
    open_one<ShutDownDialog>("Shut Down");
}

} // namespace shell
