// Flying Chinchillas: the mascot's face, in the fur colours chinchillas come
// in, flying out of the distance and growing as it nears.
#include "core/gfx/art.hpp"
#include "core/gfx/rect.hpp"
#include "internal.hpp"

namespace saver {

namespace {

using gfx::Rect;

constexpr int kSprites = 14;
constexpr int kFar = 1024, kNear = 150, kSpeed = 5;
// They start within the middle third of the screen and leave past its edges.
constexpr int kSpreadX = 2200, kSpreadY = 1650, kLens = 80;
constexpr int kMaxScale = 5;

// Fur for the face (white in the mascot): white, standard grey, beige, and
// the pale blue-grey breeders call sapphire.
constexpr gfx::Colour kFurs[4] = {gfx::kWhite, gfx::kGrey, gfx::rgb(7, 6, 2), gfx::rgb(5, 6, 3)};

struct Sprite {
    int16_t x, y, z;
    uint8_t fur;
    Rect shown; // where it is on the screen now (empty: nowhere)
};

class Chinchillas : public Animation {
public:
    const char *name() const override { return "Flying Chinchillas"; }

    void start(const Screen &s, uint32_t seed) override
    {
        random_.seed(seed);
        for (Sprite &sprite : sprites_) {
            spawn(sprite);
            sprite.z = static_cast<int16_t>(random_.range(kNear + 1, kFar));
            sprite.shown = {};
        }
        place(s);
        redraw(s);
    }

    void step(const Screen &s) override
    {
        Rect before[kSprites];
        for (int i = 0; i < kSprites; i++) {
            before[i] = sprites_[i].shown;
            sprites_[i].z = static_cast<int16_t>(sprites_[i].z - kSpeed);
        }
        place(s);
        // Each place a sprite was or is gets its final pixels in one pass
        // (black, then every sprite that covers it, far to near), so nothing
        // flashes and overlapping sprites stay whole.
        for (int i = 0; i < kSprites; i++) {
            const Rect now = sprites_[i].shown;
            if (before[i].overlaps(now)) {
                compose(s, gfx::bounding(before[i], now));
            } else {
                compose(s, before[i]);
                compose(s, now);
            }
        }
    }

    void redraw(const Screen &s) override
    {
        clear(s);
        for (int i = 0; i < kSprites; i++) {
            compose(s, sprites_[order_[i]].shown);
        }
    }

private:
    void spawn(Sprite &sprite)
    {
        // Not straight ahead: those would sit in the middle and only grow.
        do {
            sprite.x = static_cast<int16_t>(random_.range(-kSpreadX, kSpreadX));
            sprite.y = static_cast<int16_t>(random_.range(-kSpreadY, kSpreadY));
        } while (sprite.x > -350 && sprite.x < 350 && sprite.y > -260 && sprite.y < 260);
        sprite.z = kFar;
        sprite.fur = static_cast<uint8_t>(random_.below(4));
    }

    static int scale_at(int z) { return z > 800 ? 1 : (z > 620 ? 2 : (z > 470 ? 3 : (z > 340 ? 4 : kMaxScale))); }

    void place(const Screen &s)
    {
        const Rect screen = {0, 0, s.width, s.height};
        for (Sprite &sprite : sprites_) {
            for (int tries = 0; tries < 2; tries++) {
                const int size = 16 * scale_at(sprite.z);
                const Rect r = {s.width / 2 + sprite.x * kLens / sprite.z - size / 2,
                                s.height / 2 + sprite.y * kLens / sprite.z - size / 2, size, size};
                if (sprite.z > kNear && r.overlaps(screen)) {
                    sprite.shown = r;
                    break;
                }
                sprite.shown = {};
                spawn(sprite);
            }
        }
        // Far to near (an insertion sort: the order hardly changes).
        for (int i = 0; i < kSprites; i++) {
            order_[i] = static_cast<uint8_t>(i);
        }
        for (int i = 1; i < kSprites; i++) {
            const uint8_t v = order_[i];
            int j = i;
            while (j > 0 && sprites_[order_[j - 1]].z < sprites_[v].z) {
                order_[j] = order_[j - 1];
                j--;
            }
            order_[j] = v;
        }
    }

    void compose(const Screen &s, const Rect &area)
    {
        const Rect r = area.intersect({0, 0, s.width, s.height});
        if (r.empty()) {
            return;
        }
        const gfx::Bitmap &face = gfx::mascot_16();
        uint8_t row[kMaxScale * 16 * 2 + 16]; // the widest area: a sprite and where it was
        const bool fits = r.w <= static_cast<int>(sizeof(row));
        for (int y = r.y; y < r.bottom(); y++) {
            uint8_t *out = fits ? row : s.pixels + y * s.width + r.x;
            for (int x = 0; x < r.w; x++) {
                out[x] = gfx::kBlack;
            }
            for (int i = 0; i < kSprites; i++) {
                const Sprite &sprite = sprites_[order_[i]];
                const Rect &p = sprite.shown;
                if (p.empty() || y < p.y || y >= p.bottom()) {
                    continue;
                }
                const int scale = p.w / 16;
                const uint8_t *src = face.pixels + (y - p.y) / scale * face.width;
                const int from = p.x > r.x ? p.x : r.x;
                const int to = p.right() < r.right() ? p.right() : r.right();
                for (int x = from; x < to; x++) {
                    const uint8_t c = src[(x - p.x) / scale];
                    if (c != face.transparent) {
                        out[x - r.x] = c == gfx::kWhite ? kFurs[sprite.fur] : c;
                    }
                }
            }
            if (fits) {
                uint8_t *to = s.pixels + y * s.width + r.x;
                for (int x = 0; x < r.w; x++) {
                    to[x] = row[x];
                }
            }
        }
    }

    Sprite sprites_[kSprites];
    uint8_t order_[kSprites] = {};
    Random random_;
};

Chinchillas g_chinchillas;

} // namespace

Animation &chinchillas_animation() { return g_chinchillas; }

} // namespace saver
