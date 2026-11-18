// About Chinchilla: the mascot, the name and the version. A program of its
// own, written with the thin classes over windows and controls by number
// (core/app/app.hpp).
#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

class AboutWindow : public app::Window {
public:
    AboutWindow()
        : version_(label({56, 34, 220, 14}, "Version 0.2")),
          line_(label({56, 52, 220, 14}, "A 90s desktop for the ESP32-S3.")),
          ok_(button({292 - 75 - 10, 132 - 23 - 10, 75, 23}, "OK"))
    {
        set_default(ok_);
    }

    void command(app::Control which) override
    {
        if (which == ok_) {
            close();
        }
    }

    // The picture and the name in bold, over the face.
    void paint(app::Canvas &c) override
    {
        c.blit(gfx::mascot_32(), 10, 10);
        c.text(56, 14 + (14 - app::Canvas::text_height()) / 2, "Chinchilla", gfx::kBlack, true);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

private:
    app::Label version_, line_;
    app::Button ok_;
};

app::Number g_window = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void open_about()
{
    // Any program's Help menu comes here: it opens as itself, on its own task.
    in_program("About Chinchilla", [] {
        if (app::bring_forward(g_window)) {
            return;
        }
        app::Look look;
        look.title = "About Chinchilla";
        look.x = 300;
        look.y = 190;
        look.width = 300;
        look.height = 160;
        look.icon = &gfx::mascot_16();
        look.flags = app::kDialog | app::kPicture;
        g_window = app::open<AboutWindow>(look);
        if (g_window == app::kNone) {
            no_memory_for("About Chinchilla");
        }
    });
}

} // namespace shell
