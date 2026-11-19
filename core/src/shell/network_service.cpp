// The network, as the system keeps it running: the Wi-Fi manager is driven
// four times a second, the tray shows its signal and the clock takes the
// time it brings. Not a program: the panel that shows the networks is
// (network.cpp), and it asks for the service through os::network().
#include "core/gfx/art.hpp"
#include "core/net/wifi.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

net::Manager *g_net = nullptr;
uint32_t g_shown_changes = 0;

void tray_clicked(void *) { open_network(); }

void show_tray()
{
    const net::LinkStatus &link = g_net->link();
    const int icon = link.link == net::Link::Connected ? net::bars(link.rssi)
                                                       : (link.link == net::Link::Connecting ? 0 : -1);
    set_tray_icon(kTrayNetwork, &gfx::signal_icon_16(icon), &tray_clicked, nullptr);
}

void clock_from_network(const net::LocalTime &t, void *) { manager().set_time(t.hour, t.minute, t.second); }

// Four times a second: follow the radio, and show what changed. The panel,
// when it is open, looks for itself (its own timer, after this one).
void network_tick(void *)
{
    g_net->tick(manager().now_us());
    if (g_net->changes() != g_shown_changes) {
        g_shown_changes = g_net->changes();
        show_tray();
    }
}

} // namespace

void set_network(net::Manager *wifi)
{
    g_net = wifi;
    os::install_network(wifi);
}
net::Manager *network() { return g_net; }

void start_network()
{
    if (g_net == nullptr) {
        return;
    }
    // The Wi-Fi manager has no row in the process table: it is driven by the
    // timer below, on the UI task, so that timer is the desktop's. It gets a
    // row when it is a task of its own (the owner's rule, 2026-11-16).
    g_net->set_on_time(&clock_from_network, nullptr);
    g_net->start(manager().now_us());
    g_shown_changes = g_net->changes();
    show_tray();
    manager().start_timer(250000, 250000, &network_tick, nullptr);
}

} // namespace shell
