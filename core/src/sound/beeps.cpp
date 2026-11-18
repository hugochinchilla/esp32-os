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

// The owner has not chosen the chime yet (docs/journal/2026-11-18-beeper.md):
// this is candidate A, C major climbing an octave.
constexpr ports::Beep kChime[] = {{1047, 70}, {1319, 70}, {1568, 70}, {2093, 200}};

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

} // namespace sound
