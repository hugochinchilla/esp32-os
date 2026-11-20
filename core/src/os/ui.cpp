// Windows and controls by number (core/os/ui.hpp): the system's side. One
// window class of the system's own stands behind every such window; it
// owns the controls and turns what the window manager tells it into events
// for the program.
#include "core/os/ui.hpp"
#include "core/prog/prog.hpp"

#include <cstdio>
#include <cstring>
#include <new>

#include "core/os/os.hpp"
#include "core/wm/text_edit.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"

namespace os {
namespace ui {

namespace {

static_assert(kScrollBarThickness == wm::ScrollBar::kThickness, "the scroll bar's thickness, as programs are told it");

// The drawings in progress: the canvas of the Paint event being answered
// and the parts the program has asked of it. Numbers are given afresh for
// each event, never the same twice running, so one kept past its event
// draws nothing.
struct Drawings {
    static constexpr int kMax = 1 + draw::kMaxParts;
    alignas(gfx::Canvas) unsigned char room[kMax][sizeof(gfx::Canvas)];
    int count = 0;
    int base = 0; // the number of the first is base + 1

    gfx::Canvas *at(Drawing d)
    {
        const int i = d - base - 1;
        return i >= 0 && i < count ? reinterpret_cast<gfx::Canvas *>(room[i]) : nullptr;
    }
    Drawing add(const gfx::Canvas &c)
    {
        if (count >= kMax) {
            return kNoDrawing;
        }
        new (room[count]) gfx::Canvas(c);
        return base + ++count;
    }
    void end()
    {
        for (int i = 0; i < count; i++) {
            reinterpret_cast<gfx::Canvas *>(room[i])->~Canvas();
        }
        base += kMax; // the next event's numbers are all new
        count = 0;
    }
};
Drawings g_drawings;

// Every window made here, open or not yet. A window's number is the one
// the window server knows it by (os::WindowId), taken when it is made.
// There is no limit on how many (the owner's decision, 2026-11-17): the
// list starts with room for sixteen and grows out of the machine's memory,
// counted as nobody's; with none left, the next window is not made.
class Behind;
constexpr int kFirstWindows = 16;
Behind *g_first_windows[kFirstWindows] = {};
Behind **g_windows = g_first_windows;
int g_window_room = kFirstWindows;

// A free place in the list, -1 if there is none and no memory for more.
int free_window_slot()
{
    for (int i = 0; i < g_window_room; i++) {
        if (g_windows[i] == nullptr) {
            return i;
        }
    }
    ports::Memory *memory = os::installed_kit().memory;
    auto **more = memory != nullptr
                      ? static_cast<Behind **>(memory->allocate(2 * g_window_room * sizeof(Behind *)))
                      : nullptr;
    if (more == nullptr) {
        return -1;
    }
    for (int i = 0; i < 2 * g_window_room; i++) {
        more[i] = i < g_window_room ? g_windows[i] : nullptr;
    }
    if (g_windows != g_first_windows) {
        memory->release(g_windows);
    }
    g_windows = more;
    g_window_room *= 2;
    return g_window_room / 2;
}

// After a window has gone: with none left past the first sixteen places,
// the longer list goes back.
void forget_spare_slots()
{
    if (g_windows == g_first_windows) {
        return;
    }
    for (int i = kFirstWindows; i < g_window_room; i++) {
        if (g_windows[i] != nullptr) {
            return;
        }
    }
    for (int i = 0; i < kFirstWindows; i++) {
        g_first_windows[i] = g_windows[i];
    }
    os::installed_kit().memory->release(g_windows);
    g_windows = g_first_windows;
    g_window_room = kFirstWindows;
}

enum class Kind : uint8_t {
    Label,
    Plain,
    Button,
    CheckBox,
    Radio,
    TextField,
    ScrollBar,
    List,
    Group,
    TextArea,
    Picture,
    Grid,
    DropDown,
    Meter,
    TextEdit,
    IconView,
    Tabs,
};

// A line of text exactly as given, kept by the control.
class PlainText : public wm::Widget {
public:
    explicit PlainText(const gfx::Rect &r) : Widget(r) {}
    void set(const char *text)
    {
        if (strcmp(text, text_) != 0) {
            snprintf(text_, sizeof(text_), "%.*s", static_cast<int>(sizeof(text_) - 1), text); // longer text is cut
            invalidate();
        }
    }
    void paint(gfx::Canvas &c) override
    {
        const int y = (rect.h - 12) / 2;
        c.fill({0, 0, rect.w, y}, gfx::face());
        const int x = c.text_opaque(0, y, text_, gfx::ink(), gfx::face());
        c.fill({x, y, rect.w - x, 12}, gfx::face());
        c.fill({0, y + 12, rect.w, rect.h - y - 12}, gfx::face());
    }
    ~PlainText() = default;

private:
    char text_[96] = "";
};

// A rectangle the program paints.
class Picture : public wm::Widget {
public:
    using PaintFn = void (*)(Picture &picture, gfx::Canvas &c, void *context);
    using MouseFn = void (*)(Picture &picture, const wm::MouseMsg &m, void *context);
    using KeyFn = bool (*)(Picture &picture, const wm::KeyMsg *k, const char *text, void *context);
    Picture(const gfx::Rect &r, uint8_t how, PaintFn paint, MouseFn mouse, KeyFn key, void *context)
        : Widget(r), how_(how), paint_(paint), mouse_(mouse), key_(key), context_(context)
    {
    }
    bool focusable() const override { return (how_ & kKeys) != 0; }
    bool key(const wm::KeyMsg &k) override { return key_(*this, &k, nullptr, context_); }
    void text(const char *utf8) override { key_(*this, nullptr, utf8, context_); }
    gfx::Rect caret_rect() const override { return has_caret_ ? caret_ : Widget::caret_rect(); }
    void set_caret(const gfx::Rect &r)
    {
        caret_ = r;
        has_caret_ = true;
    }
    void paint(gfx::Canvas &c) override { paint_(*this, c, context_); }
    bool layered() const override { return (how_ & kOnce) == 0; }
    bool wants_mouse() const override { return (how_ & kMouse) != 0; }
    void mouse(const wm::MouseMsg &m) override { mouse_(*this, m, context_); }
    ~Picture() = default;

private:
    uint8_t how_;
    PaintFn paint_;
    MouseFn mouse_;
    KeyFn key_;
    void *context_;
    gfx::Rect caret_ = {0, 0, 0, 0};
    bool has_caret_ = false;
};

// A grid of cells to pick one from: the colours, or the patterns. The cell
// chosen has a black and white frame; arrows move it when it has the focus.
class Grid : public wm::Widget {
public:
    using PaintFn = void (*)(Grid &grid, gfx::Canvas &c, const gfx::Rect &cell, int index, void *context);
    using ChangeFn = void (*)(Grid &grid, void *context);

    Grid(const gfx::Rect &r, int columns, int count, int cell_w, int cell_h, int margin, PaintFn paint_cell,
         ChangeFn changed, void *context)
        : Widget(r), columns_(columns > 0 ? columns : 1), count_(count), cell_w_(cell_w > 0 ? cell_w : 1),
          cell_h_(cell_h > 0 ? cell_h : 1), margin_(margin), paint_cell_(paint_cell), changed_(changed),
          context_(context)
    {
    }
    ~Grid() = default;
    int selected() const { return selected_; }
    // Nobody is told; -1: no cell chosen.
    void select_quietly(int index)
    {
        if (index >= -1 && index < count_ && index != selected_) {
            selected_ = index;
            invalidate();
        }
    }
    void select(int index)
    {
        if (index < 0 || index >= count_ || index == selected_) {
            return;
        }
        selected_ = index;
        invalidate();
        changed_(*this, context_);
    }
    gfx::Rect cell(int index) const
    {
        return {(index % columns_) * cell_w_, (index / columns_) * cell_h_, cell_w_, cell_h_};
    }

    void paint(gfx::Canvas &c) override
    {
        c.fill({0, 0, rect.w, rect.h}, gfx::face());
        for (int i = 0; i < count_; i++) {
            const gfx::Rect r = cell(i);
            if (i == selected_) {
                c.edge(r, gfx::ink(), gfx::ink());
                c.edge(r.inset(1), gfx::paper(), gfx::paper());
            }
            const gfx::Rect inner = r.inset(margin_);
            c.dent(inner);
            paint_cell_(*this, c, inner.inset(1), i, context_);
            if (i == selected_ && margin_ < 3) {
                // A tight grid: the mark goes over the cell's own edge.
                c.edge(r, gfx::ink(), gfx::ink());
                c.edge(r.inset(1), gfx::paper(), gfx::paper());
                c.edge(r.inset(2), gfx::ink(), gfx::ink());
            }
        }
        if (has_focus() && selected_ >= 0) {
            c.focus_rect(cell(selected_).inset(margin_ > 2 ? 2 : 0));
        }
    }
    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind != wm::MouseKind::Down || m.x < 0 || m.y < 0 || m.x >= columns_ * cell_w_) {
            return;
        }
        select(m.y / cell_h_ * columns_ + m.x / cell_w_);
    }
    bool focusable() const override { return true; }
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        switch (k.usage) {
        case wm::key::Left:
            select(selected_ - 1);
            return true;
        case wm::key::Right:
            select(selected_ + 1);
            return true;
        case wm::key::Up:
            select(selected_ - columns_);
            return true;
        case wm::key::Down:
            select(selected_ + columns_);
            return true;
        default:
            return false;
        }
    }

private:
    int columns_, count_, cell_w_, cell_h_, margin_;
    PaintFn paint_cell_;
    ChangeFn changed_;
    void *context_;
    int selected_ = 0;
};

