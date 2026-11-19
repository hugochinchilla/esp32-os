// Unit tests for the drawing layer and the window manager. No framework:
// each failed check prints its line, the exit code is the result.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/wm/widgets.hpp"
#include "core/desk/desk.hpp"
#include "core/wm/wm.hpp"
#include "core/video/pointer.hpp"
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
    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed) {
            last_key = k.usage;
            keys++;
        }
        return true;
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
    // The desktop shell's two surfaces, as a machine has them.
    desk::Icons desk_icons{wm};
    desk::Taskbar bar{wm};
    Rig()
    {
        desk_icons.attach();
        bar.attach();
    }

    // Each event moves the clock on, so separate clicks never make a
    // double click by accident; quick() leaves it alone.
    void move(int x, int y, uint8_t buttons)
    {
        clock.now += 200000;
        quick(x, y, buttons);
    }
    // Lets time pass and fires the timers that are due.
    void wait(uint32_t us)
    {
        clock.now += us;
        wm.tick();
    }
    void quick(int x, int y, uint8_t buttons)
    {
        wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
    }
    // The wheel turned with the pointer there: up is positive, right is.
    void wheel(int x, int y, int up, int right = 0, uint8_t buttons = 0)
    {
        wm.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, static_cast<int8_t>(up),
                  static_cast<int8_t>(right)});
    }
    void double_click(int x, int y)
    {
        move(x, y, 0);
        quick(x, y, 1);
        quick(x, y, 0);
        clock.now += 100000;
        quick(x, y, 1);
        quick(x, y, 0);
    }
    void key(uint16_t usage, uint8_t mods = 0)
    {
        // Modifiers held around one key press.
        const uint16_t mod_keys[4] = {wm::key::LeftShift, wm::key::LeftCtrl, wm::key::LeftAlt, wm::key::LeftGui};
        for (int i = 0; i < 4; i++) {
            if (mods & (1 << i)) {
                wm.key(mod_keys[i], true);
            }
        }
        wm.key(usage, true);
        wm.key(usage, false);
        for (int i = 0; i < 4; i++) {
            if (mods & (1 << i)) {
                wm.key(mod_keys[i], false);
            }
        }
    }
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
    uint8_t at(int x, int y) { return display.pixels()[y * display.width() + x]; }
    // What is on the screen equals a fresh paint of the same state.
    bool no_stale_pixels()
    {
        Bytes before(display.pixels(), display.pixels() + display.width() * display.height());
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
    rig.key(0x04);
    CHECK(a.keys == 1 && b.keys == 0 && a.last_key == 0x04);

    // Clicking the desktop leaves no window focused.
    rig.click(600, 20);
    CHECK(rig.wm.active() == nullptr);
    rig.key(0x05);
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
    Rect tb = rig.bar.button(1);
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

// No limit on the windows open at once (the owner's decision, 2026-11-17):
// past the sixteen the lists begin with they grow out of the Memory port,
// and where there is no memory the next window does not open.
static void test_no_window_limit()
{
    constexpr int kMany = 70;
    static TestWindow *windows[kMany];
    char titles[kMany][8];
    Rig rig;
    for (int i = 0; i < kMany; i++) {
        snprintf(titles[i], sizeof(titles[i]), "W%d", i);
        windows[i] = new TestWindow(titles[i], {10 + i * 7, 10 + i * 4, 120, 90}, static_cast<gfx::Colour>(i * 3 + 1));
    }
    // Without a port: sixteen, and the seventeenth stays closed.
    for (int i = 0; i < 17; i++) {
        rig.wm.add(*windows[i]);
    }
    rig.wm.start();
    CHECK(rig.wm.window_count() == 16 && windows[15]->open() && !windows[16]->open());
    // With one: all of them, in order, the last in front.
    host::HostMemory memory;
    rig.wm.set_memory(&memory);
    for (int i = 16; i < kMany; i++) {
        rig.wm.add(*windows[i]);
    }
    CHECK(rig.wm.window_count() == kMany && rig.wm.active() == windows[kMany - 1] && memory.in_use() == 1);
    CHECK(rig.wm.taskbar_count() == kMany && rig.wm.taskbar_window(0) == windows[0] &&
          rig.wm.taskbar_window(kMany - 1) == windows[kMany - 1]);
    rig.wm.update();
    CHECK(rig.no_stale_pixels());
    rig.wm.activate(*windows[3]);
    rig.wm.update();
    CHECK(rig.wm.active() == windows[3] && rig.no_stale_pixels());
    // Closed again: back to the lists it began with, and the block returned.
    for (int i = kMany - 1; i >= 0; i--) {
        windows[i]->close();
        CHECK(rig.wm.window_count() == i);
    }
    CHECK(memory.in_use() == 0);
    rig.wm.update();
    CHECK(rig.no_stale_pixels());
    for (TestWindow *w : windows) {
        delete w;
    }
}


/* ---- Resizing, shortcuts --------------------------------------------------------- */

static void test_resize()
{
    Rig rig;
    TestWindow a("A", {100, 100, 200, 150}, 0x1C);
    rig.wm.add(a);
    rig.wm.start();

    // The pointer changes shape over the edges and corners.
    rig.move(100 + 1, 150, 0);
    CHECK(memcmp(rig.pointer.state().shape, &video::resize_we_pointer(), sizeof(video::PointerShape)) == 0);
    rig.move(200, 100 + 148, 0);
    CHECK(memcmp(rig.pointer.state().shape, &video::resize_ns_pointer(), sizeof(video::PointerShape)) == 0);
    rig.move(100 + 198, 100 + 148, 0);
    CHECK(memcmp(rig.pointer.state().shape, &video::resize_nwse_pointer(), sizeof(video::PointerShape)) == 0);
    rig.move(100 + 198, 101, 0);
    CHECK(memcmp(rig.pointer.state().shape, &video::resize_nesw_pointer(), sizeof(video::PointerShape)) == 0);
    rig.move(200, 200, 0);
    CHECK(memcmp(rig.pointer.state().shape, &video::arrow_pointer(), sizeof(video::PointerShape)) == 0);

    // While a program keeps the desktop waiting the pointer is the
    // hourglass, over an edge too; waits nest.
    {
        const wm::Wait outer(rig.wm);
        CHECK(rig.wm.waiting());
        CHECK(memcmp(rig.pointer.state().shape, &video::busy_pointer(), sizeof(video::PointerShape)) == 0);
        {
            const wm::Wait inner(rig.wm);
            rig.move(100 + 1, 150, 0);
        }
        CHECK(memcmp(rig.pointer.state().shape, &video::busy_pointer(), sizeof(video::PointerShape)) == 0);
    }
    CHECK(!rig.wm.waiting());
    CHECK(memcmp(rig.pointer.state().shape, &video::resize_we_pointer(), sizeof(video::PointerShape)) == 0);
    rig.move(200, 200, 0);
    CHECK(memcmp(rig.pointer.state().shape, &video::arrow_pointer(), sizeof(video::PointerShape)) == 0);

    // Bottom right corner: an outline while dragging, the new size on release.
    rig.move(298, 248, 0);
    rig.move(298, 248, 1);
    rig.move(358, 288, 1);
    CHECK(rig.wm.dragging() && a.frame() == Rect(100, 100, 200, 150));
    rig.move(358, 288, 0);
    CHECK(a.frame() == Rect(100, 100, 260, 190));
    CHECK(rig.no_stale_pixels());

    // The left edge stops at the minimum width.
    rig.drag(100, 200, 600, 200);
    CHECK(a.frame().w == wm::Window::kMinWidth && a.frame().right() == 360);
    // The top edge stops at the minimum height.
    rig.drag(250, a.frame().y + 1, 250, 470);
    CHECK(a.frame().h == a.min_height() && a.frame().bottom() == 290);
    CHECK(rig.no_stale_pixels());

    // A window that cannot be resized has no resize edges or maximise.
    TestWindow b("B", {400, 40, 200, 100}, 0xE0);
    b.set_resizable(false);
    rig.wm.add(b);
    CHECK(rig.wm.edges_at(b, 401, 80) == 0);
    rig.double_click(500, 50);
    CHECK(b.state() == wm::WindowState::Normal);
}

static void test_shortcuts()
{
    Rig rig;
    TestWindow a("A", {50, 50, 200, 150}, 0x1C), b("B", {150, 100, 200, 150}, 0xE0),
        c("C", {250, 150, 200, 150}, 0x03);
    rig.wm.add(a);
    rig.wm.add(b);
    rig.wm.add(c);
    rig.wm.start();

    // Double click on the title bar maximises and restores.
    rig.double_click(300, 160);
    CHECK(c.state() == wm::WindowState::Maximized);
    rig.double_click(300, 10);
    CHECK(c.state() == wm::WindowState::Normal && c.frame() == Rect(250, 150, 200, 150));
    // Two clicks far apart in time do not.
    rig.click(300, 160);
    rig.click(300, 160);
    CHECK(c.state() == wm::WindowState::Normal);

    // Alt+Tab brings each window to the front in turn.
    rig.key(wm::key::Tab, wm::kAlt);
    CHECK(rig.wm.active() == &a);
    rig.key(wm::key::Tab, wm::kAlt);
    CHECK(rig.wm.active() == &b);
    rig.key(wm::key::Tab, wm::kAlt);
    CHECK(rig.wm.active() == &c);
    // Alt+F4 closes the focused window; the keys never reach it.
    const int keys = c.keys;
    rig.key(wm::key::F4, wm::kAlt);
    CHECK(!c.open() && c.keys == keys);
    CHECK(rig.no_stale_pixels());
}

/* ---- Menus ------------------------------------------------------------------- */

static int g_runs[4];
static void run0(void *) { g_runs[0]++; }
static void run1(void *) { g_runs[1]++; }
static void run2(void *) { g_runs[2]++; }

class MenuWindow final : public wm::Window {
public:
    MenuWindow() : Window("Menus", {100, 60, 300, 200}, nullptr) { set_menu_bar(&bar_); }

    wm::MenuItem sub_items[2] = {
        {"&Inner", &run2, nullptr, nullptr, nullptr, 0},
        {"&Other", nullptr, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu sub = {sub_items, 2};
    wm::MenuItem file_items[4] = {
        {"&Open", &run0, nullptr, nullptr, nullptr, 0},
        {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator},
        {"&Grey", &run1, nullptr, nullptr, nullptr, wm::kMenuDisabled},
        {"&More", nullptr, nullptr, &sub, nullptr, wm::kMenuChecked},
    };
    wm::Menu file = {file_items, 4};
    wm::MenuItem edit_items[1] = {{"&Undo", &run1, nullptr, nullptr, nullptr, 0}};
    wm::Menu edit = {edit_items, 1};
    const wm::MenuBarEntry entries[2] = {{"&File", &file}, {"&Edit", &edit}};
    const wm::MenuBar bar_ = {entries, 2};
};

static void test_menus()
{
    Rig rig;
    TestWindow behind("Behind", {380, 300, 200, 120}, 0xE0);
    MenuWindow w;
    rig.wm.add(behind);
    rig.wm.add(w);
    rig.wm.start();
    memset(g_runs, 0, sizeof(g_runs));
    CHECK(w.client().y == 60 + wm::Window::kClientTop + wm::Window::kMenuBarHeight);

    // Click on File: its menu opens under it.
    const Rect file = rig.wm.menu_bar_entry(w, 0);
    rig.click(file.x + 5, file.y + 5);
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_rect(0).y == file.bottom());
    CHECK(rig.no_stale_pixels());
    // Hovering More opens its submenu to the right.
    Rect more = rig.wm.popup_item(0, 3);
    rig.move(more.x + 10, more.y + 5, 0);
    CHECK(rig.wm.popup_count() == 1); // after a short rest
    rig.wait(wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_rect(1).x >= more.right());
    // The disabled item does nothing.
    Rect grey = rig.wm.popup_item(0, 2);
    rig.click(grey.x + 10, grey.y + 5);
    CHECK(g_runs[1] == 0 && rig.wm.popup_count() == 1);
    // Choosing Open runs it and closes the menu.
    Rect open = rig.wm.popup_item(0, 0);
    rig.click(open.x + 10, open.y + 5);
    CHECK(g_runs[0] == 1 && rig.wm.popup_count() == 0);
    CHECK(rig.no_stale_pixels());

    // Keyboard: Alt+F opens File, Down Down picks More (the separator is
    // skipped), Right opens it, Enter runs Inner.
    rig.key(0x09, wm::kAlt); // F
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_hover(0) == 0);
    rig.key(wm::key::Down);
    CHECK(rig.wm.popup_hover(0) == 2);
    rig.key(wm::key::Down);
    CHECK(rig.wm.popup_hover(0) == 3);
    rig.key(wm::key::Right);
    CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_hover(1) == 0);
    rig.key(wm::key::Enter);
    CHECK(g_runs[2] == 1 && rig.wm.popup_count() == 0);
    // Access letters inside a menu, Left/Right across the bar, Escape.
    rig.key(0x09, wm::kAlt);
    rig.key(wm::key::Right);
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_rect(0).x == rig.wm.menu_bar_entry(w, 1).x);
    rig.key(0x18); // U: Undo
    CHECK(g_runs[1] == 1 && rig.wm.popup_count() == 0);
    rig.key(0x09, wm::kAlt);
    rig.key(wm::key::Escape);
    CHECK(rig.wm.popup_count() == 0);

    // A click elsewhere closes the menu and goes no further.
    rig.click(file.x + 5, file.y + 5);
    rig.click(450, 350);
    CHECK(rig.wm.popup_count() == 0 && rig.wm.active() == &w);
    CHECK(rig.no_stale_pixels());
}

