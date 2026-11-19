// The Bluetooth program (backlog item 51): the keyboards and mice that
// are connected, remembered or in range, with Pair and Forget, and the two
// switches. The work is done by bt::Manager, which the system keeps
// running (bluetooth_service.cpp); this file shows it while the window is
// open, and while it is open the radio listens harder, so a device put in
// pairing mode appears at once.
#include <cstdio>
#include <cstring>

#include "core/app/app.hpp"
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/input/bluetooth.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

// The Bluetooth service. The window only opens when there is one.
bt::Manager &devices() { return *os::bluetooth(); }

class BluetoothWindow : public app::Window {
public:
    BluetoothWindow()
        : status_(group({8, 4, 372, 54}, "Bluetooth")),
          line1_(plain({16, 20, 356, 14})),
          line2_(plain({16, 35, 356, 14})),
          list_label_(label({8, 64, 200, 14}, "&Devices:")),
          list_(list({8, 80, 284, 130}, nullptr, 0)), // after its label: Alt+D goes to it
          pair_(button({300, 80, 80, 23}, "&Pair")),
          forget_(button({300, 109, 80, 23}, "&Forget")),
          auto_(check_box({8, 216, 372, 18}, "Pair a new &keyboard or mouse by itself when one is missing", true)),
          enabled_(check_box({8, 236, 372, 18}, "&Bluetooth on (from the next start)", true)),
          hint_(plain({8, 264, 284, 14})),
          close_(button({300, 259, 80, 23}, "Close"))
    {
        fs::Ini settings;
        load_settings("bluetooth", settings); // no file yet: the defaults
        wanted_on_ = settings.get_bool("bluetooth", "enabled", true);
        enabled_.set_checked(wanted_on_);
        auto_.set_checked(devices().auto_pair());
        set_focus(list_);
        set_default(pair_);
        devices().set_listing(true);
        // Four times a second, while the window exists: has the radio news?
        timer_ = os::start_timer(250000, 250000, &BluetoothWindow::tick, this);
        refresh();
    }
    ~BluetoothWindow() override
    {
        os::stop_timer(timer_);
        if (bt::Manager *m = os::bluetooth()) {
            m->set_listing(false);
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

    void command(app::Control which) override
    {
        if (which == pair_) {
            pair();
        } else if (which == forget_) {
            const int i = list_.selected();
            if (i >= 0 && i < count_ && rows_[i].ours) {
                devices().forget(rows_[i].address);
                refresh();
            }
        } else if (which == close_) {
            close();
        }
    }
    void changed(app::Control which) override
    {
        if (which == list_) {
            sync_buttons();
        } else if (which == auto_) {
            devices().set_auto_pair(auto_.checked());
        } else if (which == enabled_) {
            // bluetooth.ini as it is, so the other keys in it stay.
            fs::Ini settings;
            load_settings("bluetooth", settings);
            wanted_on_ = enabled_.checked();
            settings.set_bool("bluetooth", "enabled", wanted_on_);
            save_settings("bluetooth", settings);
            refresh();
        }
    }
    bool chosen(app::Control which) override
    {
        if (which == list_) {
            pair();
        }
        return true;
    }

    // Shows the manager's state again; the selection follows its device.
    void refresh()
    {
        bt::Manager &m = devices();
        shown_changes_ = m.changes();
        char text[128];

        // How an attempt that was asked for here ended is told once.
        // (A device is remembered when its pairing succeeds.)
        if (asked_ && m.is_remembered(asked_for_)) {
            asked_ = false;
        } else if (asked_ && !m.is_linked(asked_for_)) {
            asked_ = false;
            if (on_screen()) {
                snprintf(text, sizeof(text), "Could not pair with %.31s.\nPut it in pairing mode and try again.",
                         asked_name_);
                message_box("Bluetooth", text);
            }
        }

        if (!m.on()) {
            line1_.set(wanted_on_ ? "Bluetooth is not running." : "Bluetooth is off.");
            line2_.set(wanted_on_ ? "It starts with the next restart, if this board has it."
                                  : "Tick the box below and restart to use it.");
        } else {
            const uint8_t has = m.kinds_ready();
            line1_.set(has == (hid::kKeyboard | hid::kMouse) ? "A keyboard and a mouse are connected."
                       : has == hid::kKeyboard               ? "A keyboard is connected; no mouse."
                       : has == hid::kMouse                  ? "A mouse is connected; no keyboard."
                                                             : "No keyboard and no mouse are connected.");
            line2_.set(wanted_on_ ? "Bluetooth Low Energy devices only." : "Bluetooth goes off with the next restart.");
        }

        // The list: ours first (connected, then remembered), then what is
        // in range and says it is a keyboard or a mouse.
        bt::Address chosen = {};
        const bool had = list_.selected() >= 0 && list_.selected() < count_;
        if (had) {
            chosen = rows_[list_.selected()].address;
        }
        Row now[kMaxRows] = {};
        int count = 0;
        const auto add = [&](const bt::Address &address, const char *name, uint8_t kinds, const char *state,
                             bool ours) {
            if (count == kMaxRows) {
                return;
            }
            Row &row = now[count++];
            row.address = address;
            row.ours = ours;
            char spelled[bt::kAddressText];
            if (name[0] == 0) { // it did not say: its address without the last word
                bt::format(address, spelled);
                spelled[17] = 0;
                name = spelled;
            }
            snprintf(row.name, sizeof(row.name), "%s", name);
            if (kinds != 0) {
                snprintf(row.text, sizeof(row.text), "%s  (%s, %s)", name, bt::kinds_text(kinds), state);
            } else {
                snprintf(row.text, sizeof(row.text), "%s  (%s)", name, state);
            }
        };
        for (int i = 0; i < m.link_count(); i++) {
            const bt::Link &l = m.link(i);
            add(l.address, l.name, l.kinds,
                l.state == bt::LinkState::Ready ? "connected" : (l.state == bt::LinkState::Securing ? "pairing..." : "connecting..."),
                true);
        }
        for (int i = 0; i < m.remembered_count(); i++) {
            const bt::Manager::Remembered &r = m.remembered(i);
            if (!m.is_linked(r.address)) {
                add(r.address, r.name, r.kinds, "not connected", true);
            }
        }
        for (int i = 0; i < m.found_count(); i++) {
            const bt::Found &f = m.found(i);
            if (f.hid && !m.is_linked(f.address) && !m.is_remembered(f.address)) {
                add(f.address, f.name, f.kinds, f.pairing ? "ready to pair" : "in range", false);
            }
        }
        bool same = count == count_;
        for (int i = 0; i < count && same; i++) {
            same = strcmp(now[i].text, rows_[i].text) == 0 && bt::same(now[i].address, rows_[i].address);
        }
        if (!same) {
            memcpy(rows_, now, sizeof(rows_));
            count_ = count;
            for (int i = 0; i < count_; i++) {
                row_ptrs_[i] = rows_[i].text;
            }
            list_.set_items(row_ptrs_, count_);
            for (int i = 0; i < count_; i++) {
                if (had && bt::same(chosen, rows_[i].address)) {
                    list_.select(i);
                }
            }
        }
        if (!m.on()) {
            hint_.set("");
        } else if (count_ == 0) {
            hint_.set("Looking for keyboards and mice...");
        } else {
            snprintf(text, sizeof(text), count_ == 1 ? "%d device" : "%d devices", count_);
            hint_.set(text);
        }
        sync_buttons();
    }

private:
    static constexpr int kMaxRows = bt::Manager::kMaxRemembered + bt::Manager::kMaxFound;
    struct Row {
        bt::Address address;
        char name[bt::kNameMax + 1];
        char text[bt::kNameMax + 40];
        bool ours; // connected or remembered: Forget is for it, Pair is not
    };

    // Pair for a device that is not ours yet, Forget for one that is.
    void sync_buttons()
    {
        const int i = list_.selected();
        const bool any = i >= 0 && i < count_;
        pair_.set_enabled(any && !rows_[i].ours);
        forget_.set_enabled(any && rows_[i].ours);
    }

    void pair()
    {
        const int i = list_.selected();
        if (i < 0 || i >= count_ || rows_[i].ours) {
            return;
        }
        if (!devices().pair(rows_[i].address)) {
            message_box("Bluetooth", "Another device is being connected,\nor three already are. Try again in a moment.");
            return;
        }
        asked_ = true;
        asked_for_ = rows_[i].address;
        snprintf(asked_name_, sizeof(asked_name_), "%s", rows_[i].name);
        refresh();
    }

    static void tick(void *context)
    {
        auto &w = *static_cast<BluetoothWindow *>(context);
        if (w.on_screen() && devices().changes() != w.shown_changes_) {
            w.refresh();
        }
    }

    app::GroupFrame status_;
    app::Plain line1_, line2_;
    app::Label list_label_;
    app::ListBox list_;
    app::Button pair_, forget_;
    app::CheckBox auto_, enabled_;
    app::Plain hint_;
    app::Button close_;

    Row rows_[kMaxRows] = {};
    const char *row_ptrs_[kMaxRows] = {};
    int count_ = 0;
    int timer_ = 0;
    uint32_t shown_changes_ = 0;
    bool wanted_on_ = true;   // `enabled` in bluetooth.ini
    bool asked_ = false;      // an attempt asked for here is under way
    bt::Address asked_for_ = {};
    char asked_name_[bt::kNameMax + 1] = "";
};

app::Number g_window = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void open_bluetooth()
{
    if (os::bluetooth() == nullptr) {
        message_box("Bluetooth", "Bluetooth is not available on this board.");
        return;
    }
    const AsProgram as("Bluetooth");
    if (app::bring_forward(g_window)) {
        return;
    }
    app::Look look;
    look.title = "Bluetooth";
    look.width = 396;
    look.height = 320;
    look.icon = &gfx::settings_icon_16();
    look.flags = app::kDialog;
    g_window = app::open<BluetoothWindow>(look);
    if (g_window == app::kNone) {
        no_memory_for("Bluetooth");
    }
}

} // namespace shell