// A box showing a value with a drop-down arrow, as the 98 "Look in" box.
class DropDown : public wm::Widget {
public:
    using Callback = void (*)(DropDown &box, void *context);
    DropDown(const gfx::Rect &r, const gfx::Bitmap *icon, Callback on_click, void *context)
        : Widget(r), icon_(icon), on_click_(on_click), context_(context)
    {
    }
    ~DropDown() = default;
    void set_text(const char *text)
    {
        text_ = text;
        invalidate();
    }
    void paint(gfx::Canvas &c) override
    {
        static const char *const kDownGlyph[] = {"XXXXXXX", " XXXXX ", "  XXX  ", "   X   "};
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        c.fill(r.inset(2), gfx::paper());
        gfx::Canvas inner = c.sub({2, 2, rect.w - 4 - 16, rect.h - 4});
        if (icon_ != nullptr) {
            inner.blit(*icon_, 2, (rect.h - 4 - 16) / 2);
        }
        inner.text(icon_ != nullptr ? 22 : 4, (rect.h - 4 - 12) / 2, text_, gfx::ink());
        const gfx::Rect b = {rect.w - 2 - 16, 2, 16, rect.h - 4};
        c.button(b, held_ ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
        c.pattern(b.x + 4 + held_, b.y + (b.h - 4) / 2 + held_, kDownGlyph, 4, gfx::ink());
    }
    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind == wm::MouseKind::Down) {
            held_ = 1;
            invalidate();
            on_click_(*this, context_);
        } else if (m.kind == wm::MouseKind::Up) {
            held_ = 0;
            invalidate();
        }
    }

private:
    const char *text_ = "";
    const gfx::Bitmap *icon_;
    Callback on_click_;
    void *context_;
    int held_ = 0;
};

// A bar that fills from the left in blocks, in a sunken box.
class Meter : public wm::Widget {
public:
    static constexpr int kBlock = 6, kGap = 2;

    Meter(const gfx::Rect &r, gfx::Colour colour) : Widget(r), colour_(colour) {}
    ~Meter() = default;

    int blocks() const { return (rect.w - 4 + kGap) / (kBlock + kGap); }
    // How full, as value out of total (shown in whole blocks).
    void set(uint32_t value, uint32_t total)
    {
        const int n = blocks();
        int lit = total == 0 ? 0 : static_cast<int>((static_cast<uint64_t>(value) * n + total / 2) / total);
        lit = lit > n ? n : lit;
        if (lit == 0 && value != 0) {
            lit = 1; // anything at all shows
        }
        if (lit != lit_) {
            lit_ = lit;
            invalidate();
        }
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        // Every pixel inside once: blocks, then the gaps between them.
        const int w = rect.w - 4, h = rect.h - 4;
        int x = 0;
        for (int i = 0; x < w; i++) {
            const int bw = x + kBlock > w ? w - x : kBlock;
            c.fill({2 + x, 2, bw, h}, i < lit_ ? colour_ : gfx::paper());
            x += bw;
            const int gw = x + kGap > w ? w - x : kGap;
            if (gw > 0) {
                c.fill({2 + x, 2, gw, h}, gfx::paper());
                x += gw;
            }
        }
    }

private:
    gfx::Colour colour_;
    int lit_ = 0;
};

// What a folder holds, shown one of three ways (backlog item 56). Icons: a
// grid of pictures with their names under them, 76 x 66 per entry, filled
// along the rows. List: small pictures with the names beside them, filled
// down the columns, which are as wide as the longest name asks. Details: one
// entry per line under a row of titles, with the program's columns (size,
// kind, date) beside the name. Several can be picked (backlog item 55):
// Ctrl+click, Shift+click, a rectangle pulled from an empty place, Ctrl+A.
// A scroll bar comes when they do not fit: down the side, or along the
// bottom for the List, which goes on to the right.
class IconView : public wm::Widget {
public:
    using Callback = void (*)(IconView &view, void *context);
    static constexpr int kCellW = 76, kCellH = 66;
    static constexpr int kLine = 18;        // a line of the List and of the Details
    static constexpr int kHeader = 18;      // the titles over the Details
    static constexpr int kListWidest = 220; // a column of the List; longer names are cut
    // ponytail: entries past this cannot be picked; a folder shows 160.
    static constexpr int kMaxPicked = 256;

    IconView(const gfx::Rect &r, Callback on_open, Callback on_select, Callback on_drag, void *context)
        : Widget(r), on_open_(on_open), on_select_(on_select), on_drag_(on_drag), context_(context),
          bar_({0, 0, wm::ScrollBar::kThickness, 10}, true, &IconView::scrolled, this),
          across_({0, 0, 10, wm::ScrollBar::kThickness}, false, &IconView::scrolled, this)
    {
        bar_.set_parent(this);
        across_.set_parent(this);
    }
    ~IconView() = default;
    // The entries: the program's own tables, which must stay valid. What is
    // picked stays while there are as many entries as before (a file was
    // saved again); otherwise only the one picked last does, as the numbers
    // now stand for other entries.
    void set_items(const char *const *names, int count, const gfx::Bitmap *const *icons)
    {
        names_ = names;
        icons_ = icons;
        if (count != count_) {
            const int lead = selected_ < count ? selected_ : -1;
            picked_ = {};
            picked_.set(lead, true);
            selected_ = lead;
            anchor_ = lead;
            highlight_ = -1;
        }
        count_ = count;
        band_ = false;
        pressed_ = false;
        collapse_ = -1;
        measure();
        layout();
    }
    // The 16 x 16 pictures the List and the Details show, and the columns
    // of the Details: the program's tables too.
    void set_small_icons(const gfx::Bitmap *const *icons)
    {
        small_ = icons;
        invalidate();
    }
    void set_columns(const Column *columns, int count)
    {
        columns_ = columns;
        column_count_ = count < 0 ? 0 : count;
        invalidate();
    }
    // The same entries another way: what is picked stays, and is in view.
    void set_mode(ViewMode mode)
    {
        if (mode == mode_) {
            return;
        }
        mode_ = mode;
        first_ = 0;
        band_ = false;
        pressed_ = false;
        collapse_ = -1;
        layout();
        show(selected_);
    }
    // The one picked last, or -1.
    int selected() const { return selected_; }
    bool picked(int i) const { return i >= 0 && i < count_ && picked_.get(i); }
    int picked_count() const { return picked_.count(); }
    // The entry whose picture or name is at a point of the view, or -1.
    int item_at(int x, int y) const
    {
        int i;
        if (mode_ == ViewMode::List) {
            const int row = (y - 4) / kLine;
            if (x < 4 || y < 4 || row >= list_rows_) {
                return -1;
            }
            i = ((x - 4) / list_w_ + first_) * list_rows_ + row;
        } else if (mode_ == ViewMode::Details) {
            if (y < 2 + kHeader) {
                return -1;
            }
            i = (y - 2 - kHeader) / kLine + first_;
        } else {
            const int col = (x - 2 - 10) / kCellW, row = (y - 2 - 8) / kCellH;
            if (x < 12 || y < 10 || col >= columns()) {
                return -1;
            }
            i = (row + first_) * columns() + col;
        }
        if (i >= count_) {
            return -1;
        }
        gfx::Rect icon, label;
        parts(i, icon, label);
        return icon.contains(x, y) || label.contains(x, y) ? i : -1;
    }
    // One alone (-1: none), brought into view; tells of it.
    void select(int i)
    {
        i = i < -1 ? -1 : (i >= count_ ? count_ - 1 : i);
        Bits next;
        next.set(i, true);
        if (i == selected_ && next == picked_) {
            return;
        }
        anchor_ = i;
        show(i);
        set_picked(next, i);
    }
    // Shown as picked without being picked; -1: none.
    void highlight(int i)
    {
        i = i >= 0 && i < count_ ? i : -1;
        if (i != highlight_) {
            invalidate_item(highlight_);
            highlight_ = i;
            invalidate_item(i);
        }
    }
    void reset()
    {
        picked_ = {};
        selected_ = -1;
        anchor_ = -1;
        highlight_ = -1;
        band_ = false;
        first_ = 0;
        layout();
    }
    void layout()
    {
        const int t = wm::ScrollBar::kThickness;
        bar_.rect = {rect.w - 2 - t, 2, t, rect.h - 4};
        across_.rect = {2, rect.h - 2 - t, rect.w - 4, t};
        int lines; // rows of the grid, columns of the List, lines of the Details
        if (mode_ == ViewMode::List) {
            // As many to a column as fit; fewer when the bar takes its strip.
            const int w = 20 + widest_ + 12;
            list_w_ = w > kListWidest ? kListWidest : w;
            list_rows_ = (rect.h - 4 - 4) / kLine;
            list_rows_ = list_rows_ < 1 ? 1 : list_rows_;
            if ((count_ + list_rows_ - 1) / list_rows_ > visible_lines()) {
                list_rows_ = (rect.h - 4 - 4 - t) / kLine;
                list_rows_ = list_rows_ < 1 ? 1 : list_rows_;
            }
            lines = (count_ + list_rows_ - 1) / list_rows_;
        } else if (mode_ == ViewMode::Details) {
            lines = count_;
        } else {
            lines = (count_ + columns() - 1) / columns();
        }
        const int visible = visible_lines();
        wm::ScrollBar &b = bar();
        b.set_range(lines > visible ? lines - visible : 0, visible);
        first_ = first_ > b.maximum() ? b.maximum() : first_;
        b.set_value_quietly(first_);
        invalidate();
    }
    // The grid of icons: how many across.
    int columns() const
    {
        const int c = (rect.w - 4 - wm::ScrollBar::kThickness - 8) / kCellW;
        return c < 1 ? 1 : c;
    }
    // How many whole lines show: rows of the grid, columns of the List,
    // lines of the Details. What the scroll bar counts in.
    int visible_lines() const
    {
        int n;
        if (mode_ == ViewMode::List) {
            n = (rect.w - 4 - 4) / list_w_;
        } else if (mode_ == ViewMode::Details) {
            n = (rect.h - 4 - kHeader) / kLine;
        } else {
            n = (rect.h - 4 - 10) / kCellH;
        }
        return n < 1 ? 1 : n;
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        const gfx::Rect area = view_area();
        c.fill(area, gfx::paper());
        gfx::Canvas inner = c.sub(area);
        if (mode_ == ViewMode::Icons) {
            paint_icons(inner);
        } else {
            paint_lines(inner, area);
        }
        // The rectangle being pulled: dotted, over the entries.
        const gfx::Rect band = band_rect();
        if (band_ && (band.w > 2 || band.h > 2)) {
            inner.focus_rect({band.x - area.x, band.y - area.y, band.w, band.h});
        }
        const wm::ScrollBar &b = bar();
        if (b.maximum() > 0) {
            gfx::Canvas bc = c.sub(b.rect);
            bar().paint(bc);
        }
    }

