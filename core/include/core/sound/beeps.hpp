// What the piezo beeper plays: the self-test's beep codes, as PCs had, and
// the chime when the desktop is up. Each is a list of steps for
// ports::System::beep. Heard, as a rough model of the part makes them, with
// tools/sounds/make_beeps.py, which holds the same lists.
#pragma once

#include <cstddef>

#include "core/ports.hpp"

namespace sound {

struct Tune {
    const ports::Beep *steps;
    int count;
};

constexpr uint16_t kBeepHz = 1000; // the PC's pitch
constexpr uint16_t kShortMs = 150, kLongMs = 600, kGapMs = 150;

// What the self-test found, worst first: the code played is the first
// that applies.
enum class PostResult : uint8_t {
    NoSystem, // long beeps without end: nothing to start
    Memory,   // three long
    Video,    // one long, two short
    Drive,    // one long, three short
    NotFatal, // two short: another check failed, starting anyway
    Ok,       // one short
};

// The code for a result. NoSystem is one long beep and its pause: the
// caller plays it again for as long as it stays there.
Tune post_code(PostResult result);

// The desktop is up.
Tune desktop_chime();

uint32_t length_ms(Tune tune);

// A tune written as text, one step a line: the pitch in Hz (0: silence),
// how long in ms, and optionally the fade in ms and the starting loudness,
// 1 to 255 (ports::Beep). A '#' starts a comment; a line that is not a step
// is skipped. Returns how many steps were written to `out`.
constexpr const char *kChimeFile = "C:\\.config\\chime.txt";
int parse_tune(const char *text, size_t size, ports::Beep *out, int capacity);

} // namespace sound
