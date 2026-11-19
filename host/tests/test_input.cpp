// Keyboards and mice: HID report descriptors read and reports turned into
// key and mouse events (core/input/hid.hpp), and the Bluetooth manager
// with a pretend radio: pairing by itself, remembering, coming back,
// failures and forgetting (core/input/bluetooth.hpp).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/input/bluetooth.hpp"
#include "core/input/hid.hpp"
#include "../fake_bluetooth.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

// What arrived, as text: "+04" a key down, "-04" up, "m dx,dy b w p".
class Recorder : public ports::InputSink {
public:
    std::vector<std::string> seen;
    void key(uint16_t usage, bool pressed) override
    {
        char line[16];
        snprintf(line, sizeof(line), "%c%02X", pressed ? '+' : '-', usage);
        seen.push_back(line);
    }
    void mouse(const ports::MouseEvent &e) override
    {
        char line[48];
        snprintf(line, sizeof(line), "m %d,%d %d %d %d%s", e.x, e.y, e.buttons, e.wheel, e.wheel_x, e.absolute ? " abs" : "");
        seen.push_back(line);
    }
    std::string take()
    {
        std::string all;
        for (const std::string &s : seen) {
            all += (all.empty() ? "" : " ") + s;
        }
        seen.clear();
        return all;
    }
};

/* ---- Descriptors ------------------------------------------------------------------ */

// The keyboard every boot-protocol keyboard has: eight modifier bits, a
// spare byte, six keys held; five lights the other way.
const uint8_t kKeyboard[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
};

// A wireless mouse as they come: report 2, five buttons, X and Y of twelve
// bits each, the wheel, and the wheel sideways (a consumer usage of two
// bytes).
const uint8_t kMouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03,
    0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C,
    0x95, 0x02, 0x81, 0x06, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x95, 0x01, 0x81, 0x06, 0xC0, 0xC0,
};

// A keyboard that reports every key as a bit (report 1): the modifiers,
// then usages 0x04 to 0x6B.
const uint8_t kEveryKey[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x19, 0x04, 0x29, 0x6B, 0x95, 0x68, 0x75, 0x01,
    0x81, 0x02, 0xC0,
};

// The simplest mouse: three buttons, X, Y and the wheel as one item of
// three values with three usages; no report numbers.
const uint8_t kPlainMouse[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03,
    0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
    0x81, 0x06, 0xC0, 0xC0,
};

void test_keyboard()
{
    hid::Decoder d;
    Recorder sink;
    CHECK(d.set_layout(kKeyboard, sizeof(kKeyboard)));
    CHECK(d.layout().kinds() == hid::kKeyboard && !d.layout().numbered());
    CHECK(d.layout().field_count() == 2);

    const uint8_t a[] = {0, 0, 0x04, 0, 0, 0, 0, 0};
    d.report(0, a, sizeof(a), sink);
    CHECK(sink.take() == "+04");
    d.report(0, a, sizeof(a), sink); // said again: nothing new
    CHECK(sink.take() == "");
    // A let go, Shift and B down in one report: the letter goes first,
    // then Shift before B.
    const uint8_t shift_b[] = {0x02, 0, 0x05, 0, 0, 0, 0, 0};
    d.report(0, shift_b, sizeof(shift_b), sink);
    CHECK(sink.take() == "-04 +E1 +05");
    // Shift and B let go together: the letter first.
    const uint8_t none[] = {0, 0, 0, 0, 0, 0, 0, 0};
    d.report(0, none, sizeof(none), sink);
    CHECK(sink.take() == "-05 -E1");
    // Too many keys: the keyboard fills the report with 1s and nothing changes.
    d.report(0, a, sizeof(a), sink);
    const uint8_t over[] = {0, 0, 1, 1, 1, 1, 1, 1};
    d.report(0, over, sizeof(over), sink);
    CHECK(sink.take() == "+04");
    // Several at once, and a report cut short counts for what it has.
    const uint8_t three[] = {0x05, 0, 0x04, 0x2C, 0x52};
    d.report(0, three, sizeof(three), sink);
    CHECK(sink.take() == "+E0 +E2 +2C +52");
    // Gone: everything it held is let go, modifiers last.
    d.release_all(sink);
    CHECK(sink.take() == "-04 -2C -52 -E0 -E2");
    d.release_all(sink);
    CHECK(sink.take() == "");
    // A report of another number is not this keyboard's.
    d.report(3, a, sizeof(a), sink);
    CHECK(sink.take() == "");
}