    bool wants_mouse() const override { return true; }
    bool focusable() const override { return true; }
    // A row of icons for each notch: the rows are tall. A column of the
    // List, which has no other way to go; three lines of the Details.
    bool wheel(const wm::WheelMsg &m) override
    {
        wm::ScrollBar &b = bar();
        if (b.maximum() > 0 && m.down != 0) {
            b.set_value(b.value() + m.down * (mode_ == ViewMode::Details ? 3 : 1));
        }
        return true;
    }
    void mouse(const wm::MouseMsg &m) override
    {
        wm::ScrollBar &b = bar();
        if (m.kind == wm::MouseKind::Down) {
            in_bar_ = b.maximum() > 0 && b.rect.contains(m.x, m.y);
        }
        if (in_bar_) {
            b.mouse({m.kind, m.x - b.rect.x, m.y - b.rect.y});
            invalidate();
            return;
        }
        if (m.kind == wm::MouseKind::Move) {
            if (band_) {
                pull_band(m.x, m.y);
                return;
            }
            // Held on a picked entry and moved a little: what is picked is
            // dragged.
            const int dx = m.x - press_x_, dy = m.y - press_y_;
            if (pressed_ && dx * dx + dy * dy > 16 && on_drag_ != nullptr) {
                pressed_ = false;
                collapse_ = -1;
                on_drag_(*this, context_);
            }
            return;
        }
        if (m.kind != wm::MouseKind::Down) {
            if (band_) {
                band_ = false;
                outline_dirty(band_rect());
            } else if (pressed_ && collapse_ >= 0) {
                // One of several was clicked and not dragged: it alone now.
                select(collapse_);
            }
            pressed_ = false;
            collapse_ = -1;
            return;
        }
        const uint8_t mods = manager() != nullptr ? manager()->modifiers() : 0;
        const bool ctrl = (mods & wm::kCtrl) != 0, shift = (mods & wm::kShift) != 0;
        const int i = item_at(m.x, m.y);
        pressed_ = false;
        collapse_ = -1;
        if (band_) { // the button came up where the view was not told
            band_ = false;
            outline_dirty(band_rect());
        }
        if (mode_ == ViewMode::Details && m.y < 2 + kHeader) {
            return; // the titles: nothing is picked or let go by them
        }
        if (i < 0) {
            // An empty place: a rectangle starts there. What was picked
            // goes, unless Ctrl is held: then the rectangle adds to it.
            if (!ctrl) {
                set_picked({}, -1);
            }
            band_base_ = picked_;
            band_ = true;
            band_x0_ = band_x1_ = clamp_x(m.x) + scroll_x();
            band_y0_ = band_y1_ = clamp_y(m.y) + scroll_y();
            return;
        }
        if (shift && anchor_ >= 0 && anchor_ < count_) {
            stretch(i, ctrl);
        } else if (ctrl) {
            Bits next = picked_;
            next.set(i, !picked_.get(i));
            anchor_ = i;
            set_picked(next, next.get(i) ? i : next.first());
        } else if (picked_.get(i) && picked_.count() > 1) {
            // One of several: they may be about to be dragged together, so
            // the others stay picked until the button comes up.
            anchor_ = i;
            collapse_ = i;
            set_picked(picked_, i);
        } else {
            select(i);
        }
        pressed_ = picked_.get(i) && !m.double_click;
        press_x_ = m.x;
        press_y_ = m.y;
        if (m.double_click && !ctrl && !shift && on_open_ != nullptr) {
            on_open_(*this, context_);
        }
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        if ((k.mods & wm::kCtrl) && wm::key::letter(k.usage) == 'a') {
            Bits all;
            for (int i = 0; i < count_ && i < kMaxPicked; i++) {
                all.set(i, true);
            }
            set_picked(all, selected_ >= 0 ? selected_ : all.first());
            return true;
        }
        // Where an arrow goes from the one picked last: to the entry shown
        // beside it or under it, which each view places its own way. With
        // Shift what is picked stretches from where it started to there.
        int to;
        switch (k.usage) {
        case wm::key::Left:
            if (mode_ == ViewMode::Details) {
                return false;
            }
            to = selected_ < 0 ? 0 : (mode_ == ViewMode::List ? step(-list_rows_) : selected_ - 1);
            break;
        case wm::key::Right:
            if (mode_ == ViewMode::Details) {
                return false;
            }
            if (mode_ != ViewMode::List) {
                to = selected_ + 1;
            } else if (selected_ < 0) {
                to = 0;
            } else if (selected_ + list_rows_ < count_) {
                to = selected_ + list_rows_;
            } else {
                // The last column is shorter: to its end, if there is one.
                to = (count_ - 1) / list_rows_ > selected_ / list_rows_ ? count_ - 1 : selected_;
            }
            break;
        case wm::key::Up:
            to = selected_ < 0 ? 0 : step(mode_ == ViewMode::Icons ? -columns() : -1);
            break;
        case wm::key::Down:
            to = selected_ < 0 ? 0 : step(mode_ == ViewMode::Icons ? columns() : 1);
            break;
        case wm::key::Home:
            to = 0;
            break;
        case wm::key::End:
            to = count_ - 1;
            break;
        case wm::key::Enter:
            if (selected_ >= 0 && on_open_ != nullptr) {
                on_open_(*this, context_);
            }
            return true;
        default:
            return false;
        }
        if ((k.mods & wm::kShift) && anchor_ >= 0 && anchor_ < count_ && count_ > 0) {
            to = to < 0 ? 0 : (to >= count_ ? count_ - 1 : to);
            show(to);
            stretch(to, false);
        } else {
            select(to);
        }
        return true;
    }

private:
    static constexpr int kChars = (kCellW - 4) / 6; // characters per line

    // Up to kChars characters per line; byte offsets, as names are UTF-8.
    static void split_name(const char *name, char (&lines)[2][48])
    {
        lines[0][0] = lines[1][0] = 0;
        const char *p = name;
        const char *cut = nullptr, *space = nullptr;
        int n = 0;
        while (*p != 0) {
            const char *before = p;
            const uint8_t c = gfx::next_latin1(p);
            if (n == kChars) {
                cut = before;
                break;
            }
            if (c == ' ') {
                space = before;
            }
            n++;
        }
        if (cut == nullptr) {
            snprintf(lines[0], sizeof(lines[0]), "%.47s", name);
            return;
        }
        const char *second = space != nullptr ? space + 1 : cut;
        const int first_len = static_cast<int>((space != nullptr ? space : cut) - name);
        snprintf(lines[0], sizeof(lines[0]), "%.*s", first_len, name);
        // The rest, cut to a line with "..." if longer.
        const char *q = second;
        int m = 0;
        while (*q != 0 && m < kChars) {
            gfx::next_latin1(q);
            m++;
        }
        if (*q == 0) {
            snprintf(lines[1], sizeof(lines[1]), "%.47s", second);
        } else {
            const char *r = second;
            for (int k = 0; k < kChars - 3; k++) {
                gfx::next_latin1(r);
            }
            snprintf(lines[1], sizeof(lines[1]), "%.*s...", static_cast<int>(r - second), second);
        }
    }
    // A text as it fits in `width` pixels of a line: whole, or cut with
    // "..." (byte offsets, as names are UTF-8). Returns how wide it is.
    static int fit(const char *text, int width, char (&out)[80])
    {
        const int whole = gfx::Canvas::text_width(text);
        if (whole <= width) {
            snprintf(out, sizeof(out), "%.76s", text);
            return whole;
        }
        const int keep = width / 6 - 3;
        const char *p = text;
        for (int k = 0; k < keep && *p != 0; k++) {
            gfx::next_latin1(p);
        }
        snprintf(out, sizeof(out), "%.*s...", static_cast<int>(p - text > 76 ? 76 : p - text), text);
        return gfx::Canvas::text_width(out);
    }

    static void scrolled(wm::ScrollBar &bar, void *context)
    {
        auto *v = static_cast<IconView *>(context);
        v->first_ = bar.value();
        v->invalidate();
    }

    // Which entries are picked: a bit each.
    struct Bits {
        uint32_t words[kMaxPicked / 32] = {};
        bool get(int i) const { return i >= 0 && i < kMaxPicked && (words[i >> 5] >> (i & 31) & 1) != 0; }
        void set(int i, bool on)
        {
            if (i >= 0 && i < kMaxPicked) {
                words[i >> 5] = on ? words[i >> 5] | (1u << (i & 31)) : words[i >> 5] & ~(1u << (i & 31));
            }
        }
        int count() const
        {
            int n = 0;
            for (const uint32_t w : words) {
                n += __builtin_popcount(w);
            }
            return n;
        }
        int first() const
        {
            for (int i = 0; i < kMaxPicked; i++) {
                if (get(i)) {
                    return i;
                }
            }
            return -1;
        }
        bool operator==(const Bits &o) const { return memcmp(words, o.words, sizeof(words)) == 0; }
    };

    // The bar of the view as it is now: along the bottom for the List.
    wm::ScrollBar &bar() { return mode_ == ViewMode::List ? across_ : bar_; }
    const wm::ScrollBar &bar() const { return mode_ == ViewMode::List ? across_ : bar_; }
    // How far the view has gone, in pixels.
    int scroll_x() const { return mode_ == ViewMode::List ? first_ * list_w_ : 0; }
    int scroll_y() const
    {
        return mode_ == ViewMode::List ? 0 : first_ * (mode_ == ViewMode::Details ? kLine : kCellH);
    }
    // The widest name, for the columns of the List.
    void measure()
    {
        widest_ = 0;
        for (int i = 0; i < count_; i++) {
            const int w = gfx::Canvas::text_width(names_[i]);
            widest_ = w > widest_ ? w : widest_;
        }
    }
    // `by` entries on from the one picked last, or where it is if that
    // would leave the entries.
    int step(int by) const
    {
        const int to = selected_ + by;
        return to >= 0 && to < count_ ? to : selected_;
    }
    // The Details: how wide the name's column is. It takes what the others
    // leave, and never less than its own width says.
    int name_width() const
    {
        int others = 0;
        for (int k = 1; k < column_count_; k++) {
            others += columns_[k].width;
        }
        const int least = column_count_ > 0 ? columns_[0].width : 0;
        const int left = view_area().w - others;
        return left > least ? left : least;
    }
    // How many pixels a name has on its line.
    int name_room() const
    {
        return mode_ == ViewMode::Details ? name_width() - 28 : list_w_ - 20 - 12;
    }

