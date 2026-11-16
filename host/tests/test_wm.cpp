// Unit tests for the drawing layer and the window manager. No framework:
// each failed check prints its line, the exit code is the result.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

using gfx::Rect;
using gfx::RectList;
using Bytes = std::vector<uint8_t>;

/* ---- Rectangles ------------------------------------------------------------ */

// Compares a list with a plain grid of booleans, and checks it never overlaps.
static bool same_as_grid(const RectList &list, const std::vector<char> &grid, int w, int h)
{
    std::vector<int> cover(w * h, 0);
    for (int i = 0; i < list.size(); i++) {
        const Rect &r = list[i];
        for (int y = r.y; y < r.bottom(); y++) {
            for (int x = r.x; x < r.right(); x++) {
                if (x < 0 || y < 0 || x >= w || y >= h) {
                    return false;
                }
                cover[y * w + x]++;
            }
        }
    }
    for (int i = 0; i < w * h; i++) {
        if (cover[i] > 1 || (cover[i] == 1) != grid[i]) {
            return false;
        }
    }
    return true;
}

static void test_rect_list()
{
    const int w = 40, h = 30;
    std::mt19937 random(7);
    for (int round = 0; round < 300; round++) {
        RectList list;
        std::vector<char> grid(w * h, 0);
        for (int step = 0; step < 12; step++) {
            Rect r(static_cast<int>(random() % 50) - 5, static_cast<int>(random() % 40) - 5,
                   static_cast<int>(random() % 25), static_cast<int>(random() % 20));
            Rect in = r.intersect({0, 0, w, h});
            int op = static_cast<int>(random() % 3);
            if (op == 0) {
                list.add(in);
            } else if (op == 1) {
                list.subtract(r);
            } else {
                list.intersect(r);
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    bool inside = r.contains(x, y);
                    char &g = grid[y * w + x];
                    g = op == 0 ? (g || inside) : op == 1 ? (g && !inside) : (g && inside);
                }
            }
            CHECK(!list.overflowed());
            CHECK(same_as_grid(list, grid, w, h));
        }
    }

    // Running out of room is reported, never silent.
    RectList many;
    for (int i = 0; i < RectList::kCapacity + 5; i++) {
        many.add({i * 2, 0, 1, 1});
    }
    CHECK(many.overflowed());
    CHECK(many.size() == RectList::kCapacity);

    CHECK(Rect(0, 0, 10, 10).intersect({5, 5, 10, 10}) == Rect(5, 5, 5, 5));
    CHECK(Rect(0, 0, 10, 10).intersect({10, 0, 5, 5}).empty());
    CHECK(gfx::bounding({0, 0, 2, 2}, {5, 5, 1, 1}) == Rect(0, 0, 6, 6));
}

/* ---- Drawing --------------------------------------------------------------- */

static void test_dither()
{
    // The rule of tools/mockup/desktop.py, in floating point.
    const double l3[8] = {0, 36, 73, 109, 146, 182, 219, 255};
    const double lb[4] = {0, 73, 182, 255};
    const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    auto pick = [](double v, const double *levels, int n, double t) {
        int lo = 0;
        while (lo + 1 < n && levels[lo + 1] <= v) {
            lo++;
        }
        if (levels[lo] == v || lo + 1 == n) {
            return lo;
        }
        return (v - levels[lo]) / (levels[lo + 1] - levels[lo]) > t ? lo + 1 : lo;
    };
    for (int v = 0; v < 256; v++) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                double t = (bayer[y][x] + 0.5) / 16;
                gfx::Colour want = gfx::rgb(pick(v, l3, 8, t), pick(255 - v, l3, 8, t), pick(v, lb, 4, t));
                gfx::Rgb c = {static_cast<uint8_t>(v), static_cast<uint8_t>(255 - v), static_cast<uint8_t>(v)};
                CHECK(gfx::dither(c, x + 8, y + 4) == want);
            }
        }
    }
    // Colours the board has are drawn plain.
    CHECK(gfx::dither({182, 182, 182}, 1, 2) == gfx::kGrey);
}

