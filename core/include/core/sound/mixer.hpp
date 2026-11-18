// The two voices of ports::Sound as samples: what a driver with a sample
// output (the PC's sound card, a browser, one day the board's I2S pin)
// puts behind the port. It keeps the time by counting the samples it is
// asked for.
//
// The beeper is made to sound like the small speaker of a PC: a square
// wave, with the low end a 5 cm cone cannot move taken away.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace sound {

class Mixer {
public:
    // As ports::Sound. None of this may run while render() does: a driver
    // whose output asks for samples on another thread locks around both.
    void play(const int16_t *samples, size_t count);
    void beep(const ports::Beep *steps, int count);
    void stop();
    bool playing() const;

    // The next `count` samples at ports::kSoundRate; silence when nothing
    // plays.
    void render(int16_t *out, size_t count);

    // How loud the beeper is, of 32767.
    static constexpr int kBeepLevel = 9000;

private:
    float beeper_sample();

    const int16_t *clip_ = nullptr;
    size_t clip_left_ = 0;

    ports::Beep steps_[ports::kBeepSteps] = {};
    int step_count_ = 0, step_ = 0;
    uint32_t step_left_ = 0; // samples of the step being played
    float phase_ = 0;        // 0 to 1 through one wave
    float gain_ = 0;         // rises and falls over a few ms: no click
    float hz_ = 0;           // the pitch sounding (kept while the gain falls)
    float low_ = 0, high_in_ = 0, high_ = 0; // the speaker's filters
};

} // namespace sound
