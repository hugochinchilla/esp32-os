// The Bluetooth program against a pretend radio, step by step: checks what
// the manager and the window do, and writes each screen for
// tools/check_pictures.py (reference pictures bt-*.png).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/desk/desk.hpp"
#include "core/fs/ini.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/gfx/art.hpp"
#include "core/gfx/theme.hpp"
#include "core/init/init.hpp"
#include "core/input/bluetooth.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../fake_bluetooth.hpp"
#include "../host_adapters.hpp"

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
wm::Wm desktop(display, pointer, clock_);
host::FakeBluetooth radio;
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
        printf("usage: render_bt_scenes <folder>\n");
        return 2;
    }
    folder = argv[1];

    static host::HostMemory memory;
    static fs::Storage storage;
    static fs::MemoryVolume ram_disk(memory.allocate(2 << 20), 2 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    static bt::Manager bluetooth(radio, &storage);
    const auto file = [&]() {
        char text[fs::Ini::kMaxText + 1] = "";
        size_t got = 0;
        storage.read_file("C:\\.config\\bluetooth.ini", text, fs::Ini::kMaxText, got);
        return std::string(text, got);
    };

    shell::set_bluetooth(&bluetooth);
    static init::Init first(proc::table()); // PID 1, as on the board; the desktop is its child
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_animations(false);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(4);
    // Nothing connected: it listens thinly for a device that calls.
    CHECK(bluetooth.on() && radio.listening == bt::Listen::Background);
    CHECK(!shell::taskbar()->tray_icon_rect(1).empty()); // its icon is in the tray

    // Start > Settings > Bluetooth.
    const gfx::Rect start = shell::taskbar()->button(-1);
    click(start.x + 10, start.y + 10);
    gfx::Rect item = desktop.popup_item(0, 1);
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(item.x + 20), static_cast<int16_t>(item.y + 5), 0, 0});
    clock_.now += wm::Wm::kSubmenuDelayUs;
    desktop.tick();
    save("bt-01-settings-menu");
    item = desktop.popup_item(1, 3);
    click(item.x + 20, item.y + 5);
    CHECK(title() == "Bluetooth");
    wait(2);
    // Open, it listens hard; nothing is in range yet.
    CHECK(radio.listening == bt::Listen::Search);
    save("bt-02-empty");

    // Two devices not in pairing mode, a thermometer (not listed), and one
    // that gives no name.
    radio.advertise(0x01, "Teclado Chinchilla", hid::kKeyboard, false);
    radio.advertise(0x02, "Raton sin cola", hid::kMouse, false);
    radio.advertise(0x03, "Termometro", 0, false, false);
    radio.advertise(0x04, "", 0, false);
    wait(2);
    CHECK(radio.connects == 0);
    save("bt-03-in-range");

    // The keyboard picked, and Pair: connecting, then connected and remembered.
    alt('d');
    key(wm::key::Down);
    save("bt-04-picked");
    alt('p');
    CHECK(radio.connects == 1 && radio.link(0x01) != nullptr);
    wait(2);
    save("bt-05-connecting");
    radio.ready(0x01, hid::kKeyboard);
    wait(2);
    CHECK(bluetooth.remembered_count() == 1 && bluetooth.kinds_ready() == hid::kKeyboard);
    save("bt-06-keyboard-connected");

    // The mouse goes into pairing mode: taken by itself, a mouse is missing.
    radio.advertise(0x02, "Raton sin cola", hid::kMouse, true);
    wait(2);
    CHECK(radio.connects == 2);
    radio.ready(0x02, hid::kMouse);
    wait(2);
    CHECK(bluetooth.kinds_ready() == (hid::kKeyboard | hid::kMouse));
    save("bt-07-both");

    // The keyboard sleeps: remembered, not connected. A pairing that fails
    // is told in a box.
    radio.drop(0x01);
    wait(2);
    save("bt-08-keyboard-asleep");
    alt('d');
    key(wm::key::End); // the nameless one, last
    alt('p');
    CHECK(radio.connects == 3);
    wait(2);
    radio.drop(0x04);
    wait(2);
    CHECK(title() == "Bluetooth" && desktop.window_count() == 2);
    save("bt-09-could-not-pair");
    key(wm::key::Enter);
    CHECK(desktop.window_count() == 1);

    // Forget the mouse: gone from the list (it advertises no more) and
    // from the file.
    alt('d');
    key(wm::key::Home);
    CHECK(file().find("mouse Raton sin cola") != std::string::npos);
    alt('f');
    wait(2);
    CHECK(radio.forgets == 1 && bluetooth.remembered_count() == 1);
    CHECK(file().find("Raton sin cola") == std::string::npos);
    save("bt-10-forgotten");

    // The two boxes: no pairing by itself; off from the next start.
    alt('k');
    CHECK(!bluetooth.auto_pair() && file().find("auto_pair = false") != std::string::npos);
    alt('b');
    CHECK(file().find("enabled = false") != std::string::npos);
    save("bt-11-switched-off");
    alt('b');
    alt('k');
    CHECK(bluetooth.auto_pair() && file().find("enabled = true") != std::string::npos);

    // In every theme.
    for (int theme = 1; theme < gfx::theme_count(); theme++) {
        desktop.set_theme(theme);
        wait(1); // the tray's icon takes the colour that shows on this bar
        save((std::string("bt-12-") + gfx::theme_at(theme).key).c_str());
    }
    desktop.set_theme(0);
    wait(1);

    // Closed: it listens thinly again (the keyboard is still away).
    key(wm::key::Escape);
    wait(2);
    CHECK(title() == "" && radio.listening == bt::Listen::Background);

    // A board whose Bluetooth is off: the window says so.
    radio.off = true;
    wait(2);
    shell::open_bluetooth();
    wait(2);
    save("bt-13-radio-off");
    key(wm::key::Escape);

    // The tray: a cross while it is off. On again, and beside the
    // network's icon (put there by hand: this test has no Wi-Fi), a click
    // on it opens the window.
    CHECK(!shell::taskbar()->tray_icon_rect(1).empty() && shell::taskbar()->tray_icon_rect(0).empty());
    save("bt-14-tray-off");
    radio.off = false;
    wait(2);
    shell::taskbar()->set_tray_icon(0, &gfx::signal_icon_16(3));
    const gfx::Rect tray = shell::taskbar()->tray_icon_rect(1);
    CHECK(tray.x + tray.w == shell::taskbar()->tray_icon_rect(0).x);
    click(tray.x + 8, tray.y + 8);
    CHECK(title() == "Bluetooth");
    save("bt-15-tray-click");
    key(wm::key::Escape);
    CHECK(title() == "");

    if (failures == 0) {
        printf("bluetooth scenes: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