    // Where the entries are shown, inside the sunken edge and beside the bar.
    gfx::Rect view_area() const
    {
        const int t = bar().maximum() > 0 ? wm::ScrollBar::kThickness : 0;
        if (mode_ == ViewMode::List) {
            return {2, 2, rect.w - 4, rect.h - 4 - t};
        }
        return {2, 2, rect.w - 4 - t, rect.h - 4};
    }
    // The place of an entry now (widget coordinates): its square of the
    // grid, its line of a column, its line under the titles.
    gfx::Rect cell(int i) const
    {
        if (mode_ == ViewMode::List) {
            return {4 + (i / list_rows_ - first_) * list_w_, 4 + (i % list_rows_) * kLine, list_w_, kLine};
        }
        if (mode_ == ViewMode::Details) {
            return {2, 2 + kHeader + (i - first_) * kLine, view_area().w, kLine};
        }
        const int cols = columns();
        return {12 + (i % cols) * kCellW, 10 + (i / cols - first_) * kCellH, kCellW, kCellH};
    }
    // Where the picture and the name of an entry are now (widget
    // coordinates): what a click and the rectangle go by.
    void parts(int i, gfx::Rect &icon, gfx::Rect &label) const
    {
        const gfx::Rect at = cell(i);
        if (mode_ != ViewMode::Icons) {
            const int whole = gfx::Canvas::text_width(names_[i]), room = name_room();
            icon = {at.x + 2, at.y + 1, 16, 16};
            label = {at.x + 20, at.y + 1, (whole < room ? whole : room) + 4, 16};
            return;
        }
        icon = {at.x + 22, at.y, 32, 35};
        char lines[2][48];
        split_name(names_[i], lines);
        const int w0 = gfx::Canvas::text_width(lines[0]), w1 = gfx::Canvas::text_width(lines[1]);
        const int tw = w0 > w1 ? w0 : w1;
        label = {at.x + 38 - tw / 2 - 1, at.y + 35, tw + 2, lines[1][0] != 0 ? 25 : 13};
    }
    void invalidate_item(int i)
    {
        if (i >= 0 && i < count_) {
            invalidate_part(cell(i));
        }
    }

    // The grid. `inner` starts at the view's area, two pixels in.
    void paint_icons(gfx::Canvas &inner)
    {
        const int cols = columns();
        for (int i = first_ * cols; i < count_; i++) {
            const int row = i / cols - first_, col = i % cols;
            if (row > visible_lines()) {
                break;
            }
            const int x = 10 + col * kCellW, y = 8 + row * kCellH;
            const gfx::Bitmap *icon = icons_[i];
            const bool chosen = picked_.get(i) || i == highlight_;
            if (chosen) {
                inner.blit_selected(*icon, x + 22, y, gfx::rgb(0, 0, 2));
            } else {
                inner.blit(*icon, x + 22, y);
            }
            // The name under it, on two lines if it must (broken at a space
            // when there is one), cut with "..." beyond that.
            char lines[2][48];
            split_name(names_[i], lines);
            for (int l = 0; l < 2 && lines[l][0] != 0; l++) {
                const int tw = gfx::Canvas::text_width(lines[l]);
                const int tx = x + 38 - tw / 2, ty = y + 36 + l * 12;
                if (chosen) {
                    inner.fill_rgb({tx - 1, ty - 1, tw + 2, 13}, gfx::theme().select);
                }
                inner.text(tx, ty, lines[l], chosen ? gfx::theme().select_ink : gfx::ink());
            }
        }
    }
    // The List and the Details: a small picture and the name on a line,
    // and in the Details the other columns after it, under their titles.
    void paint_lines(gfx::Canvas &inner, const gfx::Rect &area)
    {
        const bool details = mode_ == ViewMode::Details;
        char text[80];
        for (int i = details ? first_ : first_ * list_rows_; i < count_; i++) {
            const gfx::Rect at = cell(i);
            if (at.x >= area.x + area.w || at.y >= area.y + area.h) {
                break;
            }
            gfx::Rect icon, label;
            parts(i, icon, label);
            const bool chosen = picked_.get(i) || i == highlight_;
            if (small_ != nullptr && small_[i] != nullptr) {
                if (chosen) {
                    inner.blit_selected(*small_[i], icon.x - area.x, icon.y - area.y, gfx::rgb(0, 0, 2));
                } else {
                    inner.blit(*small_[i], icon.x - area.x, icon.y - area.y);
                }
            }
            fit(names_[i], name_room(), text);
            if (chosen) {
                inner.fill_rgb({label.x - area.x, label.y - area.y, label.w, label.h}, gfx::theme().select);
            }
            inner.text(label.x - area.x + 2, label.y - area.y + 2, text,
                       chosen ? gfx::theme().select_ink : gfx::ink());
            int x = name_width();
            for (int k = 1; details && k < column_count_; x += columns_[k].width, k++) {
                const Column &col = columns_[k];
                if (col.cells == nullptr || col.cells[i] == nullptr) {
                    continue;
                }
                const int tw = fit(col.cells[i], col.width - 12, text);
                inner.text(col.right ? x + col.width - 6 - tw : x + 6, at.y - area.y + 3, text, gfx::ink());
            }
        }
        if (!details) {
            return;
        }
        // The titles, over whatever has scrolled under them: a raised strip
        // each, and one more for what is left of the width.
        int x = 0;
        for (int k = 0; k < column_count_; k++) {
            const int w = k == 0 ? name_width() : columns_[k].width;
            inner.button({x, 0, w, kHeader}, gfx::ButtonLook::Raised);
            const int tw = fit(columns_[k].title, w - 12, text);
            inner.text(columns_[k].right ? x + w - 6 - tw : x + 6, 3, text, gfx::ink());
            x += w;
        }
        if (x < area.w) {
            inner.button({x, 0, area.w - x, kHeader}, gfx::ButtonLook::Raised);
        }
    }
    // Scrolls so that an entry is in view.
    void show(int i)
    {
        if (i < 0) {
            return;
        }
        const int line = mode_ == ViewMode::List ? i / list_rows_ : (mode_ == ViewMode::Details ? i : i / columns());
        const int visible = visible_lines();
        const int top = line < first_ ? line : (line >= first_ + visible ? line - visible + 1 : first_);
        if (top != first_) {
            first_ = top;
            layout();
        }
    }
    // What is picked becomes `next`, and `lead` the one picked last: only
    // the entries that changed are painted again, and the program is told.
    void set_picked(const Bits &next, int lead)
    {
        const bool same = next == picked_ && lead == selected_;
        for (int i = 0; i < count_ && i < kMaxPicked; i++) {
            if (next.get(i) != picked_.get(i)) {
                invalidate_item(i);
            }
        }
        picked_ = next;
        selected_ = lead;
        if (!same && on_select_ != nullptr) {
            on_select_(*this, context_);
        }
    }
    // From where the picking started to `to`, in the order shown; `add`:
    // on top of what is picked already (Ctrl+Shift).
    void stretch(int to, bool add)
    {
        Bits next = add ? picked_ : Bits{};
        const int from = anchor_ < to ? anchor_ : to, last = anchor_ < to ? to : anchor_;
        for (int i = from; i <= last; i++) {
            next.set(i, true);
        }
        set_picked(next, to);
    }

    // The rectangle: pulled from (x0, y0) to (x1, y1), which are places in
    // the whole of what is shown, so that the wheel can turn while it is
    // pulled.
    int clamp_x(int x) const
    {
        const gfx::Rect a = view_area();
        return x < a.x ? a.x : (x >= a.x + a.w ? a.x + a.w - 1 : x);
    }
    int clamp_y(int y) const
    {
        const gfx::Rect a = view_area();
        const int top = a.y + (mode_ == ViewMode::Details ? kHeader : 0);
        return y < top ? top : (y >= a.y + a.h ? a.y + a.h - 1 : y);
    }
    gfx::Rect band_rect() const // widget coordinates
    {
        const int x = band_x0_ < band_x1_ ? band_x0_ : band_x1_, y = band_y0_ < band_y1_ ? band_y0_ : band_y1_;
        const int w = band_x0_ < band_x1_ ? band_x1_ - band_x0_ : band_x0_ - band_x1_;
        const int h = band_y0_ < band_y1_ ? band_y1_ - band_y0_ : band_y0_ - band_y1_;
        return {x - scroll_x(), y - scroll_y(), w + 1, h + 1};
    }
    void outline_dirty(const gfx::Rect &r)
    {
        invalidate_part({r.x, r.y, r.w, 1});
        invalidate_part({r.x, r.y + r.h - 1, r.w, 1});
        invalidate_part({r.x, r.y, 1, r.h});
        invalidate_part({r.x + r.w - 1, r.y, 1, r.h});
    }
    void pull_band(int x, int y)
    {
        outline_dirty(band_rect());
        band_x1_ = clamp_x(x) + scroll_x();
        band_y1_ = clamp_y(y) + scroll_y();
        const gfx::Rect band = band_rect();
        outline_dirty(band);
        // What it touches, by picture or by name, on top of what was there:
        // of the grid only the rows it crosses are looked at.
        Bits next = band_base_;
        int from = 0, to = count_;
        if (mode_ == ViewMode::Icons) {
            const int cols = columns();
            const int first = (band.y - 10) / kCellH + first_, last = (band.y + band.h - 10) / kCellH + first_;
            from = (first < 0 ? 0 : first) * cols;
            to = (last + 1) * cols < count_ ? (last + 1) * cols : count_;
        }
        int lead = -1;
        for (int i = from; i < to; i++) {
            gfx::Rect icon, label;
            parts(i, icon, label);
            if (band.overlaps(icon) || band.overlaps(label)) {
                next.set(i, true);
                lead = i;
            }
        }
        if (!(next == picked_)) {
            anchor_ = lead >= 0 ? lead : next.first();
            set_picked(next, anchor_);
        }
    }