void test_every_key()
{
    hid::Decoder d;
    Recorder sink;
    CHECK(d.set_layout(kEveryKey, sizeof(kEveryKey)));
    CHECK(d.layout().kinds() == hid::kKeyboard && d.layout().numbered());
    uint8_t r[14] = {};
    r[0] = 0x01; // left Ctrl
    r[1] = 0x01; // usage 4, A
    r[13] = 0x80; // the last bit: usage 0x6B
    d.report(1, r, sizeof(r), sink);
    CHECK(sink.take() == "+E0 +04 +6B");
    r[1] = 0;
    d.report(1, r, sizeof(r), sink);
    CHECK(sink.take() == "-04");
}

void test_mouse()
{
    hid::Decoder d;
    Recorder sink;
    CHECK(d.set_layout(kMouse, sizeof(kMouse)));
    CHECK(d.layout().kinds() == hid::kMouse && d.layout().numbered());
    // Left button, 5 to the right, 3 up (-3 in twelve bits), no wheel.
    const uint8_t move[] = {0x01, 0x05, 0xD0, 0xFF, 0x00, 0x00};
    d.report(2, move, sizeof(move), sink);
    CHECK(sink.take() == "m 5,-3 1 0 0");
    // The far ends of twelve bits.
    const uint8_t far[] = {0x01, 0xFF, 0x17, 0x80, 0x00, 0x00}; // x 0x7FF, y 0x801
    d.report(2, far, sizeof(far), sink);
    CHECK(sink.take() == "m 2047,-2047 1 0 0");
    // Nothing moved and the same buttons: no event. The button let go: one.
    const uint8_t held[] = {0x01, 0, 0, 0, 0, 0};
    d.report(2, held, sizeof(held), sink);
    CHECK(sink.take() == "");
    const uint8_t idle[] = {0, 0, 0, 0, 0, 0};
    d.report(2, idle, sizeof(idle), sink);
    CHECK(sink.take() == "m 0,0 0 0 0");
    // The wheel towards the user, and sideways to the right; the right and
    // middle buttons; the fourth and fifth are not ours.
    const uint8_t wheel[] = {0x1E, 0, 0, 0, 0xFF, 0x02};
    d.report(2, wheel, sizeof(wheel), sink);
    CHECK(sink.take() == "m 0,0 6 -1 2");
    d.release_all(sink);
    CHECK(sink.take() == "m 0,0 0 0 0");
    // Not its report number.
    d.report(0, move, sizeof(move), sink);
    d.report(1, move, sizeof(move), sink);
    CHECK(sink.take() == "");

    hid::Decoder plain;
    CHECK(plain.set_layout(kPlainMouse, sizeof(kPlainMouse)));
    CHECK(plain.layout().kinds() == hid::kMouse && !plain.layout().numbered());
    const uint8_t p[] = {0x04, 0xFE, 0x7F, 0x01};
    plain.report(0, p, sizeof(p), sink);
    CHECK(sink.take() == "m -2,127 4 1 0");
}

void test_both_and_bad()
{
    // A keyboard and a mouse in one descriptor (a keyboard with a trackpad).
    std::vector<uint8_t> both(kEveryKey, kEveryKey + sizeof(kEveryKey));
    both.insert(both.end(), kMouse, kMouse + sizeof(kMouse));
    hid::Decoder d;
    Recorder sink;
    CHECK(d.set_layout(both.data(), both.size()));
    CHECK(d.layout().kinds() == (hid::kKeyboard | hid::kMouse));
    const uint8_t move[] = {0x00, 0x01, 0x10, 0x00, 0x00, 0x00};
    d.report(2, move, sizeof(move), sink);
    uint8_t keys[14] = {0, 0x02};
    d.report(1, keys, sizeof(keys), sink);
    CHECK(sink.take() == "m 1,1 0 0 0 +05");

    // Nothing of ours in it, nothing at all, and every cut of a good one:
    // no crash (the sanitizers watch), and never more fields than whole.
    const uint8_t lights[] = {0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x75, 0x01, 0x95, 0x05, 0x91, 0x02};
    hid::Layout layout;
    CHECK(!layout.parse(lights, sizeof(lights)));
    CHECK(!layout.parse(nullptr, 0));
    for (size_t cut = 0; cut <= both.size(); cut++) {
        std::vector<uint8_t> part(both.begin(), both.begin() + static_cast<long>(cut));
        layout.parse(part.data(), part.size());
        CHECK(layout.field_count() <= d.layout().field_count());
    }
    // Sizes no report could have, and a long item.
    const uint8_t odd[] = {0xFE, 0x02, 0x00, 0xAA, 0xBB, 0x05, 0x07, 0x19, 0x04, 0x29, 0x08, 0x75,
                           0x40, 0x95, 0xFF, 0x81, 0x02, 0x19, 0x04, 0x29, 0x08, 0x75, 0x01, 0x95, 0x05, 0x81, 0x02};
    CHECK(layout.parse(odd, sizeof(odd)) && layout.field_count() == 1);
    hid::Decoder o;
    CHECK(o.set_layout(odd, sizeof(odd)));
    const uint8_t empty[1] = {0};
    o.report(0, empty, 0, sink);
    o.report(0, empty, 1, sink);
    CHECK(sink.take() == "");
}

