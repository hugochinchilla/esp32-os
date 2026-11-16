// Tests of the Wi-Fi manager against a pretend radio: remembering networks,
// joining the strongest one, wrong passwords, lost links, and the clock.
#include <cstdio>
#include <cstring>
#include <string>

#include "core/net/wifi.hpp"
#include "../fake_wifi.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

uint64_t now = 0;

// A quarter of a second per tick, as the shell does.
void run(net::Manager &m, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        now += 250000;
        m.tick(now);
    }
}

void test_time()
{
    const net::TimeZone madrid = net::kDefaultZone, utc = {0, false};
    // 2026-11-16 15:11:10 UTC, a Monday.
    net::LocalTime t = net::local_time(1794841870, utc);
    CHECK(t.year == 2026 && t.month == 11 && t.day == 16);
    CHECK(t.hour == 15 && t.minute == 11 && t.second == 10 && t.weekday == 1);
    t = net::local_time(1794841870, madrid);
    CHECK(t.hour == 16 && !t.summer);
    // Summer time starts 2026-03-29 01:00 UTC and ends 2026-10-25 01:00 UTC.
    t = net::local_time(1774745999, madrid); // 00:59:59 UTC
    CHECK(t.hour == 1 && t.minute == 59 && !t.summer && t.day == 29 && t.month == 3);
    t = net::local_time(1774746000, madrid);
    CHECK(t.hour == 3 && t.minute == 0 && t.summer);
    t = net::local_time(1792889999, madrid);
    CHECK(t.hour == 2 && t.minute == 59 && t.summer && t.day == 25 && t.month == 10);
    t = net::local_time(1792890000, madrid);
    CHECK(t.hour == 2 && t.minute == 0 && !t.summer);
    // Midnight rolls the date over in local time; a leap day.
    t = net::local_time(1709247600, madrid); // 2024-02-29 23:00 UTC
    CHECK(t.year == 2024 && t.month == 3 && t.day == 1 && t.hour == 0 && t.weekday == 5);
    t = net::local_time(0, utc);
    CHECK(t.year == 1970 && t.month == 1 && t.day == 1 && t.weekday == 4);
    const net::TimeZone west = {-300, false};
    t = net::local_time(3600, west);
    CHECK(t.year == 1969 && t.month == 12 && t.day == 31 && t.hour == 20);

    CHECK(net::bars(-40) == 4 && net::bars(-60) == 3 && net::bars(-70) == 2);
    CHECK(net::bars(-85) == 1 && net::bars(-95) == 0);
}

