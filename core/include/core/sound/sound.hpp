// The system's sounds (backlog items 58 and 59): which there are, and the
// recordings behind them. They were chosen by the owner on 2026-11-16 and
// are kept in docs/sounds/ as raw samples (ports::kSoundRate).
//
// The recordings are 390 KB together and are not part of core/: a machine
// that can play them hands them in when it starts (the PC and browser
// builds compile them in). A machine that plays nothing, as the board for
// now, hands in none and pays nothing for them.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace sound {

enum class Id : uint8_t {
    Boot,  // the desktop is up: a glass pad with a quiet bell
    Close, // Shut Down: a low piano phrase
    Alert, // a message box
    Error, // something failed
    Bell,  // the terminal's bell
};
constexpr int kIds = 5;
// The recording's name: docs/sounds/<name>.pcm.
const char *name(Id id);

struct Clip {
    const int16_t *samples = nullptr;
    size_t count = 0;
};
// The recordings, kIds of them in the order of Id, kept by the caller for
// good. nullptr: none.
void set_clips(const Clip *clips);
Clip clip(Id id);

// An output that plays nothing: what the calls reach until a machine
// installs its own (os::install_sound).
ports::Sound &silence();

} // namespace sound