/* ---- Addresses -------------------------------------------------------------------- */

void test_addresses()
{
    bt::Address a = {};
    char text[bt::kAddressText];
    CHECK(bt::parse("c4:9d:ED:01:02:03 random", a) == 24 && a.random && a.bytes[0] == 0xC4 && a.bytes[5] == 0x03);
    bt::format(a, text);
    CHECK(strcmp(text, "c4:9d:ed:01:02:03 random") == 0);
    CHECK(bt::parse("00:11:22:33:44:55", a) == 17 && !a.random);
    CHECK(bt::parse("00:11:22:33:44:55 public mouse M", a) == 24 && !a.random);
    bt::format(a, text);
    CHECK(strcmp(text, "00:11:22:33:44:55 public") == 0);
    CHECK(bt::parse("00:11:22:33:44:55 keyboard K", a) == 17);
    CHECK(bt::parse("", a) == 0 && bt::parse("00:11:22:33:44", a) == 0 && bt::parse("00:11:22:33:44:5", a) == 0);
    CHECK(bt::parse("00-11-22-33-44-55", a) == 0 && bt::parse("00:11:22:33:44:55x", a) == 0);
    CHECK(strcmp(bt::kinds_text(3), "keyboard+mouse") == 0 && strcmp(bt::kinds_text(0), "unknown") == 0);
}

/* ---- The manager ------------------------------------------------------------------ */

constexpr uint64_t kSecond = 1000000;
constexpr const char *kPath = "C:\\.config\\bluetooth.ini";

struct Disk {
    std::vector<uint8_t> pool = std::vector<uint8_t>(256 * 1024);
    fs::MemoryVolume volume{pool.data(), pool.size(), "RAM disk"};
    fs::Storage storage;
    Disk()
    {
        storage.mount('C', volume);
        storage.make_folder("C:\\.config");
    }
    std::string file()
    {
        char text[fs::Ini::kMaxText + 1] = "";
        size_t got = 0;
        storage.read_file(kPath, text, fs::Ini::kMaxText, got);
        return std::string(text, got);
    }
};