void test_connect_and_remember()
{
    host::FakeWifi radio;
    host::MemoryKeyStore store;
    radio.add("Attic", -80, "attic-pass");
    radio.add("Home", -50, "home-pass");
    radio.add("Home", -70, "home-pass", 11); // a second access point
    radio.add("Cafe", -60, "");
    radio.add("", -40, "hidden");

    net::Manager m(radio, store);
    m.start(now);
    CHECK(m.scanning());
    run(m, 4);
    // One entry per name, strongest first; nothing remembered, so no join.
    CHECK(!m.scanning() && m.seen_count() == 3);
    CHECK(std::string(m.seen(0).network.ssid) == "Home" && m.seen(0).network.rssi == -50);
    CHECK(std::string(m.seen(1).network.ssid) == "Cafe");
    CHECK(m.seen(1).network.security == net::Security::Open);
    CHECK(std::string(m.seen(2).network.ssid) == "Attic");
    CHECK(m.link().link == net::Link::Idle && radio.connects == 0);

    // A wrong password: the reason is kept, nothing is remembered.
    m.connect("Home", "wrong", true);
    CHECK(m.link().link == net::Link::Connecting);
    run(m, 6);
    CHECK(m.link().link == net::Link::Idle);
    CHECK(m.last_failure() == net::Failure::WrongPassword);
    CHECK(m.saved_count() == 0 && store.saves == 0);
    run(m, 200);
    CHECK(radio.connects == 1); // and it does not keep trying

    // The right one: connected, remembered, shown first.
    const uint32_t changes = m.changes();
    m.connect("Home", "home-pass", true);
    run(m, 6);
    CHECK(m.link().link == net::Link::Connected && m.changes() != changes);
    CHECK(m.last_failure() == net::Failure::None);
    CHECK(m.saved_count() == 1 && m.is_saved("Home") && store.saves == 1);
    CHECK(m.seen(0).connected && m.seen(0).saved);
    CHECK(m.link().address[0] == 192 && m.link().rssi == -50);

    // Not remembered when asked not to.
    m.connect("Cafe", "", false);
    run(m, 6);
    CHECK(m.link().link == net::Link::Connected && std::string(m.link().ssid) == "Cafe");
    CHECK(!m.is_saved("Cafe") && m.saved_count() == 1);
    CHECK(std::string(m.seen(0).network.ssid) == "Cafe" && m.seen(0).connected);

    // The remembered password is used when none is given.
    m.connect("Home", nullptr, true);
    run(m, 6);
    CHECK(m.link().link == net::Link::Connected && radio.last_password == "home-pass");

    // Disconnect stays disconnected.
    m.disconnect();
    CHECK(m.link().link == net::Link::Idle);
    const int connects = radio.connects;
    run(m, 400);
    CHECK(radio.connects == connects && m.link().link == net::Link::Idle);

    // A new manager on the same store (a restart) joins by itself.
    host::FakeWifi radio2;
    radio2.air = radio.air;
    net::Manager again(radio2, store);
    again.start(now);
    run(again, 10);
    CHECK(again.link().link == net::Link::Connected && std::string(again.link().ssid) == "Home");

    // Forgetting the connected network disconnects and stores the change.
    again.forget("Home");
    CHECK(again.saved_count() == 0 && again.link().link == net::Link::Idle);
    host::FakeWifi radio3;
    radio3.air = radio.air;
    net::Manager third(radio3, store);
    third.start(now);
    run(third, 40);
    CHECK(third.saved_count() == 0 && radio3.connects == 0);
}

void test_auto_join()
{
    host::FakeWifi radio;
    host::MemoryKeyStore store;
    radio.add("Weak", -85, "weak-pass");
    radio.add("Strong", -45, "strong-pass");
    radio.add("Stranger", -30, "x");
    {
        net::Manager setup(radio, store);
        setup.start(now);
        run(setup, 4);
        setup.connect("Weak", "weak-pass", true);
        run(setup, 6);
        setup.connect("Strong", "old-pass", true); // fails, not kept
        run(setup, 6);
        radio.air[1].password = "old-pass";
        setup.connect("Strong", "old-pass", true);
        run(setup, 6);
        CHECK(setup.saved_count() == 2 && std::string(setup.saved_ssid(0)) == "Strong");
        radio.disconnect();
    }
    // The strongest remembered network is tried first; its password has
    // changed, so the next one is joined.
    radio.air[1].password = "strong-pass";
    radio.connects = 0;
    net::Manager m(radio, store);
    m.start(now);
    run(m, 14);
    CHECK(radio.connects == 2);
    CHECK(m.link().link == net::Link::Connected && std::string(m.link().ssid) == "Weak");
    CHECK(m.last_failure() == net::Failure::None); // nobody asked: nothing to report
    CHECK(m.saved_count() == 2);                   // and nothing is forgotten

    // The link drops and the network is gone: it looks again, with longer
    // and longer waits, and comes back when the network does.
    radio.remove("Weak");
    radio.remove("Strong");
    radio.drop();
    int scans = radio.scans;
    run(m, 4);
    CHECK(m.link().link == net::Link::Idle && radio.scans == scans + 1);
    run(m, 4 * 9);
    CHECK(radio.scans == scans + 1); // not yet: 10 s
    run(m, 4 * 3);
    CHECK(radio.scans == scans + 2);
    run(m, 4 * 22);
    CHECK(radio.scans == scans + 3); // 20 s later
    radio.add("Weak", -70, "weak-pass");
    run(m, 4 * 45);
    CHECK(m.link().link == net::Link::Connected && std::string(m.link().ssid) == "Weak");

    // A radio that never answers a connect: given up after 20 s.
    radio.connect_polls = 1000000;
    m.connect("Stranger", "x", true);
    run(m, 4 * 19);
    CHECK(m.link().link == net::Link::Connecting);
    run(m, 4 * 3);
    CHECK(m.link().link == net::Link::Idle && m.last_failure() == net::Failure::TimedOut);
    CHECK(!m.is_saved("Stranger"));
}

