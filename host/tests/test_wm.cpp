// Unit tests for the drawing layer and the window manager. No framework:
// each failed check prints its line, the exit code is the result.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/wm/widgets.hpp"
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
    // Resting on another parent item hands over to it after the delay.
    rig.wm.open_menu(menu, 100, 100);
    Rect r = rig.wm.popup_item(0, 0);
    rig.move(r.x + 20, r.y + r.h / 2, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    r = rig.wm.popup_item(0, 1);
    rig.quick(r.x + 20, r.y + r.h / 2, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs + 10000);
    CHECK(rig.wm.popup_count() == 1 && rig.wm.popup_hover(0) == 1);
    // The pointer off the menus leaves them as they are; a click there closes them.
    rig.move(500, 400, 0);
    rig.wait(wm::Wm::kSubmenuDelayUs);
    CHECK(rig.wm.popup_count() == 1);
    rig.click(500, 400);
    CHECK(rig.wm.popup_count() == 0);
    CHECK(rig.no_stale_pixels());
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
    wm::DesktopIcon icons[2] = {{labels[0], nullptr, &run1, nullptr}, {labels[1], nullptr, &run2, nullptr}};
    rig.wm.set_desktop_icons(icons, 2);
    rig.wm.start();

    // The Start button opens it above the taskbar; GUI and Ctrl+Esc toggle it.
    Rect sb = rig.wm.taskbar_button(-1);
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
    Rect one = rig.wm.desktop_icon_rect(0), two = rig.wm.desktop_icon_rect(1);
    rig.click(one.x + 30, one.y + 20);
    CHECK(rig.wm.selected_icon() == 0 && rig.wm.active() == nullptr && g_runs[1] == 0);
    rig.double_click(two.x + 30, two.y + 20);
    CHECK(rig.wm.selected_icon() == 1 && g_runs[2] == 1 && g_runs[1] == 0);
    rig.click(200, 300); // empty desktop
    CHECK(rig.wm.selected_icon() == -1);
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
    Rect tb = rig.wm.taskbar_button(1);
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
    Rect sb = rig.wm.taskbar_button(-1);
    rig.click(sb.x + 5, sb.y + 5);
    CHECK(!rig.wm.menu_open() && rig.wm.active() == &dialog);
    CHECK(rig.no_stale_pixels());
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
    test_resize();
    test_shortcuts();
    test_menus();
    test_slow_submenu();
    test_slow_key_does_not_repeat();
    test_start_menu_and_icons();
    test_widgets();
    test_timers_and_clock();
    test_modal();
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
