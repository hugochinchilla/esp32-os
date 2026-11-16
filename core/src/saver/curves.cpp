// Curves and Colours: two closed curves whose control points bounce around
// the screen, each leaving a trail that runs through the colour wheel.
#include "core/gfx/canvas.hpp"
#include "internal.hpp"

namespace saver {

namespace {

constexpr int kCurves = 2;
constexpr int kPoints = 4;  // control points of each curve
constexpr int kTrail = 12;  // copies kept on the screen
constexpr int kPieces = 12; // straight pieces per curve segment
constexpr int kHues = 252;

// The colour wheel in board colours: red, yellow, green, cyan, blue, magenta.
gfx::Colour hue(int h)
{
    h %= kHues;
    const int part = h / 42, up = (h % 42) * 7 / 41, down = 7 - up;
    int r = 0, g = 0, b = 0;
    switch (part) {
    case 0: r = 7, g = up; break;
    case 1: r = down, g = 7; break;
    case 2: g = 7, b = up; break;
    case 3: g = down, b = 7; break;
    case 4: r = up, b = 7; break;
    default: r = 7, b = down; break;
    }
    return gfx::rgb(static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>((b * 3 + 3) / 7));
}

struct Shape {
    int16_t x[kPoints], y[kPoints];
    uint8_t colour;
};

struct Curve {
    int x[kPoints], y[kPoints];   // sixteenths of a pixel
    int vx[kPoints], vy[kPoints];
    Shape trail[kTrail];          // a ring; `head` is the newest
    int head, count;
    int hue;
};

class Curves : public Animation {
public:
    const char *name() const override { return "Curves and Colours"; }

    void start(const Screen &s, uint32_t seed) override
    {
        random_.seed(seed);
        const int first_hue = random_.below(kHues);
        for (int c = 0; c < kCurves; c++) {
            Curve &curve = curves_[c];
            for (int i = 0; i < kPoints; i++) {
                curve.x[i] = random_.range(kMargin, s.width - 1 - kMargin) * 16;
                curve.y[i] = random_.range(kMargin, s.height - 1 - kMargin) * 16;
                curve.vx[i] = random_.range(28, 76) * (random_.below(2) ? 1 : -1);
                curve.vy[i] = random_.range(28, 76) * (random_.below(2) ? 1 : -1);
            }
            curve.head = -1;
            curve.count = 0;
            curve.hue = (first_hue + c * kHues / kCurves) % kHues; // opposite sides of the wheel
            remember(curve);
        }
        draw_all(s);
    }

    void step(const Screen &s) override
    {
        for (Curve &curve : curves_) {
            if (curve.count == kTrail) {
                // The oldest copy goes; what it crossed is mended below.
                draw(s, curve.trail[(curve.head + 1) % kTrail], gfx::kBlack, true);
                curve.count--;
            }
            for (int i = 0; i < kPoints; i++) {
                bounce(curve.x[i], curve.vx[i], s.width);
                bounce(curve.y[i], curve.vy[i], s.height);
            }
            curve.hue = (curve.hue + 2) % kHues;
            remember(curve);
        }
        draw_all(s);
    }

    void redraw(const Screen &s) override
    {
        clear(s);
        draw_all(s);
    }

private:
    static constexpr int kMargin = 4;

    static void bounce(int &p, int &v, int size)
    {
        p += v;
        const int lo = kMargin * 16, hi = (size - 1 - kMargin) * 16;
        if (p < lo) {
            p = 2 * lo - p;
            v = -v;
        } else if (p > hi) {
            p = 2 * hi - p;
            v = -v;
        }
    }

    static void remember(Curve &curve)
    {
        curve.head = (curve.head + 1) % kTrail;
        curve.count++;
        Shape &shape = curve.trail[curve.head];
        for (int i = 0; i < kPoints; i++) {
            shape.x[i] = static_cast<int16_t>(curve.x[i] / 16);
            shape.y[i] = static_cast<int16_t>(curve.y[i] / 16);
        }
        shape.colour = hue(curve.hue);
    }

    // A closed quadratic B-spline round the control points.
    static void draw(const Screen &s, const Shape &shape, uint8_t colour, bool report)
    {
        for (int i = 0; i < kPoints; i++) {
            const int j = (i + 1) % kPoints, k = (i + 2) % kPoints;
            int px = (shape.x[i] + shape.x[j]) / 2, py = (shape.y[i] + shape.y[j]) / 2;
            for (int n = 1; n <= kPieces; n++) {
                // Weights over 2 * kPieces^2.
                const int a = (kPieces - n) * (kPieces - n), c = n * n;
                const int b = 2 * kPieces * kPieces - a - c;
                const int d = 2 * kPieces * kPieces;
                const int x = (a * shape.x[i] + b * shape.x[j] + c * shape.x[k] + d / 2) / d;
                const int y = (a * shape.y[i] + b * shape.y[j] + c * shape.y[k] + d / 2) / d;
                line(s, px, py, x, y, colour);
                if (report) {
                    s.touch({px < x ? px : x, py < y ? py : y, (px < x ? x - px : px - x) + 1,
                             (py < y ? y - py : py - y) + 1});
                }
                px = x;
                py = y;
            }
        }
    }

    // Every copy, oldest first, so the newest lies on top. Only the newest
    // is news to a remote screen: the others mend what the erased copy crossed,
    // which was reported with it.
    void draw_all(const Screen &s)
    {
        for (int age = kTrail - 1; age >= 0; age--) {
            for (const Curve &curve : curves_) {
                if (age < curve.count) {
                    const Shape &shape = curve.trail[(curve.head - age + kTrail) % kTrail];
                    draw(s, shape, shape.colour, age == 0);
                }
            }
        }
    }

    Curve curves_[kCurves];
    Random random_;
};

Curves g_curves;

} // namespace

Animation &curves_animation() { return g_curves; }

} // namespace saver