    const char *const *names_ = nullptr;
    const gfx::Bitmap *const *icons_ = nullptr;
    const gfx::Bitmap *const *small_ = nullptr; // the List's and the Details'
    const Column *columns_ = nullptr;           // the Details'; the first is the name's
    int column_count_ = 0;
    int count_ = 0;
    ViewMode mode_ = ViewMode::Icons;
    int widest_ = 0;    // the longest name, in pixels
    int list_w_ = 80;   // the List: a column's width
    int list_rows_ = 1; // and how many entries go down one
    Callback on_open_, on_select_, on_drag_;
    void *context_;
    bool pressed_ = false; // a picked entry is held: moved, it is dragged
    int press_x_ = 0, press_y_ = 0;
    int collapse_ = -1;  // held without Ctrl among several: it alone when let go
    Bits picked_;
    int selected_ = -1;  // the one picked last
    int anchor_ = -1;    // where Shift stretches from
    int highlight_ = -1; // shown as picked for now (a dragged file is over it)
    bool band_ = false;  // a rectangle is being pulled
    int band_x0_ = 0, band_y0_ = 0, band_x1_ = 0, band_y1_ = 0;
    Bits band_base_;     // what was picked when it started (Ctrl held)
    int first_ = 0;      // the first row, column (List) or line (Details) in view
    bool in_bar_ = false;
    wm::ScrollBar bar_, across_;
};


// The text editor with its own memory for the text and for undo.
class OwnedTextEdit : public wm::TextEdit {
public:
    OwnedTextEdit(const gfx::Rect &r, size_t text_bytes, size_t undo_bytes) : TextEdit(r)
    {
        text_ = static_cast<uint8_t *>(os::allocate(text_bytes));
        undo_ = static_cast<uint8_t *>(os::allocate(undo_bytes));
        if (text_ != nullptr && undo_ != nullptr) {
            attach(text_, text_bytes, undo_, undo_bytes);
        }
    }
    ~OwnedTextEdit()
    {
        os::release(text_);
        os::release(undo_);
    }

private:
    uint8_t *text_ = nullptr, *undo_ = nullptr;
};

// The frame for a client area of this size.
gfx::Rect frame_for(const Look &look)
{
    const bool client = (look.flags & kClientSize) != 0;
    const int bar = (look.flags & kMenuBar) != 0 ? wm::Window::kMenuBarHeight : 0;
    const int w = look.width + (client ? 2 * wm::Window::kBorder : 0);
    const int h = look.height + (client ? wm::Window::kClientTop + bar + wm::Window::kBorder : 0);
    return look.x >= 0 && look.y >= 0 ? gfx::Rect{look.x, look.y, w, h} : os::centred(w, h);
}

class Behind : public wm::Window {
public:
    Behind(int slot, const Look &look, Events events, void *state)
        : Window(look.title, frame_for(look), look.icon), slot_(slot), events_(events),
          state_(state), flags_(look.flags), centred_(look.x < 0 || look.y < 0)
    {
        if ((flags_ & kDialog) != 0) {
            set_dialog(true);
        } else if ((flags_ & kFixedSize) != 0) {
            set_resizable(false);
        }
        set_id(installed_kit().wm->new_id());
        if ((flags_ & kWholeDesktop) != 0) {
            set_modal(nullptr, true);
        } else if (look.over != kNoWindow) {
            // A dialog over another window, whoever's it is; if that one
            // has closed meanwhile, over nothing.
            if (wm::Window *owner = installed_kit().wm->window(look.over)) {
                set_modal(owner);
            }
        }
    }

    ~Behind()
    {
        closed(); // one that never reached the screen: the program still hears that it is gone
        for (int i = 0; i < count_; i++) {
            switch (kinds_[i]) {
            case Kind::Label: static_cast<wm::Label *>(controls_[i])->~Label(); break;
            case Kind::Plain: static_cast<PlainText *>(controls_[i])->~PlainText(); break;
            case Kind::Button: static_cast<wm::Button *>(controls_[i])->~Button(); break;
            case Kind::CheckBox: static_cast<wm::CheckBox *>(controls_[i])->~CheckBox(); break;
            case Kind::Radio: static_cast<wm::RadioButton *>(controls_[i])->~RadioButton(); break;
            case Kind::TextField: static_cast<wm::TextField *>(controls_[i])->~TextField(); break;
            case Kind::ScrollBar: static_cast<wm::ScrollBar *>(controls_[i])->~ScrollBar(); break;
            case Kind::List: static_cast<wm::ListBox *>(controls_[i])->~ListBox(); break;
            case Kind::Group: static_cast<wm::GroupFrame *>(controls_[i])->~GroupFrame(); break;
            case Kind::TextArea: static_cast<wm::TextArea *>(controls_[i])->~TextArea(); break;
            case Kind::Picture: static_cast<Picture *>(controls_[i])->~Picture(); break;
            case Kind::Grid: static_cast<Grid *>(controls_[i])->~Grid(); break;
            case Kind::DropDown: static_cast<DropDown *>(controls_[i])->~DropDown(); break;
            case Kind::Meter: static_cast<Meter *>(controls_[i])->~Meter(); break;
            case Kind::TextEdit: static_cast<OwnedTextEdit *>(controls_[i])->~OwnedTextEdit(); break;
            case Kind::IconView: static_cast<IconView *>(controls_[i])->~IconView(); break;
            case Kind::Tabs: static_cast<wm::Tabs *>(controls_[i])->~Tabs(); break;
            }
            os::release(controls_[i]);
        }
        g_windows[slot_] = nullptr;
        forget_spare_slots();
    }

    ui::Window handle() const { return id(); }
    bool gone() const { return gone_; }
    void *state() const { return state_; }

    template <class T, class... Args> Control make(Kind kind, Args &&...args)
    {
        if (count_ == kMaxWidgets) {
            return kNoControl;
        }
        void *room = os::allocate(sizeof(T));
        if (room == nullptr) {
            return kNoControl;
        }
        T *control = new (room) T(static_cast<Args &&>(args)...);
        controls_[count_] = control;
        kinds_[count_] = kind;
        add(*control);
        return ++count_;
    }

    // The control with this number, if it is of this kind.
    template <class T> T *as(Control control, Kind kind) const
    {
        const int at = control - 1;
        return at >= 0 && at < count_ && kinds_[at] == kind ? static_cast<T *>(controls_[at]) : nullptr;
    }
    wm::Button *button(Control control) const { return as<wm::Button>(control, Kind::Button); }
    wm::Widget *any(Control control) const
    {
        return control >= 1 && control <= count_ ? controls_[control - 1] : nullptr;
    }
    Kind kind(Control control) const { return kinds_[control - 1]; }
    // The pictures of the rows of the window's list that has them.
    void set_row_icons(const gfx::Bitmap *const *icons) { row_icons_ = icons; }
    static const gfx::Bitmap *row_icon(int index, void *context)
    {
        const auto *w = static_cast<Behind *>(context);
        return w->row_icons_ != nullptr ? w->row_icons_[index] : nullptr;
    }
    int *radio_group(int group) { return group >= 0 && group < kGroups ? &groups_[group] : nullptr; }

    // A control says something happened: which one, as an event.
    bool from(const wm::Widget &control, What what)
    {
        for (int i = 0; i < count_; i++) {
            if (controls_[i] == &control) {
                Event e{what};
                e.control = i + 1;
                return tell(e);
            }
        }
        return false;
    }
    static void pressed(wm::Button &b, void *context) { static_cast<Behind *>(context)->from(b, What::Command); }
    static void ticked(wm::CheckBox &b, void *context) { static_cast<Behind *>(context)->from(b, What::Changed); }
    static void chosen_one(wm::RadioButton &b, void *context) { static_cast<Behind *>(context)->from(b, What::Changed); }
    static void scrolled(wm::ScrollBar &b, void *context) { static_cast<Behind *>(context)->from(b, What::Changed); }
    static void picked(wm::ListBox &l, void *context) { static_cast<Behind *>(context)->from(l, What::Changed); }
    static void turned(wm::Tabs &t, void *context) { static_cast<Behind *>(context)->from(t, What::Changed); }
    // Enter on a row, or a double click. A program that has no use for it
    // gets what Enter does anywhere else in the window: the default button.
    static void chosen(wm::ListBox &l, void *context)
    {
        auto *w = static_cast<Behind *>(context);
        if (!w->from(l, What::Chosen) && w->default_widget() != nullptr) {
            w->default_widget()->press();
        }
    }

