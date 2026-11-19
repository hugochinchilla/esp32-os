// Bluetooth keyboards and mice, as the system keeps them running (backlog
// item 51): the manager is driven four times a second, so a device that
// was paired is connected again when it calls, and a new one in pairing
// mode is taken when the machine has no keyboard or no mouse. Not a
// program: it has no window.
#include "core/input/bluetooth.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

bt::Manager *g_bluetooth = nullptr;

void bluetooth_tick(void *) { g_bluetooth->tick(manager().now_us()); }

} // namespace

void set_bluetooth(bt::Manager *bluetooth) { g_bluetooth = bluetooth; }
bt::Manager *bluetooth() { return g_bluetooth; }

void start_bluetooth()
{
    if (g_bluetooth == nullptr) {
        return;
    }
    // Like the Wi-Fi manager it has no row in the process table: it is
    // driven by this timer, on the UI task, so the timer is the desktop's.
    g_bluetooth->start(manager().now_us());
    manager().start_timer(250000, 250000, &bluetooth_tick, nullptr);
}

} // namespace shell
