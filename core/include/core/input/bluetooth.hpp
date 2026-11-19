// Bluetooth keyboards and mice (backlog item 51): the port an adapter
// implements (the Bluetooth LE radio, as the host of HID devices) and the
// manager on top of it, which remembers the devices that were paired,
// connects them again when they call, and pairs a new one by itself when
// the machine has no keyboard or no mouse.
//
// The keys and the mouse's movements do not pass through here: the adapter
// decodes the reports (core/input/hid.hpp) and hands the events to the
// input sink it was given. This is only who is connected.
//
// Everything here runs on the UI task. An adapter whose radio reports from
// other tasks keeps its own state and hands out copies.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/fs/ini.hpp"
#include "core/input/hid.hpp"

namespace bt {

constexpr int kNameMax = 31;

struct Address {
    uint8_t bytes[6]; // as written: the first is the most significant
    bool random;      // a random address (most keyboards and mice), not a public one
};
bool same(const Address &a, const Address &b);
// "c4:9d:ed:01:02:03 random"; `out` holds at least kAddressText bytes.
constexpr size_t kAddressText = 26;
void format(const Address &address, char *out);
// Reads that text; the second word may be missing (public). Returns the
// number of characters read, 0 if it is not an address.
size_t parse(const char *text, Address &out);

// "keyboard", "mouse", "keyboard+mouse", "unknown": hid::Kind bits as text.
const char *kinds_text(uint8_t kinds);

// A device heard advertising.
struct Found {
    Address address;
    char name[kNameMax + 1]; // empty when it did not say
    int8_t rssi;
    uint8_t kinds; // hid::Kind bits from what it says it looks like; 0: it did not say
    bool hid;      // says it is a keyboard, a mouse or the like
    bool pairing;  // discoverable: its pairing button was pressed (some say so always)
};

enum class LinkState : uint8_t {
    Connecting,
    Securing, // pairing, or turning the saved keys on
    Reading,  // asking what its reports look like
    Ready,    // its keys and movements arrive
};

struct Link {
    Address address;
    char name[kNameMax + 1];
    LinkState state;
    uint8_t kinds; // hid::Kind bits, from its report layout once Ready
};

// The radio.
class Port {
public:
    // False when there is no radio or it did not start.
    virtual bool on() = 0;
    // Listening for advertising devices, until told to stop.
    virtual void scan(bool on) = 0;
    // Those heard in the last few seconds (at most `max`).
    virtual int found(Found *out, int max) = 0;
    // Starts connecting: the device appears in links() from this call on,
    // and leaves it when the attempt fails or the device goes away. A
    // device never paired is paired; the keys are the adapter's to keep.
    virtual bool connect(const Address &address) = 0;
    virtual void disconnect(const Address &address) = 0;
    virtual int links(Link *out, int max) = 0;
    // Throws the pairing keys away and disconnects.
    virtual void forget(const Address &address) = 0;

protected:
    ~Port() = default;
};

class Manager {
public:
    static constexpr int kMaxRemembered = 8;
    static constexpr int kMaxLinks = 3;
    static constexpr int kMaxFound = 16;
    // An attempt that has not become Ready by then is given up.
    static constexpr uint64_t kAttemptUs = 30 * 1000000ull;
    // After a failed attempt the device is left alone this long, twice as
    // long after each further failure.
    static constexpr uint64_t kAvoidFirstUs = 30 * 1000000ull, kAvoidMaxUs = 480 * 1000000ull;

    struct Remembered {
        Address address;
        char name[kNameMax + 1];
        uint8_t kinds;
    };

    // `storage` nullptr: nothing is remembered across a restart.
    Manager(Port &radio, const fs::Storage *storage, const char *settings_path = "C:\\.config\\bluetooth.ini")
        : radio_(radio), storage_(storage), path_(settings_path)
    {
    }

    // Reads the remembered devices and starts listening for them.
    void start(uint64_t now_us);
    // Follows the radio; call a few times a second.
    void tick(uint64_t now_us);

    // What the user asks for.
    // Connects and pairs a device that was found, whatever it is.
    bool pair(const Address &address);
    // Disconnects it, throws its keys away and no longer remembers it.
    void forget(const Address &address);
    // A list of devices is on show: keeps listening although nothing is missing.
    void set_listing(bool on) { listing_ = on; }
    // Pairs by itself a device in pairing mode of a kind (keyboard, mouse)
    // that is not connected. Kept in the settings file.
    void set_auto_pair(bool on);
    bool auto_pair() const { return auto_pair_; }

    bool on() const { return on_; }
    bool scanning() const { return scanning_; }
    int link_count() const { return link_count_; }
    const Link &link(int index) const { return links_[index]; }
    int found_count() const { return found_count_; }
    const Found &found(int index) const { return found_[index]; }
    int remembered_count() const { return remembered_count_; }
    const Remembered &remembered(int index) const { return remembered_[index]; }
    bool is_remembered(const Address &address) const { return find_remembered(address) >= 0; }
    bool is_linked(const Address &address) const { return find_link(address) >= 0; }
    // hid::Kind bits of the devices that are Ready.
    uint8_t kinds_ready() const;
    // Adds one at every change a list of devices would show.
    uint32_t changes() const { return changes_; }

private:
    struct Avoided {
        Address address;
        uint64_t until;
        uint64_t delay;
        bool used;
    };
    static constexpr int kMaxAvoided = 8;

    int find_remembered(const Address &address) const;
    int find_link(const Address &address) const;
    void remember(const Link &link);
    void load();
    void save();
    void failed(const Address &address, uint64_t now_us);
    void succeeded(const Address &address);
    bool avoided(const Address &address, uint64_t now_us) const;
    bool wanted(const Found &found, uint64_t now_us) const;
    bool begin(const Address &address, bool by_user, uint64_t now_us);

    Port &radio_;
    const fs::Storage *storage_;
    const char *path_;

    Remembered remembered_[kMaxRemembered] = {};
    int remembered_count_ = 0;
    Link links_[kMaxLinks] = {};
    int link_count_ = 0;
    Found found_[kMaxFound] = {};
    int found_count_ = 0;
    Avoided avoided_[kMaxAvoided] = {};

    bool on_ = false;
    bool scanning_ = false;
    bool listing_ = false;
    bool auto_pair_ = true;
    uint32_t changes_ = 0;

    // The attempt under way, one at a time.
    bool attempting_ = false;
    bool attempt_listed_ = false; // the radio has shown it among its links
    Address attempt_ = {};
    uint64_t attempt_start_ = 0;
    uint64_t last_tick_ = 0;

    fs::Ini ini_; // the settings file, while it is read or written
};

} // namespace bt
