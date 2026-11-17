// The screensaver's settings: which one, after how many minutes, and a
// preview. Opened from Start > Settings; to become a part of the Display
// window (display.cpp) one day (docs/DECISIONS.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/app/app.hpp"
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/saver/saver.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

// What is in force: 0 for none, 1.. the animations, and the wait. Nothing
// is kept in memory while the dialog is closed; the settings file has it.
struct Choice {
    int saver = 1 + saver::kChinchillas;
    int minutes = kScreenSaverMinutes;
};

// Kept in C:\\.config\\screensaver.ini (docs/settings.md): written when OK is
// pressed, read at start-up and when the dialog opens.
constexpr const char *kKeys[1 + saver::kCount] = {"none", "starfield", "chinchillas", "curves"};

int in_range(int minutes) { return minutes > kScreenSaverMaxMinutes ? kScreenSaverMaxMinutes : minutes; }

Choice read_settings(fs::Ini &ini)
{
    Choice c;
    if (!load_settings("screensaver", ini)) {
        return c;
    }
    char value[32];
    if (ini.get("saver", "saver", value, sizeof(value))) {
        for (int i = 0; i <= saver::kCount; i++) {
            if (strcmp(value, kKeys[i]) == 0) {
                c.saver = i;
            }
        }
    }
    const int minutes = ini.get_int("saver", "wait_minutes", c.minutes);
    if (minutes >= 1) {
        c.minutes = in_range(minutes);
    }
    return c;
}

void write_settings(fs::Ini &ini, const Choice &c)
{
    ini.set("saver", "saver", kKeys[c.saver]);
    ini.set_int("saver", "wait_minutes", c.minutes);
    save_settings("screensaver", ini);
}

void apply(const Choice &c) { os::set_screen_saver(c.saver - 1, static_cast<uint32_t>(c.minutes) * 60); }

const char *const *names()
{
    static const char *list[1 + saver::kCount];
    list[0] = "(None)";
    for (int i = 0; i < saver::kCount; i++) {
        list[1 + i] = saver::animation(i).name();
    }
    return list;
}

class SaverDialog : public app::Window {
public:
    SaverDialog()
        : group_(group({8, 6, 276, 170}, "Screen saver")),
          list_(list({18, 24, 256, 60}, names(), 1 + saver::kCount)),
          wait_label_(label({18, 99, 36, 14}, "Wait:")),
          wait_(text_field({56, 95, 34, 20})),
          minutes_label_(label({96, 99, 60, 14}, "minutes")),
          preview_(button({199, 93, 75, 23}, "&Preview")),
          last_run_(label({18, 130, 256, 14}, last_run_text_)),
          slowest_(label({18, 146, 256, 14}, slowest_text_)),
          ok_(button({126, 188, 75, 23}, "OK")),
          cancel_(button({207, 188, 75, 23}, "Cancel"))
    {
        set_default(ok_);
        // What is in force now, to start from.
        now_ = read_settings(settings_);
        char text[8];
        snprintf(text, sizeof(text), "%d", now_.minutes);
        wait_.set(text);
        list_.select(now_.saver);
        set_focus(list_);
    }

    void painting() override
    {
        // How the last run went (a preview or the real thing): real figures,
        // as on the start-up screen.
        const os::SaverRun r = os::last_saver_run();
        if (r.frames > 0 && r.shown_us > 0) {
            const unsigned tenths = static_cast<unsigned>(r.frames * 10000000ull / r.shown_us);
            snprintf(last_run_text_, sizeof(last_run_text_), "Last run: %u.%u frames a second", tenths / 10,
                     tenths % 10);
            snprintf(slowest_text_, sizeof(slowest_text_), "Slowest frame: %u.%u ms",
                     static_cast<unsigned>(r.slowest_us / 1000), static_cast<unsigned>(r.slowest_us / 100 % 10));
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('p' - 'a')) {
            preview_.press();
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        if (which == preview_) {
            const int choice = list_.selected();
            if (choice >= 1) {
                os::preview_screen_saver(choice - 1);
            }
        } else if (which == ok_) {
            char text[2 * app::TextField::kCapacity + 1];
            wait_.get(text, sizeof(text));
            const int minutes = atoi(text);
            if (minutes >= 1) {
                now_.minutes = in_range(minutes);
            }
            if (list_.selected() >= 0) {
                now_.saver = list_.selected();
            }
            write_settings(settings_, now_);
            close(); // applies
        } else if (which == cancel_) {
            close();
        }
    }

    // Closed without OK: a preview may have put another saver in place.
    void closed() override { apply(now_); }

private:
    Choice now_; // what the settings file holds
    fs::Ini settings_;
    char last_run_text_[48] = "";
    char slowest_text_[48] = "";
    app::GroupFrame group_;
    app::ListBox list_;
    app::Label wait_label_;
    app::TextField wait_;
    app::Label minutes_label_;
    app::Button preview_;
    app::Label last_run_, slowest_;
    app::Button ok_, cancel_;
};

app::Number g_dialog = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void screen_saver_dialog()
{
    const AsProgram as("Screen Saver");
    if (app::bring_forward(g_dialog)) {
        return;
    }
    app::Look look;
    look.title = "Screen Saver";
    look.width = 300;
    look.height = 250;
    look.icon = &gfx::saver_icon_16();
    look.flags = app::kDialog;
    g_dialog = app::open<SaverDialog>(look);
    if (g_dialog == app::kNone) {
        no_memory_for("Screen Saver");
    }
}

void install_screen_saver()
{
    fs::Ini ini;
    apply(read_settings(ini));
}

} // namespace shell
