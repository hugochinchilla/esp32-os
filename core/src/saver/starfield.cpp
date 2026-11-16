// Starfield: flying through space. Stars come towards the viewer from a
// point that drifts slowly, as if the ship were being steered.
#include "core/gfx/canvas.hpp"
#include "internal.hpp"

namespace saver {

namespace {

constexpr int kStars = 260;
constexpr int kFar = 1024, kNear = 32, kSpeed = 5;
// Dim beyond kDim, grey beyond kMid, full colour nearer, two pixels wide within kBig.
constexpr int kDim = 860, kMid = 620, kBig = 300;
constexpr int kSpreadX = 4096, kSpreadY = 3072, kLens = 80; // far plane: the whole screen

struct Star {
    int16_t x, y, z;
    int16_t px, py;  // where it was drawn
    uint8_t size;    // 0: not on the screen
    uint8_t tint;
    uint8_t colour;
};

// Most stars are white; a few are pale blue, yellow or orange.
constexpr gfx::Colour kTints[4] = {gfx::kWhite, gfx::rgb(5, 6, 3), gfx::rgb(7, 7, 1), gfx::rgb(7, 5, 2)};

class Starfield : public Animation {
public:
    const char *name() const override { return "Starfield"; }

    void start(const Screen &s, uint32_t seed) override
    {
        random_.seed(seed);
        frame_ = 0;
        for (Star &star : stars_) {
            spawn(star);
            star.z = static_cast<int16_t>(random_.range(kNear + 1, kFar));
            star.size = 0;
        }
        place(s);
        draw(s);
    }

    void step(const Screen &s) override
    {
        for (const Star &star : stars_) {
            dot(s, star, gfx::kBlack);
        }
        frame_++;
        for (Star &star : stars_) {
            star.z = static_cast<int16_t>(star.z - kSpeed);
        }
        place(s);
        draw(s);
    }

    void redraw(const Screen &s) override
    {
        clear(s);
        draw(s);
    }

private:
    void spawn(Star &star)
    {
        star.x = static_cast<int16_t>(random_.range(-kSpreadX, kSpreadX));
        star.y = static_cast<int16_t>(random_.range(-kSpreadY, kSpreadY));
        star.z = kFar;
        star.tint = static_cast<uint8_t>(random_.below(8) == 0 ? random_.range(1, 3) : 0);
    }

    // Works out where each star shows now; one that left the screen starts
    // again far away.
    void place(const Screen &s)
    {
        const int cx = s.width / 2 + sine(frame_ * 2) * 70 / 1024;
        const int cy = s.height / 2 + sine(frame_ * 3 + 256) * 40 / 1024;
        for (Star &star : stars_) {
            for (int tries = 0; tries < 2; tries++) {
                const int px = cx + star.x * kLens / star.z;
                const int py = cy + star.y * kLens / star.z;
                star.size = static_cast<uint8_t>(star.z < kBig ? 2 : 1);
                if (star.z > kNear && px >= 0 && py >= 0 && px + star.size <= s.width &&
                    py + star.size <= s.height) {
                    star.px = static_cast<int16_t>(px);
                    star.py = static_cast<int16_t>(py);
                    star.colour = star.z > kDim ? gfx::kDarkGrey : (star.z > kMid ? gfx::kGrey : kTints[star.tint]);
                    break;
                }
                star.size = 0;
                spawn(star);
            }
        }
    }

    static void dot(const Screen &s, const Star &star, uint8_t c)
    {
        for (int dy = 0; dy < star.size; dy++) {
            for (int dx = 0; dx < star.size; dx++) {
                put(s, star.px + dx, star.py + dy, c);
            }
        }
    }

    // Far stars first, so a near one is never hidden by a dim one.
    void draw(const Screen &s)
    {
        for (int pass = 0; pass < 3; pass++) {
            for (const Star &star : stars_) {
                const int layer = star.z > kDim ? 0 : (star.z > kMid ? 1 : 2);
                if (layer == pass) {
                    dot(s, star, star.colour);
                }
            }
        }
    }

    Star stars_[kStars];
    Random random_;
    int frame_ = 0;
};

Starfield g_starfield;

} // namespace

Animation &starfield_animation() { return g_starfield; }

} // namespace saver
