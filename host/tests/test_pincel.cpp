// Pincel, the drawing program (programs/pincel): first what the tools do
// to a sheet, on a sheet of the test's own, then the window, driven with
// the mouse and the keyboard as the user would. Given a folder, it writes
// the program's reference pictures there instead (tools/check_pictures.py).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/fs/memory_volume.hpp"
#include "core/gfx/theme.hpp"
#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "../../programs/pincel/sheet.hpp"
#include "../../programs/test_hooks.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

using pincel::Sheet;
using pincel::Style;

constexpr gfx::Colour kInk = gfx::rgb(7, 0, 0), kPaper = gfx::rgb(7, 7, 3), kBlue = gfx::rgb(0, 0, 3);

/* ---- The sheet ------------------------------------------------------------------ */

struct Paper {
    Paper(int w, int h) : bytes(static_cast<size_t>(w) * h, kPaper) { sheet = {bytes.data(), w, h}; }
    uint8_t at(int x, int y) const { return bytes[static_cast<size_t>(y) * sheet.width + x]; }
    int count(gfx::Colour c) const
    {
        int n = 0;
        for (uint8_t b : bytes) {
            n += b == c;
        }
        return n;
    }
    std::vector<uint8_t> bytes;
    Sheet sheet;
};

void flood(Paper &p, int x, int y, gfx::Colour c, int capacity = 4096)
{
    const std::vector<uint8_t> before = p.bytes;
    std::vector<pincel::Seed> seeds(static_cast<size_t>(capacity));
    gfx::Rect changed;
    pincel::flood(p.sheet, x, y, c, before.data(), seeds.data(), capacity, changed);
}

void test_tips_and_lines()
{
    Paper p(40, 30);
    gfx::Rect changed;
    pincel::stamp(p.sheet, 5, 5, {1, false}, kInk, changed);
    CHECK(p.count(kInk) == 1 && p.at(5, 5) == kInk);
    CHECK(changed == gfx::Rect(5, 5, 1, 1));
    // A round tip of 7 has no corners; a square one has.
    pincel::stamp(p.sheet, 20, 15, {7, false}, kInk, changed);
    CHECK(p.at(20, 15) == kInk && p.at(17, 15) == kInk && p.at(23, 15) == kInk && p.at(20, 12) == kInk);
    CHECK(p.at(17, 12) == kPaper && p.at(23, 18) == kPaper);
    pincel::stamp(p.sheet, 20, 15, {7, true}, kBlue, changed);
    CHECK(p.at(17, 12) == kBlue && p.at(23, 18) == kBlue && p.at(24, 15) == kPaper);
    // Off the sheet: cut, and nothing written outside it.
    changed = gfx::Rect();
    pincel::stamp(p.sheet, -2, -2, {7, true}, kInk, changed);
    CHECK(changed == gfx::Rect(0, 0, 2, 2));
    pincel::stamp(p.sheet, 100, 100, {7, true}, kInk, changed);
    CHECK(changed == gfx::Rect(0, 0, 2, 2));

    // A line touches both ends and every row and column between them once.
    Paper q(40, 30);
    changed = gfx::Rect();
    pincel::line(q.sheet, 2, 3, 30, 12, {1, false}, kInk, changed);
    CHECK(q.at(2, 3) == kInk && q.at(30, 12) == kInk && q.count(kInk) == 29);
    CHECK(changed == gfx::Rect(2, 3, 29, 10));
    pincel::line(q.sheet, 35, 28, 35, 1, {1, false}, kBlue, changed);
    CHECK(q.count(kBlue) == 28);
    // One that leaves the sheet is cut.
    pincel::line(q.sheet, -10, 20, 50, 20, {1, false}, kBlue, changed);
    CHECK(q.at(0, 20) == kBlue && q.at(39, 20) == kBlue);
}

