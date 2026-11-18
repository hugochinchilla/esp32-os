// The icons on the desktop (core/desk/desk.hpp): a panel behind every
// window, over the whole work area. It paints the background around and
// under the icons, since each pixel is painted once.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/desk/desk.hpp"

namespace desk {

using gfx::Canvas;
using gfx::RectList;

namespace {

// Layout (tools/mockup/desktop.py).
constexpr int kIconX = 6, kIconY = 8, kIconStep = 70, kIconColumn = 76;
constexpr int kIconW = 72, kIconH = 50;

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
        picked_ = 0;
        moving_ = 0;
        collapse_ = -1;
        drop_icon_ = -1;
        pressed_ = -1;
    }
    band_ = false;
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

// No icon other than `but` is on the cell. Those of a group that is being
// put down and have not got their place yet do not count: they are leaving.
bool Icons::free_cell(int index, int but) const
{
    const Rect c = cell(index);
    for (int i = 0; i < count_; i++) {
        if (i != but && !(moving_ >> i & 1) && !rects_[i].empty() && rects_[i].overlaps(c)) {
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
    return place_for({x - kIconW / 2, y - 16, kIconW, kIconH}, but);
}

// The same for an icon whose corner would be at (r.x, r.y): kept on the
// screen, and in the nearest free cell while snapping is on.
Rect Icons::place_for(Rect r, int but) const
{
    const Rect work = wm_.work_area();
    r = {std::max(0, std::min(r.x, work.w - kIconW)), std::max(0, std::min(r.y, work.h - kIconH)), kIconW, kIconH};
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

// What is picked becomes `picked`, and `last` the one picked last: the
// icons that changed are painted again.
void Icons::pick(uint32_t picked, int last)
{
    for (int i = 0; i < count_; i++) {
        if (((picked ^ picked_) >> i & 1) != 0) {
            wm_.invalidate(icon_rect(i));
        }
    }
    picked_ = picked;
    selected_ = last;
}

// The rectangle being pulled over the desktop.
Rect Icons::band() const
{
    return {std::min(band_x0_, band_x1_), std::min(band_y0_, band_y1_), std::abs(band_x1_ - band_x0_) + 1,
            std::abs(band_y1_ - band_y0_) + 1};
}

void Icons::band_dirty()
{
    const Rect r = band();
    wm_.invalidate({r.x, r.y, r.w, 1});
    wm_.invalidate({r.x, r.bottom() - 1, r.w, 1});
    wm_.invalidate({r.x, r.y, 1, r.h});
    wm_.invalidate({r.right() - 1, r.y, 1, r.h});
}

void Icons::pull_band(int x, int y)
{
    const Rect work = wm_.work_area();
    band_dirty();
    band_x1_ = std::max(0, std::min(x, work.w - 1));
    band_y1_ = std::max(0, std::min(y, work.h - 1));
    band_dirty();
    // What it touches, by picture or by label, on top of what was there.
    const Rect r = band();
    uint32_t picked = band_base_;
    int last = -1;
    for (int i = 0; i < count_; i++) {
        const Rect at = icon_rect(i);
        const int tw = Canvas::text_width(icons_[i].label);
        const Rect picture = {at.x + 20, at.y, 32, 33};
        const Rect label = {at.x + 36 - tw / 2 - 1, at.y + 33, tw + 2, 14};
        if (r.overlaps(picture) || r.overlaps(label)) {
            picked |= 1u << i;
            last = i;
        }
    }
    if (picked != picked_) {
        pick(picked, last >= 0 ? last : (picked != 0 ? __builtin_ctz(picked) : -1));
    }
}

void Icons::mouse(const wm::MouseMsg &m)
{
    switch (m.kind) {
    case wm::MouseKind::Down: {
        pressed_ = -1;
        collapse_ = -1;
        if (band_) { // the button came up where the panel was not told
            band_ = false;
            band_dirty();
        }
        const bool ctrl = (wm_.modifiers() & wm::kCtrl) != 0;
        const int hit = icon_at(m.x, m.y);
        if (hit < 0) {
            // The desktop itself: a rectangle starts there. What was picked
            // goes, unless Ctrl is held: then the rectangle adds to it.
            if (!ctrl) {
                pick(0, -1);
            }
            last_hit_ = -1;
            band_base_ = picked_;
            band_ = true;
            band_x0_ = band_x1_ = m.x;
            band_y0_ = band_y1_ = m.y;
            break;
        }
        if (ctrl) {
            const uint32_t picked = picked_ ^ (1u << hit);
            pick(picked, (picked >> hit & 1) != 0 ? hit : (picked != 0 ? __builtin_ctz(picked) : -1));
        } else if (picked(hit) && picked_count() > 1) {
            // One of several: they may be about to be dragged together, so
            // the others stay picked until the button comes up.
            collapse_ = hit;
            selected_ = hit;
        } else {
            pick(1u << hit, hit);
        }
        // Twice on the same icon opens it; a third press starts again.
        const bool twice = m.double_click && hit == last_hit_ && !ctrl;
        last_hit_ = twice ? -1 : hit;
        if (twice && icons_[hit].open != nullptr) {
            icons_[hit].open(icons_[hit].context);
        } else if (picked(hit)) {
            pressed_ = hit; // may become a drag
            press_x_ = m.x;
            press_y_ = m.y;
        }
        break;
    }
    case wm::MouseKind::Move:
        if (band_) {
            pull_band(m.x, m.y);
        } else if (pressed_ >= 0 && pressed_ < count_ &&
                   (m.x - press_x_) * (m.x - press_x_) + (m.y - press_y_) * (m.y - press_y_) > 16) {
            // The one held, then the others that are picked. One that
            // stands for no file goes nowhere but the desktop.
            const int icon = pressed_;
            pressed_ = -1;
            collapse_ = -1;
            char name[wm::Wm::kDragPath];
            drag_name(icon, name, sizeof(name));
            wm_.begin_drag(name, icons_[icon].path == nullptr);
            for (int i = 0; i < count_; i++) {
                if (i != icon && picked(i)) {
                    drag_name(i, name, sizeof(name));
                    wm_.add_to_drag(name, icons_[i].path == nullptr);
                }
            }
        }
        break;
    case wm::MouseKind::Up:
        if (band_) {
            band_ = false;
            band_dirty();
        } else if (pressed_ >= 0 && collapse_ >= 0 && collapse_ < count_) {
            pick(1u << collapse_, collapse_); // clicked and not dragged: it alone now
        }
        pressed_ = -1;
        collapse_ = -1;
        break;
    default:
        break;
    }
}

void Icons::right_click(int x, int y)
{
    // The icon under the pointer is picked alone, as a left press would,
    // unless it is one of several picked already: those stay. Then the menu
    // for it, or for the desktop itself away from the icons.
    const int hit = icon_at(x, y);
    if (hit >= 0 && picked(hit)) {
        selected_ = hit;
    } else {
        pick(hit >= 0 ? 1u << hit : 0, hit);
    }
    last_hit_ = -1;
    if (menu_ != nullptr) {
        menu_(hit, x, y, menu_context_);
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

// When several were dragged this comes once for each, the one held first.
// The desktop's own icons keep their places among themselves: each goes as
// far as the first did. Files from somewhere else each get a place of their
// own. If the first went onto an icon that takes files, the ones it does
// not take stay where they are.
void Icons::dropped(const char *path, int x, int y)
{
    const bool first = !batch_;
    batch_ = wm_.drops_to_come() > 0;
    // Worked out again: the server has already said the drag is over.
    const int icon = target(path, x, y);
    const int mine = own(path);
    if (first) {
        batch_onto_ = icon >= 0;
        batch_moved_ = false;
        batch_files_ = 0;
        batch_dx_ = batch_dy_ = 0;
        moving_ = mine >= 0 && !batch_onto_ && batch_ ? picked_ : 0;
    }
    if (icon >= 0) {
        icons_[icon].drop(path, icons_[icon].context); // may bring other icons: `mine` is stale now
    } else if (batch_onto_) {
        // It came along with one that went into a folder or the Trash.
    } else if (mine >= 0) {
        Rect to;
        if (first) {
            to = place_at(x, y, mine);
            batch_dx_ = to.x - rects_[mine].x;
            batch_dy_ = to.y - rects_[mine].y;
        } else {
            to = place_for({rects_[mine].x + batch_dx_, rects_[mine].y + batch_dy_, kIconW, kIconH}, mine);
        }
        moving_ &= ~(1u << mine);
        if (to.x != rects_[mine].x || to.y != rects_[mine].y) {
            wm_.invalidate(rects_[mine]);
            rects_[mine] = to;
            wm_.invalidate(to);
            batch_moved_ = true;
        }
    } else if (drop_ != nullptr && path[0] != kOwnDrag) {
        // The first where it was let go; the others in the free cells
        // nearest to there, so that none lands on another.
        Rect to = place_at(x, y, -1);
        if (batch_files_++ > 0 && !snap_) {
            snap_ = true;
            to = place_at(x, y, -1);
            snap_ = false;
        }
        drop_(path, to.x, to.y, drop_context_);
    }
    if (!batch_) {
        moving_ = 0;
        if (batch_moved_ && moved_ != nullptr) {
            moved_(moved_context_);
        }
        batch_moved_ = false;
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
        const bool selected = picked(i) || i == drop_icon_;
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
            c.fill_rgb({tx - 1, y + 33, tw + 2, 14}, gfx::theme().select);
            c.text(tx, y + 34, icon.label, gfx::theme().select_ink);
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
    // The rectangle being pulled, over the icons: white with black dots,
    // which shows on any background.
    const Rect r = band();
    if (band_ && (r.w > 2 || r.h > 2) && r.overlaps(bounds)) {
        c.hline(r.x, r.y, r.w, gfx::kWhite);
        c.hline(r.x, r.bottom() - 1, r.w, gfx::kWhite);
        c.vline(r.x, r.y, r.h, gfx::kWhite);
        c.vline(r.right() - 1, r.y, r.h, gfx::kWhite);
        c.focus_rect(r);
    }
}

} // namespace desk