void test_pairs_by_itself()
{
    Disk disk;
    host::FakeBluetooth radio;
    bt::Manager m(radio, &disk.storage);
    uint64_t now = kSecond;
    m.start(now);
    // Nothing connected: it listens.
    CHECK(m.on() && m.scanning() && radio.scanning && m.auto_pair());
    CHECK(radio.listening == bt::Listen::Background); // thinly: nobody is waiting
    // Not in pairing mode, or not a keyboard or mouse: left alone.
    radio.advertise(0x10, "Somebody's keyboard", hid::kKeyboard, false);
    radio.advertise(0x11, "A thermometer", 0, true, false);
    m.tick(now += kSecond);
    CHECK(radio.connects == 0 && m.found_count() == 2 && m.link_count() == 0);
    // A keyboard in pairing mode: connected without being asked.
    radio.advertise(0x01, "Keys", hid::kKeyboard);
    const uint32_t before = m.changes();
    m.tick(now += kSecond);
    CHECK(radio.connects == 1 && radio.link(0x01) != nullptr);
    m.tick(now += kSecond);
    CHECK(m.link_count() == 1 && m.link(0).state == bt::LinkState::Connecting && m.changes() != before);
    // One at a time: the mouse waits until the keyboard is done.
    radio.advertise(0x02, "Mouse", hid::kMouse);
    m.tick(now += kSecond);
    CHECK(radio.connects == 1);
    CHECK(m.remembered_count() == 0); // not remembered until it works
    radio.ready(0x01, hid::kKeyboard);
    m.tick(now += kSecond);
    CHECK(m.remembered_count() == 1 && strcmp(m.remembered(0).name, "Keys") == 0);
    CHECK(m.kinds_ready() == hid::kKeyboard);
    CHECK(disk.file().find("1 = c4:9d:ed:00:00:01 random keyboard Keys") != std::string::npos);
    CHECK(radio.connects == 2 && radio.link(0x02) != nullptr); // now the mouse
    // A second keyboard in pairing mode is not ours to take.
    radio.advertise(0x03, "Other keys", hid::kKeyboard);
    radio.ready(0x02, hid::kMouse);
    m.tick(now += kSecond);
    m.tick(now += kSecond);
    CHECK(radio.connects == 2 && m.remembered_count() == 2 && m.kinds_ready() == 3);
    CHECK(strcmp(m.remembered(0).name, "Mouse") == 0); // the latest first
    // Both there and nobody looking: it stops listening.
    CHECK(!m.scanning() && !radio.scanning && m.found_count() == 0);
    // A list on show listens again; asked for by name, the third connects.
    m.set_listing(true);
    m.tick(now += kSecond);
    CHECK(m.scanning() && m.found_count() == 3 && radio.listening == bt::Listen::Search);
    CHECK(m.pair(host::FakeBluetooth::address(0x03)));
    CHECK(!m.pair(host::FakeBluetooth::address(0x10))); // one attempt at a time
    radio.ready(0x03, hid::kKeyboard);
    m.tick(now += kSecond);
    CHECK(m.link_count() == 3 && m.remembered_count() == 3);
    // Three is all: no more listening, list or not.
    CHECK(!m.scanning());
    m.set_listing(false);

    // The keyboard goes to sleep: listening again, and when it calls (not
    // in pairing mode now) it is connected again.
    radio.drop(0x01);
    m.tick(now += kSecond);
    CHECK(m.link_count() == 2 && m.scanning() && radio.listening == bt::Listen::Background);
    radio.advertise(0x01, "Keys", hid::kKeyboard, false);
    m.tick(now += kSecond);
    CHECK(radio.connects == 4 && radio.link(0x01) != nullptr);
    radio.ready(0x01, hid::kKeyboard);
    m.tick(now += kSecond);
    CHECK(m.remembered_count() == 3 && strcmp(m.remembered(0).name, "Other keys") == 0); // order unchanged

    // Another start: the same devices are known.
    bt::Manager again(radio, &disk.storage);
    again.start(now);
    CHECK(again.remembered_count() == 3 && strcmp(again.remembered(2).name, "Keys") == 0);
    CHECK(again.remembered(2).kinds == hid::kKeyboard && again.remembered(1).kinds == hid::kMouse);
    CHECK(again.is_remembered(host::FakeBluetooth::address(0x02)) && again.link_count() == 3);
}

void test_failures()
{
    Disk disk;
    host::FakeBluetooth radio;
    bt::Manager m(radio, &disk.storage);
    uint64_t now = kSecond;
    m.start(now);
    // The attempt dies on the way: left alone for 30 seconds, then 60.
    radio.advertise(0x01, "Keys", hid::kKeyboard);
    m.tick(now += kSecond);
    m.tick(now += kSecond);
    CHECK(radio.connects == 1 && m.link_count() == 1);
    radio.drop(0x01);
    radio.advertise(0x01, "Keys", hid::kKeyboard);
    m.tick(now += kSecond);
    m.tick(now += kSecond);
    CHECK(radio.connects == 1 && m.link_count() == 0 && m.remembered_count() == 0);
    m.tick(now += 29 * kSecond);
    CHECK(radio.connects == 2);
    radio.drop(0x01);
    radio.advertise(0x01, "Keys", hid::kKeyboard);
    m.tick(now += kSecond);
    m.tick(now += 40 * kSecond);
    CHECK(radio.connects == 2);
    m.tick(now += 21 * kSecond);
    CHECK(radio.connects == 3);
    // Stuck half way: given up after 30 seconds.
    m.tick(now += kSecond);
    m.tick(now += 28 * kSecond);
    CHECK(radio.disconnects == 0);
    m.tick(now += 2 * kSecond);
    CHECK(radio.disconnects == 1);
    m.tick(now += kSecond);
    CHECK(m.link_count() == 0);
    // Asked for by name, it is tried at once whatever failed before; once
    // it works, a later failure starts from 30 seconds again.
    radio.advertise(0x01, "Keys", hid::kKeyboard);
    m.tick(now += kSecond);
    CHECK(radio.connects == 3);
    CHECK(m.pair(host::FakeBluetooth::address(0x01)) && radio.connects == 4);
    radio.ready(0x01, hid::kKeyboard);
    m.tick(now += kSecond);
    CHECK(m.remembered_count() == 1);

    // The radio refuses outright.
    radio.refuse = true;
    radio.advertise(0x02, "Mouse", hid::kMouse);
    m.tick(now += kSecond);
    CHECK(radio.connects == 5);
    m.tick(now += kSecond);
    CHECK(radio.connects == 5); // avoided
    radio.refuse = false;
    m.tick(now += 30 * kSecond);
    CHECK(radio.connects == 6);
    radio.ready(0x02, hid::kMouse);
    m.tick(now += kSecond);

    // No radio: nothing, and nothing asked of it.
    radio.off = true;
    m.tick(now += kSecond);
    CHECK(!m.on() && m.link_count() == 0 && !m.scanning());
    CHECK(!m.pair(host::FakeBluetooth::address(0x05)));
}

