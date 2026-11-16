// The window manager: overlapping windows in a stacking order, the desktop
// behind them and the taskbar in front. It works out what is visible and
// asks each window to paint only that, routes the mouse and the keyboard,
// and draws the frames and the taskbar in the 98 look.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/ports.hpp"
#include "core/wm/window.hpp"

namespace wm {

// Parts of a window frame, for hit testing.
enum class Part : uint8_t { None, Border, Title, Client, Minimize, Maximize, Close };

class Wm : public ports::InputSink, public ports::Desktop {
public:
    static constexpr int kMaxWindows = 16;
    static constexpr int kTaskbarHeight = 28;
    static constexpr int kOutline = 3; // width of the outline while dragging

    Wm(ports::Display &display, ports::Pointer &pointer, ports::Clock &clock);

    // Sets the pointer up and paints the whole screen.
    void start();

    // Window list. add() puts the window on top and gives it the focus.
    void add(Window &w);
    void close(Window &w);
    void activate(Window &w);
    void minimize(Window &w);
    void restore(Window &w);
    void toggle_maximize(Window &w);
    void move(Window &w, int x, int y);

    // Marks part of the screen for painting; update() paints it.
    void invalidate(const Rect &r);
    void update();

    // Input (ports::InputSink). Each event is handled and painted at once.
    void key(uint16_t usage, bool pressed) override;
    void mouse(const ports::MouseEvent &event) override;

    // Inspection (ports::Desktop), used by the debug protocol and tests.
    size_t describe_windows(char *out, size_t capacity) override;
    uint32_t repaint_all() override;

    int window_count() const { return count_; }
    Window *window_at(int z) const { return z < count_ ? stack_[z] : nullptr; } // 0 = bottom
    Window *active() const { return active_; }
    Window *window_under(int x, int y) const;
    Part part_at(const Window &w, int x, int y) const;
    Rect title_button(const Window &w, Part which) const;
    Rect work_area() const { return {0, 0, width_, height_ - kTaskbarHeight}; }
    Rect taskbar() const { return {0, height_ - kTaskbarHeight, width_, kTaskbarHeight}; }
    // Taskbar buttons: -1 the Start button, 0.. the window buttons.
    Rect taskbar_button(int index) const;
    int pointer_x() const { return pointer_x_; }
    int pointer_y() const { return pointer_y_; }
    bool dragging() const { return capture_ == Capture::Drag; }
    uint32_t last_update_us() const { return last_update_us_; }

private:
    enum class Capture : uint8_t { None, Drag, TitleButton, Client, Taskbar };

    int stack_index(const Window &w) const;
    Window *topmost_visible() const;
    void raise(Window &w);
    void on_press();
    void on_release();
    void on_move();
    void taskbar_click(int index);
    int taskbar_index_at(int x, int y) const;
    void set_outline(const Rect &r, bool shown);
    void xor_outline();
    void paint(bool painter);
    void paint_window(Window &w, const gfx::RectList &clip);
    void paint_frame(gfx::Canvas &c, Window &w);
    void paint_taskbar(const gfx::RectList &clip);
    bool visible(const Window &w) const { return w.state_ != WindowState::Minimized; }

    ports::Display &display_;
    ports::Pointer &pointer_;
    ports::Clock &clock_;
    int width_, height_;

    Window *stack_[kMaxWindows] = {}; // stacking order, 0 at the bottom
    Window *order_[kMaxWindows] = {}; // order of opening, for the taskbar
    int count_ = 0;
    int next_id_ = 1;
    Window *active_ = nullptr;

    gfx::RectList dirty_;
    gfx::RectList region_;

    int pointer_x_, pointer_y_;
    uint8_t buttons_ = 0;

    Capture capture_ = Capture::None;
    Window *captured_ = nullptr;
    Part captured_part_ = Part::None;
    int captured_index_ = 0;   // taskbar button
    bool captured_inside_ = false;
    int grab_dx_ = 0, grab_dy_ = 0;
    Rect outline_;
    bool outline_shown_ = false;

    uint32_t last_update_us_ = 0;
};

} // namespace wm
