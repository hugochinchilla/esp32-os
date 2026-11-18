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
 * Behind the windows. An icon without a place of its own goes in columns
 * down the left of the work area, in the first cell of an unseen grid that
 * no other icon is on; one that has been dragged stays where it was let go
 * (or in the nearest free cell, while snapping is on). */

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
    // The corner of its place on the desktop; x < 0: none of its own yet.
    int x = -1, y = -1;
};

class Icons final : public wm::Panel {
public:
    static constexpr int kMax = 24;
    // What a dragged icon that stands for no file is called while it is
    // dragged: this and its label. No file's path starts so.
    static constexpr char kOwnDrag = '*';

    explicit Icons(wm::Wm &wm) : wm_(wm) {}
    ~Icons() { detach(); }
    Icons(const Icons &) = delete;
    Icons &operator=(const Icons &) = delete;

    void attach(proc::Pid owner = proc::kNone);
    void detach();

    // The icons: the array is the caller's and must stay valid.
    void set_icons(const Icon *icons, int count);
    // A right click on the desktop calls `menu` with the icon under the
    // pointer, which is selected first (-1: away from the icons), and the
    // pointer's place (the shell opens a menu there).
    void set_menu(void (*menu)(int icon, int x, int y, void *context), void *context)
    {
        menu_ = menu;
        menu_context_ = context;
    }
    // Files let go on the desktop that are not its own icons go to `drop`,
    // with the corner of the place their icon would get there.
    void set_drop(void (*drop)(const char *path, int x, int y, void *context), void *context)
    {
        drop_ = drop;
        drop_context_ = context;
    }
    // Called after icons were given new places (one dragged, or realign()):
    // icon_rect() has them, and whoever keeps them writes them down.
    void set_moved(void (*moved)(void *context), void *context)
    {
        moved_ = moved;
        moved_context_ = context;
    }
    // While on, an icon let go takes the nearest free cell of the grid. The
    // icons already there stay where they are.
    void set_snap(bool on) { snap_ = on; }
    bool snap() const { return snap_; }
    // Every icon into the grid, columns top to bottom from the left, in the
    // order they are in now on the screen.
    void realign();

    Rect icon_rect(int index) const;
    // Several can be picked (backlog item 55): Ctrl+click adds or takes
    // away one, and a rectangle pulled from the desktop itself picks what it
    // touches. Held and moved, the picked ones go together. selected_icon()
    // is the one picked last, or -1.
    int selected_icon() const { return selected_; }
    bool picked(int index) const { return index >= 0 && index < count_ && (picked_ >> index & 1) != 0; }
    int picked_count() const { return __builtin_popcount(picked_); }
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
    void layout();
    Rect cell(int index) const;
    int cells() const;
    bool free_cell(int index, int but) const;
    int own(const char *name) const;
    int target(const char *name, int x, int y) const;
    Rect place_at(int x, int y, int but) const;
    Rect place_for(Rect r, int but) const;
    void pick(uint32_t picked, int last);
    Rect band() const;
    void band_dirty();
    void pull_band(int x, int y);
    void drag_name(int icon, char *out, size_t capacity) const;

    wm::Wm &wm_;
    bool attached_ = false;
    const Icon *icons_ = nullptr;
    int count_ = 0;
    int selected_ = -1;   // the one picked last
    uint32_t picked_ = 0; // a bit for each icon that is picked
    int collapse_ = -1;   // held without Ctrl among several: it alone when let go
    bool band_ = false;   // a rectangle is being pulled
    int band_x0_ = 0, band_y0_ = 0, band_x1_ = 0, band_y1_ = 0;
    uint32_t band_base_ = 0; // what was picked when it started (Ctrl held)
    // While the files of one drag are let go here, one call each:
    bool batch_ = false;      // more of them are to come
    bool batch_onto_ = false; // the first went onto an icon that takes files
    bool batch_moved_ = false;
    int batch_dx_ = 0, batch_dy_ = 0; // how far the first of the desktop's own icons went
    int batch_files_ = 0;             // how many files from elsewhere got a place
    uint32_t moving_ = 0;             // its own icons still to be put down
    int pressed_ = -1; // the icon held, which may become a drag
    int press_x_ = 0, press_y_ = 0;
    int last_hit_ = -1; // what the last press hit, for a double click
    int drop_icon_ = -1;
    void (*menu_)(int icon, int x, int y, void *context) = nullptr;
    void *menu_context_ = nullptr;
    void (*drop_)(const char *path, int x, int y, void *context) = nullptr;
    void *drop_context_ = nullptr;
    void (*moved_)(void *context) = nullptr;
    void *moved_context_ = nullptr;
    bool snap_ = false;
    Rect rects_[kMax] = {};
};

} // namespace desk