void test_shapes()
{
    gfx::Rect changed;
    // The corners in any order give the same rectangle.
    Paper a(40, 30), b(40, 30);
    pincel::box(a.sheet, 5, 4, 20, 14, 1, Style::Outline, kInk, kBlue, changed);
    pincel::box(b.sheet, 20, 14, 5, 4, 1, Style::Outline, kInk, kBlue, changed);
    CHECK(a.bytes == b.bytes);
    CHECK(a.count(kInk) == 2 * 16 + 2 * 9 && a.at(5, 4) == kInk && a.at(20, 14) == kInk && a.at(6, 5) == kPaper);
    Paper c(40, 30);
    pincel::box(c.sheet, 5, 4, 20, 14, 2, Style::Both, kInk, kBlue, changed);
    CHECK(c.at(6, 5) == kInk && c.at(7, 6) == kBlue && c.count(kBlue) == 12 * 7);
    Paper d(40, 30);
    pincel::box(d.sheet, 5, 4, 20, 14, 2, Style::Solid, kInk, kBlue, changed);
    CHECK(d.count(kInk) == 16 * 11 && d.count(kBlue) == 0);
    // An edge thicker than the box: all ink, nothing outside it.
    Paper e(40, 30);
    pincel::box(e.sheet, 5, 4, 8, 6, 7, Style::Both, kInk, kBlue, changed);
    CHECK(e.count(kInk) == 4 * 3 && e.count(kBlue) == 0);

    // Ellipses of every size up to 40 by 30 and every edge: the outline
    // has no gap (a flood from a corner of the sheet does not get in), it
    // stays in its box and touches all four sides, and it is the same left
    // to right and top to bottom.
    for (int w = 1; w <= 40; w += 1) {
        for (int h = 1; h <= 30; h += (w % 3) + 1) {
            for (int edge : {1, 2, 4, 7}) {
                Paper p(44, 34);
                changed = gfx::Rect();
                pincel::ellipse(p.sheet, 2, 2, 2 + w - 1, 2 + h - 1, edge, Style::Outline, kInk, kBlue, changed);
                bool inside_box = true, mirrored = true;
                bool touch[4] = {};
                for (int y = 0; y < 34; y++) {
                    for (int x = 0; x < 44; x++) {
                        if (p.at(x, y) != kInk) {
                            continue;
                        }
                        inside_box = inside_box && x >= 2 && x < 2 + w && y >= 2 && y < 2 + h;
                        mirrored = mirrored && p.at(2 + w - 1 - (x - 2), y) == kInk &&
                                   p.at(x, 2 + h - 1 - (y - 2)) == kInk;
                        touch[0] |= x == 2;
                        touch[1] |= y == 2;
                        touch[2] |= x == 2 + w - 1;
                        touch[3] |= y == 2 + h - 1;
                    }
                }
                Paper filled = p;
                flood(filled, 0, 0, kBlue);
                // What the flood did not reach is the ellipse, edge and inside: no paper left
                // outside it, and the inside, if there is one, still paper.
                bool closed = true;
                for (int y = 2; y < 2 + h && closed; y++) {
                    int first = -1, last = -1;
                    for (int x = 2; x < 2 + w; x++) {
                        if (p.at(x, y) == kInk) {
                            first = first < 0 ? x : first;
                            last = x;
                        }
                    }
                    for (int x = first + 1; x < last; x++) {
                        closed = closed && filled.at(x, y) != kBlue;
                    }
                }
                if (!(inside_box && mirrored && closed && touch[0] && touch[1] && touch[2] && touch[3])) {
                    printf("ellipse %d x %d, edge %d: box %d mirrored %d closed %d\n", w, h, edge, inside_box,
                           mirrored, closed);
                    failures++;
                }
                CHECK(changed == gfx::Rect(2, 2, w, h));
            }
        }
    }
    // Filled with the paper: the inside is the paper colour and the edge ink;
    // solid: one colour, as many dots as outline and inside together.
    Paper f(44, 34), g(44, 34);
    pincel::ellipse(f.sheet, 2, 2, 41, 31, 2, Style::Both, kInk, kBlue, changed);
    pincel::ellipse(g.sheet, 41, 31, 2, 2, 2, Style::Solid, kInk, kBlue, changed);
    CHECK(f.at(22, 16) == kBlue && f.at(2, 16) == kInk && f.at(3, 16) == kInk && f.at(4, 16) == kBlue);
    CHECK(f.at(2, 2) == kPaper && f.count(kBlue) > 0 && f.count(kInk) + f.count(kBlue) == g.count(kInk));
}

