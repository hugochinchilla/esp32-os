// Screensavers: full-screen animations shown after a while with no input.
// All three are our own code and art. They draw straight into the one
// picture in memory and change only the pixels that move, because writing
// the whole 300 KB screen every frame would not keep 30 frames a second in
// external RAM. Integer arithmetic only, so the PC and the board draw the
// same frames from the same seed.
#pragma once

#include <cstdint>

#include "core/wm/wm.hpp"

namespace saver {

constexpr uint32_t kFrameUs = 33333; // 30 frames a second

struct Screen {
    uint8_t *pixels; // one RRRGGGBB byte per pixel
    int width;
    int height;
    // Told which parts change, for a remote screen (may be nullptr).
    void (*changed)(const gfx::Rect &r, void *context) = nullptr;
    void *context = nullptr;

    void touch(const gfx::Rect &r) const
    {
        if (changed != nullptr && !r.empty()) {
            changed(r, context);
        }
    }
};

class Animation {
public:
    virtual const char *name() const = 0;
    // The screen is black. `seed` picks the run.
    virtual void start(const Screen &s, uint32_t seed) = 0;
    // The next frame: takes away what moved and draws it in its new place.
    virtual void step(const Screen &s) = 0;
    // The current frame from nothing, on a black screen. Tests compare it
    // with what the steps left, to find pixels left behind.
    virtual void redraw(const Screen &s) = 0;

protected:
    ~Animation() = default;
};

constexpr int kCount = 3;
enum Index { kStarfield = 0, kChinchillas = 1, kCurves = 2 };
Animation &animation(int index);

// Shows one animation for the window manager, a frame every kFrameUs, and
// keeps the timings of the last run.
class Runner : public wm::ScreenSaver {
public:
    explicit Runner(wm::Wm &wm) : wm_(wm) {}

    void select(int index) { index_ = index < 0 || index >= kCount ? 0 : index; }
    int selected() const { return index_; }

    void begin(uint8_t *pixels, int width, int height, uint32_t seed) override;
    void tick(uint64_t now_us) override;
    void end(uint64_t now_us) override;

    // The last run that ended: frames drawn, how long it showed, and the
    // slowest frame (µs).
    uint32_t frames() const { return frames_; }
    uint64_t shown_us() const { return shown_us_; }
    uint32_t slowest_us() const { return slowest_us_; }

private:
    wm::Wm &wm_;
    int index_ = 0;
    Screen screen_ = {};
    uint64_t began_us_ = 0, next_us_ = 0;
    uint32_t count_ = 0, slowest_ = 0;
    bool first_ = true;
    uint32_t frames_ = 0, slowest_us_ = 0;
    uint64_t shown_us_ = 0;
};

} // namespace saver
