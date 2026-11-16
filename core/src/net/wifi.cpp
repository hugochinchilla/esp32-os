#include "core/net/wifi.hpp"

#include <cstdio>
#include <cstring>

namespace net {

/* ---- Time ---------------------------------------------------------------------- */

namespace {

// Days since 1970-01-01 of a date (proleptic Gregorian calendar).
int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = static_cast<int>(y - era * 400);
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void civil_from_days(int64_t z, int &y, int &m, int &d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = static_cast<int>(z - era * 146097);
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(yoe + era * 400) + (m <= 2);
}

int weekday_of(int64_t days) // 0 Sunday; 1970-01-01 was a Thursday
{
    return static_cast<int>(((days % 7) + 11) % 7);
}

// 01:00 UTC on the last Sunday of a month with 31 days.
int64_t last_sunday_1am(int year, int month)
{
    const int64_t last = days_from_civil(year, month, 31);
    return (last - weekday_of(last)) * 86400 + 3600;
}

int64_t floor_div(int64_t a, int64_t b) { return a / b - (a % b < 0); }

void copy_text(char *out, size_t capacity, const char *in)
{
    snprintf(out, capacity, "%s", in != nullptr ? in : "");
}

} // namespace

LocalTime local_time(int64_t utc, const TimeZone &zone)
{
    LocalTime t = {};
    int y, m, d;
    civil_from_days(floor_div(utc, 86400), y, m, d);
    t.summer = zone.eu_summer_time && utc >= last_sunday_1am(y, 3) && utc < last_sunday_1am(y, 10);
    const int64_t local = utc + zone.minutes_east * 60 + (t.summer ? 3600 : 0);
    const int64_t days = floor_div(local, 86400);
    const int rest = static_cast<int>(local - days * 86400);
    civil_from_days(days, t.year, t.month, t.day);
    t.hour = rest / 3600;
    t.minute = rest / 60 % 60;
    t.second = rest % 60;
    t.weekday = weekday_of(days);
    return t;
}

int bars(int rssi)
{
    return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -88 ? 1 : 0;
}

/* ---- Remembered networks -------------------------------------------------------- */

// Stored as: version (1), count, then for each network the length of its
// name, the name, the length of its password, the password.
size_t Manager::pack(uint8_t *out, size_t capacity) const
{
    size_t n = 0;
    if (capacity < 2) {
        return 0;
    }
    out[n++] = 1;
    out[n++] = static_cast<uint8_t>(saved_count_);
    for (int i = 0; i < saved_count_; i++) {
        const size_t sl = strlen(saved_[i].ssid), pl = strlen(saved_[i].password);
        if (n + 2 + sl + pl > capacity) {
            return 0;
        }
        out[n++] = static_cast<uint8_t>(sl);
        memcpy(out + n, saved_[i].ssid, sl);
        n += sl;
        out[n++] = static_cast<uint8_t>(pl);
        memcpy(out + n, saved_[i].password, pl);
        n += pl;
    }
    return n;
}

bool Manager::unpack(const uint8_t *data, size_t size)
{
    Saved read[kMaxSaved] = {};
    if (size < 2 || data[0] != 1 || data[1] > kMaxSaved) {
        return false;
    }
    const int count = data[1];
    size_t n = 2;
    for (int i = 0; i < count; i++) {
        if (n >= size || data[n] == 0 || data[n] > kSsidMax || n + 1 + data[n] >= size) {
            return false;
        }
        memcpy(read[i].ssid, data + n + 1, data[n]);
        n += 1 + data[n];
        if (data[n] > kPasswordMax || n + 1 + data[n] > size) {
            return false;
        }
        memcpy(read[i].password, data + n + 1, data[n]);
        n += 1 + data[n];
    }
    if (n != size) {
        return false;
    }
    memcpy(saved_, read, sizeof(saved_));
    saved_count_ = count;
    return true;
}

void Manager::store()
{
    uint8_t blob[kBlobMax];
    const size_t size = pack(blob, sizeof(blob));
    store_.save("wifi", blob, size);
    memset(blob, 0, sizeof(blob)); // no passwords left on the stack
}

int Manager::find_saved(const char *ssid) const
{
    for (int i = 0; i < saved_count_; i++) {
        if (strcmp(saved_[i].ssid, ssid) == 0) {
            return i;
        }
    }
    return -1;
}

// Puts the network first (the most recently used); the oldest drops off.
void Manager::remember(const char *ssid, const char *password)
{
    int at = find_saved(ssid);
    if (at < 0) {
        at = saved_count_ < kMaxSaved ? saved_count_++ : kMaxSaved - 1;
    }
    memmove(saved_ + 1, saved_, static_cast<size_t>(at) * sizeof(Saved));
    saved_[0] = {};
    copy_text(saved_[0].ssid, sizeof(saved_[0].ssid), ssid);
    copy_text(saved_[0].password, sizeof(saved_[0].password), password);
    store();
}

void Manager::forget(const char *ssid)
{
    const int at = find_saved(ssid);
    if (at < 0) {
        return;
    }
    memmove(saved_ + at, saved_ + at + 1, static_cast<size_t>(saved_count_ - at - 1) * sizeof(Saved));
    saved_count_--;
    saved_[saved_count_] = {};
    store();
    if (link_.link != Link::Idle && link_.link != Link::Off && strcmp(link_.ssid, ssid) == 0) {
        disconnect();
    }
    sort_seen();
    changes_++;
}

/* ---- Following the radio --------------------------------------------------------- */

void Manager::start(uint64_t now_us)
{
    uint8_t blob[kBlobMax];
    const size_t size = store_.load("wifi", blob, sizeof(blob));
    if (size > 0) {
        unpack(blob, size);
    }
    memset(blob, 0, sizeof(blob));
    started_ = true;
    radio_.status(link_);
    auto_join_ = saved_count_ > 0;
    if (link_.link != Link::Off) {
        scan();
    }
    (void)now_us;
    changes_++;
}

void Manager::scan()
{
    if (!scanning_ && link_.link != Link::Off && link_.link != Link::Connecting) {
        radio_.start_scan();
        scanning_ = true;
        retry_at_ = 0;
        changes_++;
    }
}

void Manager::begin(const char *ssid, const char *password, uint64_t now_us)
{
    attempt_ = {};
    copy_text(attempt_.ssid, sizeof(attempt_.ssid), ssid);
    copy_text(attempt_.password, sizeof(attempt_.password), password);
    attempt_start_ = now_us;
    retry_at_ = 0;
    radio_.connect(attempt_.ssid, attempt_.password);
    radio_.status(link_);
    changes_++;
}

void Manager::connect(const char *ssid, const char *password, bool remember)
{
    const int at = find_saved(ssid);
    if (password == nullptr) {
        password = at >= 0 ? saved_[at].password : "";
    }
    by_user_ = true;
    keep_ = remember;
    auto_join_ = false;
    last_failure_ = Failure::None;
    // The time of the attempt is taken at the next tick.
    begin(ssid, password, 0);
}

void Manager::disconnect()
{
    auto_join_ = false;
    by_user_ = false;
    retry_at_ = 0;
    last_failure_ = Failure::None;
    radio_.disconnect();
    radio_.status(link_);
    sort_seen();
    changes_++;
}

void Manager::retry_later(uint64_t now_us)
{
    memset(tried_, 0, sizeof(tried_));
    retry_at_ = now_us + retry_delay_;
    retry_delay_ = retry_delay_ * 2 > kRetryMaxUs ? kRetryMaxUs : retry_delay_ * 2;
}

// Joins the strongest remembered network in range that has not failed in
// this round; with none left, looks again later.
void Manager::join_next(uint64_t now_us)
{
    int best = -1, best_saved = -1;
    for (int i = 0; i < seen_count_; i++) {
        const int s = find_saved(seen_[i].network.ssid);
        if (s >= 0 && !tried_[s] && (best < 0 || seen_[i].network.rssi > seen_[best].network.rssi)) {
            best = i;
            best_saved = s;
        }
    }
    if (best < 0) {
        retry_later(now_us);
        return;
    }
    tried_[best_saved] = true;
    by_user_ = false;
    keep_ = false;
    begin(saved_[best_saved].ssid, saved_[best_saved].password, now_us);
}

void Manager::sort_seen()
{
    for (int i = 0; i < seen_count_; i++) {
        seen_[i].saved = is_saved(seen_[i].network.ssid);
        seen_[i].connected = link_.link == Link::Connected && strcmp(seen_[i].network.ssid, link_.ssid) == 0;
    }
    // Few entries: an insertion sort. The connected one first, then by signal.
    for (int i = 1; i < seen_count_; i++) {
        const Seen s = seen_[i];
        int j = i;
        for (; j > 0; j--) {
            const Seen &p = seen_[j - 1];
            const bool before = s.connected != p.connected ? s.connected : s.network.rssi > p.network.rssi;
            if (!before) {
                break;
            }
            seen_[j] = seen_[j - 1];
        }
        seen_[j] = s;
    }
}

void Manager::read_results()
{
    static Network found[2 * kMaxSeen]; // the UI task only
    const int n = radio_.results(found, 2 * kMaxSeen);
    seen_count_ = 0;
    for (int i = 0; i < n; i++) {
        if (found[i].ssid[0] == 0) {
            continue; // a network that hides its name
        }
        int at = -1;
        for (int j = 0; j < seen_count_; j++) {
            if (strcmp(seen_[j].network.ssid, found[i].ssid) == 0) {
                at = j;
            }
        }
        if (at >= 0) {
            if (found[i].rssi > seen_[at].network.rssi) {
                seen_[at].network = found[i]; // several access points: the strongest
            }
        } else if (seen_count_ < kMaxSeen) {
            seen_[seen_count_++].network = found[i];
        }
    }
    sort_seen();
}

void Manager::tick(uint64_t now_us)
{
    if (!started_) {
        return;
    }
    const Link before = link_.link;
    const int8_t rssi_before = link_.rssi;
    radio_.status(link_);
    if (before == Link::Off && link_.link != Link::Off) {
        scan(); // the radio came up after start()
    }
    if (link_.link == Link::Connecting && attempt_start_ == 0) {
        attempt_start_ = now_us;
    }

    if (scanning_ && !radio_.scanning()) {
        scanning_ = false;
        read_results();
        changes_++;
        if (auto_join_ && link_.link == Link::Idle) {
            join_next(now_us);
        }
    }

    if (link_.link == Link::Connecting && now_us - attempt_start_ > kConnectTimeoutUs) {
        radio_.disconnect();
        radio_.status(link_);
        link_.link = Link::Idle;
        link_.failure = Failure::TimedOut;
    }

    if (before == Link::Connecting && link_.link == Link::Connected) {
        if (by_user_ && keep_) {
            remember(attempt_.ssid, attempt_.password);
        }
        memset(attempt_.password, 0, sizeof(attempt_.password));
        memset(tried_, 0, sizeof(tried_));
        retry_delay_ = kRetryFirstUs;
        auto_join_ = true; // if the link drops, come back
        by_user_ = false;
        last_failure_ = Failure::None;
        sort_seen();
        changes_++;
    } else if (before == Link::Connecting && link_.link == Link::Idle) {
        memset(attempt_.password, 0, sizeof(attempt_.password));
        if (by_user_) {
            // The user's own attempt: say why, and leave it there.
            last_failure_ = link_.failure != Failure::None ? link_.failure : Failure::Other;
            by_user_ = false;
        } else if (auto_join_) {
            join_next(now_us);
        }
        sort_seen();
        changes_++;
    } else if (before == Link::Connected && link_.link == Link::Idle) {
        sort_seen();
        changes_++;
        if (auto_join_) {
            scan(); // dropped: look for a network we know
        }
    } else if (before != link_.link || bars(rssi_before) != bars(link_.rssi)) {
        changes_++;
    }

    if (retry_at_ != 0 && now_us >= retry_at_ && link_.link == Link::Idle && auto_join_) {
        retry_at_ = 0;
        scan();
    }

    // The clock, when the network time first arrives and every hour after.
    if (on_time_ != nullptr && (!time_known_ || now_us - clock_set_at_ >= kClockEveryUs)) {
        int64_t utc = 0;
        if (radio_.utc_time(utc)) {
            if (!time_known_) {
                changes_++;
            }
            time_known_ = true;
            clock_set_at_ = now_us;
            on_time_(local_time(utc, zone_), time_context_);
        }
    }
}

} // namespace net
