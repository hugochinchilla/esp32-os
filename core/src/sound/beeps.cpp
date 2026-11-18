#include "core/sound/beeps.hpp"

namespace sound {

namespace {

constexpr ports::Beep kLong = {kBeepHz, kLongMs}, kShort = {kBeepHz, kShortMs}, kGap = {0, kGapMs};

constexpr ports::Beep kOk[] = {{kBeepHz, kOkMs}};
constexpr ports::Beep kNotFatal[] = {kShort, kGap, kShort};
constexpr ports::Beep kMemory[] = {kLong, kGap, kLong, kGap, kLong};
constexpr ports::Beep kVideo[] = {kLong, kGap, kShort, kGap, kShort};
constexpr ports::Beep kDrive[] = {kLong, kGap, kShort, kGap, kShort, kGap, kShort};
constexpr ports::Beep kNoSystem[] = {kLong, {0, 400}};

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

uint32_t length_ms(Tune t)
{
    uint32_t ms = 0;
    for (int i = 0; i < t.count; i++) {
        ms += t.steps[i].ms;
    }
    return ms;
}

} // namespace sound
