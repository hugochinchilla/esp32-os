// The taskbar (core/desk/desk.hpp): a panel in the strip under the work
// area. It reads the window list from the server and asks it to minimize,
// restore and open the Start menu, as any program could.
#include <cstdio>

#include "core/desk/desk.hpp"
#include "core/gfx/art.hpp"

namespace desk {

using gfx::ButtonLook;
using gfx::Canvas;

namespace {

// Layout (tools/mockup/desktop.py).
constexpr int kStartW = 58;
constexpr int kButtonsX = 72; // first window button, after a groove (further right with quick-launch icons)
constexpr int kQuickW = 22;   // a quick-launch icon: 16x16 in a 22x22 button with no edge
// What index_at() and the held button call the quick-launch icons, so they
// are held and let go like the other buttons of the taskbar: -10, -11...
constexpr int kQuickIndex = -10;
constexpr int kNothing = -2;
constexpr int kButtonW = 150, kButtonGap = 3;
constexpr int kClockW = 60;
constexpr int kTrayW = 20; // a tray icon, left of the clock

} // namespace

void Taskbar::attach(proc::Pid owner)
{
    attached_ = true;
    wm_.set_panel(wm::PanelPlace::Bar, this, owner);
}

void Taskbar::detach()
{
    if (attached_ && wm_.panel(wm::PanelPlace::Bar) == this) {
        wm_.set_panel(wm::PanelPlace::Bar, nullptr);
    }
    attached_ = false;
    held_ = false;
}

int Taskbar::buttons_x() const { return kButtonsX + (quick_count_ > 0 ? quick_count_ * kQuickW + 6 : 0); }

Rect Taskbar::quick_launch_rect(int index) const
{
    if (index < 0 || index >= quick_count_) {
        return {};
    }
    return {kButtonsX + index * kQuickW, wm_.taskbar().y + 4, kQuickW, 22};
}

void Taskbar::set_quick_launch(const QuickLaunch *items, int count)
{
    quick_count_ = 0;
    for (int i = 0; i < count && quick_count_ < kMaxQuick; i++) {
        if (items[i].icon != nullptr && items[i].click != nullptr) {
            quick_[quick_count_++] = items[i];
        }
    }
    wm_.invalidate(wm_.taskbar());
}

Rect Taskbar::button(int index) const
{
    const Rect bar = wm_.taskbar();
    const int ty = bar.y;
    if (index <= kQuickIndex) {
        return quick_launch_rect(kQuickIndex - index);
    }
    if (index < 0) {
        return {2, ty + 4, kStartW, 22};
    }
    const int room = bar.w - kClockW - 6 - buttons_x() - (tray_icon_ != nullptr ? kTrayW : 0);
    const int shown = wm_.taskbar_count();
    int w = kButtonW;
    if (shown > 0 && shown * (kButtonW + kButtonGap) > room) {
        w = room / shown - kButtonGap;
    }
    return {buttons_x() + index * (w + kButtonGap), ty + 4, w, 22};
}

int Taskbar::index_at(int x, int y) const
{
    if (button(-1).contains(x, y)) {
        return -1;
    }
    for (int i = 0; i < quick_count_; i++) {
        if (quick_launch_rect(i).contains(x, y)) {
            return kQuickIndex - i;
        }
    }
    for (int i = 0; i < wm_.taskbar_count(); i++) {
        if (button(i).contains(x, y)) {
            return i;
        }
    }
    return kNothing;
}

Rect Taskbar::clock_rect() const
{
    const Rect bar = wm_.taskbar();
    return {bar.w - kClockW - 2, bar.y + 4, kClockW, 22};
}

Rect Taskbar::tray_icon_rect() const
{
    const Rect clock = clock_rect();
    return tray_icon_ != nullptr ? Rect{clock.x - kTrayW, clock.y, kTrayW, clock.h} : Rect{};
}

void Taskbar::set_tray_icon(const gfx::Bitmap *icon, void (*click)(void *context), void *context)
{
    tray_click_ = click;
    tray_context_ = context;
    if (icon == tray_icon_) {
        return;
    }
    // A first or a last icon moves the window buttons: the whole bar.
    const bool whole = (icon == nullptr) != (tray_icon_ == nullptr);
    tray_icon_ = icon;
    wm_.invalidate(whole ? wm_.taskbar() : tray_icon_rect());
}

void Taskbar::news(wm::PanelNews what)
{
    switch (what) {
    case wm::PanelNews::Windows:
        wm_.invalidate(wm_.taskbar());
        break;
    case wm::PanelNews::Menu:
        wm_.invalidate(button(-1)); // pressed in while its menu is open
        break;
    case wm::PanelNews::Clock:
        wm_.invalidate(clock_rect());
        break;
    }
}

void Taskbar::mouse(const wm::MouseMsg &m)
{
    switch (m.kind) {
    case wm::MouseKind::Down: {
        held_ = false;
        if (tray_icon_ != nullptr && tray_icon_rect().contains(m.x, m.y)) {
            if (tray_click_ != nullptr) {
                tray_click_(tray_context_);
            }
            return;
        }
        const int index = index_at(m.x, m.y);
        if (index == -1) {
            wm_.open_start_menu(); // the held button goes on into the menu
        } else if (index != kNothing) {
            held_ = true;
            held_index_ = index;
            held_inside_ = true;
            wm_.invalidate(button(index));
        }
        break;
    }
    case wm::MouseKind::Move:
        if (held_) {
            const bool inside = button(held_index_).contains(m.x, m.y);
            if (inside != held_inside_) {
                held_inside_ = inside;
                wm_.invalidate(button(held_index_));
            }
        }
        break;
    case wm::MouseKind::Up:
        if (held_) {
            held_ = false;
            wm_.invalidate(button(held_index_));
            if (held_inside_) {
                click(held_index_);
            }
        }
        break;
    default:
        break;
    }
}

void Taskbar::click(int index)
{
    if (index <= kQuickIndex) {
        // A quick-launch icon: the shell starts something, as itself.
        const int which = kQuickIndex - index;
        if (which < quick_count_) {
            const QuickLaunch item = quick_[which];
            item.click(item.context);
        }
        return;
    }
    wm::Window *tw = index >= 0 ? wm_.taskbar_window(index) : nullptr;
    if (tw == nullptr) {
        return;
    }
    wm::Window &w = *tw;
    if (wm_.active() == &w && w.state() != wm::WindowState::Minimized) {
        wm_.minimize(w);
    } else {
        wm_.restore(w);
    }
}

void Taskbar::paint(const gfx::RectList &clip)
{
    const Rect bar = wm_.taskbar();
    const int ty = bar.y;
    Canvas screen = wm_.canvas(clip, bar);
    screen.layered(bar, [&](Canvas &c) {
    c.fill(bar, gfx::kGrey);
    c.hline(0, ty + 1, bar.w, gfx::kWhite);

    // Start button: the mascot and "Start", pressed in while its menu is open.
    const Rect start = button(-1);
    const bool start_in = wm_.start_menu_open() || (held_ && held_index_ == -1 && held_inside_);
    c.button(start, start_in ? ButtonLook::Pressed : ButtonLook::Raised);
    const int sd = start_in ? 1 : 0;
    c.blit(gfx::mascot_16(), 5 + sd, ty + 7 + sd);
    c.text(25 + sd, ty + 9 + sd, "Start", gfx::kBlack, true);

    // A groove, then the quick-launch icons if there are any (flat, pressed
    // in while held), another groove, then the window buttons.
    c.vline(kButtonsX - 4, ty + 5, 20, gfx::kDarkGrey);
    c.vline(kButtonsX - 3, ty + 5, 20, gfx::kWhite);
    for (int i = 0; i < quick_count_; i++) {
        const Rect r = quick_launch_rect(i);
        const bool held = held_ && held_index_ == kQuickIndex - i && held_inside_;
        if (held) {
            c.edge(r, gfx::kDarkGrey, gfx::kWhite);
        }
        c.blit(*quick_[i].icon, r.x + 3 + (held ? 1 : 0), r.y + 3 + (held ? 1 : 0));
    }
    if (quick_count_ > 0) {
        c.vline(buttons_x() - 4, ty + 5, 20, gfx::kDarkGrey);
        c.vline(buttons_x() - 3, ty + 5, 20, gfx::kWhite);
    }

    // One button per window, in the order they were opened. A window whose
    // dialog is open shows pressed while the dialog has the focus.
    const wm::Window *active = wm_.active();
    for (int i = 0; i < wm_.taskbar_count(); i++) {
        wm::Window &w = *wm_.taskbar_window(i);
        const Rect r = button(i);
        const bool held = held_ && held_index_ == i && held_inside_;
        const bool owns_active = active != nullptr && active->modal_owner() == &w;
        const bool on = (active == &w || owns_active) && w.state() != wm::WindowState::Minimized;
        c.button(r, on ? ButtonLook::Checked : (held ? ButtonLook::Pressed : ButtonLook::Raised));
        const int d = (on || held) ? 1 : 0;
        Canvas inner = c.sub(r.inset(3));
        if (w.icon() != nullptr) {
            inner.blit(*w.icon(), 1 + d, 0 + d);
        }
        inner.text(21 + d, 2 + d, w.title(), gfx::kBlack, on);
    }

    // The clock, 12-hour as in the target picture.
    const Rect clock = clock_rect();
    if (tray_icon_ != nullptr) {
        const Rect icon = tray_icon_rect();
        c.edge({icon.x, clock.y, kTrayW + kClockW, clock.h}, gfx::kDarkGrey, gfx::kWhite);
        c.blit(*tray_icon_, icon.x + 3, icon.y + 3);
    } else {
        c.edge(clock, gfx::kDarkGrey, gfx::kWhite);
    }
    int h, m, s;
    wm_.get_time(h, m, s);
    char time[12];
    snprintf(time, sizeof(time), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
    c.text(clock.x + (kClockW - Canvas::text_width(time)) / 2, ty + 9, time, gfx::kBlack);
    });
}

} // namespace desk
