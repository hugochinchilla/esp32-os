// The screensaver's settings: which one, after how many minutes, and a
// preview. Opened from Start > Settings; to become a tab of a Display
// window once there is a Control Panel (docs/DECISIONS.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/saver/saver.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

int g_choice = 1 + saver::kChinchillas; // 0: none, 1.. the animations
int g_minutes = kScreenSaverMinutes;

// The settings as a plain text file in the hidden settings folder, as the
// owner wants for every program. Written when OK is pressed, read at
// start-up. ponytail: the folder and file layout here are this program's
// guess until the shared convention is on main; on the RAM disk the file
// is lost at restart like everything else.
constexpr char kConfigFolder[] = "C:\\.config";
constexpr char kConfigFile[] = "C:\\.config\\screensaver.ini";
constexpr const char *kKeys[1 + saver::kCount] = {"none", "starfield", "chinchillas", "curves"};

void load_settings()
{
    char text[256];
    size_t size = 0;
    if (storage().read_file(kConfigFile, text, sizeof(text) - 1, size) != ports::FsError::Ok) {
        return;
    }
    text[size] = 0;
    for (const char *line = text; line != nullptr && *line != 0;) {
        char value[32];
        int minutes = 0;
        if (sscanf(line, " saver = %31[a-z]", value) == 1) {
            for (int i = 0; i <= saver::kCount; i++) {
                if (strcmp(value, kKeys[i]) == 0) {
                    g_choice = i;
                }
            }
        } else if (sscanf(line, " wait_minutes = %d", &minutes) == 1 && minutes >= 1) {
            g_minutes = minutes > kScreenSaverMaxMinutes ? kScreenSaverMaxMinutes : minutes;
        }
        line = strchr(line, '\n');
        line = line != nullptr ? line + 1 : nullptr;
    }
}

void save_settings()
{
    char text[160];
    const int size = snprintf(text, sizeof(text),
                              "; Chinchilla screensaver\r\n"
                              "; saver: none, starfield, chinchillas or curves\r\n"
                              "saver = %s\r\nwait_minutes = %d\r\n",
                              kKeys[g_choice], g_minutes);
    storage().make_folder(kConfigFolder); // already there: fine
    storage().write_file(kConfigFile, text, static_cast<size_t>(size));
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
        save_settings();
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

void screen_saver_dialog() { dialog().show(); }

void install_screen_saver()
{
    load_settings();
    apply();
}

} // namespace shell