void test_flood()
{
    // A spiral of walls: the flood follows the corridor to its end, with
    // plenty of working memory and with next to none.
    auto maze = [](Paper &p) {
        gfx::Rect changed;
        for (int i = 0; i < 7; i++) {
            const int a = 2 + 4 * i, b = 59 - 4 * i;
            pincel::line(p.sheet, a, a, b, a, {1, false}, kInk, changed);
            pincel::line(p.sheet, b, a, b, b, {1, false}, kInk, changed);
            pincel::line(p.sheet, b, b, a + 4, b, {1, false}, kInk, changed);
            pincel::line(p.sheet, a + 4, b, a + 4, a + 4, {1, false}, kInk, changed);
        }
        // Teeth along the top, which leave a seed each.
        for (int x = 1; x < 60; x += 2) {
            p.sheet.pixels[x] = kInk;
        }
    };
    Paper plenty(62, 62), few(62, 62), one(62, 62);
    maze(plenty);
    maze(few);
    maze(one);
    const int paper = plenty.count(kPaper);
    flood(plenty, 0, 30, kBlue);
    flood(few, 0, 30, kBlue, 3);
    flood(one, 0, 30, kBlue, 1);
    CHECK(plenty.count(kPaper) == 0 && plenty.count(kBlue) == paper);
    CHECK(few.bytes == plenty.bytes && one.bytes == plenty.bytes);
    // It does not cross a wall, nor a corner between two walls.
    Paper walled(20, 20);
    gfx::Rect changed;
    pincel::box(walled.sheet, 4, 4, 12, 12, 1, Style::Outline, kInk, kPaper, changed);
    pincel::line(walled.sheet, 0, 19, 19, 0, {1, false}, kInk, changed); // a diagonal: closed up, down, left, right
    flood(walled, 0, 0, kBlue);
    CHECK(walled.at(8, 8) == kPaper && walled.at(19, 19) == kPaper && walled.at(1, 1) == kBlue);
    changed = gfx::Rect();
    Paper same(8, 8);
    std::vector<pincel::Seed> seeds(16);
    pincel::flood(same.sheet, 3, 3, kPaper, same.bytes.data(), seeds.data(), 16, changed); // already that colour
    CHECK(changed.empty());
    pincel::flood(same.sheet, 3, 3, kInk, same.bytes.data(), seeds.data(), 16, changed);
    CHECK(changed == gfx::Rect(0, 0, 8, 8) && same.count(kInk) == 64);
}

void test_whole_sheet()
{
    Paper p(5, 3);
    for (int i = 0; i < 15; i++) {
        p.bytes[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    }
    pincel::flip_horizontal(p.sheet);
    CHECK(p.at(0, 0) == 4 && p.at(4, 0) == 0 && p.at(2, 1) == 7);
    pincel::flip_vertical(p.sheet);
    CHECK(p.at(0, 0) == 14 && p.at(4, 2) == 0);
    pincel::invert(p.sheet);
    CHECK(p.at(4, 2) == 255 && p.at(0, 0) == 241);
}

/* ---- The window ----------------------------------------------------------------- */

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

void mouse(int x, int y, uint8_t buttons)
{
    clock_.now += 300000;
    desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
}

void click(int x, int y, uint8_t button = 1)
{
    mouse(x, y, 0);
    mouse(x, y, button);
    mouse(x, y, 0);
}

void double_click(int x, int y)
{
    mouse(x, y, 0);
    for (int i = 0; i < 2; i++) {
        clock_.now += 50000;
        desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 1, 0});
        clock_.now += 50000;
        desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), 0, 0});
    }
}

void key(uint16_t usage, uint8_t mods = 0)
{
    const uint16_t mod_keys[3] = {wm::key::LeftShift, wm::key::LeftCtrl, wm::key::LeftAlt};
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], true);
        }
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    for (int i = 0; i < 3; i++) {
        if (mods & (1 << i)) {
            desktop.key(mod_keys[i], false);
        }
    }
    clock_.now += 100000;
}
constexpr uint8_t kCtrl = 2, kAlt = 4;
void letter(char c, uint8_t mods = 0) { key(static_cast<uint16_t>(wm::key::A + (c - 'a')), mods); }

void wait(uint32_t us)
{
    clock_.now += us;
    desktop.tick();
}

