#include "core/input/hid.hpp"

#include <cstring>

namespace hid {

namespace {

constexpr uint16_t kPageDesktop = 0x01, kPageKeys = 0x07, kPageButtons = 0x09, kPageConsumer = 0x0C;
constexpr uint16_t kUsageX = 0x30, kUsageY = 0x31, kUsageWheel = 0x38, kUsagePan = 0x238;
constexpr uint16_t kFirstModifier = 0xE0, kLastModifier = 0xE7;

// Bits of an Input item.
constexpr uint32_t kConstant = 1, kVariable = 2, kRelative = 4;

// The usages named since the last main item: single ones and ranges, in the
// order the descriptor gave them.
struct Usages {
    struct Run {
        uint16_t page; // 0: the page in force at the main item
        uint16_t first, last;
    };
    static constexpr int kMax = 16;
    Run runs[kMax];
    int count = 0;
    bool have_min = false;
    Run open = {};

    void add(uint16_t page, uint16_t first, uint16_t last)
    {
        if (count < kMax) {
            runs[count++] = {page, first, last};
        }
    }
    void clear()
    {
        count = 0;
        have_min = false;
    }
};

// What applies to every item until changed.
struct Globals {
    uint16_t page = 0;
    int32_t logical_min = 0;
    uint8_t size = 0;
    uint8_t count = 0;
    uint8_t report = 0;
};

// Where the next value of each numbered report starts.
struct Offsets {
    static constexpr int kMax = 12;
    uint8_t report[kMax];
    uint16_t bit[kMax];
    int count = 0;

