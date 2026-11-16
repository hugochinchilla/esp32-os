#include "core/demo/demo_screen.hpp"

namespace demo {

namespace {

constexpr uint8_t rgb(unsigned r, unsigned g, unsigned b) // r, g: 0..7, b: 0..3
{
    return static_cast<uint8_t>((r << 5) | (g << 2) | b);
}

constexpr uint8_t kBlack = rgb(0, 0, 0);
constexpr uint8_t kWhite = rgb(7, 7, 3);
constexpr uint8_t kLightGrey = rgb(5, 5, 2);
constexpr uint8_t kDarkGrey = rgb(2, 2, 1);
constexpr uint8_t kDesktop = rgb(0, 4, 2);
constexpr uint8_t kKeyDown = rgb(7, 6, 0);
constexpr uint8_t kLeft = rgb(7, 0, 0);
constexpr uint8_t kMiddle = rgb(0, 7, 0);
constexpr uint8_t kRight = rgb(0, 0, 3);

// Key grid: 16 columns by 8 rows, one cell per usage code 0..127.
constexpr int kGridX = 176, kGridY = 152, kCell = 18, kCellInner = 14;
// Mouse button boxes.
constexpr int kBoxX = 248, kBoxY = 88, kBoxW = 40, kBoxH = 32, kBoxGap = 12;

// Pointer shape: 'X' outline, '.' fill, ' ' see-through.
const char *const kPointer[19] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X......XXXXX",
    "X...X..X    ",
    "X..X X..X   ",
    "X.X  X..X   ",
    "XX    X..X  ",
    "X     X..X  ",
    "       X..X ",
    "       XXX  ",
};

} // namespace

DemoScreen::DemoScreen(ports::Display &display)
    : display_(display), pointer_x_(display.width() / 2), pointer_y_(display.height() / 2)
{
}

void DemoScreen::fill(int x, int y, int w, int h, uint8_t colour)
{
    const int sw = display_.width(), sh = display_.height();
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w, y1 = y + h > sh ? sh : y + h;
    uint8_t *pixels = display_.pixels();
    for (int row = y0; row < y1; row++) {
        uint8_t *line = pixels + row * sw;
        for (int col = x0; col < x1; col++) {
            line[col] = colour;
        }
    }
}

// A raised (or, with the colours swapped, sunken) edge one pixel wide.
void DemoScreen::frame(int x, int y, int w, int h, uint8_t light, uint8_t dark)
{
    fill(x, y, w, 1, light);
    fill(x, y, 1, h, light);
    fill(x, y + h - 1, w, 1, dark);
    fill(x + w - 1, y, 1, h, dark);
}

void DemoScreen::draw_key(unsigned usage)
{
    bool down = (keys_down_[usage / 8] >> (usage % 8)) & 1;
    int x = kGridX + static_cast<int>(usage % 16) * kCell;
    int y = kGridY + static_cast<int>(usage / 16) * kCell;
    fill(x, y, kCellInner, kCellInner, down ? kKeyDown : kLightGrey);
    if (down) {
        frame(x, y, kCellInner, kCellInner, kDarkGrey, kWhite);
    } else {
        frame(x, y, kCellInner, kCellInner, kWhite, kDarkGrey);
    }
}

void DemoScreen::draw_buttons()
{
    static const uint8_t colours[3] = {kLeft, kMiddle, kRight};
    static const uint8_t bits[3] = {1, 4, 2}; // left, middle, right
    for (int i = 0; i < 3; i++) {
        int x = kBoxX + i * (kBoxW + kBoxGap);
        bool down = buttons_ & bits[i];
        fill(x, kBoxY, kBoxW, kBoxH, down ? colours[i] : kLightGrey);
        if (down) {
            frame(x, kBoxY, kBoxW, kBoxH, kDarkGrey, kWhite);
        } else {
            frame(x, kBoxY, kBoxW, kBoxH, kWhite, kDarkGrey);
        }
    }
}

void DemoScreen::draw()
{
    pointer_shown_ = false;
    fill(0, 0, display_.width(), display_.height(), kDesktop);

    // A panel behind the boxes and the key grid.
    const int px = 152, py = 64, pw = 336, ph = 256;
    fill(px, py, pw, ph, kLightGrey);
    frame(px, py, pw, ph, kWhite, kBlack);
    frame(px + 1, py + 1, pw - 2, ph - 2, kLightGrey, kDarkGrey);

    // A bar along the bottom.
    fill(0, display_.height() - 28, display_.width(), 28, kLightGrey);
    fill(0, display_.height() - 28, display_.width(), 1, kWhite);

    draw_buttons();
    for (unsigned usage = 0; usage < 128; usage++) {
        draw_key(usage);
    }
    show_pointer();
}

void DemoScreen::hide_pointer()
{
    if (!pointer_shown_) {
        return;
    }
    const int sw = display_.width(), sh = display_.height();
    uint8_t *pixels = display_.pixels();
    for (int row = 0; row < kPointerH; row++) {
        for (int col = 0; col < kPointerW; col++) {
            int x = pointer_x_ + col, y = pointer_y_ + row;
            if (x < sw && y < sh) {
                pixels[y * sw + x] = under_[row * kPointerW + col];
            }
        }
    }
    pointer_shown_ = false;
}

void DemoScreen::show_pointer()
{
    const int sw = display_.width(), sh = display_.height();
    uint8_t *pixels = display_.pixels();
    for (int row = 0; row < kPointerH; row++) {
        for (int col = 0; col < kPointerW; col++) {
            int x = pointer_x_ + col, y = pointer_y_ + row;
            if (x >= sw || y >= sh) {
                continue;
            }
            uint8_t *pixel = &pixels[y * sw + x];
            under_[row * kPointerW + col] = *pixel;
            char shape = kPointer[row][col];
            if (shape == 'X') {
                *pixel = kBlack;
            } else if (shape == '.') {
                *pixel = kWhite;
            }
        }
    }
    pointer_shown_ = true;
}

void DemoScreen::key(uint16_t usage, bool pressed)
{
    if (usage >= 128) {
        return;
    }
    uint8_t bit = static_cast<uint8_t>(1u << (usage % 8));
    if (pressed) {
        keys_down_[usage / 8] |= bit;
    } else {
        keys_down_[usage / 8] &= static_cast<uint8_t>(~bit);
    }
    hide_pointer();
    draw_key(usage);
    show_pointer();
}

void DemoScreen::mouse(const ports::MouseEvent &event)
{
    int x = event.absolute ? event.x : pointer_x_ + event.x;
    int y = event.absolute ? event.y : pointer_y_ + event.y;
    const int max_x = display_.width() - 1, max_y = display_.height() - 1;
    x = x < 0 ? 0 : (x > max_x ? max_x : x);
    y = y < 0 ? 0 : (y > max_y ? max_y : y);

    hide_pointer();
    pointer_x_ = x;
    pointer_y_ = y;
    if (event.buttons != buttons_) {
        buttons_ = event.buttons;
        draw_buttons();
    }
    show_pointer();
}

} // namespace demo