// The Pincel window, whatever picture it has.
wm::Window *pincel_window()
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (strstr(desktop.window_at(z)->title(), " - Pincel") != nullptr) {
            return desktop.window_at(z);
        }
    }
    return nullptr;
}
const char *front() { return desktop.active() != nullptr ? desktop.active()->title() : ""; }

// Where things are in the window (programs/pincel/pincel.cpp).
enum Tool { kPencil, kBrush, kEraser, kLine, kBox, kEllipse, kFill, kPicker };
void tool(int which)
{
    const gfx::Rect c = pincel_window()->client();
    click(c.x + 4 + (which % 2) * 26 + 13, c.y + 4 + (which / 2) * 26 + 13);
}
void size(int which)
{
    const gfx::Rect c = pincel_window()->client();
    click(c.x + 4 + 26, c.y + 8 + 4 * 26 + 4 + which * 12 + 6);
}
void style(int which)
{
    const gfx::Rect c = pincel_window()->client();
    click(c.x + 4 + 2 + which * 16 + 8, c.y + 8 + 4 * 26 + 4 + 4 * 12 + 4 + 8);
}
void swatch_xy(int which, int &x, int &y)
{
    const gfx::Rect c = pincel_window()->client();
    x = c.x + 4 + 56 + (which % 14) * 16 + 8;
    y = c.bottom() - 20 - 4 - 34 + 1 + (which / 14) * 16 + 8;
}
void colour(int which, uint8_t button = 1)
{
    int x, y;
    swatch_xy(which, x, y);
    click(x, y, button);
}
// A point of the sheet on the screen (with the sheet not scrolled).
int sx(int x) { return pincel_window()->client().x + 60 + x; }
int sy(int y) { return pincel_window()->client().y + 4 + y; }
void pull(int x0, int y0, int x1, int y1)
{
    mouse(sx(x0), sy(y0), 0);
    mouse(sx(x0), sy(y0), 1);
    mouse(sx((x0 + x1) / 2), sy(y0), 1); // by way of somewhere else
    mouse(sx(x1), sy(y1), 1);
    mouse(sx(x1), sy(y1), 0);
}

uint8_t dot(int x, int y)
{
    const Sheet *s = pincel::test_sheet();
    return s != nullptr && x < s->width && y < s->height ? s->pixels[y * s->width + x] : 0;
}
int dots(gfx::Colour c)
{
    const Sheet *s = pincel::test_sheet();
    int n = 0;
    for (int i = 0; s != nullptr && i < s->width * s->height; i++) {
        n += s->pixels[i] == c;
    }
    return n;
}
uint8_t pixel(int x, int y) { return display.pixels()[y * 640 + x]; }

// What the screen shows of the sheet is the sheet.
bool screen_shows_sheet()
{
    desktop.update();
    const Sheet *s = pincel::test_sheet();
    const gfx::Rect c = pincel_window()->client();
    for (int y = 0; y < s->height && sy(y) < c.bottom() - 70; y++) {
        for (int x = 0; x < s->width && sx(x) < c.right() - 4; x++) {
            if (pixel(sx(x), sy(y)) != s->pixels[y * s->width + x]) {
                printf("the screen differs from the sheet at %d, %d\n", x, y);
                return false;
            }
        }
    }
    return true;
}

bool no_stale_pixels()
{
    desktop.update();
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    for (size_t i = 0; i < before.size(); i++) {
        if (static_cast<uint8_t>(before[i]) != display.pixels()[i]) {
            printf("a stale pixel at %d, %d: %02x, painted again %02x\n", static_cast<int>(i % 640),
                   static_cast<int>(i / 640), static_cast<uint8_t>(before[i]), display.pixels()[i]);
            int n = 0, x0 = 999, x1 = 0, y0 = 999, y1 = 0;
            for (size_t j = 0; j < before.size(); j++) {
                if (static_cast<uint8_t>(before[j]) != display.pixels()[j]) {
                    const int x = static_cast<int>(j % 640), y = static_cast<int>(j / 640);
                    n++;
                    x0 = x < x0 ? x : x0;
                    x1 = x > x1 ? x : x1;
                    y0 = y < y0 ? y : y0;
                    y1 = y > y1 ? y : y1;
                }
            }
            printf("%d stale pixels, from %d, %d to %d, %d\n", n, x0, y0, x1, y1);
            return false;
        }
    }
    return true;
}