    uint16_t *of(uint8_t id)
    {
        for (int i = 0; i < count; i++) {
            if (report[i] == id) {
                return &bit[i];
            }
        }
        if (count == kMax) {
            return nullptr;
        }
        report[count] = id;
        bit[count] = 0;
        return &bit[count++];
    }
};

bool what_of(uint16_t page, uint16_t usage, bool relative, What &what)
{
    if (page == kPageDesktop && relative) {
        if (usage == kUsageX || usage == kUsageY || usage == kUsageWheel) {
            what = usage == kUsageX ? What::X : (usage == kUsageY ? What::Y : What::Wheel);
            return true;
        }
    } else if (page == kPageConsumer && relative && usage == kUsagePan) {
        what = What::Pan;
        return true;
    } else if (page == kPageKeys) {
        what = What::KeyBits;
        return true;
    } else if (page == kPageButtons) {
        what = What::Buttons;
        return true;
    }
    return false;
}

// `size` bits starting at `bit`, lowest first; false past the end.
bool take_bits(const uint8_t *data, size_t bytes, uint32_t bit, uint8_t size, bool is_signed, int32_t &out)
{
    if (size == 0 || size > 32 || bit + size > bytes * 8) {
        return false;
    }
    uint32_t value = 0;
    for (uint8_t i = 0; i < size; i++) {
        const uint32_t at = bit + i;
        if (data[at / 8] & (1u << (at % 8))) {
            value |= 1u << i;
        }
    }
    if (is_signed && size < 32 && (value & (1u << (size - 1)))) {
        value |= ~0u << size;
    }
    out = static_cast<int32_t>(value);
    return true;
}

bool is_modifier(int usage) { return usage >= kFirstModifier && usage <= kLastModifier; }

int clamp(int32_t value, int low, int high) { return value < low ? low : (value > high ? high : value); }

} // namespace

/* ---- The layout ----------------------------------------------------------------- */

void Layout::add(const Field &field)
{
    if (count_ == kMaxFields) {
        return;
    }
    fields_[count_++] = field;
    kinds_ |= (field.what == What::KeyBits || field.what == What::KeyArray) ? kKeyboard : kMouse;
}

bool Layout::parse(const uint8_t *descriptor, size_t size)
{
    count_ = 0;
    kinds_ = 0;
    numbered_ = false;

    Globals now;
    Globals stack[4];
    int depth = 0;
    Usages usages;
    Offsets offsets;

    size_t at = 0;
    while (at < size) {
        const uint8_t prefix = descriptor[at++];
        if (prefix == 0xFE) { // a long item: its size, its tag, its data
            if (at + 2 > size) {
                break;
            }
            at += 2 + descriptor[at];
            continue;
        }
        const size_t bytes = (prefix & 3) == 3 ? 4 : (prefix & 3);
        if (at + bytes > size) {
            break;
        }
        uint32_t raw = 0;
        for (size_t i = 0; i < bytes; i++) {
            raw |= static_cast<uint32_t>(descriptor[at + i]) << (8 * i);
        }
        int32_t value = static_cast<int32_t>(raw);
        if (bytes == 1) {
            value = static_cast<int8_t>(raw);
        } else if (bytes == 2) {
            value = static_cast<int16_t>(raw);
        }
        at += bytes;

        const uint8_t type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) { // global
            switch (tag) {
            case 0: now.page = static_cast<uint16_t>(raw); break;
            case 1: now.logical_min = value; break;
            case 7: now.size = static_cast<uint8_t>(raw > 255 ? 255 : raw); break;
            case 8:
                now.report = static_cast<uint8_t>(raw);
                numbered_ = true;
                break;
            case 9: now.count = static_cast<uint8_t>(raw > 255 ? 255 : raw); break;
            case 10:
                if (depth < 4) {
                    stack[depth++] = now;
                }
                break;
            case 11:
                if (depth > 0) {
                    now = stack[--depth];
                }
                break;
            default: break;
            }
        } else if (type == 2) { // local
            // A usage written in four bytes carries its page.
            const uint16_t page = bytes == 4 ? static_cast<uint16_t>(raw >> 16) : 0;
            const uint16_t usage = static_cast<uint16_t>(raw);
            if (tag == 0) {
                usages.add(page, usage, usage);
            } else if (tag == 1) {
                usages.open = {page, usage, usage};
                usages.have_min = true;
            } else if (tag == 2 && usages.have_min) {
                usages.add(usages.open.page, usages.open.first, usage);
                usages.have_min = false;
            }
        } else if (type == 0) { // main
            if (tag == 8) { // Input
                uint16_t *offset = offsets.of(now.report);
                if (offset == nullptr) {
                    break;
                }
                const uint16_t start = *offset;
                *offset = static_cast<uint16_t>(start + now.size * now.count);
                if (!(raw & kConstant) && now.size > 0 && now.size <= 32) {
                    const bool relative = (raw & kRelative) != 0;
                    const bool is_signed = now.logical_min < 0;
                    if (raw & kVariable) {
                        // One value per usage, in order.
                        int element = 0;
                        for (int r = 0; r < usages.count && element < now.count; r++) {
                            const Usages::Run &run = usages.runs[r];
                            const uint16_t page = run.page != 0 ? run.page : now.page;
                            int length = run.last >= run.first ? run.last - run.first + 1 : 1;
                            if (length > now.count - element) {
                                length = now.count - element;
                            }
                            What what;
                            if (what_of(page, run.first, relative, what)) {
                                const bool block = what == What::KeyBits || what == What::Buttons;
                                // Keys and buttons one bit each; a movement one value.
                                if (!block || now.size == 1) {
                                    add({now.report, what, now.size, static_cast<uint8_t>(block ? length : 1),
                                         static_cast<uint16_t>(start + element * now.size), run.first, 0, is_signed});
                                }
                            }
                            element += length;
                        }
                    } else if (usages.count > 0) {
                        // An array: each value is the number of a key held.
                        const Usages::Run &run = usages.runs[0];
                        const uint16_t page = run.page != 0 ? run.page : now.page;
                        if (page == kPageKeys) {
                            add({now.report, What::KeyArray, now.size, now.count, start, run.first,
                                 static_cast<int16_t>(now.logical_min), false});
                        }
                    }
                }
            }
            usages.clear(); // Output, Feature and collections end the usages too
        }
    }
    return count_ > 0;
}

/* ---- The decoder ---------------------------------------------------------------- */

Decoder::Keys *Decoder::keys_of(uint8_t report)
{
    for (Keys &k : keys_) {
        if (k.used && k.report == report) {
            return &k;
        }
    }
    for (Keys &k : keys_) {
        if (!k.used) {
            k.used = true;
            k.report = report;
            memset(k.down, 0, sizeof(k.down));
            return &k;
        }
    }
    return nullptr;
}

