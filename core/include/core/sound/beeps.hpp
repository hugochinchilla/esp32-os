// What the beeper says when the self-test is done, as PCs did: one short
// beep when all is well, a code of long and short beeps when not. Each is
// a list of steps for ports::Sound::beep.
#pragma once

#include <cstdint>

#include "core/ports.hpp"

namespace sound {

struct Tune {
    const ports::Beep *steps;
    int count;
};

constexpr uint16_t kBeepHz = 1000; // the pitch the owner accepted (2026-11-18)
constexpr uint16_t kOkMs = 200, kShortMs = 150, kLongMs = 600, kGapMs = 150;

// What the self-test found, worst first: the code played is for the worst
// thing found.
enum class PostResult : uint8_t {
    NoSystem, // long beeps: nothing to start
    Memory,   // three long
    Video,    // one long, two short
    Drive,    // one long, three short
    NotFatal, // two short: another check failed, starting anyway
    Ok,       // one short
};

// The worse of the two.
constexpr PostResult worse(PostResult a, PostResult b) { return a < b ? a : b; }

// The code for a result. NoSystem is one long beep and its pause: whoever
// stays on that screen may play it again.
Tune post_code(PostResult result);

uint32_t length_ms(Tune tune);

} // namespace sound
