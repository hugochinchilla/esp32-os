#include "core/input/bluetooth.hpp"

#include <cstdio>
#include <cstring>


namespace bt {

namespace {

constexpr uint8_t kBothKinds = hid::kKeyboard | hid::kMouse;

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// `word` at text, followed by a space or the end; its length, or 0.
size_t word_at(const char *text, const char *word)
{
    const size_t length = strlen(word);
    return strncmp(text, word, length) == 0 && (text[length] == ' ' || text[length] == 0) ? length : 0;
}

// What two readings of the radio's list would show differently: who is
// there and what they are called, not how strong the signal is.
bool same_found(const Found &a, const Found &b)
{
    return same(a.address, b.address) && strcmp(a.name, b.name) == 0 && a.kinds == b.kinds && a.hid == b.hid &&
           a.pairing == b.pairing;
}

bool same_link(const Link &a, const Link &b)
{
    return same(a.address, b.address) && strcmp(a.name, b.name) == 0 && a.state == b.state && a.kinds == b.kinds;
}

} // namespace

bool same(const Address &a, const Address &b)
{
    return a.random == b.random && memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

void format(const Address &a, char *out)
{
    snprintf(out, kAddressText, "%02x:%02x:%02x:%02x:%02x:%02x %s", a.bytes[0], a.bytes[1], a.bytes[2], a.bytes[3],
             a.bytes[4], a.bytes[5], a.random ? "random" : "public");
}

size_t parse(const char *text, Address &out)
{
    size_t at = 0;
    for (int i = 0; i < 6; i++) {
        const int high = hex_digit(text[at]);
        const int low = high < 0 ? -1 : hex_digit(text[at + 1]);
        if (low < 0 || (i < 5 && text[at + 2] != ':')) {
            return 0;
        }
        out.bytes[i] = static_cast<uint8_t>(high * 16 + low);
        at += i < 5 ? 3 : 2;
    }
    out.random = false;
    if (text[at] == ' ') {
        if (const size_t n = word_at(text + at + 1, "random")) {
            out.random = true;
            at += 1 + n;
        } else if (const size_t m = word_at(text + at + 1, "public")) {
            at += 1 + m;
        }
    } else if (text[at] != 0) {
        return 0;
    }
    return at;
}

const char *kinds_text(uint8_t kinds)
{
    switch (kinds & kBothKinds) {
    case hid::kKeyboard: return "keyboard";
    case hid::kMouse: return "mouse";
    case kBothKinds: return "keyboard+mouse";
    default: return "unknown";
    }
}

/* ---- Remembered devices --------------------------------------------------------- */

int Manager::find_remembered(const Address &address) const
{
    for (int i = 0; i < remembered_count_; i++) {
        if (same(remembered_[i].address, address)) {
            return i;
        }
    }
    return -1;
}

int Manager::find_link(const Address &address) const
{
    for (int i = 0; i < link_count_; i++) {
        if (same(links_[i].address, address)) {
            return i;
        }
    }
    return -1;
}

// C:\.config\bluetooth.ini:
//   [bluetooth]
//   auto_pair = true
//   [devices]
//   1 = c4:9d:ed:01:02:03 random keyboard The keyboard's name
// The most recently paired first; a line with nothing after the = is free.
// The pairing keys are not here: the adapter keeps them.
void Manager::load()
{
    remembered_count_ = 0;
    if (storage_ == nullptr) {
        return;
    }
    ini_.load(*storage_, path_);
    auto_pair_ = ini_.get_bool("bluetooth", "auto_pair", true);
    for (int i = 0; i < kMaxRemembered; i++) {
        char key[4], line[kAddressText + 16 + kNameMax + 1];
        snprintf(key, sizeof(key), "%d", i + 1);
        Remembered r = {};
        if (!ini_.get("devices", key, line, sizeof(line))) {
            continue;
        }
        size_t at = parse(line, r.address);
        if (at == 0 || find_remembered(r.address) >= 0) {
            continue;
        }
        if (line[at] == ' ') {
            at++;
            for (uint8_t kinds = 0; kinds <= kBothKinds; kinds++) {
                if (const size_t n = word_at(line + at, kinds_text(kinds))) {
                    r.kinds = kinds;
                    at += n;
                    break;
                }
            }
            if (line[at] == ' ') {
                snprintf(r.name, sizeof(r.name), "%s", line + at + 1);
            }
        }
        remembered_[remembered_count_++] = r;
    }
}

void Manager::save()
{
    if (storage_ == nullptr) {
        return;
    }
    // Read again first: what others wrote in the file stays.
    ini_.load(*storage_, path_);
    ini_.set_bool("bluetooth", "auto_pair", auto_pair_);
    for (int i = 0; i < kMaxRemembered; i++) {
        char key[4], line[kAddressText + 16 + kNameMax + 1] = "";
        snprintf(key, sizeof(key), "%d", i + 1);
        if (i < remembered_count_) {
            char address[kAddressText];
            format(remembered_[i].address, address);
            snprintf(line, sizeof(line), "%s %s %s", address, kinds_text(remembered_[i].kinds), remembered_[i].name);
        }
        ini_.set("devices", key, line);
    }
    ini_.save(*storage_, path_);
}

void Manager::remember(const Link &link)
{
    Remembered r = {};
    r.address = link.address;
    r.kinds = link.kinds;
    // Nothing in a name that would end the line or start a comment.
    snprintf(r.name, sizeof(r.name), "%s", link.name);
    for (char *c = r.name; *c != 0; c++) {
        if (static_cast<uint8_t>(*c) < 0x20 || *c == ';' || *c == '#' || *c == 0x7F) {
            *c = ' ';
        }
    }
    const int at = find_remembered(link.address);
    if (at >= 0) {
        if (r.name[0] == 0) { // it did not say this time: the name it had stays
            memcpy(r.name, remembered_[at].name, sizeof(r.name));
        }
        if (remembered_[at].kinds == r.kinds && strcmp(remembered_[at].name, r.name) == 0) {
            return;
        }
        remembered_[at] = r;
    } else {
        if (remembered_count_ == kMaxRemembered) {
            // No room: the one paired longest ago goes, keys and all.
            radio_.forget(remembered_[--remembered_count_].address);
        }
        memmove(remembered_ + 1, remembered_, sizeof(Remembered) * static_cast<size_t>(remembered_count_));
        remembered_[0] = r;
        remembered_count_++;
    }
    save();
    changes_++;
}

/* ---- Devices left alone for a while --------------------------------------------- */

void Manager::failed(const Address &address, uint64_t now_us)
{
    Avoided *slot = nullptr;
    for (Avoided &a : avoided_) {
        if (a.used && same(a.address, address)) {
            slot = &a;
            break;
        }
    }
    if (slot == nullptr) {
        // A free place, or the one whose time ends first.
        slot = &avoided_[0];
        for (Avoided &a : avoided_) {
            if (!a.used) {
                slot = &a;
                break;
            }
            if (a.until < slot->until) {
                slot = &a;
            }
        }
        *slot = {address, 0, 0, true};
    }
    slot->delay = slot->delay == 0 ? kAvoidFirstUs : (slot->delay * 2 > kAvoidMaxUs ? kAvoidMaxUs : slot->delay * 2);
    slot->until = now_us + slot->delay;
}

void Manager::succeeded(const Address &address)
{
    for (Avoided &a : avoided_) {
        if (a.used && same(a.address, address)) {
            a.used = false;
        }
    }
}

bool Manager::avoided(const Address &address, uint64_t now_us) const
{
    for (const Avoided &a : avoided_) {
        if (a.used && same(a.address, address)) {
            return now_us < a.until;
        }
    }
    return false;
}

/* ---- Following the radio -------------------------------------------------------- */

uint8_t Manager::kinds_ready() const
{
    uint8_t kinds = 0;
    for (int i = 0; i < link_count_; i++) {
        if (links_[i].state == LinkState::Ready) {
            kinds |= links_[i].kinds;
        }
    }
    return kinds & kBothKinds;
}

// Whether to connect to a device that was heard, without being asked.
bool Manager::wanted(const Found &found, uint64_t now_us) const
{
    if (is_linked(found.address) || avoided(found.address, now_us)) {
        return false;
    }
    if (is_remembered(found.address)) {
        return true; // one of ours is calling
    }
    if (!auto_pair_ || !found.pairing || !found.hid) {
        return false;
    }
    // A new one, only of a kind the machine is without. One that does not
    // say what it is: when either is missing.
    const uint8_t missing = kBothKinds & static_cast<uint8_t>(~kinds_ready());
    return found.kinds == 0 ? missing != 0 : (found.kinds & missing) != 0;
}

bool Manager::begin(const Address &address, bool by_user, uint64_t now_us)
{
    if (!on_ || attempting_ || link_count_ >= kMaxLinks || is_linked(address)) {
        return false;
    }
    if (by_user) {
        succeeded(address); // asked for by name: whatever failed before is forgotten
    }
    if (!radio_.connect(address)) {
        failed(address, now_us);
        return false;
    }
    attempting_ = true;
    attempt_listed_ = false;
    attempt_ = address;
    attempt_start_ = now_us;
    return true;
}

void Manager::start(uint64_t now_us)
{
    load();
    tick(now_us);
}

void Manager::tick(uint64_t now_us)
{
    last_tick_ = now_us;
    const bool on = radio_.on();
    if (on != on_) {
        on_ = on;
        changes_++;
    }
    if (!on_) {
        link_count_ = found_count_ = 0;
        listening_ = Listen::Off;
        attempting_ = false;
        return;
    }

    // Who is connected, and how far along.
    Link now[kMaxLinks] = {};
    int count = radio_.links(now, kMaxLinks);
    count = count < 0 ? 0 : (count > kMaxLinks ? kMaxLinks : count);
    bool changed = count != link_count_;
    for (int i = 0; i < link_count_; i++) {
        bool still = false;
        for (int j = 0; j < count; j++) {
            still = still || same(links_[i].address, now[j].address);
        }
        if (!still && links_[i].state != LinkState::Ready) {
            failed(links_[i].address, now_us); // gone before it worked
        }
        changed = changed || !still;
    }
    for (int j = 0; j < count; j++) {
        const int old = find_link(now[j].address);
        changed = changed || old < 0 || !same_link(links_[old], now[j]);
        if (now[j].state == LinkState::Ready && (old < 0 || links_[old].state != LinkState::Ready)) {
            succeeded(now[j].address);
        }
    }
    memcpy(links_, now, sizeof(links_));
    link_count_ = count;
    for (int j = 0; j < count; j++) {
        if (links_[j].state == LinkState::Ready) {
            remember(links_[j]); // a new one, or one whose name or kind changed
        }
    }
    if (attempting_) {
        const int at = find_link(attempt_);
        if (at < 0) {
            if (!attempt_listed_) {
                failed(attempt_, now_us); // gone before it was ever listed
            }
            attempting_ = false;
        } else if (links_[at].state == LinkState::Ready) {
            attempting_ = false;
        } else if (!attempt_listed_) {
            attempt_listed_ = true;
        } else if (now_us - attempt_start_ > kAttemptUs) {
            radio_.disconnect(attempt_); // it leaves the list, and counts as failed then
        }
    }

    // Listen while something is missing or somebody is looking.
    bool absent = false;
    for (int i = 0; i < remembered_count_; i++) {
        absent = absent || !is_linked(remembered_[i].address);
    }
    Listen want = Listen::Off;
    if (link_count_ < kMaxLinks) {
        if (listing_) {
            want = Listen::Search;
        } else if (absent || (auto_pair_ && kinds_ready() != kBothKinds)) {
            want = Listen::Background;
        }
    }
    if (want != listening_) {
        listening_ = want;
        radio_.listen(want);
        changed = true;
    }

    Found heard[kMaxFound] = {};
    int heard_count = scanning() ? radio_.found(heard, kMaxFound) : 0;
    heard_count = heard_count < 0 ? 0 : (heard_count > kMaxFound ? kMaxFound : heard_count);
    changed = changed || heard_count != found_count_;
    for (int i = 0; i < heard_count && !changed; i++) {
        changed = !same_found(heard[i], found_[i]);
    }
    memcpy(found_, heard, sizeof(found_));
    found_count_ = heard_count;
    if (changed) {
        changes_++;
    }

    for (int i = 0; i < found_count_ && !attempting_; i++) {
        if (wanted(found_[i], now_us)) {
            begin(found_[i].address, false, now_us);
        }
    }
}

/* ---- What the user asks for ------------------------------------------------------ */

bool Manager::pair(const Address &address)
{
    if (!begin(address, true, last_tick_)) {
        return false;
    }
    tick(last_tick_); // it is among the links from now on, for whoever asks
    return true;
}

void Manager::forget(const Address &address)
{
    radio_.forget(address);
    const int at = find_remembered(address);
    if (at >= 0) {
        memmove(remembered_ + at, remembered_ + at + 1,
                sizeof(Remembered) * static_cast<size_t>(remembered_count_ - at - 1));
        remembered_count_--;
        save();
    }
    // Off the list at once, not at the next look at the radio.
    const int link = find_link(address);
    if (link >= 0) {
        memmove(links_ + link, links_ + link + 1, sizeof(Link) * static_cast<size_t>(link_count_ - link - 1));
        link_count_--;
    }
    // Still in pairing mode, it would be paired again at once.
    failed(address, last_tick_);
    changes_++;
}

void Manager::set_auto_pair(bool on)
{
    if (on != auto_pair_) {
        auto_pair_ = on;
        save();
        changes_++;
    }
}

} // namespace bt
