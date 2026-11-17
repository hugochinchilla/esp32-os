// The screensaver's settings: which one, after how many minutes, and a
// preview. Opened from Start > Settings; to become a part of the Display
// window (display.cpp) one day (docs/DECISIONS.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/saver/saver.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
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

class SaverDialog : public wm::Window {
public:
    SaverDialog()
        : Window("Screen Saver", {0, 0, 300, 250}, &gfx::saver_icon_16()),
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
        // What is in force now, to start from.
        now_ = read_settings(settings_);
        char text[8];
        snprintf(text, sizeof(text), "%d", now_.minutes);
        wait_.set(text);
        list_.select(now_.saver);
        set_frame(os::centred(frame().w, frame().h));
        set_focus(&list_);
    }

    void paint(gfx::Canvas &c) override
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
    void closed() override { apply(now_); }

private:
    static void preview(wm::Button &, void *context)
    {
        auto *d = static_cast<SaverDialog *>(context);
        const int choice = d->list_.selected();
        if (choice >= 1) {
            os::preview_screen_saver(choice - 1);
        }
    }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<SaverDialog *>(context);
        char text[2 * wm::TextField::kCapacity + 1];
        d->wait_.get(text, sizeof(text));
        const int minutes = atoi(text);
        if (minutes >= 1) {
            d->now_.minutes = in_range(minutes);
        }
        if (d->list_.selected() >= 0) {
            d->now_.saver = d->list_.selected();
        }
        write_settings(d->settings_, d->now_);
        d->close(); // applies
    }
    static void cancel(wm::Button &, void *context) { static_cast<SaverDialog *>(context)->close(); }

    Choice now_; // what the settings file holds
    fs::Ini settings_;
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

} // namespace

void screen_saver_dialog()
{
    const AsProgram as("Screen Saver");
    open_one<SaverDialog>("Screen Saver");
}

void install_screen_saver()
{
    fs::Ini ini;
    apply(read_settings(ini));
}

} // namespace shell
