// The Network panel: the Wi-Fi networks in range and connecting with a
// password. The work is done by net::Manager, which the system keeps
// running (network_service.cpp, with the signal icon in the tray); this
// file shows it while the panel is open.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/net/wifi.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/app/app.hpp"
#include "internal.hpp"

namespace shell {

namespace {

// The Wi-Fi service. The panel only opens when there is one.
net::Manager &wifi() { return *os::network(); }

const char *failure_text(net::Failure f)
{
    switch (f) {
    case net::Failure::WrongPassword: return "the password is not right";
    case net::Failure::NotFound: return "the network was not found";
    case net::Failure::TimedOut: return "it did not answer";
    default: return "the network refused";
    }
}

/* ---- The password dialog ------------------------------------------------------- */

// Asks for the password of a network; with `ask_name`, for its name too (a
// network that is not in the list). Made when it is asked for, over the
// panel, and gone when it closes: nothing typed stays behind.
class JoinDialog : public app::Window {
public:
    JoinDialog(bool ask_name, const char *ssid) : ask_name_(ask_name)
    {
        const int top = ask_name ? 28 : 0;
        snprintf(ssid_, sizeof(ssid_), "%s", ssid);
        if (ask_name) {
            label({12, 14, 72, 14}, "&Name:");
            name_ = text_field({88, 10, 212, 21});
        } else {
            char line[96];
            snprintf(line, sizeof(line), "Password for %s", ssid);
            plain({12, 10, 290, 14}).set(line);
        }
        label({12, 34 + top, 72, 14}, "&Password:");
        password_ = text_field({88, 30 + top, 212, 21});
        show_ = check_box({88, 56 + top, 212, 18}, "&Show characters", false);
        remember_ = check_box({88, 76 + top, 212, 18}, "&Remember this network", true);
        ok_ = button({312 - 2 * 75 - 6 - 10, 110 + top, 75, 23}, "OK");
        cancel_ = button({312 - 75 - 10, 110 + top, 75, 23}, "Cancel");
        set_default(ok_);
        password_.set_hidden(true);
        set_focus(ask_name ? name_ : password_);
    }

