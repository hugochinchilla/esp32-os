// The desktop program's parts that need no window (host/desktop/screen.hpp).
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../desktop/screen.hpp"

#define CHECK(what)                                                    \
    do {                                                               \
        if (!(what)) {                                                 \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #what); \
            exit(1);                                                   \
        }                                                              \
    } while (0)

int main()
{
    using namespace desktop;
    // Colours: the ends, and blue's four levels (0, 2, 5, 7 of 7).
    CHECK(colour(0x00) == 0xFF000000u);
    CHECK(colour(0xFF) == 0xFFFFFFFFu);
    CHECK(colour(0xE0) == 0xFFFF0000u);
    CHECK(colour(0x1C) == 0xFF00FF00u);
    CHECK(colour(0x01) == 0xFF000049u); // 2/7 of 255 = 73
    CHECK(colour(0x02) == 0xFF0000B6u); // 5/7 of 255 = 182
    CHECK(colour(0x20) == 0xFF240000u); // 1/7 of 255 = 36

    // The picture in the window: exact, twice, wide and tall windows.
    Fit f = fit(640, 480, 640, 480);
    CHECK(f.x == 0 && f.y == 0 && f.w == 640 && f.h == 480);
    CHECK(whole_scale(f, 640, 480) == 1);
    f = fit(1280, 960, 640, 480);
    CHECK(f.w == 1280 && f.h == 960 && whole_scale(f, 640, 480) == 2);
    f = fit(1920, 1080, 640, 480); // bars left and right
    CHECK(f.w == 1440 && f.h == 1080 && f.x == 240 && f.y == 0);
    CHECK(whole_scale(f, 640, 480) == 3);
    f = fit(800, 1000, 640, 480); // bars above and below
    CHECK(f.w == 800 && f.h == 600 && f.x == 0 && f.y == 200);
    f = fit(0, 0, 640, 480); // minimized
    CHECK(f.w == 0 && f.h == 0);

    // The mouse: corners, the middle, and outside the picture.
    f = fit(1920, 1080, 640, 480);
    int x = -1, y = -1;
    to_screen(f, 240, 0, 640, 480, x, y);
    CHECK(x == 0 && y == 0);
    to_screen(f, 240 + 1439, 1079, 640, 480, x, y);
    CHECK(x == 639 && y == 479);
    to_screen(f, 960, 540, 640, 480, x, y);
    CHECK(x == 320 && y == 240);
    to_screen(f, 10, 540, 640, 480, x, y); // in the left bar
    CHECK(x == 0 && y == 240);
    to_screen(f, 1900, -5, 640, 480, x, y);
    CHECK(x == 639 && y == 0);
    to_screen(fit(0, 0, 640, 480), 5, 5, 640, 480, x, y);
    CHECK(x == 0 && y == 0);

    // The pointer is mixed in on the way out and the picture is left alone.
    const int w = 64, h = 48;
    std::vector<uint8_t> pixels(w * h, 0x1C);
    std::vector<uint32_t> out(w * h);
    video::PointerState pointer = {10, 10, true, &video::arrow_pointer()};
    to_rgb(pixels.data(), w, h, pointer, out.data());
    int changed = 0;
    for (int i = 0; i < w * h; i++) {
        CHECK(pixels[i] == 0x1C);
        changed += out[i] != 0xFF00FF00u;
    }
    CHECK(changed > 20); // the arrow, in black and white
    CHECK(out[0] == 0xFF00FF00u);
    pointer.visible = false;
    to_rgb(pixels.data(), w, h, pointer, out.data());
    for (int i = 0; i < w * h; i++) {
        CHECK(out[i] == 0xFF00FF00u);
    }
    // The wheel: whole notches at once, a trackpad's small parts added up,
    // each direction kept apart from the other.
    WheelNotches turned;
    CHECK(turned.take(1.0f) == 1 && turned.take(-2.0f) == -2);
    int total = 0;
    for (int i = 0; i < 10; i++) {
        const int n = turned.take(0.25f);
        CHECK(n == 0 || n == 1);
        total += n;
    }
    CHECK(total == 2 && turned.take(0.5f) == 1);
    CHECK(turned.take(-0.75f) == 0 && turned.take(-0.25f) == -1);
    CHECK(turned.take(0.9f) == 0 && turned.take(-1.0f) == -1); // back: the part kept is dropped
    CHECK(turned.take(1000.0f) == 127);
    printf("desktop: ok\n");
    return 0;
}
