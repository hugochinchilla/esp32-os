// A stand-in for the desktop while the debug protocol is brought up: a mouse
// pointer, three boxes for the mouse buttons and a grid that lights up for
// each key held down. It will be replaced by the window manager.
#pragma once

#include <cstdint>

#include "core/ports.hpp"

namespace demo {

class DemoScreen : public ports::InputSink {
public:
    explicit DemoScreen(ports::Display &display);

    // Draws the whole screen. Call once after the display is running.
    void draw();

    void key(uint16_t usage, bool pressed) override;
    void mouse(const ports::MouseEvent &event) override;

    int pointer_x() const { return pointer_x_; }
    int pointer_y() const { return pointer_y_; }

private:
    static constexpr int kPointerW = 12;
    static constexpr int kPointerH = 19;

    void fill(int x, int y, int w, int h, uint8_t colour);
    void frame(int x, int y, int w, int h, uint8_t light, uint8_t dark);
    void draw_key(unsigned usage);
    void draw_buttons();
    void hide_pointer();
    void show_pointer();

    ports::Display &display_;
    int pointer_x_;
    int pointer_y_;
    uint8_t buttons_ = 0;
    bool pointer_shown_ = false;
    uint8_t keys_down_[16] = {}; // one bit per usage code 0..127
    uint8_t under_[kPointerW * kPointerH];
};

} // namespace demo