void start_desktop()
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    constexpr size_t kRamDisk = 2 * 1024 * 1024;
    static fs::MemoryVolume disk(memory.allocate(kRamDisk), kRamDisk, "RAM disk");
    storage.mount('C', disk);
    desktop.set_animations(false);
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    desktop.set_time(9, 41, 0);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    storage.make_folder("C:\\pictures");
    wait(1000);
}

void open_pincel(const char *path = nullptr)
{
    shell::start_program("Pincel", path);
    desktop.update();
}

void close_pincel()
{
    for (int tries = 0; pincel_window() != nullptr && tries < 8; tries++) {
        desktop.activate(*pincel_window());
        key(wm::key::F4, kAlt);
        if (strcmp(front(), "Pincel") == 0) {
            letter('n', kAlt); // "Do you want to save the changes?" No
        }
        wait(1000);
    }
    CHECK(pincel_window() == nullptr);
}

// A picture file decoded to board colours, with no dithering.
std::vector<uint8_t> decoded(const char *path, int &w, int &h, image::Dither dither = image::Dither::None)
{
    image::Decoder decoder;
    image::Converter converter;
    image::Info info;
    std::vector<uint8_t> out;
    if (decoder.open(storage, path, memory, info) != image::Error::None || !converter.ready(memory)) {
        w = h = 0;
        return out;
    }
    w = info.width;
    h = info.height;
    out.resize(static_cast<size_t>(w) * h);
    converter.begin(w, h, w, h, out.data(), dither);
    while (!decoder.done() && decoder.step(converter, 16) == image::Error::None) {
    }
    decoder.release(memory);
    converter.release(memory);
    return out;
}

