// The Network panel: the Wi-Fi networks in range, connecting with a
// password, and the signal icon in the tray. The work is done by
// net::Manager; this file shows it.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/net/wifi.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

net::Manager *g_net = nullptr;
uint32_t g_shown_changes = 0;

// A line of text exactly as given (a Label would read '&' in a network's
// name as an access letter).
class Plain : public wm::Widget {
public:
    explicit Plain(const gfx::Rect &r) : Widget(r) {}
    void set(const char *text)
    {
        if (strcmp(text, text_) != 0) {
            snprintf(text_, sizeof(text_), "%.*s", static_cast<int>(sizeof(text_) - 1), text); // longer text is cut
            invalidate();
        }
    }
    const char *get() const { return text_; }
    void paint(gfx::Canvas &c) override
    {
        const int y = (rect.h - 12) / 2;
        c.fill({0, 0, rect.w, y}, gfx::kGrey);
        const int x = c.text_opaque(0, y, text_, gfx::kBlack, gfx::kGrey);
        c.fill({x, y, rect.w - x, 12}, gfx::kGrey);
        c.fill({0, y + 12, rect.w, rect.h - y - 12}, gfx::kGrey);
    }

private:
    char text_[96] = "";
};

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
// network that is not in the list).
class JoinDialog : public wm::Window {
public:
    explicit JoinDialog(bool ask_name)
        : Window(ask_name ? "Other network" : "Connect to network", {0, 0, 320, ask_name ? 204 : 176},
                 &gfx::network_icon_16()),
          ask_name_(ask_name), top_(ask_name ? 28 : 0),
          prompt_({12, 10, 290, 14}),
          name_label_({12, 14, 72, 14}, "&Name:"),
          name_({88, 10, 212, 21}),
          password_label_({12, 34 + top_, 72, 14}, "&Password:"),
          password_({88, 30 + top_, 212, 21}),
          show_({88, 56 + top_, 212, 18}, "&Show characters", false, &JoinDialog::show_changed, this),
          remember_({88, 76 + top_, 212, 18}, "&Remember this network", true),
          ok_({312 - 2 * 75 - 6 - 10, 110 + top_, 75, 23}, "OK", &JoinDialog::ok, this),
          cancel_({312 - 75 - 10, 110 + top_, 75, 23}, "Cancel", &JoinDialog::cancel, this)
    {
        set_dialog(true);
        if (ask_name) {
            add(name_label_);
            add(name_);
        } else {
            add(prompt_);
        }
        add(password_label_);
        add(password_);
        add(show_);
        add(remember_);
        add(ok_);
        add(cancel_);
        ok_.set_default(true);
        set_default(&ok_);
        password_.set_hidden(true);
    }

    void ask(wm::Window *owner, const char *ssid)
    {
        if (open()) {
            close();
        }
        snprintf(ssid_, sizeof(ssid_), "%s", ssid);
        char line[96];
        snprintf(line, sizeof(line), "Password for %s", ssid);
        prompt_.set(line);
        name_.set("");
        password_.set("");
        show_.set_checked(false);
        remember_.set_checked(true);
        set_focus(ask_name_ ? &name_ : &password_);
        set_frame(centred(shell::manager(), frame().w, frame().h));
        set_modal(owner);
        shell::manager().add(*this);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

    void closed() override
    {
        password_.set(""); // nothing typed stays behind
    }

private:
    static void show_changed(wm::CheckBox &box, void *context)
    {
        static_cast<JoinDialog *>(context)->password_.set_hidden(!box.checked());
    }
    static void cancel(wm::Button &, void *context) { static_cast<JoinDialog *>(context)->close(); }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<JoinDialog *>(context);
        char ssid[2 * wm::TextField::kCapacity + 1];
        char password[2 * wm::TextField::kCapacity + 1];
        if (d->ask_name_) {
            d->name_.get(ssid, sizeof(ssid));
        } else {
            snprintf(ssid, sizeof(ssid), "%s", d->ssid_);
        }
        d->password_.get(password, sizeof(password));
        const size_t length = strlen(password);
        const char *problem = nullptr;
        if (ssid[0] == 0 || strlen(ssid) > net::kSsidMax) {
            problem = "Type the name of the network\n(at most 32 characters).";
        } else if (length > net::kPasswordMax || (length > 0 && length < 8)) {
            problem = "A Wi-Fi password has 8 to 63 characters.";
        } else if (length == 0 && !d->ask_name_) {
            problem = "Type the password of the network.";
        }
        if (problem != nullptr) {
            memset(password, 0, sizeof(password));
            message_box(d->title(), problem);
            return;
        }
        const bool remember = d->remember_.checked();
        d->close();
        g_net->connect(ssid, password, remember);
        memset(password, 0, sizeof(password));
    }

