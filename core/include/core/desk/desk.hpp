// The desktop shell's two surfaces: the taskbar and the icons on the
// desktop. They are panels of the window server (core/wm/wm.hpp): the server
// gives each its place, asks it to paint and hands it the mouse; everything
// about what a taskbar or a desktop icon is lives here (backlog item 21).
//
// Both are plain objects that need only a window server, so the tests can
// make them without the rest of the desktop. On a machine they belong to the
// `taskbar` process (core/src/shell/desktop.cpp), which makes them when it
// starts and destroys them when it ends.
#pragma once

#include <cstdint>

#include "core/gfx/canvas.hpp"
#include "core/gfx/rect.hpp"
#include "core/proc/table.hpp"
#include "core/wm/wm.hpp"

namespace desk {

using gfx::Rect;

/* ---- The taskbar ------------------------------------------------------------
 *
 * The Start button, the quick-launch icons, a button for every window, a
 * tray icon and the clock, in the strip the server keeps under the work
 * area. */
class Taskbar final : public wm::Panel {
public:
    explicit Taskbar(wm::Wm &wm) : wm_(wm) {}
    ~Taskbar() { detach(); }
    Taskbar(const Taskbar &) = delete;
    Taskbar &operator=(const Taskbar &) = delete;

    // Takes the bar's place on the screen, as `owner`'s; and gives it up.
    void attach(proc::Pid owner = proc::kNone);
    void detach();

    // Quick launch: small icons between the Start button and the window
    // buttons, each of which does one thing when clicked. The array is
    // copied; at most kMaxQuick, none by default.
    struct QuickLaunch {
        const gfx::Bitmap *icon; // 16x16
        void (*click)(void *context);
        void *context;
    };
    static constexpr int kMaxQuick = 4;
    void set_quick_launch(const QuickLaunch *items, int count);
    int quick_launch_count() const { return quick_count_; }
    Rect quick_launch_rect(int index) const;

    // A 16x16 icon in the tray, left of the clock (nullptr: none). A click
    // on it calls `click`.
    void set_tray_icon(const gfx::Bitmap *icon, void (*click)(void *context) = nullptr, void *context = nullptr);
    Rect tray_icon_rect() const; // empty without an icon
    Rect clock_rect() const;

    // Buttons: -1 the Start button, 0.. the window buttons.
    Rect button(int index) const;

    void paint(const gfx::RectList &clip) override;
    void mouse(const wm::MouseMsg &m) override;
    void news(wm::PanelNews what) override;

private:
    int buttons_x() const; // where the window buttons start
    int index_at(int x, int y) const;
    void click(int index);

    wm::Wm &wm_;
    bool attached_ = false;
    QuickLaunch quick_[kMaxQuick] = {};
    int quick_count_ = 0;
    const gfx::Bitmap *tray_icon_ = nullptr;
    void (*tray_click_)(void *context) = nullptr;
    void *tray_context_ = nullptr;
    bool held_ = false; // a button is held down
    int held_index_ = 0;
    bool held_inside_ = false;
};

/* ---- The icons on the desktop -----------------------------------------------
 *
 * In columns down the left of the work area, behind the windows. */

// An icon on the desktop: a 32x32 picture with a label under it.
struct Icon {
    const char *label;
    const gfx::Bitmap *picture;
    void (*open)(void *context);
    void *context;
    // The file the icon stands for: such icons can be dragged.
    const char *path = nullptr;
    // Takes a dragged file let go on the icon; nullptr: not a drop target.
    void (*drop)(const char *path, void *context) = nullptr;
};

class Icons final : public wm::Panel {
public:
    static constexpr int kMax = 24; // in columns, top to bottom, then left to right

    explicit Icons(wm::Wm &wm) : wm_(wm) {}
    ~Icons() { detach(); }
    Icons(const Icons &) = delete;
    Icons &operator=(const Icons &) = delete;

    void attach(proc::Pid owner = proc::kNone);
    void detach();

    // The icons: the array is the caller's and must stay valid.
    void set_icons(const Icon *icons, int count);
    // A right click on the desktop away from the icons calls `menu` with
    // the pointer's place (the shell opens a menu there).
    void set_menu(void (*menu)(int x, int y, void *context), void *context)
    {
        menu_ = menu;
        menu_context_ = context;
    }
    // Files let go on the desktop away from the icons go to `drop`.
    void set_drop(void (*drop)(const char *path, void *context), void *context)
    {
        drop_ = drop;
        drop_context_ = context;
    }

    Rect icon_rect(int index) const;
    int selected_icon() const { return selected_; }
    int drop_icon() const { return drop_icon_; }

    void paint(const gfx::RectList &clip) override;
    void mouse(const wm::MouseMsg &m) override;
    void right_click(int x, int y) override;
    bool drag_over(const char *path, int x, int y) override;
    void dropped(const char *path, int x, int y) override;
    int selected() const override { return selected_; }

private:
    int icon_at(int x, int y) const;
    void set_drop_icon(int icon);

    wm::Wm &wm_;
    bool attached_ = false;
    const Icon *icons_ = nullptr;
    int count_ = 0;
    int selected_ = -1;
    int pressed_ = -1; // the icon held, which may become a drag
    int press_x_ = 0, press_y_ = 0;
    int last_hit_ = -1; // what the last press hit, for a double click
    int drop_icon_ = -1;
    void (*menu_)(int x, int y, void *context) = nullptr;
    void *menu_context_ = nullptr;
    void (*drop_)(const char *path, void *context) = nullptr;
    void *drop_context_ = nullptr;
};

} // namespace desk