    // Over the panel, which cannot be used meanwhile.
    static void ask(app::Number panel, bool ask_name, const char *ssid)
    {
        app::Look look;
        look.title = ask_name ? "Other network" : "Connect to network";
        look.width = 320;
        look.height = ask_name ? 204 : 176;
        look.icon = &gfx::network_icon_16();
        look.flags = app::kDialog;
        look.over = panel;
        if (app::open<JoinDialog>(look, ask_name, ssid) == app::kNone) {
            no_memory_for(look.title);
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

    void changed(app::Control which) override
    {
        if (which == show_) {
            password_.set_hidden(!show_.checked());
        }
    }

    void command(app::Control which) override
    {
        if (which == cancel_) {
            close();
        } else if (which == ok_) {
            ok();
        }
    }

private:
    void ok()
    {
        char ssid[2 * app::TextField::kCapacity + 1];
        char password[2 * app::TextField::kCapacity + 1];
        if (ask_name_) {
            name_.get(ssid, sizeof(ssid));
        } else {
            snprintf(ssid, sizeof(ssid), "%s", ssid_);
        }
        password_.get(password, sizeof(password));
        const size_t length = strlen(password);
        const char *problem = nullptr;
        if (ssid[0] == 0 || strlen(ssid) > net::kSsidMax) {
            problem = "Type the name of the network\n(at most 32 characters).";
        } else if (length > net::kPasswordMax || (length > 0 && length < 8)) {
            problem = "A Wi-Fi password has 8 to 63 characters.";
        } else if (length == 0 && !ask_name_) {
            problem = "Type the password of the network.";
        }
        if (problem != nullptr) {
            memset(password, 0, sizeof(password));
            message_box(ask_name_ ? "Other network" : "Connect to network", problem);
            return;
        }
        const bool remember = remember_.checked();
        close();
        wifi().connect(ssid, password, remember);
        memset(password, 0, sizeof(password));
    }

    bool ask_name_;
    char ssid_[net::kSsidMax + 1] = "";
    app::TextField name_, password_;
    app::CheckBox show_, remember_;
    app::Button ok_, cancel_;
};

/* ---- The panel ----------------------------------------------------------------- */

class NetworkWindow : public app::Window {
public:
    NetworkWindow()
        : status_(group({8, 4, 372, 70}, "Wi-Fi")),
          line1_(plain({60, 20, 312, 14})),
          line2_(plain({60, 35, 312, 14})),
          line3_(plain({60, 50, 312, 14})),
          list_label_(label({8, 80, 200, 14}, "Wireless &networks:")),
          list_(list({8, 96, 284, 150}, nullptr, 0)), // after its label: Alt+N goes to it
          connect_(button({300, 96, 80, 23}, "&Connect")),
          forget_(button({300, 125, 80, 23}, "&Forget")),
          refresh_(button({300, 154, 80, 23}, "&Refresh")),
          other_(button({300, 183, 80, 23}, "&Other...")),
          hint_(plain({8, 252, 284, 14})),
          close_(button({300, 255, 80, 23}, "Close"))
    {
        list_.set_items(nullptr, 0, row_icons_);
        set_focus(list_);
        set_default(connect_);
        // Four times a second, while the panel exists: has the radio news?
        timer_ = os::start_timer(250000, 250000, &NetworkWindow::tick, this);
        refresh();
        if (!wifi().scanning()) {
            wifi().scan(); // a fresh list each time it opens
            refresh();
        }
    }
    ~NetworkWindow() override { os::stop_timer(timer_); }

    void paint(gfx::Canvas &c) override { c.blit2x(gfx::signal_icon_16(shown_bars_), 18, 26); } // over the face

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        if (which == connect_) {
            join();
        } else if (which == forget_) {
            if (const net::Manager::Seen *s = chosen_network()) {
                char ssid[net::kSsidMax + 1];
                snprintf(ssid, sizeof(ssid), "%s", s->network.ssid);
                wifi().forget(ssid);
            }
        } else if (which == refresh_) {
            wifi().scan();
        } else if (which == other_) {
            JoinDialog::ask(number(), true, "");
        } else if (which == close_) {
            close();
        }
    }
    void changed(app::Control which) override
    {
        if (which == list_) {
            sync_button();
        }
    }
    bool chosen(app::Control which) override
    {
        if (which == list_) {
            join();
        }
        return true;
    }

    // Shows the manager's state again; the selection follows its network.
    void refresh()
    {
        net::Manager &m = wifi();
        shown_changes_ = m.changes();
        const net::LinkStatus &link = m.link();
        char text[128];

        // The reason a connect failed is told once, in a box.
        if (was_connecting_ && link.link == net::Link::Idle && m.last_failure() != net::Failure::None &&
            on_screen()) {
            snprintf(text, sizeof(text), "Could not connect to %.32s:\n%s.", m.attempt_ssid(),
                     failure_text(m.last_failure()));
            message_box("Network", text);
        }
        was_connecting_ = link.link == net::Link::Connecting;

        const int icon = link.link == net::Link::Connected ? net::bars(link.rssi)
                                                           : (link.link == net::Link::Connecting ? 0 : -1);
        if (icon != shown_bars_) {
            shown_bars_ = icon;
            redraw({18, 26, 32, 32});
        }
        switch (link.link) {
        case net::Link::Off:
            line1_.set("This board has no Wi-Fi radio.");
            break;
        case net::Link::Idle:
            if (m.last_failure() != net::Failure::None) {
                snprintf(text, sizeof(text), "Not connected: %s.", failure_text(m.last_failure()));
                line1_.set(text);
            } else {
                line1_.set("Not connected.");
            }
            break;
        case net::Link::Connecting:
            snprintf(text, sizeof(text), "Connecting to %s...", link.ssid);
            line1_.set(text);
            break;
        case net::Link::Connected:
            snprintf(text, sizeof(text), "Connected to %s", link.ssid);
            line1_.set(text);
            break;
        }
        if (link.link == net::Link::Connected) {
            snprintf(text, sizeof(text), "Address: %d.%d.%d.%d   Router: %d.%d.%d.%d", link.address[0],
                     link.address[1], link.address[2], link.address[3], link.gateway[0], link.gateway[1],
                     link.gateway[2], link.gateway[3]);
            line2_.set(text);
            snprintf(text, sizeof(text), "Signal: %d dBm, channel %d   Clock: %s", link.rssi, link.channel,
                     m.time_known() ? "from network" : "not set yet");
            line3_.set(text);
        } else {
            snprintf(text, sizeof(text), "This board: %02X:%02X:%02X:%02X:%02X:%02X", link.mac[0], link.mac[1],
                     link.mac[2], link.mac[3], link.mac[4], link.mac[5]);
            line2_.set(link.link == net::Link::Off ? "" : text);
            line3_.set("");
        }

        // The list: keep the same network chosen.
        char chosen[net::kSsidMax + 1] = "";
        if (list_.selected() >= 0 && list_.selected() < count_) {
            snprintf(chosen, sizeof(chosen), "%s", ssids_[list_.selected()]);
        }
        count_ = m.seen_count();
        bool same = count_ == listed_;
        for (int i = 0; i < count_; i++) {
            const net::Manager::Seen &s = m.seen(i);
            char row[sizeof(rows_[0])];
            snprintf(row, sizeof(row), "%s%s", s.network.ssid,
                     s.connected ? "  (connected)" : (s.saved ? "  (remembered)" : ""));
            const int icon_now = net::bars(s.network.rssi) + (s.network.security != net::Security::Open ? 8 : 0);
            same = same && strcmp(row, rows_[i]) == 0 && icon_now == icons_[i];
            snprintf(rows_[i], sizeof(rows_[i]), "%s", row);
            snprintf(ssids_[i], sizeof(ssids_[i]), "%s", s.network.ssid);
            icons_[i] = static_cast<uint8_t>(icon_now);
            row_ptrs_[i] = rows_[i];
            row_icons_[i] = &gfx::signal_icon_16(icon_now & 7, (icon_now & 8) != 0);
        }
        if (!same) {
            listed_ = count_;
            list_.set_items(row_ptrs_, count_, row_icons_);
            for (int i = 0; i < count_; i++) {
                if (chosen[0] != 0 && strcmp(chosen, ssids_[i]) == 0) {
                    list_.select(i);
                }
            }
        }
        if (m.scanning()) {
            hint_.set("Looking for networks...");
        } else if (link.link == net::Link::Off) {
            hint_.set("");
        } else {
            snprintf(text, sizeof(text), count_ == 1 ? "%d network in range" : "%d networks in range", count_);
            hint_.set(text);
        }
        sync_button();
    }

private:
    const net::Manager::Seen *chosen_network() const
    {
        const int i = list_.selected();
        return i >= 0 && i < wifi().seen_count() ? &wifi().seen(i) : nullptr;
    }

    void sync_button()
    {
        const net::Manager::Seen *s = chosen_network();
        const bool on = s != nullptr && s->connected;
        if (on != shows_disconnect_) {
            shows_disconnect_ = on;
            connect_.set_label(on ? "&Disconnect" : "&Connect");
        }
    }

    void join()
    {
        const net::Manager::Seen *s = chosen_network();
        if (s == nullptr) {
            return;
        }
        if (s->connected) {
            wifi().disconnect();
        } else if (s->network.security == net::Security::Enterprise) {
            message_box("Network", "This network asks for a user name as\nwell as a password. Chinchilla cannot\njoin such networks.");
        } else if (s->saved || s->network.security == net::Security::Open) {
            wifi().connect(s->network.ssid, nullptr, true);
        } else {
            JoinDialog::ask(number(), false, s->network.ssid);
        }
    }

    static void tick(void *context)
    {
        auto &w = *static_cast<NetworkWindow *>(context);
        if (w.on_screen() && wifi().changes() != w.shown_changes_) {
            w.refresh();
        }
    }

    app::GroupFrame status_;
    app::Plain line1_, line2_, line3_;
    app::Label list_label_;
    app::ListBox list_;
    app::Button connect_, forget_, refresh_, other_;
    app::Plain hint_;
    app::Button close_;

    char rows_[net::Manager::kMaxSeen][net::kSsidMax + 16] = {};
    char ssids_[net::Manager::kMaxSeen][net::kSsidMax + 1] = {};
    const char *row_ptrs_[net::Manager::kMaxSeen] = {};
    const gfx::Bitmap *row_icons_[net::Manager::kMaxSeen] = {};
    int listed_ = 0; // rows the list was last given
    uint8_t icons_[net::Manager::kMaxSeen] = {};
    int count_ = 0;
    int timer_ = 0;
    uint32_t shown_changes_ = 0;
    int shown_bars_ = -1;
    bool shows_disconnect_ = false;
    bool was_connecting_ = false;
};

app::Number g_panel = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void open_network()
{
    if (os::network() == nullptr) {
        message_box("Network", "The network is not available on this board.");
        return;
    }
    const AsProgram as("Network");
    if (app::bring_forward(g_panel)) {
        return;
    }
    app::Look look;
    look.title = "Network";
    look.width = 396;
    look.height = 316;
    look.icon = &gfx::network_icon_16();
    look.flags = app::kDialog | app::kPicture;
    g_panel = app::open<NetworkWindow>(look);
    if (g_panel == app::kNone) {
        no_memory_for("Network");
    }
}

} // namespace shell
