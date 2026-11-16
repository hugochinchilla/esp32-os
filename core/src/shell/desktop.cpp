// The desktop: Start menu, desktop icons, message box and Shut Down dialog.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

wm::Wm *g_wm = nullptr;
ports::System *g_system = nullptr;

/* ---- Message box ---------------------------------------------------------------- */

class MessageBox : public wm::Window {
public:
    static constexpr int kMaxLines = 8;
    static constexpr int kMaxButtons = 3;

    MessageBox()
        : Window("", {0, 0, 300, 140}, nullptr),
          buttons_{{{0, 0, 75, 23}, "", &MessageBox::pressed, this},
                   {{0, 0, 75, 23}, "", &MessageBox::pressed, this},
                   {{0, 0, 75, 23}, "", &MessageBox::pressed, this}}
    {
        set_dialog(true);
        for (wm::Button &b : buttons_) {
            add(b);
        }
    }

    void ask(const char *title, const char *text, const char *const *labels, int count,
             AnswerFn answer, void *context)
    {
        if (open()) {
            answered_ = true; // the old question is dropped without an answer
            close();
        }
        snprintf(title_, sizeof(title_), "%s", title);
        snprintf(text_, sizeof(text_), "%s", text);
        answer_ = answer;
        context_ = context;
        answered_ = false;
        count_ = count < 1 ? 1 : (count > kMaxButtons ? kMaxButtons : count);
        // Size to the text: the longest line and the number of lines.
        int lines = 1, longest = 0, run = 0;
        for (const char *p = text_; *p != 0; p++) {
            if (*p == '\n') {
                lines++;
                run = 0;
            } else if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) {
                run++;
                longest = run > longest ? run : longest;
            }
        }
        lines = lines > kMaxLines ? kMaxLines : lines;
        const int buttons_w = count_ * 75 + (count_ - 1) * 6;
        int w = 56 + longest * 6 + 20 + 2 * kBorder;
        w = w < buttons_w + 40 ? buttons_w + 40 : w;
        w = w < 220 ? 220 : w;
        const int text_h = lines * 13;
        const int client_h = (text_h > 32 ? text_h : 32) + 20 + 23 + 12;
        const int h = kClientTop + client_h + kBorder;
        set_frame(centred(shell::manager(), w, h));
        const int x0 = (w - 2 * kBorder - buttons_w) / 2;
        for (int i = 0; i < kMaxButtons; i++) {
            buttons_[i].rect = i < count_ ? gfx::Rect{x0 + i * 81, client_h - 23 - 10, 75, 23}
                                          : gfx::Rect{-200, -200, 0, 0};
            labels_[i] = i < count_ ? labels[i] : "";
            buttons_[i].set_label(labels_[i]);
            buttons_[i].set_default(i == 0);
        }
        set_default(&buttons_[0]);
        set_focus(&buttons_[0]);
        set_title(title_);
        // Modal to the window that was in use.
        wm::Window *owner = shell::manager().active();
        set_modal(owner != this ? owner : nullptr);
        shell::manager().add(*this);
    }

    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit2x(gfx::mascot_16(), 12, 12);
        char line[96];
        const char *p = text_;
        for (int y = 14; *p != 0; y += 13) {
            int n = 0;
            while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(line)) - 1) {
                line[n] = p[n];
                n++;
            }
            line[n] = 0;
            c.text(56, y, line, gfx::kBlack);
            p += n;
            if (*p == '\n') {
                p++;
            }
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close(); // answers as Cancel (closed() below)
            return true;
        }
        return Window::key(k);
    }

    void closed() override
    {
        // Closed without a button (Escape, the close box): the last button,
        // which is Cancel or No when there is more than one.
        finish(count_ - 1);
    }

private:
    static void pressed(wm::Button &button, void *context)
    {
        auto *box = static_cast<MessageBox *>(context);
        for (int i = 0; i < box->count_; i++) {
            if (&box->buttons_[i] == &button) {
                box->chosen_ = i;
            }
        }
        box->answered_by_button_ = true;
        box->close();
    }

    void finish(int fallback)
    {
        if (answered_) {
            return;
        }
        answered_ = true;
        const int choice = answered_by_button_ ? chosen_ : fallback;
        answered_by_button_ = false;
        if (answer_ != nullptr) {
            answer_(choice, context_);
        }
    }

    char title_[64] = "";
    char text_[320] = "";
    const char *labels_[kMaxButtons] = {"", "", ""};
    wm::Button buttons_[kMaxButtons];
    int count_ = 1;
    int chosen_ = 0;
    bool answered_ = true;
    bool answered_by_button_ = false;
    AnswerFn answer_ = nullptr;
    void *context_ = nullptr;
};

MessageBox &message_window()
{
    static MessageBox box;
    return box;
}

/* ---- Shut Down dialog ----------------------------------------------------------- */

