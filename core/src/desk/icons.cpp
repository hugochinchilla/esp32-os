// The icons on the desktop (core/desk/desk.hpp): a panel behind every
// window, over the whole work area. It paints the background around and
// under the icons, since each pixel is painted once.
#include <cstring>

#include "core/desk/desk.hpp"

namespace desk {

using gfx::Canvas;
using gfx::RectList;

namespace {

// Layout (tools/mockup/desktop.py).
constexpr int kIconX = 6, kIconY = 8, kIconStep = 70, kIconColumn = 76;
constexpr gfx::Rgb kHighlight = {0, 0, 128};

} // namespace

void Icons::attach(proc::Pid owner)
{
    attached_ = true;
    wm_.set_panel(wm::PanelPlace::Desktop, this, owner);
}

void Icons::detach()
{
    if (attached_ && wm_.panel(wm::PanelPlace::Desktop) == this) {
        wm_.set_panel(wm::PanelPlace::Desktop, nullptr);
    }
    attached_ = false;
    pressed_ = -1;
}

void Icons::set_icons(const Icon *icons, int count)
{
    icons_ = icons;
    count = count < kMax ? count : kMax;
    if (count != count_) {
        // Icons came or went, so the others moved: nothing stays selected.
        selected_ = -1;
        drop_icon_ = -1;
        pressed_ = -1;
    }
    count_ = count;
    wm_.invalidate(wm_.work_area());
}

Rect Icons::icon_rect(int index) const
{
    // Columns down the left, as many rows as fit above the taskbar.
    const int rows = (wm_.work_area().h - kIconY) / kIconStep;
    const int col = rows > 0 ? index / rows : 0, row = rows > 0 ? index % rows : index;
    return {kIconX - 4 + col * kIconColumn, kIconY + row * kIconStep, 72, 50};
}

int Icons::icon_at(int x, int y) const
{
    int hit = -1;
    for (int i = 0; i < count_; i++) {
        if (icon_rect(i).contains(x, y)) {
            hit = i;
        }
    }
    return hit;
}

void Icons::mouse(const wm::MouseMsg &m)
{
    switch (m.kind) {
    case wm::MouseKind::Down: {
        pressed_ = -1;
        const int hit = icon_at(m.x, m.y);
        if (hit != selected_) {
            if (selected_ >= 0) {
                wm_.invalidate(icon_rect(selected_));
            }
            selected_ = hit;
            if (hit >= 0) {
                wm_.invalidate(icon_rect(hit));
            }
        }
        // Twice on the same icon opens it; a third press starts again.
        const bool twice = m.double_click && hit == last_hit_;
        last_hit_ = twice ? -1 : hit;
        if (twice && hit >= 0 && icons_[hit].open != nullptr) {
            icons_[hit].open(icons_[hit].context);
        } else if (hit >= 0 && icons_[hit].path != nullptr) {
            pressed_ = hit; // may become a drag
            press_x_ = m.x;
            press_y_ = m.y;
        }
        break;
    }
    case wm::MouseKind::Move:
        if (pressed_ >= 0 && pressed_ < count_ &&
            (m.x - press_x_) * (m.x - press_x_) + (m.y - press_y_) * (m.y - press_y_) > 16) {
            const int icon = pressed_;
            pressed_ = -1;
            wm_.begin_drag(icons_[icon].path);
        }
        break;
    case wm::MouseKind::Up:
        pressed_ = -1;
        break;
    default:
        break;
    }
}

void Icons::right_click(int x, int y)
{
    // The desktop itself, away from the icons.
    if (menu_ != nullptr && icon_at(x, y) < 0) {
        menu_(x, y, menu_context_);
    }
}

void Icons::set_drop_icon(int icon)
{
    if (icon == drop_icon_) {
        return;
    }
    if (drop_icon_ >= 0 && drop_icon_ < count_) {
        wm_.invalidate(icon_rect(drop_icon_));
    }
    drop_icon_ = icon;
    if (icon >= 0) {
        wm_.invalidate(icon_rect(icon));
    }
}

// An icon that takes files (shown selected while the file is over it), or
// the desktop itself.
bool Icons::drag_over(const char *path, int x, int y)
{
    if (path == nullptr) {
        set_drop_icon(-1);
        return false;
    }
    int icon = -1;
    bool on_icon = false;
    for (int i = 0; i < count_; i++) {
        if (icon_rect(i).contains(x, y)) {
            on_icon = true;
            if (icons_[i].drop != nullptr && (icons_[i].path == nullptr || strcmp(icons_[i].path, path) != 0)) {
                icon = i;
            }
        }
    }
    set_drop_icon(icon);
    return icon >= 0 || (!on_icon && drop_ != nullptr);
}

void Icons::dropped(const char *path, int x, int y)
{
    // Worked out again: the server has already said the drag is over.
    int icon = -1;
    bool on_icon = false;
    for (int i = 0; i < count_; i++) {
        if (icon_rect(i).contains(x, y)) {
            on_icon = true;
            if (icons_[i].drop != nullptr && (icons_[i].path == nullptr || strcmp(icons_[i].path, path) != 0)) {
                icon = i;
            }
        }
    }
    if (icon >= 0) {
        icons_[icon].drop(path, icons_[icon].context);
    } else if (!on_icon && drop_ != nullptr) {
        drop_(path, drop_context_);
    }
}

void Icons::paint(const RectList &clip)
{
    const Rect work = wm_.work_area();
    const Rect screen = {0, 0, work.w, work.h + wm::Wm::kTaskbarHeight};
    static RectList around; // ponytail: one UI task
    around = clip;
    for (int i = 0; i < count_; i++) {
        around.subtract(icon_rect(i));
    }
    const bool split = !around.overflowed();
    Canvas plain = wm_.canvas(split ? around : clip, screen);
    wm_.fill_backdrop(plain, screen);
    Canvas c = wm_.canvas(clip, screen);
    const Rect bounds = clip.bounds();
    for (int i = 0; i < count_; i++) {
        const Rect r = icon_rect(i);
        if (!r.overlaps(bounds)) {
            continue;
        }
        const Icon &icon = icons_[i];
        const int x = r.x + 4, y = r.y;
        const bool selected = i == selected_ || i == drop_icon_;
        const auto draw = [&](Canvas &c) {
        if (split) {
            wm_.fill_backdrop(c, r);
        }
        if (icon.picture != nullptr) {
            if (selected) {
                c.blit_selected(*icon.picture, x + 16, y, gfx::rgb(0, 0, 2));
            } else {
                c.blit(*icon.picture, x + 16, y);
            }
        }
        const int tw = Canvas::text_width(icon.label);
        const int tx = x + 32 - tw / 2;
        if (selected) {
            c.fill_rgb({tx - 1, y + 33, tw + 2, 14}, kHighlight);
            c.text(tx, y + 34, icon.label, gfx::kWhite);
        } else {
            c.text(tx + 1, y + 35, icon.label, gfx::kBlack);
            c.text(tx, y + 34, icon.label, gfx::kWhite);
        }
        };
        if (split) {
            c.layered(r, draw);
        } else {
            draw(c); // the background is down already
        }
    }
}

} // namespace desk
