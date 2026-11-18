#include "core/sound/mixer.hpp"

namespace sound {

namespace {

constexpr float kRate = static_cast<float>(ports::kSoundRate);
// The gain moves this much a sample: all the way in about 2 ms.
constexpr float kGainStep = 1.0f / (kRate * 0.002f);
// The speaker: nothing much under 350 Hz, rounded off above 6 kHz. Each is
// a filter of one stage; the numbers are 2*pi*f/rate and 1/(1 + 2*pi*f/rate).
constexpr float kLowPass = 0.63f;   // 6 kHz
constexpr float kHighPass = 0.909f; // 350 Hz
constexpr float kSilent = 1.0f / 32768.0f;

// A square wave has edges, and an edge that falls between two samples
// rings at pitches that were never asked for. This rounds each edge over
// the two samples around it (known as polyBLEP). `t` is the phase, `dt`
// how far it moves in one sample.
float edge(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

} // namespace

void Mixer::play(const int16_t *samples, size_t count)
{
    clip_ = samples;
    clip_left_ = samples != nullptr ? count : 0;
}

void Mixer::beep(const ports::Beep *steps, int count)
{
    count = steps == nullptr || count < 0 ? 0 : (count > ports::kBeepSteps ? ports::kBeepSteps : count);
    for (int i = 0; i < count; i++) {
        steps_[i] = steps[i];
    }
    step_count_ = count;
    step_ = 0;
    step_left_ = count > 0 ? static_cast<uint32_t>(steps_[0].ms) * ports::kSoundRate / 1000 : 0;
}

void Mixer::stop()
{
    play(nullptr, 0);
    beep(nullptr, 0);
}

bool Mixer::playing() const
{
    return clip_left_ > 0 || step_ < step_count_ || gain_ > 0 || high_ > kSilent || high_ < -kSilent;
}

float Mixer::beeper_sample()
{
    // Which step this sample belongs to.
    while (step_ < step_count_ && step_left_ == 0) {
        step_++;
        step_left_ = step_ < step_count_ ? static_cast<uint32_t>(steps_[step_].ms) * ports::kSoundRate / 1000 : 0;
    }
    float want = 0;
    if (step_ < step_count_) {
        step_left_--;
        if (steps_[step_].hz != 0) {
            want = 1;
            // Kept through a silent step, so that the note fades at its own
            // pitch.
            hz_ = static_cast<float>(steps_[step_].hz);
        }
    }
    gain_ = want > gain_ ? (gain_ + kGainStep > want ? want : gain_ + kGainStep)
                         : (gain_ - kGainStep < want ? want : gain_ - kGainStep);
    float square = 0;
    if (gain_ > 0 && hz_ > 0) {
        float dt = hz_ / kRate;
        dt = dt > 0.45f ? 0.45f : dt; // above what the rate can carry
        phase_ += dt;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
        }
        const float half = phase_ < 0.5f ? phase_ + 0.5f : phase_ - 0.5f;
        square = (phase_ < 0.5f ? 1.0f : -1.0f) + edge(phase_, dt) - edge(half, dt);
    } else {
        phase_ = 0;
    }
    // The speaker.
    const float in = square * gain_;
    low_ += kLowPass * (in - low_);
    high_ = kHighPass * (high_ + low_ - high_in_);
    high_in_ = low_;
    return high_;
}

void Mixer::render(int16_t *out, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        int32_t sample = 0;
        if (clip_left_ > 0) {
            sample = *clip_++;
            clip_left_--;
        }
        if (step_ < step_count_ || gain_ > 0 || high_ > kSilent || high_ < -kSilent) {
            sample += static_cast<int32_t>(beeper_sample() * static_cast<float>(kBeepLevel));
        } else {
            low_ = high_in_ = high_ = 0;
        }
        out[i] = static_cast<int16_t>(sample > 32767 ? 32767 : (sample < -32768 ? -32768 : sample));
    }
}

} // namespace sound