void test_storage()
{
    host::FakeWifi radio;
    host::MemoryKeyStore store;
    net::Manager m(radio, store);
    m.start(now);
    // Nine networks: the oldest drops off; the longest names and passwords fit.
    const std::string long_ssid(32, 'S'), long_pass(64, 'f');
    radio.add(long_ssid.c_str(), -50, long_pass.c_str());
    m.connect(long_ssid.c_str(), long_pass.c_str(), true);
    run(m, 6);
    for (int i = 0; i < 8; i++) {
        const std::string name = "net" + std::to_string(i);
        radio.add(name.c_str(), -60, "password");
        m.connect(name.c_str(), "password", true);
        run(m, 6);
    }
    CHECK(m.saved_count() == 8 && !m.is_saved(long_ssid.c_str()) && m.is_saved("net0"));
    CHECK(std::string(m.saved_ssid(0)) == "net7");
    m.connect(long_ssid.c_str(), long_pass.c_str(), true);
    run(m, 6);
    CHECK(m.is_saved(long_ssid.c_str()) && !m.is_saved("net0"));

    uint8_t blob[net::Manager::kBlobMax];
    const size_t size = m.pack(blob, sizeof(blob));
    CHECK(size > 0 && size <= sizeof(blob));
    net::Manager other(radio, store);
    CHECK(other.unpack(blob, size) && other.saved_count() == 8);
    CHECK(std::string(other.saved_ssid(0)) == long_ssid);
    // Damaged data is refused whole, wherever it is cut or changed.
    for (size_t cut = 0; cut < size; cut++) {
        net::Manager fresh(radio, store);
        CHECK(!fresh.unpack(blob, cut) || cut == 2);
        CHECK(fresh.saved_count() == 0 || cut == 2);
    }
    for (size_t i = 0; i < size; i++) {
        uint8_t copy[net::Manager::kBlobMax];
        memcpy(copy, blob, size);
        copy[i] = 0xFF;
        net::Manager fresh(radio, store);
        fresh.unpack(copy, size); // must not read outside (sanitizers check)
    }
    blob[0] = 2; // a version from the future
    CHECK(!other.unpack(blob, size));
}

net::LocalTime shown;
int times_set = 0;

void test_clock()
{
    host::FakeWifi radio;
    host::MemoryKeyStore store;
    radio.add("Home", -50, "pw");
    net::Manager m(radio, store);
    m.set_on_time([](const net::LocalTime &t, void *) {
        shown = t;
        times_set++;
    }, nullptr);
    m.start(now);
    run(m, 4);
    CHECK(times_set == 0 && !m.time_known());
    m.connect("Home", "pw", false);
    run(m, 6);
    CHECK(times_set == 0); // connected, but no answer from the time server yet
    radio.utc = 1794841870;
    radio.has_time = true;
    run(m, 1);
    CHECK(times_set == 1 && m.time_known() && shown.hour == 16 && shown.minute == 11);
    run(m, 4 * 3599);
    CHECK(times_set == 1);
    radio.utc += 3600;
    run(m, 8);
    CHECK(times_set == 2 && shown.hour == 17); // set again every hour
    m.set_zone({0, false});
    run(m, 4 * 3601);
    CHECK(times_set == 3 && shown.hour == 16);
}

void test_no_radio()
{
    host::FakeWifi radio;
    host::MemoryKeyStore store;
    radio.off = true;
    net::Manager m(radio, store);
    m.start(now);
    run(m, 20);
    CHECK(m.link().link == net::Link::Off && radio.scans == 0 && !m.scanning());
    m.scan();
    CHECK(radio.scans == 0);
    radio.off = false; // it came up late
    run(m, 1);
    CHECK(radio.scans == 1);
}

} // namespace

int main()
{
    test_time();
    test_connect_and_remember();
    test_auto_join();
    test_storage();
    test_clock();
    test_no_radio();
    if (failures == 0) {
        printf("net: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