/* ---- The hourglass on demand --------------------------------------------------- */

static bool pointer_is(Rig &rig, const video::PointerShape &shape)
{
    const video::PointerShape *now = rig.pointer.state().shape;
    return now != nullptr && memcmp(now, &shape, sizeof(video::PointerShape)) == 0;
}

// A program that is working something out asks for the hourglass over its
// window (backlog item 49). Only the pointer changes: the window answers.
static int g_leaves;
static void leave(void *) { g_leaves++; }

// Alt+F4 with no window in the focus is the shell's (Shut Down); the logo
// key with D shows the desktop and brings the windows back.
static void test_desktop_shortcuts()
{
    Rig rig;
    TestWindow a("A", {50, 50, 200, 150}, 0x1C), b("B", {150, 100, 200, 150}, 0xE0),
        c("C", {250, 150, 200, 150}, 0x03);
    rig.wm.add(a);
    rig.wm.add(b);
    rig.wm.add(c);
    wm::MenuItem items[1] = {{"Sh&ut Down...", &run0, nullptr, nullptr, nullptr, 0}};
    wm::Menu start = {items, 1};
    rig.wm.set_start_menu(&start, "Chinchilla");
    rig.wm.start();
    rig.wm.set_leave_key(&leave, nullptr);
    g_leaves = 0;
    const auto held = [&rig](uint16_t modifier, uint16_t usage) {
        rig.wm.key(modifier, true);
        rig.wm.key(usage, true);
        rig.wait(wm::Wm::kRepeatDelayUs + 10000);
        for (int i = 0; i < 5; i++) {
            rig.wait(wm::Wm::kRepeatUs);
        }
        rig.wm.key(usage, false);
        rig.wm.key(modifier, false);
    };

    // With a window in the focus Alt+F4 closes it and the shell hears nothing.
    rig.key(wm::key::F4, wm::kAlt);
    CHECK(!c.open() && g_leaves == 0 && rig.wm.active() == &b);
    // A click on the desktop takes the focus from every window: now the key
    // is the shell's, once however long it is held.
    rig.click(600, 400);
    CHECK(rig.wm.active() == nullptr);
    held(wm::key::LeftAlt, wm::key::F4);
    CHECK(g_leaves == 1 && a.open() && b.open());

    // Logo+D: both windows minimised, nothing in the focus, their buttons
    // still on the taskbar, and no Start menu when the logo key comes up.
    const uint16_t d = static_cast<uint16_t>(wm::key::A + 3);
    rig.click(200, 110); // B, on top of A
    CHECK(rig.wm.active() == &b);
    b.keys = 0;
    rig.key(d, wm::kGui);
    CHECK(a.state() == wm::WindowState::Minimized && b.state() == wm::WindowState::Minimized);
    CHECK(rig.wm.active() == nullptr && rig.wm.taskbar_count() == 2 && b.keys == 0);
    CHECK(!rig.wm.start_menu_open());
    CHECK(rig.no_stale_pixels());
    // Alt+F4 there is the shell's too.
    rig.key(wm::key::F4, wm::kAlt);
    CHECK(g_leaves == 2);
    // Logo+D again: they come back as they were, B on top and in the focus.
    rig.key(d, wm::kGui);
    CHECK(a.state() == wm::WindowState::Normal && b.state() == wm::WindowState::Normal);
    CHECK(rig.wm.active() == &b && rig.wm.window_under(160, 110) == &b);
    CHECK(rig.no_stale_pixels());
    // Held, it acts once.
    held(wm::key::LeftGui, d);
    CHECK(a.state() == wm::WindowState::Minimized && b.state() == wm::WindowState::Minimized);
    CHECK(!rig.wm.start_menu_open());

    // A window brought back by hand is all the next logo+D puts away; with
    // nothing showing, the one after returns every window it put away.
    rig.wm.restore(a);
    rig.key(d, wm::kGui);
    CHECK(a.state() == wm::WindowState::Minimized && rig.wm.active() == nullptr);
    rig.key(d, wm::kGui);
    CHECK(a.state() == wm::WindowState::Normal && b.state() == wm::WindowState::Normal);
    CHECK(rig.wm.active() == &a); // on top since it was brought back
    // A window the user minimised himself stays down.
    rig.wm.minimize(a);
    rig.key(d, wm::kGui);
    rig.key(d, wm::kGui);
    CHECK(a.state() == wm::WindowState::Minimized && b.state() == wm::WindowState::Normal);
    // A maximised window comes back maximised.
    rig.wm.toggle_maximize(b);
    rig.key(d, wm::kGui);
    rig.key(d, wm::kGui);
    CHECK(b.state() == wm::WindowState::Maximized);
    rig.wm.toggle_maximize(b);

    // The logo key alone is still the Start key, when it comes up; and
    // logo+D closes an open menu on its way.
    rig.wm.key(wm::key::LeftGui, true);
    CHECK(!rig.wm.start_menu_open());
    rig.wm.key(wm::key::LeftGui, false);
    CHECK(rig.wm.start_menu_open());
    rig.key(d, wm::kGui);
    CHECK(!rig.wm.start_menu_open() && b.state() == wm::WindowState::Minimized);
    rig.key(d, wm::kGui);

    // A dialog that holds the whole desktop: neither key does anything
    // behind it (Alt+F4 closes the dialog, as it closes any window).
    TestWindow dialog("Dialog", {120, 100, 200, 120}, 0x03);
    dialog.set_dialog(true);
    dialog.set_modal(nullptr, true);
    rig.wm.add(dialog);
    rig.key(d, wm::kGui);
    CHECK(dialog.state() == wm::WindowState::Normal && b.state() == wm::WindowState::Normal);
    rig.key(wm::key::F4, wm::kAlt);
    CHECK(!dialog.open() && g_leaves == 2);
    CHECK(rig.no_stale_pixels());
}

static void test_hourglass()
{
    Rig rig;
    TestWindow other("Other", {420, 300, 180, 120}, 0xE0);
    MenuWindow w; // frame {100, 60, 300, 200}
    rig.wm.add(other);
    rig.wm.add(w);
    rig.wm.start();
    memset(g_runs, 0, sizeof(g_runs));
    const Rect c = w.client();
    const int cx = c.x + c.w / 2, cy = c.y + c.h / 2;
    const Rect file = rig.wm.menu_bar_entry(w, 0);

    rig.move(cx, cy, 0);
    CHECK(!w.hourglass() && pointer_is(rig, video::arrow_pointer()));

    // Asked for with the pointer at rest over the client area: it changes
    // at once, without a move of the mouse.
    w.set_hourglass(true);
    CHECK(w.hourglass() && !w.locked() && !rig.wm.waiting());
    CHECK(pointer_is(rig, video::busy_pointer()));
    // Every corner of the client area, and nothing beside it.
    rig.move(c.x, c.y, 0);
    CHECK(pointer_is(rig, video::busy_pointer()));
    rig.move(c.right() - 1, c.bottom() - 1, 0);
    CHECK(pointer_is(rig, video::busy_pointer()));
    // The menu bar, the title bar and its buttons still answer: the arrow.
    rig.move(file.x + 5, file.y + 5, 0);
    CHECK(pointer_is(rig, video::arrow_pointer()));
    rig.move(100 + 150, 60 + 12, 0);
    CHECK(pointer_is(rig, video::arrow_pointer()));
    rig.move(100 + 300 - 14, 60 + 12, 0); // Close
    CHECK(pointer_is(rig, video::arrow_pointer()));
    // The borders keep their arrows for sizing.
    rig.move(100 + 1, 60 + 100, 0);
    CHECK(pointer_is(rig, video::resize_we_pointer()));
    // Outside it and over another window: the arrow.
    rig.move(50, 200, 0);
    CHECK(pointer_is(rig, video::arrow_pointer()));
    rig.move(420 + 90, 300 + 60, 0);
    CHECK(pointer_is(rig, video::arrow_pointer()));

    // The window is not locked: its menus open and run. While a menu is
    // open the pointer is the arrow, over the client area too.
    rig.click(file.x + 5, file.y + 5);
    CHECK(rig.wm.popup_count() == 1);
    rig.move(cx + 100, cy + 40, 0);
    CHECK(pointer_is(rig, video::arrow_pointer()));
    const Rect open = rig.wm.popup_item(0, 0);
    rig.click(open.x + 10, open.y + 5);
    CHECK(g_runs[0] == 1 && rig.wm.popup_count() == 0);
    rig.move(cx, cy, 0);
    CHECK(pointer_is(rig, video::busy_pointer()));

    // Another window in front covers it where the two overlap.
    TestWindow cover("Cover", {cx - 40, cy - 40, 160, 120}, 0x03);
    rig.wm.add(cover);
    rig.move(cx, cy, 0);
    CHECK(pointer_is(rig, video::arrow_pointer()));
    rig.move(c.x + 5, c.y + 5, 0); // a part of it still seen
    CHECK(pointer_is(rig, video::busy_pointer()));
    cover.close();
    CHECK(pointer_is(rig, video::busy_pointer()));

    // It can be moved by its title bar, and the hourglass goes with it.
    rig.drag(100 + 150, 60 + 12, 100 + 170, 60 + 22);
    CHECK(w.frame().x == 120 && w.frame().y == 70);
    rig.move(cx + 20, cy + 10, 0);
    CHECK(pointer_is(rig, video::busy_pointer()));

    // Asked for twice and given back once: it is a switch, not a count.
    w.set_hourglass(true);
    w.set_hourglass(false);
    CHECK(!w.hourglass() && pointer_is(rig, video::arrow_pointer()));

    // Minimised with the hourglass on: the arrow where the window was, and
    // the hourglass again when it comes back.
    w.set_hourglass(true);
    CHECK(pointer_is(rig, video::busy_pointer()));
    rig.wm.minimize(w);
    CHECK(pointer_is(rig, video::arrow_pointer()));
    rig.wm.restore(w);
    rig.move(cx + 21, cy + 10, 0);
    CHECK(pointer_is(rig, video::busy_pointer()));

    // The whole-screen wait is over it as over everything, and ends as it was.
    {
        const wm::Wait wait(rig.wm);
        rig.move(50, 200, 0);
        CHECK(pointer_is(rig, video::busy_pointer()));
    }
    CHECK(pointer_is(rig, video::arrow_pointer()));

    // Closed with the hourglass on and the pointer over it: the arrow at
    // once, and a window opened again starts without it.
    rig.move(cx + 20, cy + 10, 0);
    CHECK(pointer_is(rig, video::busy_pointer()));
    w.close();
    CHECK(pointer_is(rig, video::arrow_pointer()));
    rig.wm.update();
    CHECK(rig.no_stale_pixels());
}