void test_forget_and_settings()
{
    Disk disk;
    // What somebody else wrote in the file stays.
    disk.storage.write_file(kPath, "[bluetooth]\nenabled = true\n", 27);
    host::FakeBluetooth radio;
    bt::Manager m(radio, &disk.storage);
    uint64_t now = kSecond;
    m.start(now);
    radio.advertise(0x01, "Keys; #1\x01", hid::kKeyboard);
    m.tick(now += kSecond);
    radio.ready(0x01, hid::kKeyboard);
    m.tick(now += kSecond);
    CHECK(m.remembered_count() == 1 && strcmp(m.remembered(0).name, "Keys   1 ") == 0);
    CHECK(disk.file().find("enabled = true") != std::string::npos);
    // Forgotten: the keys go, the file's line empties, and although it is
    // still in pairing mode it is not taken again at once.
    m.forget(host::FakeBluetooth::address(0x01));
    CHECK(radio.forgets == 1 && m.remembered_count() == 0);
    CHECK(disk.file().find("c4:9d") == std::string::npos);
    radio.advertise(0x01, "Keys", hid::kKeyboard);
    m.tick(now += kSecond);
    m.tick(now += kSecond);
    CHECK(radio.connects == 1 && m.link_count() == 0);

    // Told not to pair by itself: only what is asked for. Kept in the file.
    m.set_auto_pair(false);
    m.tick(now += 60 * kSecond);
    CHECK(radio.connects == 1 && !m.scanning()); // nothing to listen for
    bt::Manager again(radio, &disk.storage);
    again.start(now);
    CHECK(!again.auto_pair());
    again.set_listing(true);
    again.tick(now += kSecond);
    CHECK(again.scanning() && again.found_count() == 1 && radio.connects == 1);
    CHECK(again.pair(host::FakeBluetooth::address(0x01)) && radio.connects == 2);
    radio.ready(0x01, hid::kKeyboard | hid::kMouse);
    again.tick(now += kSecond);
    CHECK(disk.file().find("1 = c4:9d:ed:00:00:01 random keyboard+mouse Keys") != std::string::npos);
    CHECK(disk.file().find("auto_pair = false") != std::string::npos);

    // A ninth device: the one paired longest ago goes, keys and all.
    Disk other;
    host::FakeBluetooth air;
    bt::Manager many(air, &other.storage);
    many.start(now);
    for (uint8_t i = 1; i <= 9; i++) {
        air.advertise(i, "Mouse", hid::kMouse);
        many.tick(now += kSecond);
        air.ready(i, hid::kMouse);
        many.tick(now += kSecond);
        air.drop(i);
        many.tick(now += kSecond);
    }
    CHECK(many.remembered_count() == 8 && air.forgets == 1);
    CHECK(bt::same(air.last_forgotten, host::FakeBluetooth::address(1)));
    CHECK(!many.is_remembered(host::FakeBluetooth::address(1)) && many.is_remembered(host::FakeBluetooth::address(9)));

    // Without a drive it still works, and remembers until the restart.
    host::FakeBluetooth bare;
    bt::Manager none(bare, nullptr);
    none.start(now);
    bare.advertise(0x01, "Keys", hid::kKeyboard);
    none.tick(now += kSecond);
    bare.ready(0x01, hid::kKeyboard);
    none.tick(now += kSecond);
    CHECK(none.remembered_count() == 1);
}

} // namespace

int main()
{
    test_keyboard();
    test_every_key();
    test_mouse();
    test_both_and_bad();
    test_addresses();
    test_pairs_by_itself();
    test_failures();
    test_forget_and_settings();
    if (failures == 0) {
        printf("input: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
