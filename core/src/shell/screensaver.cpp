// The screensaver's settings: which one, after how many minutes, and a
// preview. Opened from Start > Settings; to become a tab of a Display
// window once there is a Control Panel (docs/DECISIONS.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/saver/saver.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

int g_choice = 1 + saver::kChinchillas; // 0: none, 1.. the animations
int g_minutes = kScreenSaverMinutes;

// Kept in C:\\.config\\screensaver.ini (docs/settings.md): written when OK is
// pressed, read at start-up.
constexpr const char *kKeys[1 + saver::kCount] = {"none", "starfield", "chinchillas", "curves"};

fs::Ini &settings()
{
    static fs::Ini ini;
    return ini;
}

void read_settings()
{
    if (!load_settings("screensaver", settings())) {
        return;
    }
    char value[32];
    if (settings().get("saver", "saver", value, sizeof(value))) {
        for (int i = 0; i <= saver::kCount; i++) {
            if (strcmp(value, kKeys[i]) == 0) {
                g_choice = i;
            }
        }
    }
    const int minutes = settings().get_int("saver", "wait_minutes", g_minutes);
    if (minutes >= 1) {
        g_minutes = minutes > kScreenSaverMaxMinutes ? kScreenSaverMaxMinutes : minutes;
    }
}

void write_settings()
{
    settings().set("saver", "saver", kKeys[g_choice]);
    settings().set_int("saver", "wait_minutes", g_minutes);
    save_settings("screensaver", settings());
}

saver::Runner &runner()
{
    static saver::Runner r(manager());
    return r;
}

void apply()
{
    if (g_choice == 0) {
        manager().set_screen_saver(nullptr, 0);
    } else {
        runner().select(g_choice - 1);
        manager().set_screen_saver(&runner(), static_cast<uint32_t>(g_minutes) * 60);
    }
}

const char *const *names()
{
    static const char *list[1 + saver::kCount];
    list[0] = "(None)";
    for (int i = 0; i < saver::kCount; i++) {
        list[1 + i] = saver::animation(i).name();
    }
    return list;
}

class SaverDialog : public wm::Window {
public:
    SaverDialog()
        : Window("Screen Saver", {0, 0, 300, 250}, &gfx::settings_icon_16()),
          group_({8, 6, 276, 170}, "Screen saver"),
          list_({18, 24, 256, 60}, names(), 1 + saver::kCount),
          wait_label_({18, 99, 36, 14}, "Wait:"),
          wait_({56, 95, 34, 20}),
          minutes_label_({96, 99, 60, 14}, "minutes"),
          preview_({199, 93, 75, 23}, "&Preview", &SaverDialog::preview, this),
          last_run_({18, 130, 256, 14}, last_run_text_),
          slowest_({18, 146, 256, 14}, slowest_text_),
          ok_({126, 188, 75, 23}, "OK", &SaverDialog::ok, this),
          cancel_({207, 188, 75, 23}, "Cancel", &SaverDialog::cancel, this)
    {
        set_dialog(true);
        add(group_);
        add(list_);
        add(wait_label_);
        add(wait_);
        add(minutes_label_);
        add(preview_);
        add(last_run_);
        add(slowest_);
        add(ok_);
        add(cancel_);
        ok_.set_default(true);
        set_default(&ok_);
    }

    void show()
    {
        if (!open()) {
            char text[8];
            snprintf(text, sizeof(text), "%d", g_minutes);
            wait_.set(text);
            list_.select(g_choice);
            set_frame(centred(shell::manager(), frame().w, frame().h));
            set_focus(&list_);
        }
        shell::show(*this);
    }

    void paint(gfx::Canvas &c) override
    {
        // How the last run went (a preview or the real thing): real figures,
        // as on the start-up screen.
        const saver::Runner &r = runner();
        if (r.frames() > 0 && r.shown_us() > 0) {
            const unsigned tenths = static_cast<unsigned>(r.frames() * 10000000ull / r.shown_us());
            snprintf(last_run_text_, sizeof(last_run_text_), "Last run: %u.%u frames a second", tenths / 10,
                     tenths % 10);
            snprintf(slowest_text_, sizeof(slowest_text_), "Slowest frame: %u.%u ms",
                     static_cast<unsigned>(r.slowest_us() / 1000), static_cast<unsigned>(r.slowest_us() / 100 % 10));
        }
        Window::paint(c);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('p' - 'a')) {
            preview_.click();
            return true;
        }
        return Window::key(k);
    }

    // Closed without OK: a preview may have put another saver in place.
    void closed() override { apply(); }

private:
    static void preview(wm::Button &, void *context)
    {
        auto *d = static_cast<SaverDialog *>(context);
        const int choice = d->list_.selected();
        if (choice < 1) {
            return;
        }
        runner().select(choice - 1);
        shell::manager().set_screen_saver(&runner(), g_choice != 0 ? static_cast<uint32_t>(g_minutes) * 60 : 0);
        shell::manager().start_screen_saver();
    }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<SaverDialog *>(context);
        char text[2 * wm::TextField::kCapacity + 1];
        d->wait_.get(text, sizeof(text));
        const int minutes = atoi(text);
        if (minutes >= 1) {
            g_minutes = minutes > kScreenSaverMaxMinutes ? kScreenSaverMaxMinutes : minutes;
        }
        if (d->list_.selected() >= 0) {
            g_choice = d->list_.selected();
        }
        write_settings();
        d->close(); // applies
    }
    static void cancel(wm::Button &, void *context) { static_cast<SaverDialog *>(context)->close(); }

    char last_run_text_[48] = "";
    char slowest_text_[48] = "";
    wm::GroupFrame group_;
    wm::ListBox list_;
    wm::Label wait_label_;
    wm::TextField wait_;
    wm::Label minutes_label_;
    wm::Button preview_;
    wm::Label last_run_, slowest_;
    wm::Button ok_, cancel_;
};

SaverDialog &dialog()
{
    static SaverDialog d;
    return d;
}

} // namespace

void screen_saver_dialog()
{
    const AsProgram as("Screen Saver");
    dialog().show();
}

void install_screen_saver()
{
    read_settings();
    apply();
}

} // namespace shell