// A slow pointer, as a person moves it: from a parent item into its open
// submenu, 2 pixels every 20 ms, straight across and on a slant that
// crosses the next parent item. The submenu must stay open and end with the
// item under the pointer highlighted.
static void slow_path(Rig &rig, int x0, int y0, int x1, int y1)
{
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0)) / 2 + 1;
    for (int i = 1; i <= steps; i++) {
        rig.clock.now += 20000;
        rig.wm.tick();
        rig.quick(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, 0);
    }
}

static void test_slow_submenu()
{
    Rig rig;
    rig.wm.start();
    wm::MenuItem sub_items[2] = {
        {"&Inner", &run2, nullptr, nullptr, nullptr, 0},
        {"&Other", &run2, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu sub = {sub_items, 2};
    wm::MenuItem items[3] = {
        {"&Programs", nullptr, nullptr, &sub, nullptr, 0},
        {"&Two", &run1, nullptr, nullptr, nullptr, 0},
        {"&Three", &run1, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu menu = {items, 3};
    for (int pass = 0; pass < 2; pass++) {
        rig.wm.open_menu(menu, 100, 100);
        const Rect parent = rig.wm.popup_item(0, 0);
        const int px = parent.x + 20, py = parent.y + parent.h / 2;
        rig.move(px, py, 0);
        rig.wait(wm::Wm::kSubmenuDelayUs);
        CHECK(rig.wm.popup_count() == 2);
        const int want = pass; // straight to Inner, on a slant to Other
        const Rect target = rig.wm.popup_item(1, want);
        slow_path(rig, px, py, target.x + 20, target.y + target.h / 2);
        rig.wait(wm::Wm::kSubmenuDelayUs); // and resting there
        CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_hover(0) == 0 && rig.wm.popup_hover(1) == want);
        rig.wm.close_menus();
    }
    // Another parent item takes over at once: no wait.
    rig.wm.open_menu(menu, 100, 100);
    Rect r = rig.wm.popup_item(0, 0);
    rig.move(r.x + 20, r.y + r.h / 2, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    r = rig.wm.popup_item(0, 1);
    rig.quick(r.x + 20, r.y + r.h / 2, 0);
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_hover(0) == 1);
    // The pointer off the menus leaves them as they are; a click there closes them.
    rig.move(500, 400, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 1);
    rig.click(500, 400);
    CHECK(rig.wm.popup_count() == 0);
    CHECK(rig.no_stale_pixels());
}

// The owner's report of 2026-11-17: with a submenu open, the pointer moving
// onto another item of a parent menu must close that submenu, highlight the
// item and open its own submenu in the same move, at every depth; the
// parent's border and the space off the menus must still change nothing.
static void test_submenu_switches_at_once()
{
    Rig rig;
    rig.wm.start();
    wm::MenuItem deep_items[2] = {
        {"&Deep", &run2, nullptr, nullptr, nullptr, 0},
        {"D&eeper", &run2, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu deep = {deep_items, 2};
    wm::MenuItem other_items[1] = {{"&Else", &run2, nullptr, nullptr, nullptr, 0}};
    wm::Menu other = {other_items, 1};
    wm::MenuItem programs_items[3] = {
        {"&Games", nullptr, nullptr, &deep, nullptr, 0},
        {"&Tools", nullptr, nullptr, &other, nullptr, 0},
        {"&Editor", &run2, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu programs = {programs_items, 3};
    wm::MenuItem settings_items[2] = {
        {"&Display", &run2, nullptr, nullptr, nullptr, 0},
        {"&Network", &run2, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu settings = {settings_items, 2};
    wm::MenuItem items[4] = {
        {"&Programs", nullptr, nullptr, &programs, nullptr, 0},
        {"&Settings", nullptr, nullptr, &settings, nullptr, 0},
        {"&Greyed", nullptr, nullptr, &settings, nullptr, wm::kMenuDisabled},
        {"&Run", &run1, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu menu = {items, 4};
    const auto centre = [&](int level, int item, int &x, int &y) {
        const Rect r = rig.wm.popup_item(level, item);
        x = r.x + r.w / 2;
        y = r.y + r.h / 2;
    };
    int x, y, sx, sy;
    rig.wm.open_menu(menu, 100, 100);
    centre(0, 0, x, y);
    rig.move(x, y, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_menu(1) == &programs);
    // Into the program list, then out of it sideways onto Settings.
    centre(1, 1, sx, sy);
    slow_path(rig, x, y, sx, y);
    slow_path(rig, sx, y, sx, sy);
    CHECK(rig.wm.popup_hover(1) == 1);
    centre(0, 1, x, y);
    const Rect list = rig.wm.popup_rect(1);
    slow_path(rig, sx, sy, list.x + 2, y);       // still inside the list
    CHECK(rig.wm.popup_menu(1) == &programs);
    rig.quick(list.x - 6, y, 0);                  // the first report on Settings
    CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_hover(0) == 1 && rig.wm.popup_menu(1) == &settings);
    CHECK(rig.no_stale_pixels());
    // Straight down the parent: a greyed entry and a plain one close it at once.
    centre(0, 2, x, y);
    rig.quick(x, y, 0);
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_hover(0) == 2);
    // The border of the parent and the space off the menus change nothing.
    centre(0, 0, x, y);
    rig.move(x, y, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 2);
    centre(1, 2, sx, sy);
    slow_path(rig, x, y, sx, sy);
    CHECK(rig.wm.popup_hover(1) == 2);
    const Rect panel = rig.wm.popup_rect(0);
    rig.quick(panel.right() - 1, sy, 0); // the parent's right border
    rig.wait(2 * wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_hover(0) == 0);
    rig.quick(list.right() + 30, list.bottom() + 30, 0); // off every menu
    rig.wait(2 * wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 2 && rig.wm.popup_hover(0) == 0);
    // The same one level down: Games open, the pointer comes back from it
    // onto Tools, which takes over; then onto Run two levels up, which
    // closes everything below the first menu.
    centre(1, 0, x, y);
    rig.move(x, y, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 3 && rig.wm.popup_menu(2) == &deep);
    centre(2, 1, sx, sy);
    slow_path(rig, x, y, sx, sy);
    CHECK(rig.wm.popup_count() == 3 && rig.wm.popup_hover(2) == 1);
    centre(1, 1, x, y);
    slow_path(rig, sx, sy, x, y);
    CHECK(rig.wm.popup_count() == 3 && rig.wm.popup_hover(1) == 1 && rig.wm.popup_menu(2) == &other);
    centre(0, 3, x, y);
    rig.quick(x, y, 0);
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_hover(0) == 3);
    CHECK(rig.no_stale_pixels());
    rig.wm.close_menus();
}

// A key whose handling takes long (a save, about 1 s on the board) must not
// repeat as soon as it returns: the repeat counts from the end of the
// handling. Before, the press chose File > Save in a menu, and the overdue
// repeat then typed the letter into the Editor.
class SlowKeyWindow final : public wm::Window {
public:
    SlowKeyWindow(host::FixedClock &clock) : Window("Slow", {100, 100, 200, 100}, nullptr), clock_(clock) {}
    void paint(gfx::Canvas &c) override { c.fill({0, 0, client().w, client().h}, gfx::kGrey); }
    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed) {
            presses++;
            clock_.now += 1000000; // a second's work
        }
        return true;
    }
    int presses = 0;

private:
    host::FixedClock &clock_;
};

static void test_slow_key_does_not_repeat()
{
    Rig rig;
    SlowKeyWindow w(rig.clock);
    rig.wm.add(w);
    rig.wm.start();
    rig.wm.key(wm::key::A, true);
    rig.wm.tick(); // the loop comes back after the slow press...
    rig.wm.key(wm::key::A, false); // ...and finds the release waiting
    rig.wait(wm::Wm::kRepeatDelayUs + 100000);
    CHECK(w.presses == 1);
    // A key really held still repeats.
    rig.wm.key(static_cast<uint16_t>(wm::key::A + 1), true); // B
    rig.wait(wm::Wm::kRepeatDelayUs + 10000);
    CHECK(w.presses >= 2);
    rig.wm.key(static_cast<uint16_t>(wm::key::A + 1), false);
}

static void test_start_menu_and_icons()
{
    Rig rig;
    TestWindow a("A", {300, 100, 200, 150}, 0x1C);
    rig.wm.add(a);
    memset(g_runs, 0, sizeof(g_runs));
    wm::MenuItem items[3] = {
        {"&Programs", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
        {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator},
        {"Sh&ut Down...", &run0, nullptr, nullptr, nullptr, 0},
    };
    wm::Menu start = {items, 3};
    rig.wm.set_start_menu(&start, "Chinchilla");
    static const char *const labels[2] = {"One", "Two"};
    desk::Icon icons[2] = {{labels[0], nullptr, &run1, nullptr}, {labels[1], nullptr, &run2, nullptr}};
    rig.desk_icons.set_icons(icons, 2);
    rig.wm.start();

    // The Start button opens it above the taskbar; GUI and Ctrl+Esc toggle it.
    Rect sb = rig.bar.button(-1);
    rig.click(sb.x + 10, sb.y + 10);
    CHECK(rig.wm.start_menu_open() && rig.wm.popup_rect(0).bottom() == 452);
    CHECK(rig.no_stale_pixels());
    rig.key(wm::key::LeftGui);
    CHECK(!rig.wm.start_menu_open());
    rig.key(wm::key::Escape, wm::kCtrl);
    CHECK(rig.wm.start_menu_open());
    Rect shut = rig.wm.popup_item(0, 2);
    rig.click(shut.x + 30, shut.y + 5);
    CHECK(g_runs[0] == 1 && !rig.wm.start_menu_open());

    // Icons: a click selects, a double click opens.
    Rect one = rig.desk_icons.icon_rect(0), two = rig.desk_icons.icon_rect(1);
    rig.click(one.x + 30, one.y + 20);
    CHECK(rig.desk_icons.selected_icon() == 0 && rig.wm.active() == nullptr && g_runs[1] == 0);
    rig.double_click(two.x + 30, two.y + 20);
    CHECK(rig.desk_icons.selected_icon() == 1 && g_runs[2] == 1 && g_runs[1] == 0);
    rig.click(200, 300); // empty desktop
    CHECK(rig.desk_icons.selected_icon() == -1);
    CHECK(rig.no_stale_pixels());
}

/* ---- Widgets ----------------------------------------------------------------- */

class WidgetWindow final : public wm::Window {
public:
    WidgetWindow()
        : Window("Widgets", {20, 20, 500, 400}, nullptr),
          check({10, 10, 120, 18}, "Check"),
          r0({10, 40, 100, 18}, "Zero", &choice, 0),
          r1({10, 60, 100, 18}, "One", &choice, 1),
          field({10, 90, 120, 21}, "abc"),
          bar({200, 10, 16, 200}, true),
          list({250, 10, 150, 100}, kItems, 20),
          ok({300, 300, 75, 23}, "OK", &WidgetWindow::clicked, this)
    {
        add(field);
        add(check);
        add(r0);
        add(r1);
        add(bar);
        add(list);
        add(ok);
        ok.set_default(true);
        set_default(&ok);
        bar.set_range(90, 10);
    }
    static constexpr const char *kItems[20] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
                                                "k", "l", "m", "n", "o", "p", "q", "r", "s", "t"};
    int choice = 0, oks = 0;
    wm::CheckBox check;
    wm::RadioButton r0, r1;
    wm::TextField field;
    wm::ScrollBar bar;
    wm::ListBox list;
    wm::Button ok;

private:
    static void clicked(wm::Button &, void *context) { static_cast<WidgetWindow *>(context)->oks++; }
};
constexpr const char *WidgetWindow::kItems[20];

// The wheel scrolls what is under the pointer (backlog item 53).
class BarsWindow final : public wm::Window {
public:
    BarsWindow() : Window("Bars", {300, 250, 300, 200}, nullptr), down({250, 0, 16, 150}, true), across({0, 150, 250, 16}, false)
    {
        add(down);
        add(across);
        down.set_range(1000, 400); // a picture, in pixels
        across.set_range(40, 10);
    }
    wm::ScrollBar down, across;
};

static void test_wheel()
{
    Rig rig;
    WidgetWindow w;
    BarsWindow bars;
    rig.wm.add(w);
    rig.wm.add(bars);
    rig.wm.start();
    CHECK(rig.wm.active() == &bars);
    const Rect c = w.client();
    auto over = [&](const wm::Widget &wd) { return std::pair<int, int>(c.x + wd.rect.x + 5, c.y + wd.rect.y + 5); };

    // Over the list: the list, three rows a notch, and the window stays
    // behind, not the one in use.
    auto [lx, ly] = over(w.list);
    rig.wheel(lx, ly, -1);
    CHECK(w.list.top() == 3 && w.bar.value() == 0);
    CHECK(rig.wm.active() == &bars);
    rig.wheel(lx, ly, -2);
    CHECK(w.list.top() == 9);
    rig.wheel(lx, ly, -100); // to the end and no further
    CHECK(w.list.top() == 20 - w.list.visible_rows());
    rig.wheel(lx, ly, 1);
    CHECK(w.list.top() == 20 - w.list.visible_rows() - 3);
    CHECK(w.list.selected() == -1); // scrolling picks nothing
    rig.wheel(lx, ly, 100);
    CHECK(w.list.top() == 0 && w.bar.value() == 0); // at its end the list keeps the wheel

    // Over the scroll bar itself, over a control that does not scroll and
    // over the bare window: the window's bar.
    auto [bx, by] = over(w.bar);
    rig.wheel(bx, by, -1);
    CHECK(w.bar.value() == 3);
    auto [kx, ky] = over(w.check);
    rig.wheel(kx, ky, -1);
    CHECK(w.bar.value() == 6 && !w.check.checked());
    rig.wheel(c.x + 150, c.y + 300, 1);
    CHECK(w.bar.value() == 3);
    CHECK(rig.no_stale_pixels());

    // Not on the title bar, and not with a button held.
    rig.wheel(c.x + 150, w.frame().y + 8, -1);
    rig.wheel(c.x + 150, c.y + 300, -1, 0, 1);
    rig.wheel(c.x + 150, c.y + 300, 0, 0, 0);
    CHECK(w.bar.value() == 3);

    // A bar counted in pixels goes an eighth of the view a notch. Sideways:
    // two fingers, or the wheel with Shift held.
    const Rect bc = bars.client();
    rig.wheel(bc.x + 230, bc.y + 50, -1);
    CHECK(bars.down.value() == 50 && bars.across.value() == 0);
    rig.wheel(bc.x + 230, bc.y + 50, 0, 1);
    CHECK(bars.down.value() == 50 && bars.across.value() == 3);
    rig.wheel(bc.x + 230, bc.y + 50, 1, -1); // both at once
    CHECK(bars.down.value() == 0 && bars.across.value() == 0);
    rig.wm.key(wm::key::LeftShift, true);
    rig.wheel(bc.x + 230, bc.y + 50, -2);
    rig.wm.key(wm::key::LeftShift, false);
    CHECK(bars.down.value() == 0 && bars.across.value() == 6);
    // While the thumb is held the wheel does nothing.
    const Rect thumb = bars.down.thumb();
    rig.move(bc.x + 250 + 8, bc.y + thumb.y + 3, 1);
    rig.wheel(bc.x + 250 + 8, bc.y + thumb.y + 3, -1, 0, 1);
    CHECK(bars.down.value() == 0);
    rig.move(bc.x + 250 + 8, bc.y + thumb.y + 3, 0);

    // A window with a dialog over it is not scrolled.
    TestWindow dialog("Dialog", {40, 300, 150, 100}, 0x03);
    dialog.set_dialog(true);
    dialog.set_modal(&w);
    rig.wm.add(dialog);
    rig.wheel(bx, by, -1);
    CHECK(w.bar.value() == 3);
    rig.wm.close(dialog);
    rig.wheel(bx, by, -1);
    CHECK(w.bar.value() == 6);
    CHECK(rig.no_stale_pixels());
}

// Desktop icons stay where they are dragged to (backlog item 41).
static int g_moves = 0;
static char g_dropped[64] = "";
static int g_drop_x = 0, g_drop_y = 0;
static void test_icon_places()
{
    Rig rig;
    static const char *const labels[4] = {"One", "Two", "Three", "File"};
    desk::Icon icons[4] = {{labels[0], nullptr, &run1, nullptr},
                           {labels[1], nullptr, &run2, nullptr},
                           {labels[2], nullptr, &run1, nullptr},
                           {labels[3], nullptr, &run1, nullptr, "C:\\Documents\\Desktop\\file.txt"}};
    rig.desk_icons.set_icons(icons, 4);
    rig.desk_icons.set_moved([](void *) { g_moves++; }, nullptr);
    rig.desk_icons.set_drop(
        [](const char *path, int x, int y, void *) {
            snprintf(g_dropped, sizeof(g_dropped), "%s", path);
            g_drop_x = x;
            g_drop_y = y;
        },
        nullptr);
    rig.wm.start();
    const auto drag = [&](int from_x, int from_y, int to_x, int to_y) {
        rig.move(from_x, from_y, 0);
        rig.move(from_x, from_y, 1);
        rig.move(from_x + 8, from_y + 8, 1);
        rig.move(to_x, to_y, 1);
        rig.move(to_x, to_y, 0);
    };
    // Nothing placed: a column down the left, as always.
    const Rect one = rig.desk_icons.icon_rect(0), two = rig.desk_icons.icon_rect(1);
    CHECK(one.x == 2 && one.y == 8 && two.x == 2 && two.y == 78);
    CHECK(rig.desk_icons.icon_rect(4).empty());

    // An icon that stands for no file is dragged to where it is let go:
    // its picture under the pointer. The others stay.
    drag(one.x + 30, one.y + 20, 300, 200);
    CHECK(rig.desk_icons.icon_rect(0).x == 300 - 36 && rig.desk_icons.icon_rect(0).y == 200 - 16);
    CHECK(rig.desk_icons.icon_rect(1).x == two.x && rig.desk_icons.icon_rect(1).y == two.y);
    CHECK(g_moves == 1 && g_dropped[0] == 0);
    CHECK(rig.no_stale_pixels());
    // Never off the screen or under the taskbar.
    drag(300, 200, 639, 450);
    CHECK(rig.desk_icons.icon_rect(0).right() == 640 && rig.desk_icons.icon_rect(0).bottom() == 452);
    drag(620, 430, 300, 200);
    // A press without a move is not a move.
    rig.click(300, 200);
    CHECK(g_moves == 3 && rig.desk_icons.selected_icon() == 0);

    // It is not offered to a window, nor to an icon that takes files.
    TestWindow a("A", {350, 100, 200, 150}, 0x1C);
    rig.wm.add(a);
    drag(300, 200, 450, 180);
    CHECK(rig.desk_icons.icon_rect(0).x == 300 - 36 && g_moves == 3);
    rig.wm.close(a);

    // A file's icon moves the same way; a file from somewhere else goes to
    // whoever takes those, with the place its icon would get.
    const Rect file = rig.desk_icons.icon_rect(3);
    drag(file.x + 30, file.y + 20, 500, 100);
    CHECK(rig.desk_icons.icon_rect(3).x == 500 - 36 && g_moves == 4 && g_dropped[0] == 0);
    rig.move(400, 400, 1);
    rig.wm.begin_drag("C:\\other.txt");
    rig.move(410, 300, 1);
    rig.move(410, 300, 0);
    CHECK(strcmp(g_dropped, "C:\\other.txt") == 0 && g_drop_x == 410 - 36 && g_drop_y == 300 - 16 && g_moves == 4);

    // Snap on: nothing moves by itself; the next icon let go takes the
    // nearest cell of the grid, or the nearest free one.
    rig.desk_icons.set_snap(true);
    CHECK(rig.desk_icons.icon_rect(0).x == 300 - 36 && rig.desk_icons.icon_rect(3).x == 500 - 36);
    drag(two.x + 30, two.y + 20, 2 + 5 * 76 + 40, 8 + 2 * 70 + 20);
    CHECK(rig.desk_icons.icon_rect(1).x == 2 + 5 * 76 && rig.desk_icons.icon_rect(1).y == 8 + 2 * 70);
    const Rect three = rig.desk_icons.icon_rect(2);
    drag(three.x + 30, three.y + 20, 2 + 5 * 76 + 36, 8 + 2 * 70 + 16); // on Two's cell
    const Rect near = rig.desk_icons.icon_rect(2);
    CHECK(!near.overlaps(rig.desk_icons.icon_rect(1)) && (near.x - 2) % 76 == 0 && (near.y - 8) % 70 == 0);
    CHECK(abs(near.x - (2 + 5 * 76)) + abs(near.y - (8 + 2 * 70)) <= 76);

    // Realign: all into the grid from the top left, in the order they are
    // in on the screen (by column, then down).
    rig.desk_icons.set_snap(false);
    drag(rig.desk_icons.icon_rect(0).x + 30, rig.desk_icons.icon_rect(0).y + 20, 60, 300);
    const int before = g_moves;
    rig.desk_icons.realign();
    rig.move(600, 300, 0); // an event, so the screen is painted
    CHECK(g_moves == before + 1);
    int at[4];
    for (int i = 0; i < 4; i++) {
        const Rect r = rig.desk_icons.icon_rect(i);
        CHECK(r.x == 2 && (r.y - 8) % 70 == 0);
        at[i] = (r.y - 8) / 70;
    }
    CHECK(at[0] == 0);                      // One was alone in the first column
    CHECK(at[1] + at[2] + at[3] == 1 + 2 + 3 && at[3] == 3); // File was furthest right
    CHECK(rig.no_stale_pixels());

    // Places given with the icons are used; an icon without one takes the
    // first cell nothing is on.
    icons[0].x = 2;
    icons[0].y = 8 + 70;
    icons[1].x = 400;
    icons[1].y = 30;
    rig.desk_icons.set_icons(icons, 3);
    CHECK(rig.desk_icons.icon_rect(0).y == 78 && rig.desk_icons.icon_rect(1).x == 400);
    CHECK(rig.desk_icons.icon_rect(2).x == 2 && rig.desk_icons.icon_rect(2).y == 8);
}

// Quick launch: icons by the Start button that do something when clicked.
static void test_quick_launch()
{
    Rig rig;
    TestWindow a("A", {300, 100, 200, 150}, 0x1C);
    rig.wm.add(a);
    rig.wm.start();
    memset(g_runs, 0, sizeof(g_runs));

    // None by default: the window buttons start where they always did.
    CHECK(rig.bar.quick_launch_count() == 0 && rig.bar.quick_launch_rect(0).w == 0);
    const Rect before = rig.bar.button(0);
    CHECK(before.x == 72);

    // Three: side by side after the Start button, and the window buttons
    // move right to make room. One without an icon or without something to
    // do is left out, and there is room for four at most.
    static uint8_t pixels[16 * 16] = {};
    const gfx::Bitmap icon = {16, 16, pixels, -1};
    const desk::Taskbar::QuickLaunch items[6] = {{&icon, &run0, nullptr}, {nullptr, &run1, nullptr}, {&icon, nullptr, nullptr},
                                          {&icon, &run1, nullptr}, {&icon, &run2, nullptr}, {&icon, &run0, nullptr}};
    rig.bar.set_quick_launch(items, 4);
    CHECK(rig.bar.quick_launch_count() == 2);
    rig.bar.set_quick_launch(items, 5);
    CHECK(rig.bar.quick_launch_count() == 3);
    rig.wait(1000);
    const Rect start = rig.bar.button(-1);
    const Rect q0 = rig.bar.quick_launch_rect(0), q1 = rig.bar.quick_launch_rect(1), q2 = rig.bar.quick_launch_rect(2);
    CHECK(q0.x > start.right() && q1.x == q0.right() && q2.x == q1.right() && q0.y == start.y && q0.h == start.h);
    const Rect after = rig.bar.button(0);
    CHECK(after.x > q2.right() && after.x > before.x && after.w == before.w);
    CHECK(rig.no_stale_pixels());

    // It acts when the button is let go over it, like the other buttons of
    // the taskbar, and shows pressed in while it is held.
    rig.move(q1.x + 8, q1.y + 8, 0);
    const auto shot = [&rig] { return Bytes(rig.display.pixels(), rig.display.pixels() + 640 * 480); };
    const Bytes up = shot();
    rig.move(q1.x + 8, q1.y + 8, 1);
    CHECK(g_runs[1] == 0 && shot() != up && rig.no_stale_pixels());
    rig.move(q1.x + 8, q1.y + 8, 0);
    CHECK(g_runs[1] == 1 && g_runs[0] == 0 && g_runs[2] == 0);
    CHECK(shot() == up && rig.no_stale_pixels());
    // Let go somewhere else: nothing happens.
    rig.move(q2.x + 8, q2.y + 8, 1);
    rig.move(q2.x + 8, q2.y - 100, 1);
    rig.move(q2.x + 8, q2.y - 100, 0);
    CHECK(g_runs[2] == 0);
    rig.click(q0.x + 2, q0.y + 2);
    rig.click(q2.x + 20, q2.y + 20);
    CHECK(g_runs[0] == 1 && g_runs[1] == 1 && g_runs[2] == 1);
    // The window's button still works beside them.
    rig.click(after.x + 10, after.y + 10);
    CHECK(a.state() == wm::WindowState::Minimized);
    // The gap between the last icon and the first window button is nobody's.
    rig.click(after.x - 3, after.y + 10);
    CHECK(g_runs[2] == 1 && a.state() == wm::WindowState::Minimized);

    // Taken away again: as before.
    rig.bar.set_quick_launch(nullptr, 0);
    rig.wait(1000);
    CHECK(rig.bar.quick_launch_count() == 0 && rig.bar.button(0).x == before.x && rig.no_stale_pixels());
}

static void test_widgets()
{
    Rig rig;
    WidgetWindow w;
    rig.wm.add(w);
    rig.wm.start();
    const Rect c = w.client();
    auto at = [&](const wm::Widget &wd, int dx, int dy) {
        rig.click(c.x + wd.rect.x + dx, c.y + wd.rect.y + dy);
    };
    char text[64];

    // Check box and radio buttons by mouse and by Space.
    at(w.check, 5, 9);
    CHECK(w.check.checked() && w.focus() == &w.check);
    rig.key(wm::key::Space);
    CHECK(!w.check.checked());
    at(w.r1, 5, 9);
    CHECK(w.choice == 1 && w.r1.selected() && !w.r0.selected());

    // Text field: typing, caret keys, selection, deleting, Latin-1.
    at(w.field, 4, 10);
    CHECK(w.focus() == &w.field && w.field.caret() == 0);
    rig.key(wm::key::End);
    rig.wm.text("ñé");
    w.field.get(text, sizeof(text));
    CHECK(strcmp(text, "abcñé") == 0 && w.field.caret() == 5);
    rig.key(wm::key::Left, wm::kShift);
    rig.key(wm::key::Left, wm::kShift);
    CHECK(w.field.selection_start() == 3 && w.field.selection_end() == 5);
    rig.key(0x1B, wm::kShift); // X with Shift types over the selection
    w.field.get(text, sizeof(text));
    CHECK(strcmp(text, "abcX") == 0);
    rig.key(wm::key::Home);
    rig.key(wm::key::Delete);
    rig.key(wm::key::End);
    rig.key(wm::key::Backspace);
    w.field.get(text, sizeof(text));
    CHECK(strcmp(text, "bc") == 0);
    rig.key(wm::key::A, wm::kCtrl);
    CHECK(w.field.selection_start() == 0 && w.field.selection_end() == 2);
    rig.key(0x1E); // 1
    w.field.get(text, sizeof(text));
    CHECK(strcmp(text, "1") == 0);
    // Enter in the field presses the default button.
    rig.key(wm::key::Enter);
    CHECK(w.oks == 1);
    // Tab moves the focus on.
    rig.key(wm::key::Tab);
    CHECK(w.focus() == &w.check);
    rig.key(wm::key::Tab, wm::kShift);
    CHECK(w.focus() == &w.field);

    // Scroll bar: arrows, track and dragging the thumb.
    at(w.bar, 8, 200 - 8);
    CHECK(w.bar.value() == 1);
    at(w.bar, 8, 4);
    CHECK(w.bar.value() == 0);
    at(w.bar, 8, 150);
    CHECK(w.bar.value() == 10);
    const Rect thumb = w.bar.thumb();
    rig.drag(c.x + 200 + 8, c.y + 10 + thumb.y + 2, c.x + 200 + 8, c.y + 10 + 200);
    CHECK(w.bar.value() == 90);

    // List box: click, keys, and it scrolls to keep the choice in view.
    at(w.list, 20, 2 + 14 * 2 + 5);
    CHECK(w.list.selected() == 2 && w.focus() == &w.list);
    rig.key(wm::key::End);
    CHECK(w.list.selected() == 19 && w.list.top() == 20 - w.list.visible_rows());
    rig.key(wm::key::Home);
    CHECK(w.list.selected() == 0 && w.list.top() == 0);
    CHECK(rig.no_stale_pixels());
}


/* ---- Timers, clock, modal dialogs ---------------------------------------------- */

static int g_fired = 0;
static void fire(void *) { g_fired++; }

static void test_timers_and_clock()
{
    Rig rig;
    WidgetWindow w;
    rig.wm.add(w);
    rig.wm.start();

    // One-shot and repeating timers, and stopping them.
    g_fired = 0;
    int once = rig.wm.start_timer(1000, 0, &fire, nullptr);
    int every = rig.wm.start_timer(500, 500, &fire, nullptr);
    CHECK(once != 0 && every != 0);
    rig.wait(400);
    CHECK(g_fired == 0);
    rig.wait(600); // t = 1000: once, and every at 500 and 1000 (one call per tick)
    CHECK(g_fired == 2);
    rig.wait(500);
    CHECK(g_fired == 3);
    rig.wm.stop_timer(every);
    rig.wait(5000);
    CHECK(g_fired == 3);

    // The caret blinks, and shows again at once when something is typed.
    const Rect c = w.client();
    rig.click(c.x + w.field.rect.x + 5, c.y + w.field.rect.y + 5);
    CHECK(rig.wm.caret_visible());
    rig.wait(wm::Wm::kBlinkUs);
    CHECK(!rig.wm.caret_visible());
    CHECK(rig.no_stale_pixels());
    rig.wm.text("x");
    CHECK(rig.wm.caret_visible());
    rig.wm.set_animations(false);
    rig.wait(wm::Wm::kBlinkUs);
    CHECK(rig.wm.caret_visible());
    rig.wm.set_animations(true);

    // A blink repaints the caret's 12 pixels only, each once.
    {
        std::vector<uint8_t> counts(640 * 480, 0);
        gfx::Canvas::count_writes(counts.data());
        rig.wait(wm::Wm::kBlinkUs);
        gfx::Canvas::count_writes(nullptr);
        int touched = 0, most = 0;
        for (uint8_t n : counts) {
            touched += n != 0;
            most = n > most ? n : most;
        }
        CHECK(touched == 12 && most == 1);
        rig.wait(wm::Wm::kBlinkUs);
    }

    // A held key repeats after a pause; letting go stops it.
    char text[64];
    rig.key(wm::key::End);
    rig.wm.key(0x04, true); // a, held
    rig.wait(wm::Wm::kRepeatDelayUs);
    rig.wait(wm::Wm::kRepeatUs);
    rig.wait(wm::Wm::kRepeatUs);
    rig.wm.key(0x04, false);
    rig.wait(1000000);
    w.field.get(text, sizeof(text));
    CHECK(strcmp(text, "xabcaaaa") == 0); // "x" went in where the click put the caret

    // A held scroll arrow repeats too.
    const Rect bar = w.bar.rect.offset(c.x, c.y);
    rig.move(bar.x + 8, bar.bottom() - 8, 0);
    rig.quick(bar.x + 8, bar.bottom() - 8, 1);
    CHECK(w.bar.value() == 1);
    rig.wait(400000);
    rig.wait(50000);
    rig.wait(50000);
    CHECK(w.bar.value() == 4);
    rig.quick(bar.x + 8, bar.bottom() - 8, 0);
    rig.wait(500000);
    CHECK(w.bar.value() == 4);

    // The clock counts from midnight, can be set, and shows on the taskbar.
    int h, m, s;
    rig.wm.set_time(13, 59, 58);
    rig.wait(3000000);
    rig.wm.get_time(h, m, s);
    CHECK(h == 14 && m == 0 && s == 1);
    rig.wm.set_time(23, 59, 59);
    rig.wait(2000000);
    rig.wm.get_time(h, m, s);
    CHECK(h == 0 && m == 0 && s == 1);
    CHECK(rig.no_stale_pixels());
}

static void test_modal()
{
    Rig rig;
    TestWindow owner("Owner", {50, 50, 300, 200}, 0x1C), other("Other", {380, 60, 200, 150}, 0xE0);
    TestWindow dialog("Dialog", {120, 100, 200, 120}, 0x03);
    rig.wm.add(other);
    rig.wm.add(owner);
    dialog.set_dialog(true);
    dialog.set_modal(&owner);
    rig.wm.add(dialog);
    rig.wm.start();
    CHECK(rig.wm.active() == &dialog && rig.wm.blocker(owner) == &dialog);
    CHECK(rig.wm.blocker(other) == nullptr);

    // Only the close button on a dialog, and it cannot be resized.
    CHECK(rig.wm.title_button(dialog, wm::Part::Maximize).empty());
    CHECK(rig.wm.edges_at(dialog, 121, 150) == 0);

    // A click on the owner brings the dialog forward; the owner gets nothing.
    owner.clicks = 0;
    const Rect oc = owner.client();
    rig.click(oc.x + 20, oc.y + 20);
    CHECK(rig.wm.active() == &dialog && owner.clicks == 0);
    // So do its taskbar button and Alt+Tab.
    Rect tb = rig.bar.button(1);
    rig.click(tb.x + 10, tb.y + 10);
    CHECK(rig.wm.active() == &dialog);
    // Other windows still work.
    rig.click(450, 150);
    CHECK(rig.wm.active() == &other);
    // Closing the dialog gives the owner the focus back.
    Rect close = rig.wm.title_button(dialog, wm::Part::Close);
    rig.click(close.x + 5, close.y + 5);
    CHECK(!dialog.open());
    rig.wm.activate(owner);
    rig.click(oc.x + 15, oc.y + 15);
    CHECK(owner.clicks == 1);

    // A dialog for the whole desktop blocks everything else.
    dialog.set_modal(nullptr, true);
    rig.wm.add(dialog);
    rig.click(450, 150);
    CHECK(rig.wm.active() == &dialog);
    Rect sb = rig.bar.button(-1);
    rig.click(sb.x + 5, sb.y + 5);
    CHECK(!rig.wm.menu_open() && rig.wm.active() == &dialog);
    CHECK(rig.no_stale_pixels());
}

// The server alone, and the server with a panel that is not a taskbar: it
// knows nothing of what a panel shows, only when to ask it.
struct ProbePanel : wm::Panel {
    wm::Wm &wm;
    wm::PanelPlace place;
    uint8_t colour;
    std::string seen; // what it was asked, in order
    bool takes = false;
    explicit ProbePanel(wm::Wm &w, wm::PanelPlace p, uint8_t c) : wm(w), place(p), colour(c) {}
    void paint(const gfx::RectList &clip) override
    {
        const Rect area = place == wm::PanelPlace::Bar ? wm.taskbar() : wm.work_area();
        gfx::Canvas c = wm.canvas(clip, area);
        c.fill(area, colour);
    }
    void mouse(const wm::MouseMsg &m) override
    {
        seen += m.kind == wm::MouseKind::Down ? (m.double_click ? "D2 " : "D ")
                                               : (m.kind == wm::MouseKind::Up ? "U " : "M ");
    }
    void right_click(int, int) override { seen += "R "; }
    bool drag_over(const char *path, int, int) override
    {
        seen += path != nullptr ? "over " : "left ";
        return takes;
    }
    void dropped(const char *path, int, int) override { seen += std::string("drop:") + path + " "; }
    void news(wm::PanelNews what) override
    {
        seen += what == wm::PanelNews::Windows ? "W " : (what == wm::PanelNews::Menu ? "S " : "C ");
    }
};

// Several files in one drag (backlog item 55): the first is what targets
// are asked about, and let go they are dropped in turn.
struct DropLog final : wm::Window {
    DropLog(const char *title, const Rect &frame) : Window(title, frame, nullptr) {}
    void paint(gfx::Canvas &c) override { c.fill({0, 0, client().w, client().h}, 0x03); }
    bool drag_over(const char *path, int, int) override
    {
        asked += path != nullptr ? std::string(path) + " " : "";
        return true;
    }
    void dropped(const char *path, int, int) override
    {
        seen += std::string(path) + ":" + std::to_string(manager()->drops_to_come()) + " ";
    }
    std::string asked, seen;
};

static std::string g_desk_drops;
static void test_drag_several()
{
    Rig rig;
    host::HostMemory memory;
    rig.wm.set_memory(&memory);
    static const char *const labels[4] = {"One", "Two", "Three", "Bin"};
    static desk::Icon icons[6] = {{labels[0], nullptr, &run1, nullptr},
                                  {labels[1], nullptr, &run1, nullptr},
                                  {labels[2], nullptr, &run1, nullptr, "C:\\Documents\\Desktop\\three.txt"},
                                  {labels[3], nullptr, &run1, nullptr}};
    icons[3].drop = [](const char *path, void *) { g_desk_drops += std::string("bin:") + path + " "; };
    rig.desk_icons.set_icons(icons, 4);
    static int moves;
    moves = 0;
    rig.desk_icons.set_moved([](void *) { moves++; }, nullptr);
    rig.desk_icons.set_drop(
        [](const char *path, int x, int y, void *) {
            g_desk_drops += std::string(path) + "@" + std::to_string(x) + "," + std::to_string(y) + " ";
        },
        nullptr);
    DropLog w("W", {300, 60, 250, 200});
    rig.wm.add(w);
    rig.wm.start();

    // From a window: three paths, dropped on the window in order, each told
    // how many are still to come. Only the first was asked about.
    const auto carry = [&](int to_x, int to_y) {
        rig.move(600, 300, 0);
        rig.move(600, 300, 1);
        rig.wm.begin_drag("C:\\a.txt");
        CHECK(rig.wm.add_to_drag("C:\\b.txt") && rig.wm.add_to_drag("*label", true) && rig.wm.add_to_drag("C:\\c.txt"));
        rig.move(to_x, to_y, 1);
        rig.move(to_x, to_y, 0);
    };
    carry(400, 150);
    CHECK(w.seen == "C:\\a.txt:3 C:\\b.txt:2 C:\\c.txt:0 "); // the one for the desktop only is not the window's
    CHECK(w.asked.find("b.txt") == std::string::npos && rig.wm.drops_to_come() == 0);
    // Nothing is carried over into the next drag, and nothing is added
    // when no drag is going on.
    w.seen.clear();
    CHECK(!rig.wm.add_to_drag("C:\\late.txt"));
    rig.move(600, 300, 1);
    rig.wm.begin_drag("C:\\d.txt");
    rig.move(400, 150, 1);
    rig.move(400, 150, 0);
    CHECK(w.seen == "C:\\d.txt:0 ");
    // Escape: none is dropped.
    w.seen.clear();
    rig.move(600, 300, 1);
    rig.wm.begin_drag("C:\\a.txt");
    rig.wm.add_to_drag("C:\\b.txt");
    rig.move(400, 150, 1);
    rig.key(wm::key::Escape);
    rig.move(400, 150, 0);
    CHECK(w.seen.empty());

    // On the desktop itself, files from somewhere else: the first where it
    // was let go, the others in free cells of the grid, no two in one place.
    g_desk_drops.clear();
    carry(200, 400);
    CHECK(g_desk_drops.find("C:\\a.txt@164,384 ") == 0);
    CHECK(g_desk_drops.find("C:\\b.txt@") != std::string::npos && g_desk_drops.find("C:\\c.txt@") != std::string::npos);
    CHECK(g_desk_drops.find("label") == std::string::npos); // no icon of the desktop's is called that
    // Onto an icon that takes files: each file goes to it.
    g_desk_drops.clear();
    const Rect bin = rig.desk_icons.icon_rect(3);
    carry(bin.x + 30, bin.y + 20);
    CHECK(g_desk_drops == "bin:C:\\a.txt bin:C:\\b.txt bin:C:\\c.txt ");

    // The desktop's own: Ctrl+click picks a second and a third, and they go
    // together when one is dragged, as far as the one held. The one moved
    // callback comes once.
    const Rect one = rig.desk_icons.icon_rect(0), two = rig.desk_icons.icon_rect(1), three = rig.desk_icons.icon_rect(2);
    rig.click(one.x + 30, one.y + 20);
    rig.wm.key(wm::key::LeftCtrl, true);
    rig.click(two.x + 30, two.y + 20);
    rig.click(three.x + 30, three.y + 20);
    rig.wm.key(wm::key::LeftCtrl, false);
    CHECK(rig.desk_icons.picked_count() == 3 && rig.desk_icons.selected_icon() == 2 && !rig.desk_icons.picked(3));
    rig.move(two.x + 36, two.y + 16, 1);
    rig.move(two.x + 46, two.y + 26, 1);
    rig.move(two.x + 36 + 200, two.y + 16 + 100, 1);
    rig.move(two.x + 36 + 200, two.y + 16 + 100, 0);
    CHECK(rig.desk_icons.icon_rect(1).x == two.x + 200 && rig.desk_icons.icon_rect(1).y == two.y + 100);
    CHECK(rig.desk_icons.icon_rect(0).x == one.x + 200 && rig.desk_icons.icon_rect(0).y == one.y + 100);
    CHECK(rig.desk_icons.icon_rect(2).x == three.x + 200 && rig.desk_icons.icon_rect(2).y == three.y + 100);
    CHECK(moves == 1 && rig.desk_icons.picked_count() == 3);
    // An icon that takes files (a folder) dragged with a file to an empty
    // place: both move, and the file does not go into the folder that has
    // just been put down under the pointer.
    {
        const Rect folder = rig.desk_icons.icon_rect(3), other = rig.desk_icons.icon_rect(2);
        g_desk_drops.clear();
        rig.click(folder.x + 36, folder.y + 16);
        rig.wm.key(wm::key::LeftCtrl, true);
        rig.click(other.x + 36, other.y + 16);
        rig.wm.key(wm::key::LeftCtrl, false);
        rig.move(folder.x + 36, folder.y + 16, 1);
        rig.move(folder.x + 46, folder.y + 26, 1);
        rig.move(folder.x + 36 + 150, folder.y + 16 + 100, 1);
        rig.move(folder.x + 36 + 150, folder.y + 16 + 100, 0);
        CHECK(g_desk_drops.empty());
        CHECK(rig.desk_icons.icon_rect(3).x == folder.x + 150 && rig.desk_icons.icon_rect(3).y == folder.y + 100);
        CHECK(rig.desk_icons.icon_rect(2).x == other.x + 150 && rig.desk_icons.icon_rect(2).y == other.y + 100);
        CHECK(moves == 2);
        // Back as they were picked before, for what follows.
        rig.click(600, 20);
        rig.click(rig.desk_icons.icon_rect(0).x + 36, rig.desk_icons.icon_rect(0).y + 16);
        rig.wm.key(wm::key::LeftCtrl, true);
        rig.click(rig.desk_icons.icon_rect(1).x + 36, rig.desk_icons.icon_rect(1).y + 16);
        rig.click(rig.desk_icons.icon_rect(2).x + 36, rig.desk_icons.icon_rect(2).y + 16);
        rig.wm.key(wm::key::LeftCtrl, false);
    }
    // Onto a window, only the one that is a file is offered (it was held).
    w.seen.clear();
    const Rect file = rig.desk_icons.icon_rect(2);
    rig.move(file.x + 36, file.y + 16, 0);
    rig.move(file.x + 36, file.y + 16, 1);
    rig.move(file.x + 46, file.y + 26, 1);
    rig.move(400, 150, 1);
    rig.move(400, 150, 0);
    CHECK(w.seen == "C:\\Documents\\Desktop\\three.txt:2 " && moves == 2);
    // A click on one of several, not dragged, picks it alone; Ctrl+click
    // takes one away; a click on the desktop itself, all.
    rig.click(file.x + 36, file.y + 16);
    CHECK(rig.desk_icons.picked_count() == 1 && rig.desk_icons.picked(2));
    rig.wm.key(wm::key::LeftCtrl, true);
    rig.click(file.x + 36, file.y + 16);
    rig.wm.key(wm::key::LeftCtrl, false);
    CHECK(rig.desk_icons.picked_count() == 0 && rig.desk_icons.selected_icon() == -1);
    // A rectangle picks what it touches; with Ctrl it adds to what is picked.
    const Rect a = rig.desk_icons.icon_rect(0), b = rig.desk_icons.icon_rect(1);
    rig.move(a.x - 20, a.y - 6, 0);
    rig.move(a.x - 20, a.y - 6, 1);
    rig.move(a.x + 40, a.y + 20, 1);
    CHECK(rig.desk_icons.picked(0) && rig.desk_icons.picked_count() == 1);
    rig.move(b.x + 40, b.y + 20, 1);
    CHECK(rig.desk_icons.picked(0) && rig.desk_icons.picked(1) && rig.desk_icons.picked_count() == 2);
    rig.move(a.x + 40, a.y + 20, 1); // pulled back: Two is let off again
    rig.move(a.x + 40, a.y + 20, 0);
    CHECK(rig.desk_icons.picked(0) && rig.desk_icons.picked_count() == 1);
    CHECK(rig.no_stale_pixels());
    rig.wm.key(wm::key::LeftCtrl, true);
    rig.move(file.x - 20, file.y - 6, 1);
    rig.move(file.x + 40, file.y + 20, 1);
    rig.move(file.x + 40, file.y + 20, 0);
    rig.wm.key(wm::key::LeftCtrl, false);
    CHECK(rig.desk_icons.picked(0) && rig.desk_icons.picked(2) && rig.desk_icons.picked_count() == 2);
    CHECK(rig.no_stale_pixels());
    rig.wm.close(w);
}

struct DragSource final : wm::Window {
    DragSource(const char *title, const Rect &frame, gfx::Colour colour) : Window(title, frame, nullptr), colour_(colour) {}
    void paint(gfx::Canvas &c) override { c.fill({0, 0, client().w, client().h}, colour_); }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind == wm::MouseKind::Move && manager() != nullptr) {
            manager()->begin_drag("C:\\a.txt");
        }
    }
    gfx::Colour colour_;
};

static void test_panels()
{
    host::MemoryDisplay display{640, 480};
    host::MemoryPointer pointer;
    host::FixedClock clock;
    wm::Wm server{display, pointer, clock};
    server.start();
    const auto at = [&](int x, int y) { return display.pixels()[y * 640 + x]; };
    const auto mouse = [&](int x, int y, uint8_t buttons) {
        clock.now += 200000;
        server.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
    };
    const auto click = [&](int x, int y) {
        mouse(x, y, 0);
        mouse(x, y, 1);
        mouse(x, y, 0);
    };

    // No panels: the background everywhere, the strip included, and a click
    // or the Start key does nothing.
    // (The default background is a mixed colour: dots of two palette
    // entries. What matters is that the strip looks like the rest.)
    const uint8_t background = at(300, 470);
    CHECK(at(300, 470) != gfx::kGrey);
    click(10, 470);
    click(300, 200);
    server.key(wm::key::LeftGui, true);
    server.key(wm::key::LeftGui, false);
    CHECK(!server.menu_open() && server.window_count() == 0);
    CHECK(server.panel(wm::PanelPlace::Bar) == nullptr && server.panel_owner(wm::PanelPlace::Bar) == proc::kNone);

    // A bar panel paints the strip and nothing else, and gets the mouse
    // there: Down, then Move and Up wherever the pointer goes.
    ProbePanel bar(server, wm::PanelPlace::Bar, 0xE0);
    server.set_panel(wm::PanelPlace::Bar, &bar);
    server.update();
    CHECK(at(300, 470) == 0xE0 && at(300, 451) != 0xE0 && at(300, 452) == 0xE0);
    mouse(100, 470, 0);
    mouse(100, 470, 1);
    mouse(100, 100, 1);
    mouse(100, 100, 0);
    CHECK(bar.seen == "D M U ");
    // A click on the desktop is not the bar's.
    bar.seen.clear();
    click(300, 200);
    CHECK(bar.seen.empty());

    // A desktop panel paints the work area behind the windows, gets the
    // clicks no window takes, a double click, and the right button.
    ProbePanel desk(server, wm::PanelPlace::Desktop, 0x1C);
    server.set_panel(wm::PanelPlace::Desktop, &desk);
    DragSource a("A", {50, 50, 200, 150}, 0x03);
    server.add(a);
    server.update();
    CHECK(at(300, 300) == 0x1C && at(100, 120) == 0x03 && at(300, 470) == 0xE0);
    // A window opened and is the one in use: news for both.
    CHECK(bar.seen.find("W ") == 0 && desk.seen.find("W ") == 0);
    bar.seen.clear();
    desk.seen.clear();
    click(300, 300); // no window is in use now: news, then the press
    CHECK(desk.seen == "W D U ");
    desk.seen.clear();
    mouse(300, 300, 1);
    mouse(300, 300, 0);
    clock.now -= 300000; // the next press comes quickly
    mouse(300, 300, 1);
    mouse(300, 300, 0);
    CHECK(desk.seen.find("D2 ") != std::string::npos);
    desk.seen.clear();
    mouse(300, 300, 2);
    mouse(300, 300, 0);
    CHECK(desk.seen == "R ");
    click(100, 120); // on the window: not the panel's
    desk.seen.clear();
    bar.seen.clear();

    // A file dragged from the window: the desktop panel is shown it, told
    // when it leaves, and gets it if it said it would take it.
    desk.takes = true;
    mouse(100, 120, 0);
    mouse(100, 120, 1);
    mouse(110, 125, 1); // the window starts the drag
    CHECK(server.item_dragging());
    mouse(300, 300, 1);
    CHECK(desk.seen == "over ");
    mouse(300, 470, 1); // over the bar: nowhere to drop
    CHECK(desk.seen == "over left ");
    mouse(300, 300, 1);
    mouse(300, 300, 0);
    CHECK(desk.seen == "over left over left drop:C:\\a.txt ");
    // One that does not take it gets no drop.
    desk.takes = false;
    desk.seen.clear();
    mouse(100, 120, 0);
    mouse(100, 120, 1);
    mouse(110, 125, 1);
    mouse(300, 300, 1);
    mouse(300, 300, 0);
    CHECK(desk.seen == "over left ");

    // The clock: news when the time is set.
    bar.seen.clear();
    server.set_time(10, 30, 0);
    CHECK(bar.seen == "C ");
    // After that, news once for each minute that ends and none in between:
    // the clock is not painted again to show what it shows already.
    bar.seen.clear();
    clock.now += 1000000;
    server.tick();
    clock.now += 58000000;
    server.tick();
    CHECK(bar.seen.empty());
    clock.now += 1000000;
    server.tick();
    clock.now += 1000000;
    server.tick();
    CHECK(bar.seen == "C ");
    // The same from the start, before anyone has set the time: nothing in
    // the first second (a remote viewer was sent the clock again for it).
    {
        host::MemoryDisplay display2{640, 480};
        wm::Wm fresh{display2, pointer, clock};
        ProbePanel bar2(fresh, wm::PanelPlace::Bar, 0xE0);
        fresh.set_panel(wm::PanelPlace::Bar, &bar2);
        fresh.start();
        bar2.seen.clear();
        clock.now += 1000000;
        fresh.tick();
        clock.now += 58000000;
        fresh.tick();
        CHECK(bar2.seen.empty());
        clock.now += 1000000;
        fresh.tick();
        CHECK(bar2.seen == "C ");
    }

    // A panel taken away in the middle of a press: the rest of the press
    // goes nowhere, and its place is background again.
    bar.seen.clear();
    mouse(100, 470, 0);
    mouse(100, 470, 1);
    server.set_panel(wm::PanelPlace::Bar, nullptr);
    mouse(120, 470, 1);
    mouse(120, 470, 0);
    server.update();
    CHECK(bar.seen == "D " && at(300, 470) == background);
    server.set_panel(wm::PanelPlace::Desktop, nullptr);
    server.close(a);
    server.update();
    CHECK(at(300, 300) != 0x1C && at(300, 300) != 0x03);
}

/* ---- The screen's mode (backlog item 68) ------------------------------------- */

static void test_video_mode()
{
    static_assert(gfx::Canvas::kMaxWidth == ports::Display::kMaxWidth, "the scratch band is as wide as the widest screen");
    Rig rig;
    TestWindow normal("Normal", {400, 250, 200, 150}, 0x1C), big("Big", {20, 20, 300, 200}, 0xE0);
    TestWindow fixed("Fixed", {500, 300, 120, 100}, 0x03), dialog("Dialog", {220, 151, 200, 150}, 0x1F);
    fixed.set_resizable(false);
    dialog.set_dialog(true); // in the middle of 640 x 452, as a dialog opens
    rig.wm.add(normal);
    rig.wm.add(big);
    rig.wm.add(fixed);
    rig.wm.add(dialog);
    rig.wm.start();
    rig.wm.toggle_maximize(big);
    rig.wm.update();
    int modes_told = 0;
    rig.wm.set_mode_listener([](void *count) { ++*static_cast<int *>(count); }, &modes_told);

    // Two modes; the first is in use, with its own frame time.
    CHECK(rig.wm.video_mode_count() == 2 && rig.wm.video_mode() == 0);
    CHECK(rig.wm.video_mode_at(1).width == 800 && rig.wm.video_mode_at(1).height == 600);
    CHECK(!rig.wm.set_video_mode(2) && !rig.wm.set_video_mode(-1));

    // Asked for, it is the mode from then on, but the screen changes at the
    // next tick; the loop is told not to sleep.
    CHECK(rig.wm.set_video_mode(1));
    CHECK(rig.wm.video_mode() == 1 && rig.display.width() == 640 && rig.wm.idle_us() == 0);
    rig.wait(1000);
    CHECK(rig.display.width() == 800 && rig.display.height() == 600 && modes_told == 1);
    CHECK(rig.wm.work_area() == Rect(0, 0, 800, 572) && rig.wm.taskbar() == Rect(0, 572, 800, 28));
    CHECK(rig.wm.frames()->totals().budget_us == 16579);
    // The maximised window fills the new work area; the dialog is in the
    // middle again; the others stay where they were.
    CHECK(big.frame() == Rect(0, 0, 800, 572));
    CHECK(dialog.frame() == Rect(300, 211, 200, 150));
    CHECK(normal.frame() == Rect(400, 250, 200, 150) && fixed.frame() == Rect(500, 300, 120, 100));
    // Painted to the new size: the taskbar's grey at the bottom right, and
    // nothing stale anywhere.
    CHECK(rig.at(790, 590) == rig.at(790, 575) && rig.no_stale_pixels());
    // The pointer reaches the new corner.
    rig.move(799, 599, 0);
    CHECK(rig.wm.pointer_x() == 799 && rig.wm.pointer_y() == 599);

    // Back: windows that would be off the smaller screen are brought onto
    // it, a resizable one made smaller if it must be; the pointer too.
    rig.wm.resize(normal, {100, 100, 690, 460});
    rig.wm.move(fixed, 700, 500);
    rig.wm.resize(dialog, {10, 10, 780, 560});
    CHECK(rig.wm.set_video_mode(0));
    rig.wait(1000);
    CHECK(rig.display.width() == 640 && modes_told == 2);
    CHECK(rig.wm.frames()->totals().budget_us == 16800);
    CHECK(big.frame() == Rect(0, 0, 640, 452));
    CHECK(normal.frame() == Rect(0, 0, 640, 452) && fixed.frame() == Rect(520, 352, 120, 100));
    CHECK(dialog.frame() == Rect(0, 0, 780, 560)); // a dialog keeps its size: its corner is on the screen
    CHECK(rig.wm.pointer_x() == 639 && rig.wm.pointer_y() == 479);
    CHECK(rig.no_stale_pixels());
    // Restored, the maximised window is its old self.
    rig.wm.toggle_maximize(big);
    CHECK(big.frame() == Rect(20, 20, 300, 200));

    // Asked for before the desktop starts (the settings), it is the mode of
    // the first picture.
    Rig early;
    CHECK(early.wm.set_video_mode(1));
    early.wm.start();
    CHECK(early.display.width() == 800 && early.wm.taskbar().y == 572);
    // A screen of another size has the one mode.
    host::MemoryDisplay small(320, 240);
    host::MemoryPointer pointer;
    host::FixedClock clock;
    wm::Wm one(small, pointer, clock);
    CHECK(one.video_mode_count() == 1 && one.set_video_mode(0) && !one.set_video_mode(1));
}

// A window with tabs: two pages, a button on each.
class TabsWindow final : public wm::Window {
public:
    TabsWindow()
        : Window("Tabs", {100, 100, 300, 200}, nullptr), tabs({4, 4, 280, 140}, kTitles, 2, &TabsWindow::turned, this),
          first({20, 40, 60, 24}, "&First"), second({0, 0, 0, 0}, "&Second"), ok({200, 150, 60, 24}, "OK")
    {
        add(tabs);
        add(first);
        add(second);
        add(ok);
    }
    static void turned(wm::Tabs &t, void *context)
    {
        auto *w = static_cast<TabsWindow *>(context);
        w->turns++;
        w->first.rect = t.selected() == 0 ? Rect(20, 40, 60, 24) : Rect(0, 0, 0, 0);
        w->second.rect = t.selected() == 1 ? Rect(20, 40, 60, 24) : Rect(0, 0, 0, 0);
    }
    static constexpr const char *kTitles[2] = {"One", "Another"};
    wm::Tabs tabs;
    wm::Button first, second, ok;
    int turns = 0;
};

static void test_tabs()
{
    Rig rig;
    TabsWindow w;
    rig.wm.add(w);
    rig.wm.start();
    const Rect c = w.client();
    CHECK(w.tabs.selected() == 0 && w.tabs.count() == 2);
    // The titles stand side by side in the strip; the page is inside the frame.
    const Rect one = w.tabs.tab(0), another = w.tabs.tab(1);
    CHECK(one.x == 2 && another.x == one.right() && another.w > one.w && one.h == wm::Tabs::kStrip - 2);
    CHECK(w.tabs.page() == Rect(6, 4 + wm::Tabs::kStrip + 2, 276, 140 - wm::Tabs::kStrip - 4));

    // A click on a title turns to its page and tells; on the one in front, nothing.
    rig.click(c.x + 4 + another.x + 5, c.y + 4 + 8);
    CHECK(w.tabs.selected() == 1 && w.turns == 1 && w.focus() == &w.tabs);
    rig.click(c.x + 4 + another.x + 5, c.y + 4 + 8);
    CHECK(w.turns == 1);
    // A click under the strip is the page's, not a title's.
    rig.click(c.x + 4 + one.x + 5, c.y + 4 + wm::Tabs::kStrip + 30);
    CHECK(w.tabs.selected() == 1);
    CHECK(rig.no_stale_pixels());

    // The arrows, with the focus on the tabs; they stop at the ends.
    rig.key(wm::key::Left);
    CHECK(w.tabs.selected() == 0 && w.turns == 2);
    rig.key(wm::key::Left);
    CHECK(w.tabs.selected() == 0 && w.turns == 2);
    rig.key(wm::key::Right);
    rig.key(wm::key::Right);
    CHECK(w.tabs.selected() == 1 && w.turns == 3);

    // Ctrl+Tab from anywhere in the window, round and round; with Shift, back.
    w.set_focus(&w.ok);
    rig.key(wm::key::Tab, wm::kCtrl);
    CHECK(w.tabs.selected() == 0 && w.turns == 4 && w.focus() == &w.ok);
    rig.key(wm::key::Tab, wm::kCtrl);
    CHECK(w.tabs.selected() == 1);
    rig.key(wm::key::Tab, wm::kCtrl | wm::kShift);
    CHECK(w.tabs.selected() == 0);

    // Tab passes over the button of the page that is not shown, and its
    // Alt+letter does nothing.
    w.set_focus(&w.tabs);
    rig.key(wm::key::Tab);
    CHECK(w.focus() == &w.first);
    rig.key(wm::key::Tab);
    CHECK(w.focus() == &w.ok);
    rig.key(wm::key::Tab);
    CHECK(w.focus() == &w.tabs);
    rig.key(0x16, wm::kAlt); // Alt+S: Second is on the other page
    CHECK(w.focus() == &w.tabs);
    rig.key(wm::key::Right);
    rig.key(0x16, wm::kAlt);
    CHECK(w.focus() == &w.second);
    CHECK(rig.no_stale_pixels());
}

/* ---- The desktop fades in (backlog item 74) ------------------------------- */

static bool plain_picture(const host::MemoryDisplay &d)
{
    // The plain desktop repeats every 8 pixels both ways.
    const uint8_t *px = const_cast<host::MemoryDisplay &>(d).pixels();
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            if (px[y * 640 + x] != px[(y & 7) * 640 + (x & 7)]) {
                return false;
            }
        }
    }
    return true;
}

static void test_arrival()
{
    const size_t size = 640 * 480;
    const auto picture = [size](Rig &r) { return Bytes(r.display.pixels(), r.display.pixels() + size); };
    // A Start menu and two icons, so that there is something to come up.
    static wm::MenuItem items[1] = {{"&Run...", nullptr, nullptr, nullptr, nullptr, 0}};
    static wm::Menu menu = {items, 1};
    static desk::Icon icons[2] = {{"One", nullptr, nullptr, nullptr}, {"Two", nullptr, nullptr, nullptr}};
    const auto dress = [](Rig &r) {
        r.wm.set_start_menu(&menu, "Chinchilla");
        r.desk_icons.set_icons(icons, 2);
    };
    // What the desktop looks like when it is shown at once, and what Shut
    // Down's fade makes of that, step by step.
    Rig once;
    dress(once);
    once.wm.start();
    CHECK(!once.wm.arriving() && !once.wm.taken_over());
    const Bytes whole = picture(once);
    std::vector<Bytes> leaving;
    once.wm.hold_screen();
    for (int step = 1; step <= 7; step++) {
        once.wm.fade_step(true, gfx::kBlack, step % 2 == 0);
        leaving.push_back(picture(once));
    }
    const Bytes plain = leaving.back();

    // Out of the plain colour: it shows from start(), rests five steps,
    // and the desktop comes up in seven, Shut Down's fade backwards.
    {
        host::HostMemory memory;
        Rig r;
        dress(r);
        r.wm.set_memory(&memory);
        int damaged = 0;
        r.wm.set_damage_listener([](const gfx::Rect &, void *n) { ++*static_cast<int *>(n); }, &damaged);
        r.wm.start(wm::Wm::Arrival::FromColour);
        CHECK(r.wm.arriving() && r.wm.taken_over());
        CHECK(!r.pointer.state().visible);
        CHECK(plain_picture(r.display) && picture(r) == plain);
        CHECK(damaged == 1);
        CHECK(memory.in_use() == 0);
        // Input is ignored: a click on the Start button opens nothing.
        const Rect sb = r.bar.button(-1);
        r.click(sb.x + 10, sb.y + 10);
        CHECK(!r.wm.menu_open());
        for (int step = 1; step <= wm::Wm::kFadeRestSteps; step++) {
            r.wait(wm::Wm::kFadeStepUs);
            CHECK(picture(r) == plain);
        }
        CHECK(memory.in_use() == 1 && memory.bytes_in_use() == size); // the desktop's picture, kept for the fade
        for (int step = 1; step <= 7; step++) {
            damaged = 0;
            r.wait(wm::Wm::kFadeStepUs);
            if (step < 7) {
                CHECK(r.wm.arriving());
                CHECK(picture(r) == leaving[static_cast<size_t>(6 - step)]);
                CHECK(damaged == 1);
            }
        }
        CHECK(picture(r) == whole);
        CHECK(!r.wm.arriving() && !r.wm.taken_over());
        CHECK(r.pointer.state().visible);
        CHECK(memory.in_use() == 0);
        // And the desktop works.
        r.click(sb.x + 10, sb.y + 10);
        CHECK(r.wm.menu_open());
    }

    // No memory for the picture: the desktop at once when the rest is over.
    {
        Rig r;
        dress(r);
        r.wm.start(wm::Wm::Arrival::FromColour);
        for (int step = 1; step < wm::Wm::kFadeRestSteps; step++) {
            r.wait(wm::Wm::kFadeStepUs);
        }
        CHECK(picture(r) == plain && r.wm.arriving());
        r.wait(wm::Wm::kFadeStepUs);
        CHECK(picture(r) == whole && !r.wm.arriving() && r.pointer.state().visible);
    }

    // Out of what the screen shows (a start-up screen): that fades into
    // the plain desktop first.
    {
        host::HostMemory memory;
        Rig r;
        dress(r);
        r.wm.set_memory(&memory);
        memset(r.display.pixels(), gfx::kWhite, size);
        r.wm.start(wm::Wm::Arrival::FromPicture);
        CHECK(r.display.pixels()[0] == gfx::kWhite && r.wm.arriving());
        r.wait(wm::Wm::kFadeStepUs);
        CHECK(r.display.pixels()[0] != gfx::kWhite && picture(r) != plain);
        for (int step = 2; step <= 7; step++) {
            r.wait(wm::Wm::kFadeStepUs);
        }
        CHECK(picture(r) == plain);
        for (int step = 1; step <= wm::Wm::kFadeRestSteps + 7; step++) {
            r.wait(wm::Wm::kFadeStepUs);
        }
        CHECK(picture(r) == whole && !r.wm.arriving());
        CHECK(memory.in_use() == 0);
    }

    // A machine switched off in the middle: the picture is given back.
    {
        host::HostMemory memory;
        {
            Rig r;
            dress(r);
            r.wm.set_memory(&memory);
            r.wm.start(wm::Wm::Arrival::FromColour);
            for (int step = 1; step <= wm::Wm::kFadeRestSteps + 2; step++) {
                r.wait(wm::Wm::kFadeStepUs);
            }
            CHECK(memory.in_use() == 1);
        }
        CHECK(memory.in_use() == 0);
    }
}

int main()
{
    test_arrival();
    test_video_mode();
    test_tabs();
    test_rect_list();
    test_panels();
    test_dither();
    test_canvas_clip();
    test_stacking_and_focus();
    test_drag();
    test_title_buttons_and_taskbar();
    test_widget_capture();
    test_describe();
    test_many_windows();
    test_no_window_limit();
    test_resize();
    test_shortcuts();
    test_desktop_shortcuts();
    test_menus();
    test_hourglass();
    test_slow_submenu();
    test_submenu_switches_at_once();
    test_slow_key_does_not_repeat();
    test_start_menu_and_icons();
    test_icon_places();
    test_drag_several();
    test_quick_launch();
    test_widgets();
    test_timers_and_clock();
    test_modal();
    test_wheel();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