static void test_canvas_clip()
{
    // Drawing never touches pixels outside the clip list or the limit.
    const int w = 64, h = 48;
    Bytes pixels(w * h, 0x11);
    RectList clip;
    clip.add({0, 0, 64, 48});
    clip.subtract({10, 10, 20, 20});
    gfx::Canvas c(pixels.data(), w, h, clip, {4, 4, 50, 40}, 4, 4);
    c.fill({-10, -10, 100, 100}, 0x22);
    c.text(0, 0, "Hola, año", gfx::kBlack, true);
    c.fill_fade({0, 0, 50, 20}, {0, 0, 128}, {16, 132, 208});
    c.raised({0, 0, 50, 40});
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            bool allowed = Rect(4, 4, 50, 40).contains(x, y) && !Rect(10, 10, 20, 20).contains(x, y);
            if (!allowed) {
                CHECK(pixels[y * w + x] == 0x11);
            }
        }
    }
    // Latin-1 through UTF-8, and bold is one pixel wider per character.
    const char *s = "ñ€";
    CHECK(gfx::next_latin1(s) == 0xF1);
    CHECK(gfx::next_latin1(s) == '?');
    CHECK(gfx::next_latin1(s) == 0);
    CHECK(gfx::Canvas::text_width("año") == 18);
    CHECK(gfx::Canvas::text_width("año", true) == 21);
}

/* ---- Window manager -------------------------------------------------------- */

class TestWindow final : public wm::Window {
public:
    TestWindow(const char *title, const Rect &frame, gfx::Colour colour)
        : Window(title, frame, nullptr), colour_(colour),
          button_({10, 10, 60, 24}, "Press", &TestWindow::pressed, this)
    {
        add(button_);
    }
    void paint(gfx::Canvas &c) override
    {
        paints++;
        const Rect cl = client();
        c.fill({0, 0, cl.w, cl.h}, colour_);
        gfx::Canvas b = c.sub(button_.rect);
        button_.paint(b);
    }
    void key(uint16_t usage, bool pressed) override
    {
        if (pressed) {
            last_key = usage;
            keys++;
        }
    }
    void closed() override { was_closed = true; }

    int paints = 0, keys = 0, clicks = 0;
    uint16_t last_key = 0;
    bool was_closed = false;

private:
    static void pressed(wm::Button &, void *context) { static_cast<TestWindow *>(context)->clicks++; }
    gfx::Colour colour_;
    wm::Button button_;
};

struct Rig {
    host::MemoryDisplay display{640, 480};
    host::MemoryPointer pointer;
    host::FixedClock clock;
    wm::Wm wm{display, pointer, clock};

    void move(int x, int y, uint8_t buttons) { wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0}); }
    void click(int x, int y)
    {
        move(x, y, 0);
        move(x, y, 1);
        move(x, y, 0);
    }
    void drag(int x0, int y0, int x1, int y1)
    {
        move(x0, y0, 0);
        move(x0, y0, 1);
        move((x0 + x1) / 2, (y0 + y1) / 2, 1);
        move(x1, y1, 1);
        move(x1, y1, 0);
    }
    uint8_t at(int x, int y) { return display.pixels()[y * 640 + x]; }
    // What is on the screen equals a fresh paint of the same state.
    bool no_stale_pixels()
    {
        Bytes before(display.pixels(), display.pixels() + 640 * 480);
        wm.repaint_all();
        return memcmp(before.data(), display.pixels(), before.size()) == 0;
    }
};

