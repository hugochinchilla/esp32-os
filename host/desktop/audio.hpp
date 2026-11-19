// The sound output of the desktop program and of the browser version
// (backlog item 58): the core's mixer behind an SDL audio device. SDL asks
// for samples on a thread of its own on the PC, so every call locks the
// device around the mixer; in a browser it asks on the one thread there is.
#pragma once

#include <cstdio>
#include <cstring>

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "core/sound/mixer.hpp"
#include "core/sound/sound.hpp"

namespace host {

#ifdef __EMSCRIPTEN__
// Whether the browser lets the page be heard. It does not until the page
// has been clicked or typed on, and then asks for no samples at all.
EM_JS(int, web_sound_allowed, (), {
    const sdl = Module['SDL2'];
    return sdl && sdl.audioContext && sdl.audioContext.state === 'running' ? 1 : 0;
});
#endif

// The system's sounds, compiled in (tools/sounds/embed_sounds.py).
extern const sound::Clip kSystemSounds[sound::kIds];

class SdlSound final : public ports::Sound {
public:
    // False: this PC has no sound output (or SDL was built without one),
    // and everything played is dropped. `record`: for the tests, every
    // sample that goes to the device is also written to this WAV file.
    bool open(const char *record = nullptr)
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            return false;
        }
        SDL_AudioSpec want = {};
        want.freq = static_cast<int>(ports::kSoundRate);
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512; // about 23 ms: a beep starts when its line appears
        want.callback = &SdlSound::fill;
        want.userdata = this;
        // No changes allowed: SDL converts to whatever the card wants.
        device_ = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (device_ == 0) {
            return false;
        }
        if (record != nullptr && record[0] != 0) {
            record_ = fopen(record, "wb");
            if (record_ != nullptr) {
                const uint8_t header[44] = {};
                fwrite(header, 1, sizeof(header), record_); // filled in by close()
            }
        }
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    void close()
    {
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_); // no more calls to fill() after this
            device_ = 0;
        }
        if (record_ != nullptr) {
            const uint32_t bytes = recorded_ * 2, rate = ports::kSoundRate;
            uint8_t h[44];
            auto put32 = [&](int at, uint32_t v) {
                for (int i = 0; i < 4; i++) {
                    h[at + i] = static_cast<uint8_t>(v >> (8 * i));
                }
            };
            memcpy(h, "RIFF", 4);
            put32(4, 36 + bytes);
            memcpy(h + 8, "WAVEfmt ", 8);
            put32(16, 16);
            put32(20, 1 | (1u << 16)); // plain samples, one channel
            put32(24, rate);
            put32(28, rate * 2);
            put32(32, 2 | (16u << 16)); // two bytes a sample, 16 bits
            memcpy(h + 36, "data", 4);
            put32(40, bytes);
            fseek(record_, 0, SEEK_SET);
            fwrite(h, 1, sizeof(h), record_);
            fclose(record_);
            record_ = nullptr;
        }
    }

    bool available() const { return device_ != 0; }

    void play(const int16_t *samples, size_t count) override
    {
        if (device_ != 0) {
            SDL_LockAudioDevice(device_);
            mixer_.play(samples, count);
            SDL_UnlockAudioDevice(device_);
        }
    }
    void beep(const ports::Beep *steps, int count) override
    {
        if (device_ != 0) {
            SDL_LockAudioDevice(device_);
            mixer_.beep(steps, count);
            SDL_UnlockAudioDevice(device_);
        }
    }
    void stop() override
    {
        if (device_ != 0) {
            SDL_LockAudioDevice(device_);
            mixer_.stop();
            SDL_UnlockAudioDevice(device_);
        }
    }
    bool playing() override
    {
        if (device_ == 0) {
            return false;
        }
        SDL_LockAudioDevice(device_);
        const bool busy = mixer_.playing();
        SDL_UnlockAudioDevice(device_);
        return busy;
    }

    // Called every round of the program's loop. In a browser that does not
    // let the page be heard yet, nothing asks for samples and time stands
    // still for the mixer: a sound asked for now would come out whenever
    // the page is first clicked, long after its moment. One that has
    // waited half a second is dropped instead.
    void watch()
    {
#ifdef __EMSCRIPTEN__
        if (device_ == 0 || web_sound_allowed() || !mixer_.playing()) {
            waiting_since_ = 0;
        } else if (waiting_since_ == 0) {
            waiting_since_ = SDL_GetTicks() | 1;
        } else if (SDL_GetTicks() - waiting_since_ > 500) {
            mixer_.stop();
            waiting_since_ = 0;
        }
#endif
    }

private:
    // SDL wants `bytes` of samples (the device is locked while this runs).
    static void fill(void *self, Uint8 *stream, int bytes)
    {
        SdlSound &s = *static_cast<SdlSound *>(self);
        const size_t count = static_cast<size_t>(bytes) / 2;
        int16_t *out = reinterpret_cast<int16_t *>(stream);
        s.mixer_.render(out, count);
        if (s.record_ != nullptr) {
            for (size_t i = 0; i < count; i++) { // the file is little-endian whatever the PC is
                const uint8_t pair[2] = {static_cast<uint8_t>(out[i] & 0xFF), static_cast<uint8_t>((out[i] >> 8) & 0xFF)};
                fwrite(pair, 1, 2, s.record_);
            }
            s.recorded_ += static_cast<uint32_t>(count);
        }
    }

    SDL_AudioDeviceID device_ = 0;
    sound::Mixer mixer_;
    FILE *record_ = nullptr;
    uint32_t recorded_ = 0; // samples
#ifdef __EMSCRIPTEN__
    uint32_t waiting_since_ = 0; // SDL_GetTicks() when a sound began to wait for the browser; 0: none does
#endif
};

} // namespace host