    // A control of the program's own painting: the event, with a drawing.
    void paint_control(const wm::Widget &control, gfx::Canvas &c, What what, int index, const gfx::Rect &part)
    {
        for (int i = 0; i < count_; i++) {
            if (controls_[i] == &control) {
                Event e{what};
                e.control = i + 1;
                e.index = index;
                e.part = part;
                e.drawing = g_drawings.add(c);
                tell(e);
                g_drawings.end();
                if (prog::runner().late(owner())) {
                    // Not responding: grey where it would have painted.
                    c.fill(what == What::Cell ? part : gfx::Rect{0, 0, control.rect.w, control.rect.h}, gfx::face());
                }
            }
        }
    }
    static void paint_picture(Picture &p, gfx::Canvas &c, void *context)
    {
        static_cast<Behind *>(context)->paint_control(p, c, What::Paint, 0, {});
    }
    static void picture_mouse(Picture &p, const wm::MouseMsg &m, void *context)
    {
        auto *w = static_cast<Behind *>(context);
        for (int i = 0; i < w->count_; i++) {
            if (w->controls_[i] == &p) {
                Event e{What::Mouse};
                e.control = i + 1;
                e.mouse = &m;
                w->tell(e);
            }
        }
    }
    static bool picture_key(Picture &p, const wm::KeyMsg *k, const char *text, void *context)
    {
        auto *w = static_cast<Behind *>(context);
        for (int i = 0; i < w->count_; i++) {
            if (w->controls_[i] == &p) {
                Event e{k != nullptr ? What::Key : What::Text};
                e.control = i + 1;
                e.key = k;
                e.text = text;
                return w->tell(e);
            }
        }
        return false;
    }
    // The right button: to the topmost picture under it that takes the
    // mouse, or icon view (the program asks item_at() what is there).
    void right_click(int x, int y) override
    {
        for (int i = count_ - 1; i >= 0; i--) {
            if (((kinds_[i] == Kind::Picture && controls_[i]->wants_mouse()) || kinds_[i] == Kind::IconView) &&
                controls_[i]->rect.contains(x, y)) {
                Event e{What::RightClick};
                e.control = i + 1;
                e.x = x - controls_[i]->rect.x;
                e.y = y - controls_[i]->rect.y;
                tell(e);
                return;
            }
        }
    }
    void resize_client(int w, int h)
    {
        const int fw = w + 2 * kBorder;
        const int fh = h + kClientTop + ((flags_ & kMenuBar) != 0 ? kMenuBarHeight : 0) + kBorder;
        if (!open()) {
            set_frame(centred_ ? os::centred(fw, fh) : gfx::Rect{frame().x, frame().y, fw, fh});
            return;
        }
        const gfx::Rect area = os::work_area();
        int x = frame().x, y = frame().y;
        x = x + fw > area.w ? area.w - fw : x;
        y = y + fh > area.h ? area.h - fh : y;
        os::move(*this, {x < 0 ? 0 : x, y < 0 ? 0 : y, fw, fh});
    }
    static void paint_cell(Grid &g, gfx::Canvas &c, const gfx::Rect &cell, int index, void *context)
    {
        static_cast<Behind *>(context)->paint_control(g, c, What::Cell, index, cell);
    }
    static void arrow_pressed(DropDown &d, void *context) { static_cast<Behind *>(context)->from(d, What::Command); }
    // The number of the control the keys go to.
    Control focus_number() const
    {
        for (int i = 0; i < count_; i++) {
            if (controls_[i] == focus()) {
                return i + 1;
            }
        }
        return kNoControl;
    }
    static void cell_picked(Grid &g, void *context) { static_cast<Behind *>(context)->from(g, What::Changed); }

    bool layered() const override { return (flags_ & kPicture) != 0 || Window::layered(); }
    void paint(gfx::Canvas &c) override
    {
        tell(Event{What::Painting});
        Window::paint(c);
        if ((flags_ & kPicture) != 0) {
            Event e{What::Paint};
            e.drawing = g_drawings.add(c);
            tell(e);
            g_drawings.end();
        }
    }
    bool key(const wm::KeyMsg &k) override
    {
        Event e{What::Key};
        e.key = &k;
        return tell(e) || Window::key(k);
    }
    void resized() override
    {
        Event e{What::Resized};
        e.x = client().w;
        e.y = client().h;
        tell(e);
    }
    void mouse_moved(int dx, int dy, uint8_t buttons) override
    {
        Event e{What::MouseMoved};
        e.x = dx;
        e.y = dy;
        e.index = buttons;
        tell(e);
    }
    bool close_requested() override { return !tell(Event{What::Closing}); }
    static void icon_opened(IconView &v, void *context) { static_cast<Behind *>(context)->from(v, What::Chosen); }
    static void icon_picked(IconView &v, void *context) { static_cast<Behind *>(context)->from(v, What::Changed); }
    static void icon_dragged(IconView &v, void *context) { static_cast<Behind *>(context)->from(v, What::Drag); }
    bool drag_over(const char *path, int x, int y) override
    {
        Event e{What::DragOver};
        e.text = path;
        e.x = x;
        e.y = y;
        return tell(e);
    }
    void dropped(const char *path, int x, int y) override
    {
        Event e{What::Dropped};
        e.text = path;
        e.x = x;
        e.y = y;
        tell(e);
    }
    // The editor's text changed between modified and not.
    static void edited(wm::TextEdit &edit, void *context)
    {
        auto *w = static_cast<Behind *>(context);
        if (edit.modified() != w->edit_modified_) {
            w->edit_modified_ = edit.modified();
            w->from(edit, What::Changed);
        }
    }
    void note_modified(bool modified) { edit_modified_ = modified; }
    // The last control made, taken back (it could not get what it needs).
    template <class T> void unmake()
    {
        count_--;
        remove_last();
        static_cast<T *>(controls_[count_])->~T();
        os::release(controls_[count_]);
        controls_[count_] = nullptr;
    }
    void menu_opening(int entry) override
    {
        Event e{What::Menu};
        e.index = entry;
        tell(e);
    }
    bool has_menu_room() const { return (flags_ & kMenuBar) != 0; }
    void closed() override
    {
        if (!told_closed_) {
            told_closed_ = true;
            tell(Event{What::Closed});
            gone_ = true;
        }
    }

private:
    // Nothing reaches a program whose object has gone.
    // Every event goes to the program through here: on its own task, if it
    // has one (core/prog), while this one waits.
    bool tell(const Event &e)
    {
        if (gone_) {
            return false;
        }
        if (owner() == proc::kNone) {
            return events_(handle(), e, state_);
        }
        // Handed over whole, by copy: a program that does not come back in
        // time is not left with pointers into this task's stack.
        Told told;
        told.window = this;
        told.event = e;
        if (e.key != nullptr) {
            told.key = *e.key;
        }
        if (e.mouse != nullptr) {
            told.mouse = *e.mouse;
        }
        if (e.text != nullptr) {
            snprintf(told.text, sizeof told.text, "%s", e.text);
        }
        return proc::call_as(&proc::table(), owner(), told) && told.used;
    }

    struct Told {
        Behind *window;
        Event event;
        wm::KeyMsg key;
        wm::MouseMsg mouse;
        char text[fs::kMaxPath + 1];
        bool used;
        void operator()()
        {
            Event e = event;
            e.key = event.key != nullptr ? &key : nullptr;
            e.mouse = event.mouse != nullptr ? &mouse : nullptr;
            e.text = event.text != nullptr ? text : nullptr;
            used = window->events_(window->handle(), e, window->state_);
        }
    };

    int slot_;
    Events events_;
    void *state_;
    uint8_t flags_;
    bool centred_;
    bool edit_modified_ = false; // as the program was last told (one editor a window)
    bool told_closed_ = false;
    bool gone_ = false; // the program has heard Closed: its object is destroyed
    const gfx::Bitmap *const *row_icons_ = nullptr;
    static constexpr int kGroups = 4;
    int groups_[kGroups] = {};
    wm::Widget *controls_[kMaxWidgets] = {};
    Kind kinds_[kMaxWidgets] = {};
    int count_ = 0;
};

Behind *behind(Window window)
{
    for (int i = 0; i < g_window_room; i++) {
        Behind *w = g_windows[i];
        // One that has closed is dead to the program at once, though its
        // memory goes back a moment later: the program's object went with
        // the Closed event.
        if (w != nullptr && window != kNoWindow && w->id() == window && !w->gone()) {
            return w;
        }
    }
    return nullptr;
}

// The window manager's safe point after the window closed: the object, the
// controls and the program's state go back.
void destroy(wm::Window &w)
{
    Behind *window = static_cast<Behind *>(&w);
    window->~Behind();
    os::release(window);
}

constexpr size_t kAligned = (sizeof(Behind) + 15) & ~static_cast<size_t>(15);

} // namespace

namespace {
template <class T> T *control_of(Window window, Control control, Kind kind)
{
    Behind *w = behind(window);
    return w != nullptr ? w->as<T>(control, kind) : nullptr;
}
} // namespace

Window create(const Look &look, Events events, size_t state_bytes)
{
    const prog::InCall in_call;
    const int slot = events != nullptr ? free_window_slot() : -1;
    void *room = slot >= 0 && events != nullptr ? os::allocate(kAligned + state_bytes) : nullptr;
    if (room == nullptr) {
        return kNoWindow;
    }
    Behind *w = new (room) Behind(slot, look, events, static_cast<uint8_t *>(room) + kAligned);
    w->set_dispose(&destroy);
    g_windows[slot] = w;
    return w->handle();
}

void *state(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->state() : nullptr;
}

bool alive(Window window)
{
    const prog::InCall in_call;
    return behind(window) != nullptr;
}

bool on_screen(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr && w->open();
}

void redraw(Window window, const gfx::Rect &part)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->invalidate_client(part);
    }
}

Control plain(Window window, const gfx::Rect &place)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<PlainText>(Kind::Plain, place) : kNoControl;
}

Control label(Window window, const gfx::Rect &place, const char *text)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Label>(Kind::Label, place, text) : kNoControl;
}

Control button(Window window, const gfx::Rect &place, const char *text)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Button>(Kind::Button, place, text, &Behind::pressed, w) : kNoControl;
}

Control check_box(Window window, const gfx::Rect &place, const char *text, bool checked)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::CheckBox>(Kind::CheckBox, place, text, checked, &Behind::ticked, w) : kNoControl;
}

Control radio(Window window, const gfx::Rect &place, const char *text, int group, int value)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    int *chosen = w != nullptr ? w->radio_group(group) : nullptr;
    return chosen != nullptr ? w->make<wm::RadioButton>(Kind::Radio, place, text, chosen, value, &Behind::chosen_one, w)
                             : kNoControl;
}

Control text_field(Window window, const gfx::Rect &place, const char *initial)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::TextField>(Kind::TextField, place, initial) : kNoControl;
}

Control scroll_bar(Window window, const gfx::Rect &place, bool vertical)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::ScrollBar>(Kind::ScrollBar, place, vertical, &Behind::scrolled, w) : kNoControl;
}

Control list(Window window, const gfx::Rect &place, const char *const *items, int count)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    const Control c = w != nullptr ? w->make<wm::ListBox>(Kind::List, place, items, count, &Behind::picked, w) : kNoControl;
    if (c != kNoControl) {
        w->as<wm::ListBox>(c, Kind::List)->set_on_activate(&Behind::chosen);
    }
    return c;
}

