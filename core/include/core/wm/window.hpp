// Windows and the widgets inside them. A window keeps no picture of itself:
// when part of it needs to be seen again, the window manager asks it to
// paint that part (paint() with a clipped canvas).
#pragma once

#include <cstdint>

#include "core/gfx/canvas.hpp"

namespace wm {

using gfx::Rect;

class Wm;
class Window;

enum class MouseKind : uint8_t { Down, Move, Up };

// A mouse message for a window or widget, in its own coordinates. Only the
// left button is routed for now.
struct MouseMsg {
    MouseKind kind;
    int x;
    int y;
};

class Widget {
public:
    // Position in the window's client area.
    Rect rect;

    explicit Widget(const Rect &r) : rect(r) {}

    // Draws the widget with (0, 0) at its top left; drawing is clipped.
    virtual void paint(gfx::Canvas &c) = 0;
    virtual bool wants_mouse() const { return false; }
    virtual void mouse(const MouseMsg &) {}

    // Asks for the widget to be painted again.
    void invalidate();
    Window *window() const { return window_; }

protected:
    ~Widget() = default;

private:
    friend class Window;
    Window *window_ = nullptr;
};

enum class WindowState : uint8_t { Normal, Minimized, Maximized };

class Window {
public:
    // Frame metrics, as in the 98 look.
    static constexpr int kBorder = 4;
    static constexpr int kTitleHeight = 18;
    static constexpr int kClientTop = 24; // from the frame's top edge
    static constexpr int kMaxWidgets = 16;

    Window(const char *title, const Rect &frame, const gfx::Bitmap *icon)
        : title_(title), icon_(icon), frame_(frame)
    {
    }

    int id() const { return id_; }
    const char *title() const { return title_; }
    const gfx::Bitmap *icon() const { return icon_; }
    const Rect &frame() const { return frame_; }   // screen coordinates
    Rect client() const;                           // screen coordinates
    WindowState state() const { return state_; }
    bool open() const { return wm_ != nullptr; }
    bool focused() const;
    Wm *manager() const { return wm_; }

    void add(Widget &w);

    // Asks for the whole window, or a part of its client area, to be
    // painted again.
    void invalidate();
    void invalidate_client(const Rect &r);
    // Closes the window (the manager forgets it).
    void close();

    // Paints the client area, (0, 0) at its top left. The default fills it
    // with the face colour and paints the widgets.
    virtual void paint(gfx::Canvas &c);
    // Mouse messages for the client area. The default routes them to the
    // widget under the pointer, which keeps them until the button is let go.
    virtual void mouse(const MouseMsg &m);
    virtual void key(uint16_t usage, bool pressed);
    // Called after the window was closed.
    virtual void closed() {}

protected:
    ~Window() = default;

private:
    friend class Wm;

    const char *title_;
    const gfx::Bitmap *icon_;
    Rect frame_;
    Rect restore_;               // frame before maximising
    WindowState state_ = WindowState::Normal;
    bool was_maximized_ = false; // state to return to from minimised
    Wm *wm_ = nullptr;
    int id_ = 0;
    Widget *widgets_[kMaxWidgets] = {};
    int widget_count_ = 0;
    Widget *captured_ = nullptr;
};

} // namespace wm
