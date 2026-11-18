// Wi-Fi: the ports an adapter implements (the radio, and a small store for
// settings) and the manager on top of them, which remembers networks, joins
// the strongest one it knows, tries again when the link drops, and sets the
// clock from the network.
//
// Everything here runs on the UI task. An adapter whose radio reports from
// other tasks keeps its own state and hands out copies.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

constexpr int kSsidMax = 32;     // bytes, as the standard says
constexpr int kPasswordMax = 64; // 8 to 63 characters, or 64 hex digits

enum class Security : uint8_t {
    Open,
    Password,   // WEP, WPA, WPA2, WPA3 with a shared password
    Enterprise, // needs a user name: not supported
};

// A network found by a scan.
struct Network {
    char ssid[kSsidMax + 1];
    int8_t rssi; // dBm, e.g. -55
    uint8_t channel;
    Security security;
};

enum class Link : uint8_t {
    Off,        // no radio, or it did not start
    Idle,       // not connected
    Connecting, // joining, or waiting for an address
    Connected,  // has an address
};

enum class Failure : uint8_t {
    None,
    WrongPassword,
    NotFound, // no network of that name in range
    TimedOut,
    Other,
};

struct LinkStatus {
    Link link;
    Failure failure; // why the last connect() ended, while Idle
    char ssid[kSsidMax + 1];
    int8_t rssi;
    uint8_t channel;
    uint8_t address[4], mask[4], gateway[4], dns[4];
    uint8_t mac[6];
};

// The radio's top transmit power, in dBm: the range of `tx_power_dbm` in
// C:\.config\network.ini, and what a board uses when the file says nothing.
constexpr int kTxPowerMin = 2, kTxPowerMax = 20, kTxPowerDefault = 20;
constexpr int clamp_tx_power(int dbm)
{
    return dbm < kTxPowerMin ? kTxPowerMin : (dbm > kTxPowerMax ? kTxPowerMax : dbm);
}

// The radio.
class WifiPort {
public:
    // Starts looking for networks. scanning() is true until it is done;
    // results() then gives what was found (at most `max`).
    virtual void start_scan() = 0;
    virtual bool scanning() = 0;
    virtual int results(Network *out, int max) = 0;
    // Starts joining; status().link is Connecting from this call on, then
    // Connected, or Idle with the failure. An empty password: an open
    // network.
    virtual void connect(const char *ssid, const char *password) = 0;
    virtual void disconnect() = 0;
    virtual void status(LinkStatus &out) = 0;
    // The most the radio transmits with, in dBm, from now on. The Manager
    // gives a value from kTxPowerMin to kTxPowerMax.
    virtual void set_tx_power(int dbm) = 0;
    // The time from the network (NTP), in seconds since 1970, UTC. False
    // until it is known.
    virtual bool utc_time(int64_t &seconds) = 0;

protected:
    ~WifiPort() = default;
};

// Small named settings that survive a restart and are not files on a drive
// (the board keeps them in its settings area in the flash).
class KeyStore {
public:
    // Returns the size read (0: nothing stored, or it does not fit).
    virtual size_t load(const char *key, void *out, size_t capacity) = 0;
    virtual bool save(const char *key, const void *data, size_t size) = 0;

protected:
    ~KeyStore() = default;
};

/* ---- Time ---------------------------------------------------------------------- */

struct TimeZone {
    int16_t minutes_east;   // of UTC, in winter
    bool eu_summer_time;    // one hour more from the last Sunday of March to
                            // the last Sunday of October (01:00 UTC both)
};
// Spanish mainland time (CET / CEST): the default until there is a setting.
constexpr TimeZone kDefaultZone = {60, true};

struct LocalTime {
    int year, month, day; // month and day from 1
    int hour, minute, second;
    int weekday; // 0 Sunday
    bool summer;
};
LocalTime local_time(int64_t utc_seconds, const TimeZone &zone);

/* ---- The manager --------------------------------------------------------------- */

// Signal strength as 0 to 4 bars.
int bars(int rssi);

