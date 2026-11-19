// HID reports from a keyboard or a mouse turned into the system's key and
// mouse events. A device says how its reports are laid out in a report
// descriptor (the "report map" of a Bluetooth LE device); Layout reads
// that, and Decoder follows the reports that arrive afterwards.
//
// Read in the device's own layout rather than the simpler "boot" one, so
// that the wheel works and so do the many Bluetooth mice with no boot
// mode. Nothing here knows how the bytes arrive: Bluetooth today, a USB
// host port later.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace hid {

// What a device is, as bits: one with keys and a trackpad is both.
enum Kind : uint8_t { kKeyboard = 1, kMouse = 2 };

// What one stretch of an input report holds.
enum class What : uint8_t {
    KeyBits,  // one bit per key, `usage` the first (the modifiers; a keyboard that reports every key)
    KeyArray, // `count` numbers, each the key held: `usage` + (number - `base`)
    Buttons,  // one bit per mouse button, `usage` the first (1 is the left one)
    X,
    Y,
    Wheel,
    Pan, // the wheel sideways
};

struct Field {
    uint8_t report;  // the report's number, 0 when the device does not number them
    What what;
    uint8_t size;    // bits per value
    uint8_t count;   // values
    uint16_t bit;    // where the first value starts in the report
    uint16_t usage;
    int16_t base;    // KeyArray: the number that means `usage`
    bool is_signed;
};

// The input reports of one device.
class Layout {
public:
    static constexpr int kMaxFields = 24;

    // Reads a report descriptor. False when nothing in it is a key, a
    // button or a movement; a descriptor cut short gives what came before
    // the cut.
    bool parse(const uint8_t *descriptor, size_t size);

    uint8_t kinds() const { return kinds_; }
    int field_count() const { return count_; }
    const Field &field(int index) const { return fields_[index]; }
    // Whether the device numbers its reports.
    bool numbered() const { return numbered_; }

private:
    void add(const Field &field);

    Field fields_[kMaxFields] = {};
    int count_ = 0;
    uint8_t kinds_ = 0;
    bool numbered_ = false;
};

// Follows one device's reports. All calls on one task.
class Decoder {
public:
    bool set_layout(const uint8_t *descriptor, size_t size) { return layout_.parse(descriptor, size); }
    const Layout &layout() const { return layout_; }

    // One input report, without its number in front: `report` is the
    // number (0 when the device does not number them). Keys that went down
    // or up and the mouse's movement go to `sink`.
    void report(uint8_t report, const uint8_t *data, size_t size, ports::InputSink &sink);
    // The device is gone: every key and button it held is let go.
    void release_all(ports::InputSink &sink);

private:
    // The keys held, as the last report with keys said: one bit per usage.
    struct Keys {
        uint8_t report;
        bool used;
        uint8_t down[32];
    };
    static constexpr int kMaxKeyReports = 2;

    Keys *keys_of(uint8_t report);

    Layout layout_;
    Keys keys_[kMaxKeyReports] = {};
    uint8_t buttons_ = 0;
};

} // namespace hid