static void test_stacking_and_focus()
{
    Rig rig;
    TestWindow a("A", {50, 50, 200, 150}, 0x1C), b("B", {150, 100, 200, 150}, 0xE0);
    rig.wm.add(a);
    rig.wm.add(b);
    rig.wm.start();
    CHECK(rig.wm.window_count() == 2);
    CHECK(rig.wm.window_at(0) == &a && rig.wm.window_at(1) == &b);
    CHECK(rig.wm.active() == &b && b.focused() && !a.focused());
    CHECK(a.id() != b.id());

    // In the overlap, B is in front: its colour shows.
    CHECK(rig.at(200, 160) == 0xE0);
    CHECK(rig.wm.window_under(200, 160) == &b);
    CHECK(rig.wm.window_under(60, 100) == &a);
    CHECK(rig.wm.window_under(600, 10) == nullptr);

    // Hit testing of the frame.
    CHECK(rig.wm.part_at(b, 200, 110) == wm::Part::Title);
    CHECK(rig.wm.part_at(b, 200, 200) == wm::Part::Client);
    CHECK(rig.wm.part_at(b, 151, 200) == wm::Part::Border);
    Rect close = rig.wm.title_button(b, wm::Part::Close);
    CHECK(rig.wm.part_at(b, close.x + 3, close.y + 3) == wm::Part::Close);

    // Clicking A brings it to the front and gives it the focus; B is asked
    // to paint less, A paints the part that was covered.
    a.paints = 0;
    rig.click(60, 150);
    CHECK(rig.wm.active() == &a && rig.wm.window_at(1) == &a);
    CHECK(rig.at(200, 160) == 0x1C);
    CHECK(a.paints > 0);
    CHECK(rig.no_stale_pixels());

    // Keys go to the focused window only.
    rig.wm.key(0x04, true);
    CHECK(a.keys == 1 && b.keys == 0 && a.last_key == 0x04);

    // Clicking the desktop leaves no window focused.
    rig.click(600, 20);
    CHECK(rig.wm.active() == nullptr);
    rig.wm.key(0x05, true);
    CHECK(a.keys == 1 && b.keys == 0);
    CHECK(rig.no_stale_pixels());
}

static void test_drag()
{
    Rig rig;
    TestWindow a("A", {50, 50, 200, 150}, 0x1C), b("B", {300, 200, 200, 150}, 0xE0);
    rig.wm.add(a);
    rig.wm.add(b);
    rig.wm.start();

    // While dragging only an outline moves; the window moves on release.
    rig.move(100, 60, 0);
    rig.move(100, 60, 1);
    rig.move(200, 160, 1);
    CHECK(rig.wm.dragging());
    CHECK(a.frame().x == 50 && a.frame().y == 50);
    CHECK(rig.wm.active() == &a);
    rig.move(350, 210, 1);
    rig.move(350, 210, 0);
    CHECK(!rig.wm.dragging());
    CHECK(a.frame() == Rect(300, 200, 200, 150));
    CHECK(rig.at(400, 300) == 0x1C); // A now covers B
    CHECK(rig.no_stale_pixels());

    // A window cannot be lost off the screen.
    rig.drag(400, 210, 2000, 2000);
    CHECK(a.frame().x <= 600 && a.frame().y <= 452 - wm::Window::kClientTop);
    rig.drag(a.frame().x + 50, a.frame().y + 10, -5000, -5000);
    CHECK(a.frame().right() >= 40 && a.frame().y >= 0);
    CHECK(rig.no_stale_pixels());
}

static void test_title_buttons_and_taskbar()
{
    Rig rig;
    TestWindow a("A", {50, 50, 200, 150}, 0x1C), b("B", {300, 200, 200, 150}, 0xE0);
    rig.wm.add(a);
    rig.wm.add(b);
    rig.wm.start();

    // Maximise and back.
    Rect max = rig.wm.title_button(b, wm::Part::Maximize);
    rig.click(max.x + 5, max.y + 5);
    CHECK(b.state() == wm::WindowState::Maximized && b.frame() == rig.wm.work_area());
    CHECK(rig.no_stale_pixels());
    max = rig.wm.title_button(b, wm::Part::Maximize);
    rig.click(max.x + 5, max.y + 5);
    CHECK(b.state() == wm::WindowState::Normal && b.frame() == Rect(300, 200, 200, 150));

    // Minimise: hidden, and A gets the focus.
    Rect min = rig.wm.title_button(b, wm::Part::Minimize);
    rig.click(min.x + 5, min.y + 5);
    CHECK(b.state() == wm::WindowState::Minimized);
    CHECK(rig.wm.active() == &a);
    CHECK(rig.wm.window_under(400, 300) == nullptr);
    CHECK(rig.no_stale_pixels());

    // Its taskbar button brings it back, on top and focused...
    Rect tb = rig.wm.taskbar_button(1);
    rig.click(tb.x + 10, tb.y + 10);
    CHECK(b.state() == wm::WindowState::Normal && rig.wm.active() == &b);
    // ...and clicking it again while focused minimises it.
    rig.click(tb.x + 10, tb.y + 10);
    CHECK(b.state() == wm::WindowState::Minimized);
    rig.click(tb.x + 10, tb.y + 10);
    CHECK(rig.no_stale_pixels());

    // A button press that ends outside the button does nothing.
    Rect close = rig.wm.title_button(b, wm::Part::Close);
    rig.move(close.x + 5, close.y + 5, 0);
    rig.move(close.x + 5, close.y + 5, 1);
    rig.move(close.x + 60, close.y + 60, 1);
    rig.move(close.x + 60, close.y + 60, 0);
    CHECK(b.open());
    // A real click closes it.
    rig.click(close.x + 5, close.y + 5);
    CHECK(!b.open() && b.was_closed && rig.wm.window_count() == 1);
    CHECK(rig.wm.active() == &a);
    CHECK(rig.no_stale_pixels());
}