void test_drawing()
{
    start_desktop();
    const uint32_t out_before = os::bytes_out();
    open_pincel();
    CHECK(pincel_window() != nullptr && strcmp(front(), "Untitled - Pincel") == 0);
    const Sheet *s = pincel::test_sheet();
    CHECK(s != nullptr && s->width == 320 && s->height == 240);
    CHECK(dots(kPaper) == 320 * 240 && pincel::test_ink() == gfx::kBlack && pincel::test_paper() == kPaper);
    // The sheet and its copy for Undo, and little else.
    CHECK(os::bytes_out() - out_before >= 2 * 320 * 240 && os::bytes_out() - out_before < 2 * 320 * 240 + 8192);

    // The pencil: a dot, then a stroke of one dot's width, in the ink.
    click(sx(10), sy(10));
    CHECK(dot(10, 10) == gfx::kBlack && dots(gfx::kBlack) == 1);
    CHECK(strcmp(front(), "Untitled* - Pincel") == 0);
    colour(16); // red
    CHECK(pincel::test_ink() == kInk);
    pull(20, 20, 60, 40);
    CHECK(dot(20, 20) == kInk && dot(60, 40) == kInk && dot(40, 20) == kInk && dots(kInk) == 41);
    CHECK(screen_shows_sheet());

    // Undo takes the stroke away, and again brings it back.
    letter('z', kCtrl);
    CHECK(dots(kInk) == 0 && dots(gfx::kBlack) == 1);
    CHECK(screen_shows_sheet());
    letter('e', kAlt);
    letter('u');
    CHECK(dots(kInk) == 41);

    // The brush is thicker, and thicker still at the last size.
    tool(kBrush);
    click(sx(100), sy(100));
    CHECK(dot(100, 100) == kInk && dot(101, 100) == kInk && dot(98, 100) == kInk && dot(98, 98) == kPaper && dot(102, 100) == kPaper);
    size(3);
    click(sx(120), sy(100));
    CHECK(dot(117, 100) == kInk && dot(123, 100) == kInk && dot(117, 97) == kPaper);

    // The eraser leaves the paper colour, square.
    tool(kEraser);
    size(0);
    click(sx(120), sy(100));
    CHECK(dot(118, 98) == kPaper && dot(121, 101) == kPaper && dot(122, 100) == kInk);

    // A line: while it is pulled it follows the pointer and leaves nothing
    // behind; let go, it stays.
    tool(kLine);
    const int before_line = dots(kInk);
    mouse(sx(200), sy(10), 0);
    mouse(sx(200), sy(10), 1);
    mouse(sx(300), sy(100), 1);
    CHECK(dot(300, 100) == kInk);
    CHECK(screen_shows_sheet());
    mouse(sx(300), sy(10), 1);
    CHECK(dot(300, 100) == kPaper && dot(250, 10) == kInk && dots(kInk) == before_line + 101);
    CHECK(screen_shows_sheet());
    mouse(sx(300), sy(10), 0);
    CHECK(dots(kInk) == before_line + 101);

    // A rectangle of each style; the right button on a colour is the paper.
    colour(20, 2); // blue paper
    CHECK(pincel::test_paper() == kBlue && pincel::test_ink() == kInk);
    tool(kBox);
    pull(10, 120, 50, 150);
    CHECK(dot(10, 120) == kInk && dot(50, 150) == kInk && dot(30, 135) == kPaper);
    style(1);
    pull(60, 120, 100, 150);
    CHECK(dot(60, 120) == kInk && dot(80, 135) == kBlue);
    style(2);
    pull(110, 150, 150, 120); // from the other corner
    CHECK(dot(110, 120) == kInk && dot(130, 135) == kInk && dot(150, 150) == kInk);

    // An ellipse, thick, filled with the paper.
    tool(kEllipse);
    style(1);
    size(2);
    pull(10, 160, 90, 220);
    CHECK(dot(10, 190) == kInk && dot(13, 190) == kInk && dot(14, 190) == kBlue && dot(50, 190) == kBlue);
    CHECK(dot(10, 160) == kPaper && dot(90, 220) == kPaper);

    // The fill: the inside of the first rectangle, and no further.
    tool(kFill);
    colour(18); // green
    const gfx::Colour green = pincel::test_ink();
    click(sx(30), sy(135));
    CHECK(dots(green) == 39 * 29 && dot(11, 121) == green && dot(49, 149) == green && dot(51, 135) == kPaper);

    // Picking a colour from the sheet.
    tool(kPicker);
    click(sx(80), sy(135));
    CHECK(pincel::test_ink() == kBlue);
    CHECK(screen_shows_sheet());
    CHECK(no_stale_pixels());

    // Drawing off the edge of the sheet's area does no harm.
    tool(kBrush);
    const gfx::Rect c = pincel_window()->client();
    mouse(sx(300), sy(200), 0);
    mouse(sx(300), sy(200), 1);
    mouse(c.right() + 30, c.bottom() + 30, 1);
    mouse(c.x - 20, c.y - 40, 1);
    mouse(c.x - 20, c.y - 40, 0);
    CHECK(dot(319, 239) == kPaper || dot(319, 239) == kBlue);
    CHECK(screen_shows_sheet());

    // The whole sheet: flipped, the dot at 10, 10 is at 309, 10; Undo.
    letter('z', kCtrl);
    letter('i', kAlt);
    letter('h');
    CHECK(dot(309, 10) == gfx::kBlack && dot(10, 10) == kPaper);
    letter('z', kCtrl);
    CHECK(dot(10, 10) == gfx::kBlack);
    letter('i', kAlt);
    letter('v');
    CHECK(dot(10, 229) == gfx::kBlack);
    letter('z', kCtrl);
    letter('i', kAlt);
    letter('i');
    CHECK(dot(10, 10) == kPaper && dot(0, 0) == gfx::kBlack);
    letter('z', kCtrl);
    CHECK(screen_shows_sheet());
}

