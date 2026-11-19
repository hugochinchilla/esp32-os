// Bluetooth keyboards and mice, as the system keeps them running (backlog
// item 51): the manager is driven four times a second, so a device that
// was paired is connected again when it calls, and a new one in pairing
// mode is taken when the machine has no keyboard or no mouse; the tray
// shows whether it is off, on, or has a device connected. Not a
// program: the window that shows the devices is (bluetooth.cpp), and it
// asks for the service through os::bluetooth().
#include "core/gfx/art.hpp"
#include "core/gfx/theme.hpp"
#include "core/input/bluetooth.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

bt::Manager *g_bluetooth = nullptr;

uint32_t g_shown_changes = 0;
int g_shown_theme = 0;

void tray_clicked(void *) { open_bluetooth(); }

void show_tray()
{
    const gfx::BluetoothIcon icon = !g_bluetooth->on()                 ? gfx::BluetoothIcon::Off
                                    : g_bluetooth->kinds_ready() != 0 ? gfx::BluetoothIcon::Connected
                                                                      : gfx::BluetoothIcon::On;
    // Blue on the classic grey bar; white where the theme writes the bar
    // in a light ink, which is where the bar is dark or blue.
    g_shown_theme = gfx::theme_index();
    const bool dark_bar = gfx::theme().bar_ink != gfx::kBlack;
    set_tray_icon(kTrayBluetooth, &gfx::bluetooth_tray_16(icon, dark_bar), &tray_clicked, nullptr);
}

// Four times a second: follow the radio, and show what changed.
void bluetooth_tick(void *)
{
    g_bluetooth->tick(manager().now_us());
    if (g_bluetooth->changes() != g_shown_changes || gfx::theme_index() != g_shown_theme) {
        g_shown_changes = g_bluetooth->changes();
        show_tray();
    }
}

} // namespace

void set_bluetooth(bt::Manager *bluetooth)
{
    g_bluetooth = bluetooth;
    os::install_bluetooth(bluetooth);
}
bt::Manager *bluetooth() { return g_bluetooth; }

void start_bluetooth()
{
    if (g_bluetooth == nullptr) {
        return;
    }
    // Like the Wi-Fi manager it has no row in the process table: it is
    // driven by this timer, on the UI task, so the timer is the desktop's.
    g_bluetooth->start(manager().now_us());
    g_shown_changes = g_bluetooth->changes();
    show_tray();
    manager().start_timer(250000, 250000, &bluetooth_tick, nullptr);
}

} // namespace shell
