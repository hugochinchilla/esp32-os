// A pretend radio and settings store for the PC: networks "in the air" with
// their passwords, and operations that finish after a number of polls, so
// tests and pictures do not depend on the speed of the computer.
#pragma once

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/net/wifi.hpp"

namespace host {

class FakeWifi : public net::WifiPort {
public:
    struct Ap {
        std::string ssid;
        int rssi;
        int channel;
        net::Security security;
        std::string password;
    };

    std::vector<Ap> air;      // what is in range
    bool off = false;         // no radio
    int scan_polls = 2;       // scanning() calls until a scan is done
    int connect_polls = 3;    // status() calls until a connect ends
    bool has_time = false;    // the network time is known once connected
    int64_t utc = 0;
    int scans = 0, connects = 0;
    std::string last_password; // what the last connect() was given
    int tx_power = 0;          // what the last set_tx_power() was given; 0: never asked

    void add(const char *ssid, int rssi, const char *password, int channel = 6)
    {
        air.push_back({ssid, rssi, channel,
                       password[0] != 0 ? net::Security::Password : net::Security::Open, password});
    }
    void remove(const char *ssid)
    {
        for (size_t i = 0; i < air.size(); i++) {
            if (air[i].ssid == ssid) {
                air.erase(air.begin() + static_cast<long>(i));
                return;
            }
        }
    }
    // The access point went away, or the link was lost.
    void drop()
    {
        if (link_ == net::Link::Connected) {
            link_ = net::Link::Idle;
            failure_ = net::Failure::None;
        }
    }

    void set_tx_power(int dbm) override { tx_power = dbm; }
    void start_scan() override
    {
        scans++;
        scan_left_ = scan_polls;
        found_ = air;
    }
    bool scanning() override
    {
        if (scan_left_ > 0) {
            scan_left_--;
            return true;
        }
        return false;
    }
    int results(net::Network *out, int max) override
    {
        int n = 0;
        for (const Ap &ap : found_) {
            if (n == max) {
                break;
            }
            out[n] = {};
            snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ap.ssid.c_str());
            out[n].rssi = static_cast<int8_t>(ap.rssi);
            out[n].channel = static_cast<uint8_t>(ap.channel);
            out[n].security = ap.security;
            n++;
        }
        return n;
    }
    void connect(const char *ssid, const char *password) override
    {
        connects++;
        ssid_ = ssid;
        last_password = password;
        link_ = net::Link::Connecting;
        failure_ = net::Failure::None;
        connect_left_ = connect_polls;
    }
    void disconnect() override
    {
        link_ = net::Link::Idle;
        failure_ = net::Failure::None;
    }
    void status(net::LinkStatus &out) override
    {
        if (link_ == net::Link::Connecting && connect_left_-- <= 0) {
            finish();
        }
        out = {};
        out.link = off ? net::Link::Off : link_;
        out.failure = failure_;
        const uint8_t mac[6] = {0x24, 0x58, 0x7C, 0x10, 0x20, 0x30};
        memcpy(out.mac, mac, 6);
        if (link_ == net::Link::Idle || off) {
            return;
        }
        snprintf(out.ssid, sizeof(out.ssid), "%s", ssid_.c_str());
        if (link_ == net::Link::Connected) {
            const Ap *ap = find(ssid_);
            out.rssi = static_cast<int8_t>(ap != nullptr ? ap->rssi : -60);
            out.channel = static_cast<uint8_t>(ap != nullptr ? ap->channel : 1);
            const uint8_t address[4] = {192, 168, 1, 37}, mask[4] = {255, 255, 255, 0};
            const uint8_t gateway[4] = {192, 168, 1, 1};
            memcpy(out.address, address, 4);
            memcpy(out.mask, mask, 4);
            memcpy(out.gateway, gateway, 4);
            memcpy(out.dns, gateway, 4);
        }
    }
    bool utc_time(int64_t &seconds) override
    {
        seconds = utc;
        return has_time && link_ == net::Link::Connected;
    }

private:
    const Ap *find(const std::string &ssid) const
    {
        for (const Ap &ap : air) {
            if (ap.ssid == ssid) {
                return &ap;
            }
        }
        return nullptr;
    }
    void finish()
    {
        const Ap *ap = find(ssid_);
        if (ap == nullptr) {
            link_ = net::Link::Idle;
            failure_ = net::Failure::NotFound;
        } else if (ap->password != last_password) {
            link_ = net::Link::Idle;
            failure_ = net::Failure::WrongPassword;
        } else {
            link_ = net::Link::Connected;
        }
    }

    std::vector<Ap> found_;
    int scan_left_ = 0, connect_left_ = 0;
    net::Link link_ = net::Link::Idle;
    net::Failure failure_ = net::Failure::None;
    std::string ssid_;
};

// Settings kept in memory (lost when the program ends).
class MemoryKeyStore : public net::KeyStore {
public:
    size_t load(const char *key, void *out, size_t capacity) override
    {
        auto it = values.find(key);
        if (it == values.end() || it->second.size() > capacity) {
            return 0;
        }
        memcpy(out, it->second.data(), it->second.size());
        return it->second.size();
    }
    bool save(const char *key, const void *data, size_t size) override
    {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        values[key] = std::vector<uint8_t>(p, p + size);
        saves++;
        return true;
    }
    std::map<std::string, std::vector<uint8_t>> values;
    int saves = 0;
};

} // namespace host