void test_files()
{
    // Save: asks for a name the first time, and writes a BMP that decodes
    // to exactly the sheet, whatever dithering reads it.
    const Sheet *s = pincel::test_sheet();
    const std::vector<uint8_t> drawn(s->pixels, s->pixels + 320 * 240);
    letter('s', kCtrl);
    CHECK(strcmp(front(), "Save As") == 0);
    desktop.text("first");
    key(wm::key::Enter);
    wait(1000);
    CHECK(strcmp(front(), "first.bmp - Pincel") == 0);
    fs::DirEntry entry;
    CHECK(storage.stat("C:\\pictures\\first.bmp", entry) == ports::FsError::Ok);
    CHECK(entry.size == pincel::kBmpHeader + 320 * 240);
    int w = 0, h = 0;
    CHECK(decoded("C:\\pictures\\first.bmp", w, h) == drawn && w == 320 && h == 240);
    CHECK(decoded("C:\\pictures\\first.bmp", w, h, image::Dither::FloydSteinberg) == drawn);
    CHECK(decoded("C:\\pictures\\first.bmp", w, h, image::Dither::Bayer4) == drawn);

    // A change, then Save again: no question, the same file.
    tool(kPencil);
    click(sx(5), sy(5));
    CHECK(strcmp(front(), "first.bmp* - Pincel") == 0);
    letter('s', kCtrl);
    wait(1000);
    CHECK(strcmp(front(), "first.bmp - Pincel") == 0);
    CHECK(decoded("C:\\pictures\\first.bmp", w, h)[5 * 320 + 5] == dot(5, 5));

    // Another size: what was drawn stays, the new room is paper, and a
    // width that is not a multiple of four is saved and read back whole.
    letter('i', kAlt);
    letter('s');
    CHECK(strcmp(front(), "Size") == 0);
    desktop.text("333");
    key(wm::key::Tab);
    desktop.update();
    desktop.text("0"); // in place of the 240: Tab picked all of it
    key(wm::key::Enter);
    CHECK(strcmp(front(), "Pincel") == 0); // "from 1 by 1 to 800 by 600"
    key(wm::key::Enter);
    CHECK(strcmp(front(), "Size") == 0);
    key(wm::key::Backspace);
    desktop.text("77");
    key(wm::key::Enter);
    s = pincel::test_sheet();
    CHECK(s->width == 333 && s->height == 77 && dot(40, 20) == drawn[20 * 320 + 40] && dot(332, 0) == pincel::test_paper());
    CHECK(dot(250, 10) == kInk);
    CHECK(screen_shows_sheet());
    letter('f', kAlt);
    letter('a');
    desktop.text("odd.bmp");
    key(wm::key::Enter);
    wait(1000);
    CHECK(strcmp(front(), "odd.bmp - Pincel") == 0);
    CHECK(storage.stat("C:\\pictures\\odd.bmp", entry) == ports::FsError::Ok && entry.size == pincel::kBmpHeader + 336 * 77);
    const std::vector<uint8_t> odd(s->pixels, s->pixels + 333 * 77);
    CHECK(decoded("C:\\pictures\\odd.bmp", w, h) == odd && w == 333 && h == 77);

    // New: a white... a sheet of paper of the first size, with no name.
    letter('n', kCtrl);
    s = pincel::test_sheet();
    CHECK(strcmp(front(), "Untitled - Pincel") == 0 && s->width == 320 && s->height == 240);
    CHECK(dots(pincel::test_paper()) == 320 * 240);

    // Open: the file comes back as it was drawn.
    letter('o', kCtrl);
    CHECK(strcmp(front(), "Open") == 0);
    desktop.text("first.bmp");
    key(wm::key::Enter);
    wait(1000);
    CHECK(strcmp(front(), "first.bmp - Pincel") == 0);
    s = pincel::test_sheet();
    CHECK(s->width == 320 && s->height == 240 && dot(5, 5) != kPaper && dot(250, 10) == kInk);
    CHECK(screen_shows_sheet());

    // Closing with changes asks; Cancel keeps the window, No lets it go.
    click(sx(7), sy(7));
    key(wm::key::F4, kAlt);
    CHECK(strcmp(front(), "Pincel") == 0);
    key(wm::key::Escape);
    CHECK(pincel_window() != nullptr);
    const uint32_t files = static_cast<uint32_t>(os::file::open_files());
    close_pincel();
    CHECK(pincel_window() == nullptr && files == 0);

    // Started with a file, as the File manager or Run would: it opens it.
    // A file that is no picture says so and leaves an empty sheet.
    open_pincel("C:\\pictures\\odd.bmp");
    CHECK(strcmp(front(), "odd.bmp - Pincel") == 0 && pincel::test_sheet()->width == 333);
    close_pincel();
    storage.write_file("C:\\pictures\\not.bmp", "hello", 5);
    open_pincel("C:\\pictures\\not.bmp");
    CHECK(strcmp(front(), "Pincel") == 0);
    key(wm::key::Enter);
    CHECK(strcmp(front(), "Untitled - Pincel") == 0);
    close_pincel();
}

