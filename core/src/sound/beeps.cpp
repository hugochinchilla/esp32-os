#include "core/sound/beeps.hpp"

namespace sound {

namespace {

constexpr ports::Beep kLong = {kBeepHz, kLongMs}, kShort = {kBeepHz, kShortMs}, kGap = {0, kGapMs};

constexpr ports::Beep kOk[] = {{kBeepHz, 200}};
constexpr ports::Beep kNotFatal[] = {kShort, kGap, kShort};
constexpr ports::Beep kMemory[] = {kLong, kGap, kLong, kGap, kLong};
constexpr ports::Beep kVideo[] = {kLong, kGap, kShort, kGap, kShort};
constexpr ports::Beep kDrive[] = {kLong, kGap, kShort, kGap, kShort, kGap, kShort};
constexpr ports::Beep kNoSystem[] = {kLong, {0, 400}};

// "First light" (candidate N of round four, docs/journal/2026-11-18-beeper.md),
// a melody of our own in D major: a quiet held note, a full one, a small
// figure, then four bells that climb and fade. About 5.4 s. Put forward as
// the default while the owner had not picked one; his to change.
constexpr ports::Beep kChime[] = {
    {587, 850, 0, 89},                                                    // D5, quiet and steady
    {880, 520, 300},   {740, 260, 300},     {880, 260, 300}, {1175, 620, 300}, // A5 F#5 A5 D6
    {1480, 720, 330, 204}, {1760, 720, 330, 140}, {2349, 720, 330, 97}, {2960, 720, 330, 56}, // F#6 A6 D7 F#7
};

template <int N> constexpr Tune tune(const ports::Beep (&steps)[N]) { return {steps, N}; }

} // namespace

Tune post_code(PostResult result)
{
    switch (result) {
    case PostResult::NoSystem: return tune(kNoSystem);
    case PostResult::Memory: return tune(kMemory);
    case PostResult::Video: return tune(kVideo);
    case PostResult::Drive: return tune(kDrive);
    case PostResult::NotFatal: return tune(kNotFatal);
    case PostResult::Ok: break;
    }
    return tune(kOk);
}

Tune desktop_chime() { return tune(kChime); }

uint32_t length_ms(Tune t)
{
    uint32_t ms = 0;
    for (int i = 0; i < t.count; i++) {
        ms += t.steps[i].ms;
    }
    return ms;
}

int parse_tune(const char *text, size_t size, ports::Beep *out, int capacity)
{
    int count = 0;
    size_t i = 0;
    while (i < size && count < capacity) {
        // One line: up to four numbers.
        uint32_t numbers[4] = {0, 0, 0, 0};
        int have = 0;
        bool bad = false;
        while (i < size && text[i] != '\n') {
            const char ch = text[i];
            if (ch == '#') {
                while (i < size && text[i] != '\n') {
                    i++;
                }
                break;
            }
            if (ch >= '0' && ch <= '9') {
                uint32_t n = 0;
                while (i < size && text[i] >= '0' && text[i] <= '9') {
                    n = n < 100000 ? n * 10 + static_cast<uint32_t>(text[i] - '0') : n;
                    i++;
                }
                if (have < 4) {
                    numbers[have] = n;
                }
                have++;
                continue;
            }
            bad = bad || !(ch == ' ' || ch == '\t' || ch == '\r' || ch == ',');
            i++;
        }
        i++; // the line's end
        if (bad || have < 2 || have > 4 || numbers[0] > 20000 || numbers[1] == 0 || numbers[1] > 10000 ||
            numbers[2] > 10000 || numbers[3] > 255) {
            continue;
        }
        out[count++] = {static_cast<uint16_t>(numbers[0]), static_cast<uint16_t>(numbers[1]),
                        static_cast<uint16_t>(numbers[2]), static_cast<uint8_t>(numbers[3])};
    }
    return count;
}

} // namespace sound