class ShutDownDialog : public wm::Window {
public:
    ShutDownDialog()
        : Window("Shut Down Chinchilla", {0, 0, 320, 150}, &gfx::mascot_16()),
          question_({56, 16, 240, 14}, "What do you want the computer to do?"),
          detail_({56, 34, 240, 14}, "Restart starts Chinchilla again."),
          restart_({312 - 2 * 75 - 6 - 10, 122 - 23 - 10, 75, 23}, "&Restart", &ShutDownDialog::restart, this),
          cancel_({312 - 75 - 10, 122 - 23 - 10, 75, 23}, "Cancel", &ShutDownDialog::cancel, this)
    {
        set_dialog(true);
        set_modal(nullptr, true); // the whole desktop waits for an answer
        add(question_);
        add(detail_);
        add(restart_);
        add(cancel_);
        restart_.set_default(true);
        set_default(&restart_);
    }

    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit2x(gfx::mascot_16(), 12, 12);
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
    static void restart(wm::Button &, void *) { system_port().reboot(); }
    static void cancel(wm::Button &, void *context) { static_cast<ShutDownDialog *>(context)->close(); }

    wm::Label question_, detail_;
    wm::Button restart_, cancel_;
};

ShutDownDialog &shut_down_window()
{
    static ShutDownDialog dialog;
    return dialog;
}

/* ---- Start menu and desktop ------------------------------------------------------ */

constexpr int kMaxApps = 8;
wm::MenuItem g_program_items[kMaxApps];
wm::Menu g_programs = {g_program_items, 0};
wm::MenuItem g_settings_items[] = {
    {"&Control Panel", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Taskbar...", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
};
wm::Menu g_settings = {g_settings_items, 2};
wm::MenuItem g_start_items[5];
wm::Menu g_start = {g_start_items, 5};

void launch_app(void *context) { launch(*static_cast<const App *>(context)); }
void shut_down(void *) { shut_down_dialog(); }

struct NotYet {
    const char *title;
    const char *text;
};
const NotYet kDocuments = {"Documents", "Documents is not available yet.\nFiles come in a later version."};
const NotYet kNetwork = {"Network", "The network is not available yet.\nWi-Fi comes in a later version."};
const NotYet kRecycleBin = {"Recycle Bin", "The Recycle Bin is not available yet.\nIt needs files first."};

void not_yet(void *context)
{
    const NotYet *n = static_cast<const NotYet *>(context);
    message_box(n->title, n->text);
}

void open_my_board(void *)
{
    launch(*find_app("About Chinchilla"));
}

wm::DesktopIcon g_icons[4];

} // namespace

wm::Wm &manager() { return *g_wm; }
ports::System &system_port() { return *g_system; }

void show(wm::Window &w)
{
    if (w.open()) {
        manager().activate(w);
    } else {
        manager().add(w);
    }
}

gfx::Rect centred(const wm::Wm &wm, int w, int h)
{
    const gfx::Rect area = wm.work_area();
    return {(area.w - w) / 2, (area.h - h) / 2, w, h};
}

void message_box(const char *title, const char *text)
{
    static const char *const ok[] = {"OK"};
    message_window().ask(title, text, ok, 1, nullptr, nullptr);
}

void ask(const char *title, const char *text, const char *const *buttons, int count, AnswerFn answer,
         void *context)
{
    message_window().ask(title, text, buttons, count, answer, context);
}

void shut_down_dialog()
{
    ShutDownDialog &d = shut_down_window();
    if (!d.open()) {
        d.set_frame(centred(manager(), d.frame().w, d.frame().h));
    }
    show(d);
}

void start(wm::Wm &wm, ports::System &system)
{
    g_wm = &wm;
    g_system = &system;

    const int n = app_count() < kMaxApps ? app_count() : kMaxApps;
    for (int i = 0; i < n; i++) {
        const App &a = app(i);
        g_program_items[i] = {a.name, &launch_app, const_cast<App *>(&a), nullptr, a.icon, 0};
    }
    g_programs.count = n;
    g_start_items[0] = {"&Programs", nullptr, nullptr, &g_programs, &gfx::folder_icon_16(), 0};
    g_start_items[1] = {"&Settings", nullptr, nullptr, &g_settings, &gfx::settings_icon_16(), 0};
    g_start_items[2] = {"&Run...", nullptr, nullptr, nullptr, &gfx::text_icon_16(), wm::kMenuDisabled};
    g_start_items[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
    g_start_items[4] = {"Sh&ut Down...", &shut_down, nullptr, nullptr, &gfx::mascot_16(), 0};
    wm.set_start_menu(&g_start, "Chinchilla");

    g_icons[0] = {"My Board", &gfx::board_icon_32(), &open_my_board, nullptr};
    g_icons[1] = {"Documents", &gfx::folder_icon_32(), &not_yet, const_cast<NotYet *>(&kDocuments)};
    g_icons[2] = {"Network", &gfx::network_icon_32(), &not_yet, const_cast<NotYet *>(&kNetwork)};
    g_icons[3] = {"Recycle Bin", &gfx::bin_icon_32(), &not_yet, const_cast<NotYet *>(&kRecycleBin)};
    wm.set_desktop_icons(g_icons, 4);

    launch(*find_app("Welcome"));
    launch(*find_app("About Chinchilla"));
}

} // namespace shell
