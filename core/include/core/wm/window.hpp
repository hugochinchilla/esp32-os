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
// left button is routed this way; the right one calls Window::right_click().
struct MouseMsg {
    MouseKind kind;
    int x;
    int y;
    bool double_click = false; // a Down that completes a double click
};

// Modifier keys held, as bits.
enum Modifier : uint8_t { kShift = 1, kCtrl = 2, kAlt = 4, kGui = 8 };

struct KeyMsg {
    uint16_t usage; // USB HID keyboard usage code
    bool pressed;
    uint8_t mods;   // Modifier bits
};

// USB HID usage codes used by the interface.
namespace key {
constexpr uint16_t A = 0x04, Z = 0x1D, Digit1 = 0x1E, Digit0 = 0x27;
constexpr uint16_t Enter = 0x28, Escape = 0x29, Backspace = 0x2A, Tab = 0x2B, Space = 0x2C;
constexpr uint16_t F4 = 0x3D, Home = 0x4A, PageUp = 0x4B, Delete = 0x4C, End = 0x4D;
constexpr uint16_t PageDown = 0x4E, Right = 0x4F, Left = 0x50, Down = 0x51, Up = 0x52;
constexpr uint16_t LeftCtrl = 0xE0, LeftShift = 0xE1, LeftAlt = 0xE2, LeftGui = 0xE3;
constexpr uint16_t RightCtrl = 0xE4, RightShift = 0xE5, RightAlt = 0xE6, RightGui = 0xE7;
// The letter for A..Z and digits, 0 otherwise.
char letter(uint16_t usage);
// The character a US keyboard types for this key, 0 if none.
// ponytail: US layout only; other layouts come with real keyboards.
char us_char(uint16_t usage, bool shift);
} // namespace key

/* ---- Menus ------------------------------------------------------------------ */

enum MenuFlag : uint8_t {
    kMenuDisabled = 1,
    kMenuChecked = 2,
    kMenuSeparator = 4,
};

struct Menu;

// A menu entry. In the label, '&' marks the access letter ("&File").
struct MenuItem {
    const char *label;
    void (*action)(void *context);
    void *context;
    Menu *submenu;
    const gfx::Bitmap *icon; // 16x16, shown in the Start menu
    uint8_t flags;           // MenuFlag bits; may change at any time
};

struct Menu {
    MenuItem *items;
    int count;
};

struct MenuBarEntry {
    const char *label; // with '&' for the access letter
    Menu *menu;
};

struct MenuBar {
    const MenuBarEntry *entries;
    int count;
};

// The label without '&', and the index of the access letter in it (-1: none).
int strip_access(const char *label, char *out, int capacity);
// The access letter of a label, lower case, or 0.
char access_letter(const char *label);

/* ---- Widgets ---------------------------------------------------------------- */

class Widget {
public:
    // Position in the window's client area.
    Rect rect;

    explicit Widget(const Rect &r) : rect(r) {}

    // Draws the widget with (0, 0) at its top left; drawing is clipped.
    virtual void paint(gfx::Canvas &c) = 0;
    virtual bool wants_mouse() const { return false; }
    virtual void mouse(const MouseMsg &) {}
    // Keyboard: widgets that can take the focus get keys and text.
    virtual bool focusable() const { return false; }
    // Returns true if the key was used.
    virtual bool key(const KeyMsg &) { return false; }
    virtual void text(const char * /*utf8*/) {}
    // Acts as if clicked (default button, keyboard).
    virtual void press() {}
    // The underlined letter of its caption (Alt+letter), lower case, or 0.
    virtual char access() const { return 0; }

    bool has_focus() const;
    // Whether the widget covers all of its rect when it paints (the window
    // does not paint its background there, so nothing is drawn twice).
    virtual bool opaque() const { return true; }
    // An opaque widget is painted through a scratch band (Canvas::layered),
    // so text and icons over its background reach the screen in one write
    // per pixel. Big widgets that already paint each pixel once say false
    // (the band runs their paint once per 12 lines).
    virtual bool layered() const { return true; }
    // Where the caret is, in widget coordinates: all a blink repaints.
    virtual Rect caret_rect() const { return {0, 0, rect.w, rect.h}; }
    // Asks for a part of the widget (widget coordinates) to be painted again.
    void invalidate_part(const Rect &r);
    // The window manager, through the window (nullptr before it opens).
    Wm *manager() const;
    // Whether a caret should be drawn now (it blinks).
    bool caret_shown() const;
    // Asks for the widget to be painted again.
    void invalidate();
    Window *window() const { return window_; }

protected:
    ~Widget() = default;

private:
    friend class Window;
    Window *window_ = nullptr;
};

