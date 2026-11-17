// The Network panel against a pretend radio, step by step: checks what the
// manager and the windows do, and writes each screen for
// tools/check_pictures.py (reference pictures net-*.png).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/init/init.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/net/wifi.hpp"
#include "core/desk/desk.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../fake_wifi.hpp"
#include "../host_adapters.hpp"

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
wm::Wm desktop(display, pointer, clock_);
host::FakeWifi radio;
host::MemoryKeyStore store;
net::Manager wifi(radio, store);
std::string folder;
int failures = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

void save(const char *name)
{
    desktop.update();
    // Nothing stale: what is on the screen equals a full repaint.
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    if (memcmp(before.data(), display.pixels(), before.size()) != 0) {
        printf("stale pixels in %s\n", name);
        failures++;
    }
    std::string path = folder + "/" + name + ".raw";
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr || fwrite(display.pixels(), 1, 640 * 480, f) != 640 * 480) {
        printf("cannot write %s\n", path.c_str());
        failures++;
    }
    if (f != nullptr) {
        fclose(f);
    }
}

// Time passes: the shell follows the radio four times a second.
void wait(int quarters)
{
    for (int i = 0; i < quarters; i++) {
        clock_.now += 250000;
        desktop.tick();
    }
}

void click(int x, int y)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 0});
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
}

void key(uint16_t usage, bool alt = false)
{
    if (alt) {
        desktop.key(wm::key::LeftAlt, true);
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    if (alt) {
        desktop.key(wm::key::LeftAlt, false);
    }
}

void alt(char letter) { key(static_cast<uint16_t>(wm::key::A + (letter - 'a')), true); }

std::string title() { return desktop.active() != nullptr ? desktop.active()->title() : ""; }

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: render_net_scenes <folder>\n");
        return 2;
    }
    folder = argv[1];

    static host::HostMemory memory;
    static fs::Storage storage;
    static fs::MemoryVolume ram_disk(memory.allocate(2 << 20), 2 << 20, "RAM disk");
    storage.mount('C', ram_disk);

    radio.add("Vecinos 2.4G", -74, "no-la-sabemos");
    radio.add("Casa Chinchilla", -48, "semillas-y-heno");
    radio.add("Cafe & Churros", -63, "");
    radio.add("Casa Chinchilla", -80, "semillas-y-heno", 11);
    radio.add("eduroam", -85, "x");
    radio.air.back().security = net::Security::Enterprise;
    radio.utc = 1794841870; // 2026-11-16 15:11:10 UTC: 4:11 PM in Madrid
    radio.has_time = true;

    shell::set_network(&wifi);
    static init::Init first(proc::table()); // PID 1, as on the board; the desktop is its child
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(4);
    // Nothing remembered: not connected, a cross on the tray icon.
    CHECK(wifi.link().link == net::Link::Idle && radio.connects == 0);
    CHECK(!shell::taskbar()->tray_icon_rect().empty());
    save("net-01-tray-not-connected");

    // A click on the tray icon opens the panel, which looks for networks.
    const gfx::Rect tray = shell::taskbar()->tray_icon_rect();
    click(tray.x + 8, tray.y + 8);
    CHECK(title() == "Network" && wifi.scanning());
    save("net-02-panel-looking");
    wait(4);
    CHECK(!wifi.scanning() && wifi.seen_count() == 4);
    save("net-03-panel-list");

    // The strongest network wants a password: Enter asks for it.
    key(wm::key::Down);
    key(wm::key::Enter);
    CHECK(title() == "Connect to network");
    desktop.text("semilla");
    save("net-04-password-hidden");
    alt('s'); // Show characters
    save("net-05-password-shown");

    // Too short: said at once, the dialog stays.
    key(wm::key::Enter);
        save("net-06-password-too-short");
    key(wm::key::Enter);
    CHECK(title() == "Connect to network" && radio.connects == 0);

    // A wrong password: the radio says so after a moment.
    alt('p');
    desktop.text("s-y-nada");
    key(wm::key::Enter);
    CHECK(title() == "Network" && wifi.link().link == net::Link::Connecting);
    wait(1);
    save("net-07-connecting");
    wait(4);
    CHECK(wifi.link().link == net::Link::Idle && wifi.last_failure() == net::Failure::WrongPassword);
    CHECK(wifi.saved_count() == 0);
    save("net-08-wrong-password");
    key(wm::key::Enter);
    CHECK(title() == "Network");

    // The right one: connected, remembered, the clock set from the network.
    key(wm::key::Enter);
    CHECK(title() == "Connect to network");
    desktop.text("semillas-y-heno");
    key(wm::key::Enter);
    wait(5);
    CHECK(wifi.link().link == net::Link::Connected && wifi.is_saved("Casa Chinchilla"));
    CHECK(radio.last_password == "semillas-y-heno");
    int h, m, s;
    desktop.get_time(h, m, s);
    CHECK(h == 16 && m == 11);
    save("net-09-connected");

    // The button now disconnects; the network stays remembered.
    alt('d');
    wait(1);
    CHECK(wifi.link().link == net::Link::Idle && wifi.is_saved("Casa Chinchilla"));
    save("net-10-disconnected");
    // A remembered network connects without asking again.
    key(wm::key::Enter);
    wait(5);
    CHECK(title() == "Network" && wifi.link().link == net::Link::Connected);

    // An open network with '&' in its name; a network that needs a user name.
    alt('n'); // back to the list
    key(wm::key::Down);
    key(wm::key::Enter);
    wait(5);
    CHECK(std::string(wifi.link().ssid) == "Cafe & Churros");
    save("net-11-open-network");
    key(wm::key::End);
    key(wm::key::Enter);
    CHECK(radio.connects == 4);
    save("net-12-enterprise");
    key(wm::key::Enter);

    // Forget: the open network is dropped and disconnected.
    key(wm::key::Home);
    alt('f');
    wait(1);
    CHECK(!wifi.is_saved("Cafe & Churros") && wifi.link().link == net::Link::Idle);

    // A network that is not in the list.
    alt('o');
    CHECK(title() == "Other network");
    desktop.text("Oculta");
    alt('p');
    desktop.text("contraseña");
    save("net-13-other-network");
    key(wm::key::Enter);
    wait(5);
    CHECK(wifi.last_failure() == net::Failure::NotFound);
    key(wm::key::Enter);

    // Closed with Escape; Start > Settings > Network opens it again.
    key(wm::key::Escape);
    CHECK(title() == "");
    const gfx::Rect start = shell::taskbar()->button(-1);
    click(start.x + 10, start.y + 10);
    gfx::Rect item = desktop.popup_item(0, 1);
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(item.x + 20), static_cast<int16_t>(item.y + 5), 0, 0});
    clock_.now += wm::Wm::kSubmenuDelayUs;
    desktop.tick();
    save("net-14-settings-menu");
    item = desktop.popup_item(1, 2);
    click(item.x + 20, item.y + 5);
    CHECK(title() == "Network");

    if (failures == 0) {
        printf("network scenes: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