class Manager {
public:
    static constexpr int kMaxSaved = 8;
    static constexpr int kMaxSeen = 24;
    static constexpr uint32_t kConnectTimeoutUs = 20 * 1000000u;
    static constexpr uint32_t kRetryFirstUs = 10 * 1000000u, kRetryMaxUs = 80 * 1000000u;
    static constexpr uint64_t kClockEveryUs = 3600 * 1000000ull;
    static constexpr size_t kBlobMax = 2 + kMaxSaved * (2 + kSsidMax + kPasswordMax);

    // A network in the list the panel shows.
    struct Seen {
        Network network;
        bool saved;
        bool connected;
    };
    using TimeFn = void (*)(const LocalTime &time, void *context);

    Manager(WifiPort &radio, KeyStore &store) : radio_(radio), store_(store) {}

    // Reads the remembered networks and starts joining one.
    void start(uint64_t now_us);
    // Follows the radio; call a few times a second.
    void tick(uint64_t now_us);

    // What the user asks for.
    void scan();
    // `password` nullptr: the remembered one. With `remember`, the network is
    // kept once it has connected (never after a failure).
    void connect(const char *ssid, const char *password, bool remember);
    // Stays disconnected until connect() is asked again.
    void disconnect();
    void forget(const char *ssid);
    // The radio's top transmit power, at once. Not kept: whoever asks
    // writes `tx_power_dbm` in network.ini, which the board reads at start.
    void set_tx_power(int dbm) { radio_.set_tx_power(clamp_tx_power(dbm)); }

    const LinkStatus &link() const { return link_; }
    bool scanning() const { return scanning_; }
    // The last scan: one entry per name (the strongest), the connected one
    // first, then by signal.
    int seen_count() const { return seen_count_; }
    const Seen &seen(int index) const { return seen_[index]; }
    int saved_count() const { return saved_count_; }
    const char *saved_ssid(int index) const { return saved_[index].ssid; }
    bool is_saved(const char *ssid) const { return find_saved(ssid) >= 0; }
    // Why the last connect asked by the user failed (None after a success).
    Failure last_failure() const { return last_failure_; }
    // The network of the last attempt to connect.
    const char *attempt_ssid() const { return attempt_.ssid; }
    // Adds one at every change the panel or the tray would show.
    uint32_t changes() const { return changes_; }

    // The clock: called when the network time first arrives, and every hour.
    void set_on_time(TimeFn fn, void *context)
    {
        on_time_ = fn;
        time_context_ = context;
    }
    void set_zone(const TimeZone &zone) { zone_ = zone; }
    const TimeZone &zone() const { return zone_; }
    bool time_known() const { return time_known_; }

    // The remembered networks as stored (tests).
    size_t pack(uint8_t *out, size_t capacity) const;
    bool unpack(const uint8_t *data, size_t size);

private:
    struct Saved {
        char ssid[kSsidMax + 1];
        char password[kPasswordMax + 1];
    };

    int find_saved(const char *ssid) const;
    void remember(const char *ssid, const char *password);
    void store();
    void read_results();
    void sort_seen();
    void join_next(uint64_t now_us);
    void begin(const char *ssid, const char *password, uint64_t now_us);
    void first_join(uint64_t now_us);
    void retry_later(uint64_t now_us);

    WifiPort &radio_;
    KeyStore &store_;

    Saved saved_[kMaxSaved] = {};
    int saved_count_ = 0;
    Seen seen_[kMaxSeen] = {};
    int seen_count_ = 0;
    LinkStatus link_ = {};
    bool scanning_ = false;
    uint32_t changes_ = 0;

    bool started_ = false;
    bool auto_join_ = false;     // join a remembered network when one is seen
    bool scanned_ = false;       // a scan has finished since start (first_join skips it)
    bool by_user_ = false;       // the attempt under way was asked by the user
    bool keep_ = false;          // remember it when it connects
    Saved attempt_ = {};
    uint64_t attempt_start_ = 0;
    bool tried_[kMaxSaved] = {}; // failed in this round of auto-joining
    uint64_t retry_at_ = 0;      // 0: no rescan planned
    uint32_t retry_delay_ = kRetryFirstUs;
    Failure last_failure_ = Failure::None;

    TimeFn on_time_ = nullptr;
    void *time_context_ = nullptr;
    TimeZone zone_ = kDefaultZone;
    bool time_known_ = false;
    uint64_t clock_set_at_ = 0;
};

} // namespace net
