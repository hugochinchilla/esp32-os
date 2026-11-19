// A pretend Bluetooth radio for the PC: devices "in the air" that the test
// puts there, and connections the test moves along by hand.
#pragma once

#include <cstring>
#include <vector>

#include "core/input/bluetooth.hpp"

namespace host {

class FakeBluetooth : public bt::Port {
public:
    std::vector<bt::Found> air;   // who is advertising
    std::vector<bt::Link> linked; // connections, at whatever stage
    bool off = false;             // no radio
    bool refuse = false;          // connect() fails at once
    bool scanning = false;
    // links() calls until a connection becomes Ready by itself, as what it
    // advertised it was; 0: the test moves it along by hand.
    int ready_polls = 0;
    int connects = 0, disconnects = 0, forgets = 0;
    bt::Address last_forgotten = {};

    static bt::Address address(uint8_t last, bool random = true) { return {{0xC4, 0x9D, 0xED, 0x00, 0x00, last}, random}; }

    void advertise(uint8_t last, const char *name, uint8_t kinds, bool pairing = true, bool hid = true)
    {
        bt::Found f = {};
        f.address = address(last);
        snprintf(f.name, sizeof(f.name), "%s", name);
        f.rssi = -50;
        f.kinds = kinds;
        f.hid = hid;
        f.pairing = pairing;
        silence(last);
        air.push_back(f);
    }
    void silence(uint8_t last)
    {
        for (size_t i = 0; i < air.size(); i++) {
            if (bt::same(air[i].address, address(last))) {
                air.erase(air.begin() + static_cast<long>(i));
                return;
            }
        }
    }
    bt::Link *link(uint8_t last)
    {
        for (bt::Link &l : linked) {
            if (bt::same(l.address, address(last))) {
                return &l;
            }
        }
        return nullptr;
    }
    // The connection is up and its reports understood: a device that
    // connects stops advertising.
    void ready(uint8_t last, uint8_t kinds)
    {
        if (bt::Link *l = link(last)) {
            l->state = bt::LinkState::Ready;
            l->kinds = kinds;
        }
    }
    void drop(const bt::Address &a)
    {
        for (size_t i = 0; i < linked.size(); i++) {
            if (bt::same(linked[i].address, a)) {
                linked.erase(linked.begin() + static_cast<long>(i));
                return;
            }
        }
    }
    void drop(uint8_t last) { drop(address(last)); }

    bool on() override { return !off; }
    void scan(bool want) override { scanning = want; }
    int found(bt::Found *out, int max) override
    {
        int n = 0;
        for (const bt::Found &f : air) {
            if (n < max) {
                out[n++] = f;
            }
        }
        return n;
    }
    bool connect(const bt::Address &a) override
    {
        connects++;
        if (refuse) {
            return false;
        }
        bt::Link l = {};
        l.address = a;
        for (size_t i = 0; i < air.size(); i++) {
            if (bt::same(air[i].address, a)) {
                memcpy(l.name, air[i].name, sizeof(l.name));
                l.kinds = air[i].kinds;
                air.erase(air.begin() + static_cast<long>(i));
                break;
            }
        }
        l.state = bt::LinkState::Connecting;
        linked.push_back(l);
        polls_ = 0;
        return true;
    }
    void disconnect(const bt::Address &a) override
    {
        disconnects++;
        drop(a);
    }
    int links(bt::Link *out, int max) override
    {
        int n = 0;
        if (ready_polls > 0 && ++polls_ >= ready_polls) {
            for (bt::Link &l : linked) {
                l.state = bt::LinkState::Ready;
            }
        }
        for (const bt::Link &l : linked) {
            if (n < max) {
                out[n++] = l;
            }
        }
        return n;
    }
    void forget(const bt::Address &a) override
    {
        forgets++;
        last_forgotten = a;
        drop(a);
    }

private:
    int polls_ = 0;
};

} // namespace host