/* ---- Windows ---------------------------------------------------------------- */

enum class WindowState : uint8_t { Normal, Minimized, Maximized };

class Window {
public:
    // Frame metrics, as in the 98 look.
    static constexpr int kBorder = 4;
    static constexpr int kTitleHeight = 18;
    static constexpr int kClientTop = 24;     // from the frame's top edge
    static constexpr int kMenuBarHeight = 18; // below the title, when there is one
    static constexpr int kMaxWidgets = 24;
    static constexpr int kMinWidth = 128;

    Window(const char *title, const Rect &frame, const gfx::Bitmap *icon)
        : title_(title), icon_(icon), frame_(frame)
    {
    }

    int id() const { return id_; }
    const char *title() const { return title_; }
    const gfx::Bitmap *icon() const { return icon_; }
    const Rect &frame() const { return frame_; } // screen coordinates
    Rect client() const;                         // screen coordinates
    Rect menu_bar_rect() const;                  // screen; empty without a menu bar
    WindowState state() const { return state_; }
    bool open() const { return wm_ != nullptr; }
    bool focused() const;
    Wm *manager() const { return wm_; }

    // Settings, usually made once in the constructor.
    void set_menu_bar(const MenuBar *bar) { menu_bar_ = bar; }
    const MenuBar *menu_bar() const { return menu_bar_; }
    void set_resizable(bool on) { resizable_ = on; }
    bool resizable() const { return resizable_; }
    // A dialog: fixed size, only the close button in its title bar.
    void set_dialog(bool on)
    {
        dialog_ = on;
        resizable_ = !on;
    }
    bool dialog() const { return dialog_; }
    // Modal: while open, `owner` cannot be used (with no owner, nothing else
    // can). Set before opening.
    void set_modal(Window *owner, bool whole_desktop = false)
    {
        modal_ = true;
        modal_owner_ = owner;
        system_modal_ = whole_desktop;
    }
    void set_modeless()
    {
        modal_ = false;
        modal_owner_ = nullptr;
        system_modal_ = false;
    }
    bool modal() const { return modal_; }
    Window *modal_owner() const { return modal_owner_; }
    int min_height() const { return kClientTop + (menu_bar_ ? kMenuBarHeight : 0) + 24; }
    void set_frame(const Rect &r); // before the window is opened, or to move it
    void set_title(const char *title); // the text must stay valid

    void add(Widget &w);
    // Keyboard focus inside the window.
    void set_focus(Widget *w);
    Widget *focus() const { return focus_; }
    // Pressed by Enter when the focused widget does not use it.
    void set_default(Widget *w) { default_ = w; }
    Widget *default_widget() const { return default_; }

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
    // The right button went down in the client area (client coordinates).
    // Not captured: there is no matching release.
    virtual void right_click(int /*x*/, int /*y*/) {}
    // Keys for the window. The default gives them to the focused widget,
    // moves the focus with Tab and presses the default widget with Enter.
    // Returns true if the key was used.
    virtual bool key(const KeyMsg &k);
    virtual void text(const char *utf8);
    // Called after the size changed (resizing, maximising).
    virtual void resized() {}
    // The close button, Alt+F4: return false to stay open (and ask first).
    virtual bool close_requested() { return true; }
    // A menu of the menu bar is about to open: a chance to grey items out.
    virtual void menu_opening(int /*entry*/) {}
    // Called after the window was closed.
    virtual void closed() {}
    // The whole client area painted through a scratch band (see
    // Canvas::layered), for windows that draw over their widgets' background:
    // widgets that are not opaque (group frames), or an override of paint()
    // that adds pictures (say true). Big windows should not.
    virtual bool layered() const;
    // A file dragged over the client area (client coordinates): return true
    // if it would be taken there (and show where). path is nullptr when the
    // drag has left the window or ended.
    virtual bool drag_over(const char * /*path*/, int /*x*/, int /*y*/) { return false; }
    // The file let go over the client area, after drag_over() said yes.
    virtual void dropped(const char * /*path*/, int /*x*/, int /*y*/) {}

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
    bool resizable_ = true;
    bool dialog_ = false;
    bool modal_ = false;
    bool system_modal_ = false;
    Window *modal_owner_ = nullptr;
    const MenuBar *menu_bar_ = nullptr;
    Wm *wm_ = nullptr;
    int id_ = 0;
    Widget *widgets_[kMaxWidgets] = {};
    int widget_count_ = 0;
    Widget *captured_ = nullptr;
    Widget *focus_ = nullptr;
    Widget *default_ = nullptr;
};

} // namespace wm