Control group(Window window, const gfx::Rect &place, const char *caption)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::GroupFrame>(Kind::Group, place, caption) : kNoControl;
}

Control tabs(Window window, const gfx::Rect &place, const char *const *titles, int count)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Tabs>(Kind::Tabs, place, titles, count, &Behind::turned, w) : kNoControl;
}

Control text_area(Window window, const gfx::Rect &place, const char *text)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::TextArea>(Kind::TextArea, place, text) : kNoControl;
}

Control picture(Window window, const gfx::Rect &place, uint8_t how)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<Picture>(Kind::Picture, place, how, &Behind::paint_picture, &Behind::picture_mouse,
                                           &Behind::picture_key, w)
                        : kNoControl;
}

Control icon_view(Window window, const gfx::Rect &place)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<IconView>(Kind::IconView, place, &Behind::icon_opened, &Behind::icon_picked,
                                            &Behind::icon_dragged, w)
                        : kNoControl;
}

void set_view_mode(Window window, Control control, ViewMode mode)
{
    const prog::InCall in_call;
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->set_mode(mode);
    }
}

void set_small_icons(Window window, Control control, const gfx::Bitmap *const *icons)
{
    const prog::InCall in_call;
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->set_small_icons(icons);
    }
}

void set_columns(Window window, Control control, const Column *columns, int count)
{
    const prog::InCall in_call;
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->set_columns(columns, count);
    }
}

void reset_view(Window window, Control control)
{
    const prog::InCall in_call;
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->reset();
    }
}

int item_at(Window window, Control control, int x, int y)
{
    const prog::InCall in_call;
    auto *v = control_of<IconView>(window, control, Kind::IconView);
    return v != nullptr ? v->item_at(x, y) : -1;
}

int selected_count(Window window, Control control)
{
    const prog::InCall in_call;
    auto *v = control_of<IconView>(window, control, Kind::IconView);
    return v != nullptr ? v->picked_count() : 0;
}

bool is_selected(Window window, Control control, int index)
{
    const prog::InCall in_call;
    auto *v = control_of<IconView>(window, control, Kind::IconView);
    return v != nullptr && v->picked(index);
}

void highlight(Window window, Control control, int index)
{
    const prog::InCall in_call;
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->highlight(index);
    }
}

Control text_edit(Window window, const gfx::Rect &place, size_t text_bytes, size_t undo_bytes)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    const Control c = w != nullptr ? w->make<OwnedTextEdit>(Kind::TextEdit, place, text_bytes, undo_bytes) : kNoControl;
    if (c == kNoControl) {
        return kNoControl;
    }
    auto *e = w->as<OwnedTextEdit>(c, Kind::TextEdit);
    if (!e->attached()) {
        w->unmake<OwnedTextEdit>(); // no memory for the text
        return kNoControl;
    }
    e->set_on_change(&Behind::edited, w);
    e->layout();
    return c;
}

void edit(Window window, Control control, Edit what)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    auto *e = w != nullptr ? w->as<OwnedTextEdit>(control, Kind::TextEdit) : nullptr;
    if (e == nullptr) {
        return;
    }
    switch (what) {
    case Edit::Clear:
        e->clear();
        w->note_modified(e->modified()); // a new text: the program is not told of that as a change
        break;
    case Edit::Undo: e->undo(); break;
    case Edit::Cut: e->cut(); break;
    case Edit::Copy: e->copy(); break;
    case Edit::Paste: e->paste(); break;
    case Edit::Delete: e->delete_selection(); break;
    case Edit::SelectAll: e->select_all(); break;
    }
}

uint8_t edit_state(Window window, Control control)
{
    const prog::InCall in_call;
    auto *e = control_of<OwnedTextEdit>(window, control, Kind::TextEdit);
    return e == nullptr ? 0
                        : static_cast<uint8_t>((e->modified() ? kModified : 0) | (e->can_undo() ? kCanUndo : 0) |
                                               (e->has_selection() ? kHasSelection : 0));
}

void set_modified(Window window, Control control, bool modified)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (auto *e = w != nullptr ? w->as<OwnedTextEdit>(control, Kind::TextEdit) : nullptr) {
        e->set_modified(modified);
        w->note_modified(modified);
    }
}

void load_begin(Window window, Control control)
{
    const prog::InCall in_call;
    if (auto *e = control_of<OwnedTextEdit>(window, control, Kind::TextEdit)) {
        e->load_begin();
    }
}

bool load_append(Window window, Control control, const uint8_t *bytes, size_t size)
{
    const prog::InCall in_call;
    auto *e = control_of<OwnedTextEdit>(window, control, Kind::TextEdit);
    return e != nullptr && e->load_append(bytes, size);
}

void load_finish(Window window, Control control)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (auto *e = w != nullptr ? w->as<OwnedTextEdit>(control, Kind::TextEdit) : nullptr) {
        e->load_finish();
        w->note_modified(e->modified());
    }
}

TextReader text_reader(Window window, Control control)
{
    const prog::InCall in_call;
    return control_of<OwnedTextEdit>(window, control, Kind::TextEdit);
}

size_t read_text(TextReader reader, size_t offset, uint8_t *out, size_t room)
{
    const prog::InCall in_call;
    return reader != nullptr ? static_cast<const OwnedTextEdit *>(reader)->save_bytes(offset, out, room) : 0;
}

size_t clipboard_size()
{
    const prog::InCall in_call;
    return wm::clipboard::size();
}

void set_locked(Window window, bool locked)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->set_locked(locked);
    }
}

bool locked(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr && w->locked();
}

void set_hourglass(Window window, bool on)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->set_hourglass(on);
    }
}

bool hourglass(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr && w->hourglass();
}

void set_caret(Window window, Control control, const gfx::Rect &caret)
{
    const prog::InCall in_call;
    if (auto *p = control_of<Picture>(window, control, Kind::Picture)) {
        p->set_caret(caret);
    }
}

bool control_has_keys(Window window, Control control)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    const wm::Widget *c = w != nullptr ? w->any(control) : nullptr;
    return c != nullptr && c->has_focus();
}

bool caret_lit(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr && (w->manager() == nullptr || w->manager()->caret_visible());
}

void place_control(Window window, Control control, const gfx::Rect &place)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        c->invalidate(); // where it was
        c->rect = place;
        if (w->kind(control) == Kind::List) {
            static_cast<wm::ListBox *>(c)->layout(); // its scroll bar, and what of it shows
        } else if (w->kind(control) == Kind::TextEdit) {
            static_cast<OwnedTextEdit *>(c)->layout();
        } else if (w->kind(control) == Kind::IconView) {
            static_cast<IconView *>(c)->layout();
        }
        c->invalidate();
    }
}

gfx::Rect client_size(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? gfx::Rect{0, 0, w->client().w, w->client().h} : gfx::Rect{0, 0, 0, 0};
}

void resize(Window window, int client_width, int client_height)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->resize_client(client_width, client_height);
    }
}

void redraw_control(Window window, Control control)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        c->invalidate();
    }
}

void redraw_control_part(Window window, Control control, const gfx::Rect &part)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        c->invalidate_part(part);
    }
}

Control grid(Window window, const gfx::Rect &place, int columns, int count, int cell_w, int cell_h, int margin)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<Grid>(Kind::Grid, place, columns, count, cell_w, cell_h, margin, &Behind::paint_cell,
                                        &Behind::cell_picked, w)
                        : kNoControl;
}

Control drop_down(Window window, const gfx::Bitmap *icon, const gfx::Rect &place)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<DropDown>(Kind::DropDown, place, icon, &Behind::arrow_pressed, w) : kNoControl;
}

void menu_under(Window window, Control control, wm::Menu &menu)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (const wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        const gfx::Rect client = w->client();
        os::open_menu(menu, client.x + c->rect.x, client.y + c->rect.bottom());
    }
}

void menu_at(Window window, Control control, int x, int y, wm::Menu &menu)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (const wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        const gfx::Rect client = w->client();
        os::open_menu(menu, client.x + c->rect.x + x, client.y + c->rect.y + y);
    }
}

Control meter(Window window, const gfx::Rect &place, gfx::Colour colour)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->make<Meter>(Kind::Meter, place, colour) : kNoControl;
}

void set_level(Window window, Control control, uint32_t value, uint32_t total)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (Meter *m = w != nullptr ? w->as<Meter>(control, Kind::Meter) : nullptr) {
        m->set(value, total);
    }
}

bool in_front(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr && os::in_front(*w);
}

bool has_dialog(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (w == nullptr || os::blocker(*w) == nullptr) {
        return false;
    }
    os::bring_forward(window);
    return true;
}

bool take_screen(Window window, Screen &out, int coarse)
{
    const prog::InCall in_call;
    out = {};
    Behind *w = behind(window);
    wm::Wm *wm = installed_kit().wm;
    if (w == nullptr || wm == nullptr) {
        return false;
    }
    out.pixels = wm->lend_screen(*w, coarse);
    if (out.pixels == nullptr) {
        return false;
    }
    out.width = wm->lent_width();
    out.height = wm->lent_height();
    return true;
}

bool has_screen(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    wm::Wm *wm = installed_kit().wm;
    return w != nullptr && wm != nullptr && wm->screen_lent_to() == w;
}

void give_screen_back(Window window)
{
    const prog::InCall in_call;
    if (has_screen(window)) {
        installed_kit().wm->return_screen();
    }
}

void screen_changed(Window window, const gfx::Rect &part)
{
    const prog::InCall in_call;
    if (has_screen(window)) {
        installed_kit().wm->screen_saver_drew(part);
    }
}

bool has_keys(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr && w->wm::Window::focused() && w->wm::Window::state() != wm::WindowState::Minimized;
}

void set_menu_bar(Window window, const wm::MenuBar *bar)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (w != nullptr && w->has_menu_room()) {
        w->set_menu_bar(bar);
    }
}

void set_title(Window window, const char *title)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->set_title(title);
    }
}

void set_document(Window window, const char *path)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->set_document(path);
    }
}

void set_icon(Window window, const gfx::Bitmap *icon)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        w->set_icon(icon);
    }
}

Control focus(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    return w != nullptr ? w->focus_number() : kNoControl;
}

