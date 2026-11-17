// The icons on the desktop (core/desk/desk.hpp): a panel behind every
// window, over the whole work area. It paints the background around and
// under the icons, since each pixel is painted once.
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/desk/desk.hpp"

namespace desk {

using gfx::Canvas;
using gfx::RectList;

namespace {

// Layout (tools/mockup/desktop.py).
constexpr int kIconX = 6, kIconY = 8, kIconStep = 70, kIconColumn = 76;
constexpr int kIconW = 72, kIconH = 50;
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
    layout();
    wm_.invalidate(wm_.work_area());
}

// The grid: columns from the left, as many rows as fit above the taskbar.
int Icons::cells() const
{
    const Rect work = wm_.work_area();
    const int rows = (work.h - kIconY) / kIconStep;
    const int columns = (work.w - (kIconX - 4) + kIconColumn - kIconW) / kIconColumn;
    return rows > 0 && columns > 0 ? rows * columns : 0;
}

Rect Icons::cell(int index) const
{
    const int rows = (wm_.work_area().h - kIconY) / kIconStep;
    const int col = rows > 0 ? index / rows : 0, row = rows > 0 ? index % rows : index;
    return {kIconX - 4 + col * kIconColumn, kIconY + row * kIconStep, kIconW, kIconH};
}

// No icon other than `but` is on the cell.
bool Icons::free_cell(int index, int but) const
{
    const Rect c = cell(index);
    for (int i = 0; i < count_; i++) {
        if (i != but && !rects_[i].empty() && rects_[i].overlaps(c)) {
            return false;
        }
    }
    return true;
}

// Where each icon is: its own place, kept on the screen; the others after
// them, each in the first cell nothing is on.
void Icons::layout()
{
    const Rect work = wm_.work_area();
    for (int i = 0; i < count_; i++) {
        rects_[i] = {};
        if (icons_[i].x >= 0) {
            rects_[i] = {std::max(0, std::min(icons_[i].x, work.w - kIconW)),
                         std::max(0, std::min(icons_[i].y, work.h - kIconH)), kIconW, kIconH};
        }
    }
    const int n = cells();
    for (int i = 0; i < count_; i++) {
        if (!rects_[i].empty()) {
            continue;
        }
        int at = 0;
        while (at < n && !free_cell(at, i)) {
            at++;
        }
        rects_[i] = cell(at < n ? at : i); // a full desktop: one on another
    }
}

// The place an icon gets when it is let go with the pointer at (x, y): its
// picture under the pointer, as the outline showed it, or the nearest free
// cell while snapping is on.
Rect Icons::place_at(int x, int y, int but) const
{
    const Rect work = wm_.work_area();
    Rect r = {std::max(0, std::min(x - kIconW / 2, work.w - kIconW)), std::max(0, std::min(y - 16, work.h - kIconH)),
              kIconW, kIconH};
    if (snap_) {
        const Rect free = r;
        long best = -1;
        const int n = cells();
        for (int i = 0; i < n; i++) {
            const Rect c = cell(i);
            const long d = static_cast<long>(c.x - free.x) * (c.x - free.x) +
                           static_cast<long>(c.y - free.y) * (c.y - free.y);
            if ((best < 0 || d < best) && free_cell(i, but)) {
                best = d;
                r = c;
            }
        }
    }
    return r;
}

void Icons::realign()
{
    // The order they are in now: by column, then down it.
    int order[kMax];
    for (int i = 0; i < count_; i++) {
        order[i] = i;
    }
    const auto column = [&](int i) { return (rects_[i].x - (kIconX - 4) + kIconColumn / 2) / kIconColumn; };
    std::sort(order, order + count_, [&](int a, int b) {
        if (column(a) != column(b)) {
            return column(a) < column(b);
        }
        if (rects_[a].y != rects_[b].y) {
            return rects_[a].y < rects_[b].y;
        }
        return a < b;
    });
    for (int i = 0; i < count_; i++) {
        rects_[order[i]] = cell(i);
    }
    wm_.invalidate(wm_.work_area());
    if (moved_ != nullptr) {
        moved_(moved_context_);
    }
}

// What an icon is called while it is dragged: its file, or for one that
// stands for no file a name no file has.
void Icons::drag_name(int icon, char *out, size_t capacity) const
{
    if (icons_[icon].path != nullptr) {
        snprintf(out, capacity, "%s", icons_[icon].path);
    } else {
        snprintf(out, capacity, "%c%s", kOwnDrag, icons_[icon].label);
    }
}

// The icon a dragged name is, or -1 for a file from somewhere else.
int Icons::own(const char *name) const
{
    for (int i = 0; i < count_; i++) {
        char mine[wm::Wm::kDragPath];
        drag_name(i, mine, sizeof(mine));
        if (strcmp(mine, name) == 0) {
            return i;
        }
    }
    return -1;
}

// The icon under (x, y) that would take the dragged file, or -1.
int Icons::target(const char *name, int x, int y) const
{
    if (name[0] == kOwnDrag) {
        return -1; // not a file
    }
    int icon = -1;
    for (int i = 0; i < count_; i++) {
        if (rects_[i].contains(x, y) && icons_[i].drop != nullptr &&
            (icons_[i].path == nullptr || strcmp(icons_[i].path, name) != 0)) {
            icon = i;
        }
    }
    return icon;
}

Rect Icons::icon_rect(int index) const
{
    return index >= 0 && index < count_ ? rects_[index] : Rect{};
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
        } else if (hit >= 0) {
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
            char name[wm::Wm::kDragPath];
            drag_name(icon, name, sizeof(name));
            wm_.begin_drag(name, icons_[icon].path == nullptr);
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
// the desktop itself: one of its own icons goes to that place, a file from
// somewhere else to whoever takes those.
bool Icons::drag_over(const char *path, int x, int y)
{
    if (path == nullptr) {
        set_drop_icon(-1);
        return false;
    }
    const int icon = target(path, x, y);
    set_drop_icon(icon);
    return icon >= 0 || own(path) >= 0 || drop_ != nullptr;
}

void Icons::dropped(const char *path, int x, int y)
{
    // Worked out again: the server has already said the drag is over.
    const int icon = target(path, x, y);
    const int mine = own(path);
    if (icon >= 0) {
        icons_[icon].drop(path, icons_[icon].context);
    } else if (mine >= 0) {
        const Rect to = place_at(x, y, mine);
        if (to.x != rects_[mine].x || to.y != rects_[mine].y) {
            wm_.invalidate(rects_[mine]);
            rects_[mine] = to;
            wm_.invalidate(to);
            if (moved_ != nullptr) {
                moved_(moved_context_);
            }
        }
    } else if (drop_ != nullptr) {
        const Rect to = place_at(x, y, -1);
        drop_(path, to.x, to.y, drop_context_);
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