    bool ask_name_;
    int top_;
    char ssid_[net::kSsidMax + 1] = "";
    Plain prompt_;
    wm::Label name_label_;
    wm::TextField name_;
    wm::Label password_label_;
    wm::TextField password_;
    wm::CheckBox show_, remember_;
    wm::Button ok_, cancel_;
};

JoinDialog &password_dialog()
{
    static JoinDialog dialog(false);
    return dialog;
}

JoinDialog &other_dialog()
{
    static JoinDialog dialog(true);
    return dialog;
}

/* ---- The panel ----------------------------------------------------------------- */

class NetworkWindow : public wm::Window {
public:
    NetworkWindow()
        : Window("Network", {0, 0, 396, 316}, &gfx::network_icon_16()),
          status_({8, 4, 372, 70}, "Wi-Fi"),
          line1_({60, 20, 312, 14}),
          line2_({60, 35, 312, 14}),
          line3_({60, 50, 312, 14}),
          list_label_({8, 80, 200, 14}, "Wireless &networks:"),
          list_({8, 96, 284, 150}, nullptr, 0, &NetworkWindow::selected, this),
          connect_({300, 96, 80, 23}, "&Connect", &NetworkWindow::connect, this),
          forget_({300, 125, 80, 23}, "&Forget", &NetworkWindow::forget, this),
          refresh_({300, 154, 80, 23}, "&Refresh", &NetworkWindow::rescan, this),
          other_({300, 183, 80, 23}, "&Other...", &NetworkWindow::other, this),
          hint_({8, 252, 284, 14}),
          close_({300, 255, 80, 23}, "Close", &NetworkWindow::close_clicked, this)
    {
        set_dialog(true);
        list_.set_icons(&NetworkWindow::row_icon);
        list_.set_on_activate(&NetworkWindow::activated);
        add(status_);
        add(line1_);
        add(line2_);
        add(line3_);
        add(list_label_);
        add(list_); // after its label: Alt+N goes to it
        set_focus(&list_);
        add(connect_);
        add(forget_);
        add(refresh_);
        add(other_);
        add(hint_);
        add(close_);
        connect_.set_default(true);
        set_default(&connect_);
    }

    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit2x(gfx::signal_icon_16(shown_bars_), 18, 26);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