void set_text(Window window, Control control, const char *text)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (w == nullptr || w->any(control) == nullptr) {
        return;
    }
    switch (w->kind(control)) {
    case Kind::Label: w->as<wm::Label>(control, Kind::Label)->set_text(text); break;
    case Kind::Plain: w->as<PlainText>(control, Kind::Plain)->set(text); break;
    case Kind::Button: w->button(control)->set_label(text); break;
    case Kind::TextArea: w->as<wm::TextArea>(control, Kind::TextArea)->set_text(text); break;
    case Kind::TextField: w->as<wm::TextField>(control, Kind::TextField)->set(text); break;
    case Kind::DropDown: w->as<DropDown>(control, Kind::DropDown)->set_text(text); break;
    default: break;
    }
}

void get_text(Window window, Control control, char *out, int capacity)
{
    const prog::InCall in_call;
    if (capacity > 0) {
        out[0] = 0;
    }
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->get(out, capacity);
    }
}

void select_all(Window window, Control control)
{
    const prog::InCall in_call;
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->select_all();
    }
}

void set_hidden(Window window, Control control, bool hidden)
{
    const prog::InCall in_call;
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->set_hidden(hidden);
    }
}

void set_enabled(Window window, Control control, bool enabled)
{
    const prog::InCall in_call;
    if (auto *b = control_of<wm::Button>(window, control, Kind::Button)) {
        b->set_enabled(enabled);
    }
}

bool checked(Window window, Control control)
{
    const prog::InCall in_call;
    auto *b = control_of<wm::CheckBox>(window, control, Kind::CheckBox);
    return b != nullptr && b->checked();
}

void set_checked(Window window, Control control, bool on)
{
    const prog::InCall in_call;
    if (auto *b = control_of<wm::CheckBox>(window, control, Kind::CheckBox)) {
        b->set_checked(on);
    }
}

int radio_value(Window window, int group)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    const int *chosen = w != nullptr ? w->radio_group(group) : nullptr;
    return chosen != nullptr ? *chosen : 0;
}

void set_radio_value(Window window, int group, int value)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (int *chosen = w != nullptr ? w->radio_group(group) : nullptr) {
        *chosen = value;
        w->invalidate();
    }
}

void set_range(Window window, Control control, int maximum, int page)
{
    const prog::InCall in_call;
    if (auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar)) {
        b->set_range(maximum, page);
    }
}

int value(Window window, Control control)
{
    const prog::InCall in_call;
    auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar);
    return b != nullptr ? b->value() : 0;
}

void set_value(Window window, Control control, int value)
{
    const prog::InCall in_call;
    if (auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar)) {
        b->set_value_quietly(value);
    }
}

void set_items(Window window, Control control, const char *const *items, int count,
               const gfx::Bitmap *const *icons)
{
    if (auto *l = control_of<wm::ListBox>(window, control, Kind::List)) {
        if (icons != nullptr) {
            behind(window)->set_row_icons(icons);
            l->set_icons(&Behind::row_icon);
        }
        l->set_items(items, count);
    } else if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->set_items(items, count, icons);
    }
}

int selected(Window window, Control control)
{
    const prog::InCall in_call;
    if (auto *g = control_of<Grid>(window, control, Kind::Grid)) {
        return g->selected();
    }
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        return v->selected();
    }
    if (auto *t = control_of<wm::Tabs>(window, control, Kind::Tabs)) {
        return t->selected();
    }
    auto *l = control_of<wm::ListBox>(window, control, Kind::List);
    return l != nullptr ? l->selected() : -1;
}

void select(Window window, Control control, int index)
{
    const prog::InCall in_call;
    if (auto *l = control_of<wm::ListBox>(window, control, Kind::List)) {
        l->select(index);
    } else if (auto *g = control_of<Grid>(window, control, Kind::Grid)) {
        g->select_quietly(index);
    } else if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->select(index);
    } else if (auto *t = control_of<wm::Tabs>(window, control, Kind::Tabs)) {
        t->select(index);
    }
}

void set_focus(Window window, Control control)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        w->set_focus(c);
    }
}

void set_default(Window window, Control control)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (wm::Button *b = w != nullptr ? w->button(control) : nullptr) {
        b->set_default(true);
        w->set_default(b);
    }
}

void press(Window window, Control control)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (wm::Button *b = w != nullptr ? w->button(control) : nullptr) {
        b->click();
    }
}

bool show(Window window)
{
    const prog::InCall in_call;
    Behind *w = behind(window);
    if (w == nullptr) {
        return false;
    }
    os::show(*w);
    if (!w->open()) {
        installed_kit().wm->discard(*w); // the window manager is full
        return false;
    }
    return true;
}

void close(Window window)
{
    const prog::InCall in_call;
    if (Behind *w = behind(window)) {
        if (w->open()) {
            w->close();
        } else {
            w->closed(); // the program hears of it now, not when the memory goes back
            installed_kit().wm->discard(*w);
        }
    }
}

/* ---- Drawing ---- */

namespace draw {

Drawing part(Drawing on, const gfx::Rect &part)
{
    const prog::InCall in_call;
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? g_drawings.add(c->sub(part)) : kNoDrawing;
}

#define DRAW(call)                                                                                                     \
    if (gfx::Canvas *c = g_drawings.at(on)) {                                                                          \
        c->call;                                                                                                       \
    }

void fill(Drawing on, const gfx::Rect &r, gfx::Colour col)
{
    const prog::InCall in_call;
    DRAW(fill(r, col))
}
void hline(Drawing on, int x, int y, int w, gfx::Colour col)
{
    const prog::InCall in_call;
    DRAW(hline(x, y, w, col))
}
void vline(Drawing on, int x, int y, int h, gfx::Colour col)
{
    const prog::InCall in_call;
    DRAW(vline(x, y, h, col))
}
void fill_rgb(Drawing on, const gfx::Rect &r, gfx::Rgb col)
{
    const prog::InCall in_call;
    DRAW(fill_rgb(r, col))
}
void fill_pattern(Drawing on, const gfx::Rect &r, gfx::Rgb paper, gfx::Rgb ink, const uint8_t rows[8])
{
    const prog::InCall in_call;
    DRAW(fill_pattern(r, paper, ink, rows))
}
void fill_fade(Drawing on, const gfx::Rect &r, gfx::Rgb from, gfx::Rgb to, bool vertical, const gfx::Rect *span)
{
    const prog::InCall in_call;
    DRAW(fill_fade(r, from, to, vertical, span))
}
void focus_rect(Drawing on, const gfx::Rect &r)
{
    const prog::InCall in_call;
    DRAW(focus_rect(r))
}
void checker(Drawing on, const gfx::Rect &r)
{
    const prog::InCall in_call;
    DRAW(checker(r))
}
void edge(Drawing on, const gfx::Rect &r, gfx::Colour tl, gfx::Colour br)
{
    const prog::InCall in_call;
    DRAW(edge(r, tl, br))
}
void dent(Drawing on, const gfx::Rect &r)
{
    const prog::InCall in_call;
    DRAW(dent(r))
}
void raised(Drawing on, const gfx::Rect &r)
{
    const prog::InCall in_call;
    DRAW(raised(r))
}
void sunken(Drawing on, const gfx::Rect &r)
{
    const prog::InCall in_call;
    DRAW(sunken(r))
}
void button(Drawing on, const gfx::Rect &r, gfx::ButtonLook look)
{
    const prog::InCall in_call;
    DRAW(button(r, look))
}
void button_frame(Drawing on, const gfx::Rect &r, gfx::ButtonLook look)
{
    const prog::InCall in_call;
    DRAW(button_frame(r, look))
}
void blit(Drawing on, const gfx::Bitmap &b, int x, int y)
{
    const prog::InCall in_call;
    DRAW(blit(b, x, y))
}
void blit_selected(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour tint)
{
    const prog::InCall in_call;
    DRAW(blit_selected(b, x, y, tint))
}
void blit2x(Drawing on, const gfx::Bitmap &b, int x, int y)
{
    const prog::InCall in_call;
    DRAW(blit2x(b, x, y))
}
void blit_opaque(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour bg)
{
    const prog::InCall in_call;
    DRAW(blit_opaque(b, x, y, bg))
}
void pattern(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour col)
{
    const prog::InCall in_call;
    DRAW(pattern(x, y, rows, count, col))
}
void pattern_opaque(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour fg, gfx::Colour bg)
{
    const prog::InCall in_call;
    DRAW(pattern_opaque(x, y, rows, count, fg, bg))
}
void text_up(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold)
{
    const prog::InCall in_call;
    DRAW(text_up(x, y, s, col, bold))
}
void fill_around(Drawing on, const gfx::Rect &area, const gfx::Rect &hole, gfx::Colour col)
{
    const prog::InCall in_call;
    DRAW(fill_around(area, hole, col))
}

#undef DRAW

// Drawing nothing, a text still ends where it would have.
int text(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold)
{
    const prog::InCall in_call;
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text(x, y, s, col, bold) : x + gfx::Canvas::text_width(s, bold);
}
int text_scaled(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold, bool slanted, int scale)
{
    const prog::InCall in_call;
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_scaled(x, y, s, col, bold, slanted, scale)
                        : x + gfx::Canvas::text_width(s, bold) * scale;
}
int text_opaque(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg, int caret_x)
{
    const prog::InCall in_call;
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_opaque(x, y, s, fg, bg, caret_x) : x + gfx::Canvas::text_width(s);
}
int text_opaque_rgb(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Rgb bg, int caret_x)
{
    const prog::InCall in_call;
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_opaque(x, y, s, fg, bg, caret_x) : x + gfx::Canvas::text_width(s);
}

gfx::Rect bounds(Drawing on)
{
    const prog::InCall in_call;
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->limit().offset(-c->origin_x(), -c->origin_y()) : gfx::Rect{0, 0, 0, 0};
}

int text_width(const char *s, bool bold)
{
    const prog::InCall in_call;
    return gfx::Canvas::text_width(s, bold);
}
int text_height()
{
    const prog::InCall in_call;
    return gfx::Canvas::text_height();
}

} // namespace draw

} // namespace ui
} // namespace os