void test_palette_and_memory()
{
    // A double click on a colour opens the colour dialog; the colour chosen
    // takes its place and is kept for the next time.
    const uint32_t out_before = os::bytes_out();
    open_pincel();
    int x, y;
    swatch_xy(27, x, y);
    double_click(x, y);
    CHECK(strcmp(front(), "Colour") == 0);
    key(wm::key::Left); // the colour beside it on the sheet of 256
    key(wm::key::Enter);
    CHECK(strcmp(front(), "Untitled - Pincel") == 0);
    const gfx::Colour chosen = pincel::test_ink();
    CHECK(chosen != gfx::rgb(7, 4, 3));
    close_pincel();
    CHECK(os::bytes_out() == out_before); // everything given back
    open_pincel();
    colour(0);
    colour(27);
    CHECK(pincel::test_ink() == chosen);

    // Two at once: each a process with its own sheet.
    open_pincel();
    int count = 0;
    for (int z = 0; z < desktop.window_count(); z++) {
        count += strstr(desktop.window_at(z)->title(), " - Pincel") != nullptr;
    }
    CHECK(count == 2);
    close_pincel();
    CHECK(os::bytes_out() == out_before);
}

/* ---- Reference pictures --------------------------------------------------------- */

// Something to look at: a house under a sun, with every tool.
void draw_scene()
{
    tool(kFill);
    colour(25); // sky
    click(sx(5), sy(5));
    colour(18, 2); // paper: green
    tool(kBox);
    style(2);
    colour(18);
    pull(0, 180, 319, 239); // the grass
    colour(0);
    colour(22, 2);
    style(1);
    size(1);
    pull(90, 110, 200, 200); // the house
    colour(14, 2);
    pull(110, 130, 140, 160); // a window
    colour(8, 2);
    pull(155, 150, 180, 200); // the door
    tool(kLine);
    size(2);
    colour(2);
    pull(84, 112, 145, 60);
    pull(145, 60, 206, 112); // the roof
    tool(kEllipse);
    colour(23);
    colour(17, 2);
    size(1);
    pull(240, 20, 290, 70); // the sun
    tool(kBrush);
    colour(14);
    size(3);
    pull(30, 40, 70, 44);
    pull(50, 34, 80, 38); // a cloud
    tool(kPencil);
    colour(0);
    pull(125, 130, 125, 160);
    pull(110, 145, 140, 145);
}

int write_pictures(const std::string &folder)
{
    auto save = [&](const char *name) {
        desktop.update();
        const std::string path = folder + "/" + name + ".raw";
        FILE *file = fopen(path.c_str(), "wb");
        if (file == nullptr || fwrite(display.pixels(), 1, 640 * 480, file) != 640 * 480) {
            printf("cannot write %s\n", path.c_str());
            failures++;
        }
        if (file != nullptr) {
            fclose(file);
        }
    };
    start_desktop();
    open_pincel();
    save("90-pincel-new");
    draw_scene();
    tool(kEllipse);
    save("91-pincel-drawing");
    // A shape while it is pulled, and the Image menu.
    mouse(sx(20), sy(90), 0);
    mouse(sx(20), sy(90), 1);
    mouse(sx(70), sy(150), 1);
    save("92-pincel-pulling");
    mouse(sx(20), sy(90), 1);
    mouse(sx(20), sy(90), 0);
    letter('z', kCtrl);
    letter('i', kAlt);
    save("93-pincel-image-menu");
    letter('s');
    save("94-pincel-size");
    desktop.text("700");
    key(wm::key::Tab);
    desktop.update();
    desktop.text("500");
    key(wm::key::Enter);
    save("95-pincel-scroll-bars");
    key(wm::key::F4, kAlt);
    save("96-pincel-save-changes");
    key(wm::key::Escape);
    for (int theme = 1; theme < gfx::theme_count(); theme++) {
        os::set_theme(theme);
        char name[48];
        snprintf(name, sizeof(name), "97-pincel-theme-%s", gfx::theme_at(theme).key);
        save(name);
    }
    os::set_theme(0);
    return failures != 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc > 1) {
        return write_pictures(argv[1]);
    }
    test_tips_and_lines();
    test_shapes();
    test_flood();
    test_whole_sheet();
    test_drawing();
    test_files();
    test_palette_and_memory();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("pincel: all checks passed\n");
    return 0;
}