    // Shows the manager's state again; the selection follows its network.
    void refresh()
    {
        net::Manager &m = *g_net;
        const net::LinkStatus &link = m.link();
        char text[128];

        // The reason a connect failed is told once, in a box.
        if (was_connecting_ && link.link == net::Link::Idle && m.last_failure() != net::Failure::None &&
            open()) {
            snprintf(text, sizeof(text), "Could not connect to %.32s:\n%s.", m.attempt_ssid(),
                     failure_text(m.last_failure()));
            message_box("Network", text);
        }
        was_connecting_ = link.link == net::Link::Connecting;

        const int icon = link.link == net::Link::Connected ? net::bars(link.rssi)
                                                           : (link.link == net::Link::Connecting ? 0 : -1);
        if (icon != shown_bars_) {
            shown_bars_ = icon;
            invalidate_client({18, 26, 32, 32});
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
        bool same = count_ == list_.count();
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
        }
        if (!same) {
            list_.set_items(row_ptrs_, count_);
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
    static NetworkWindow &self(void *context) { return *static_cast<NetworkWindow *>(context); }

    const net::Manager::Seen *chosen() const
    {
        const int i = list_.selected();
        return i >= 0 && i < g_net->seen_count() ? &g_net->seen(i) : nullptr;
    }

    void sync_button()
    {
        const net::Manager::Seen *s = chosen();
        const bool on = s != nullptr && s->connected;
        if (on != shows_disconnect_) {
            shows_disconnect_ = on;
            connect_.set_label(on ? "&Disconnect" : "&Connect");
        }
    }

    void join()
    {
        const net::Manager::Seen *s = chosen();
        if (s == nullptr) {
            return;
        }
        if (s->connected) {
            g_net->disconnect();
        } else if (s->network.security == net::Security::Enterprise) {
            message_box("Network", "This network asks for a user name as\nwell as a password. Chinchilla cannot\njoin such networks.");
        } else if (s->saved || s->network.security == net::Security::Open) {
            g_net->connect(s->network.ssid, nullptr, true);
        } else {
            password_dialog().ask(this, s->network.ssid);
        }
    }

    static void selected(wm::ListBox &, void *context) { self(context).sync_button(); }
    static void activated(wm::ListBox &, void *context) { self(context).join(); }
    static void connect(wm::Button &, void *context) { self(context).join(); }
    static void forget(wm::Button &, void *context)
    {
        if (const net::Manager::Seen *s = self(context).chosen()) {
            char ssid[net::kSsidMax + 1];
            snprintf(ssid, sizeof(ssid), "%s", s->network.ssid);
            g_net->forget(ssid);
        }
    }
    static void rescan(wm::Button &, void *) { g_net->scan(); }
    static void other(wm::Button &, void *context) { other_dialog().ask(&self(context), ""); }
    static void close_clicked(wm::Button &, void *context) { self(context).close(); }
    static const gfx::Bitmap *row_icon(int index, void *context)
    {
        const uint8_t v = self(context).icons_[index];
        return &gfx::signal_icon_16(v & 7, (v & 8) != 0);
    }

    wm::GroupFrame status_;
    Plain line1_, line2_, line3_;
    wm::Label list_label_;
    wm::ListBox list_;
    wm::Button connect_, forget_, refresh_, other_;
    Plain hint_;
    wm::Button close_;

    char rows_[net::Manager::kMaxSeen][net::kSsidMax + 16] = {};
    char ssids_[net::Manager::kMaxSeen][net::kSsidMax + 1] = {};
    const char *row_ptrs_[net::Manager::kMaxSeen] = {};
    uint8_t icons_[net::Manager::kMaxSeen] = {};
    int count_ = 0;
    int shown_bars_ = -1;
    bool shows_disconnect_ = false;
    bool was_connecting_ = false;
};

NetworkWindow &panel()
{
    static NetworkWindow window;
    return window;
}

void tray_clicked(void *) { open_network(); }

void show_tray()
{
    const net::LinkStatus &link = g_net->link();
    const int icon = link.link == net::Link::Connected ? net::bars(link.rssi)
                                                       : (link.link == net::Link::Connecting ? 0 : -1);
    manager().set_tray_icon(&gfx::signal_icon_16(icon), &tray_clicked, nullptr);
}

void clock_from_network(const net::LocalTime &t, void *) { manager().set_time(t.hour, t.minute, t.second); }

// Four times a second: follow the radio, and show what changed.
void network_tick(void *)
{
    g_net->tick(manager().now_us());
    if (g_net->changes() != g_shown_changes) {
        g_shown_changes = g_net->changes();
        show_tray();
        panel().refresh();
    }
}

} // namespace

void set_network(net::Manager *wifi) { g_net = wifi; }
net::Manager *network() { return g_net; }

void start_network()
{
    if (g_net == nullptr) {
        return;
    }
    g_net->set_on_time(&clock_from_network, nullptr);
    g_net->start(manager().now_us());
    g_shown_changes = g_net->changes();
    show_tray();
    manager().start_timer(250000, 250000, &network_tick, nullptr);
}

void open_network()
{
    if (g_net == nullptr) {
        message_box("Network", "The network is not available on this board.");
        return;
    }
    NetworkWindow &w = panel();
    if (!w.open()) {
        w.set_frame(centred(manager(), w.frame().w, w.frame().h));
        w.refresh();
        if (!g_net->scanning()) {
            g_net->scan(); // a fresh list each time it opens
            w.refresh();
        }
    }
    show(w);
}

} // namespace shell