void Decoder::report(uint8_t report, const uint8_t *data, size_t size, ports::InputSink &sink)
{
    uint8_t down[32] = {};
    bool has_keys = false, has_mouse = false, rolled_over = false;
    uint8_t buttons = 0;
    int32_t dx = 0, dy = 0, wheel = 0, pan = 0;

    for (int f = 0; f < layout_.field_count(); f++) {
        const Field &field = layout_.field(f);
        if (field.report != report) {
            continue;
        }
        for (int i = 0; i < field.count; i++) {
            int32_t value;
            if (!take_bits(data, size, field.bit + i * field.size, field.size, field.is_signed, value)) {
                break; // a report shorter than its layout: what is there counts
            }
            switch (field.what) {
            case What::KeyBits: {
                has_keys = true;
                const int usage = field.usage + i;
                if (value != 0 && usage < 256) {
                    down[usage / 8] |= static_cast<uint8_t>(1u << (usage % 8));
                }
                break;
            }
            case What::KeyArray: {
                has_keys = true;
                const int usage = field.usage + (value - field.base);
                if (usage == 1) {
                    rolled_over = true; // too many keys held: the keyboard cannot say which
                } else if (usage >= 4 && usage < 256) {
                    down[usage / 8] |= static_cast<uint8_t>(1u << (usage % 8));
                }
                break;
            }
            case What::Buttons: {
                has_mouse = true;
                const int button = field.usage + i; // 1 left, 2 right, 3 middle
                if (value != 0 && button >= 1 && button <= 3) {
                    buttons |= static_cast<uint8_t>(1u << (button - 1));
                }
                break;
            }
            case What::X: has_mouse = true; dx += value; break;
            case What::Y: has_mouse = true; dy += value; break;
            case What::Wheel: has_mouse = true; wheel += value; break;
            case What::Pan: has_mouse = true; pan += value; break;
            }
        }
    }

    if (has_keys && !rolled_over) {
        if (Keys *keys = keys_of(report)) {
            // Let go first, and the modifiers last; pressed, the modifiers
            // first: Shift and a letter in one report type a capital.
            for (int pass = 0; pass < 4; pass++) {
                const bool pressing = pass >= 2;
                const bool modifiers = pass == 1 || pass == 2;
                for (int usage = 4; usage < 256; usage++) {
                    const uint8_t mask = static_cast<uint8_t>(1u << (usage % 8));
                    const bool was = keys->down[usage / 8] & mask, is = down[usage / 8] & mask;
                    if (was != is && is == pressing && is_modifier(usage) == modifiers) {
                        sink.key(static_cast<uint16_t>(usage), is);
                    }
                }
            }
            memcpy(keys->down, down, sizeof(down));
        }
    }
    if (has_mouse) {
        const bool moved = dx != 0 || dy != 0 || wheel != 0 || pan != 0;
        if (moved || buttons != buttons_) {
            buttons_ = buttons;
            ports::MouseEvent event{false, static_cast<int16_t>(clamp(dx, -32767, 32767)),
                                    static_cast<int16_t>(clamp(dy, -32767, 32767)), buttons,
                                    static_cast<int8_t>(clamp(wheel, -127, 127))};
            event.wheel_x = static_cast<int8_t>(clamp(pan, -127, 127));
            sink.mouse(event);
        }
    }
}

void Decoder::release_all(ports::InputSink &sink)
{
    for (Keys &keys : keys_) {
        if (!keys.used) {
            continue;
        }
        for (int pass = 0; pass < 2; pass++) {
            for (int usage = 4; usage < 256; usage++) {
                if ((keys.down[usage / 8] & (1u << (usage % 8))) && is_modifier(usage) == (pass == 1)) {
                    sink.key(static_cast<uint16_t>(usage), false);
                }
            }
        }
        keys.used = false;
    }
    if (buttons_ != 0) {
        buttons_ = 0;
        sink.mouse({false, 0, 0, 0, 0});
    }
}

} // namespace hid