static void test_widget_capture()
{
    Rig rig;
    TestWindow a("A", {50, 50, 200, 150}, 0x1C);
    rig.wm.add(a);
    rig.wm.start();
    const Rect c = a.client();
    // Press on the button, leave it, let go outside: no click.
    rig.move(c.x + 20, c.y + 20, 0);
    rig.move(c.x + 20, c.y + 20, 1);
    rig.move(c.x + 150, c.y + 100, 1);
    rig.move(c.x + 150, c.y + 100, 0);
    CHECK(a.clicks == 0);
    // Press, wander off and come back, let go on it: one click.
    rig.move(c.x + 20, c.y + 20, 1);
    rig.move(c.x + 150, c.y + 100, 1);
    rig.move(c.x + 25, c.y + 25, 1);
    rig.move(c.x + 25, c.y + 25, 0);
    CHECK(a.clicks == 1);
    CHECK(rig.no_stale_pixels());
}

static void test_describe()
{
    Rig rig;
    TestWindow a("Say \"hi\"", {50, 50, 200, 150}, 0x1C);
    rig.wm.add(a);
    rig.wm.start();
    char json[512];
    size_t n = rig.wm.describe_windows(json, sizeof(json));
    CHECK(n == strlen(json));
    CHECK(strstr(json, "\"title\":\"Say \\\"hi\\\"\"") != nullptr);
    CHECK(strstr(json, "\"x\":50,\"y\":50,\"w\":200,\"h\":150,\"state\":\"normal\",\"focused\":true") != nullptr);
    // Too small a buffer gives valid JSON saying so.
    n = rig.wm.describe_windows(json, 40);
    CHECK(strcmp(json, "{\"error\":\"too many windows\"}") == 0);
}

static void test_many_windows()
{
    // A stack of 16 overlapping windows, shuffled about: still no stale pixels.
    Rig rig;
    static TestWindow *windows[16];
    char titles[16][8];
    for (int i = 0; i < 16; i++) {
        snprintf(titles[i], sizeof(titles[i]), "W%d", i);
        windows[i] = new TestWindow(titles[i], {20 + i * 23, 15 + i * 17, 180, 140},
                                    static_cast<gfx::Colour>(i * 16 + 3));
        rig.wm.add(*windows[i]);
    }
    rig.wm.start();
    CHECK(rig.no_stale_pixels());
    std::mt19937 random(3);
    for (int i = 0; i < 40; i++) {
        TestWindow &w = *windows[random() % 16];
        if (!w.open() || w.state() != wm::WindowState::Normal) {
            continue;
        }
        rig.drag(w.frame().x + 40, w.frame().y + 10, static_cast<int>(random() % 600),
                 static_cast<int>(random() % 420));
    }
    CHECK(rig.no_stale_pixels());
    for (TestWindow *w : windows) {
        if (w->open()) {
            w->close();
        }
        delete w;
    }
}

int main()
{
    test_rect_list();
    test_dither();
    test_canvas_clip();
    test_stacking_and_focus();
    test_drag();
    test_title_buttons_and_taskbar();
    test_widget_capture();
    test_describe();
    test_many_windows();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
