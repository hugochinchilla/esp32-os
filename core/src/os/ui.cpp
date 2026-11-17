// Windows and controls by number (core/os/ui.hpp): the system's side. One
// window class of the system's own stands behind every such window; it
// owns the controls and turns what the window manager tells it into events
// for the program.
#include "core/os/ui.hpp"

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

constexpr int kMaxWindows = 16; // ponytail: as many as the window manager shows

// Every window made here, open or not yet. A window's number is the one
// the window server knows it by (os::WindowId), taken when it is made.
class Behind;
Behind *g_windows[kMaxWindows] = {};

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
        c.fill({0, 0, rect.w, y}, gfx::kGrey);
        const int x = c.text_opaque(0, y, text_, gfx::kBlack, gfx::kGrey);
        c.fill({x, y, rect.w - x, 12}, gfx::kGrey);
        c.fill({0, y + 12, rect.w, rect.h - y - 12}, gfx::kGrey);
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
        c.fill({0, 0, rect.w, rect.h}, gfx::kGrey);
        for (int i = 0; i < count_; i++) {
            const gfx::Rect r = cell(i);
            if (i == selected_) {
                c.edge(r, gfx::kBlack, gfx::kBlack);
                c.edge(r.inset(1), gfx::kWhite, gfx::kWhite);
            }
            const gfx::Rect inner = r.inset(margin_);
            c.edge(inner, gfx::kDarkGrey, gfx::kWhite);
            paint_cell_(*this, c, inner.inset(1), i, context_);
            if (i == selected_ && margin_ < 3) {
                // A tight grid: the mark goes over the cell's own edge.
                c.edge(r, gfx::kBlack, gfx::kBlack);
                c.edge(r.inset(1), gfx::kWhite, gfx::kWhite);
                c.edge(r.inset(2), gfx::kBlack, gfx::kBlack);
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
        c.fill(r.inset(2), gfx::kWhite);
        gfx::Canvas inner = c.sub({2, 2, rect.w - 4 - 16, rect.h - 4});
        if (icon_ != nullptr) {
            inner.blit(*icon_, 2, (rect.h - 4 - 16) / 2);
        }
        inner.text(icon_ != nullptr ? 22 : 4, (rect.h - 4 - 12) / 2, text_, gfx::kBlack);
        const gfx::Rect b = {rect.w - 2 - 16, 2, 16, rect.h - 4};
        c.button(b, held_ ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
        c.pattern(b.x + 4 + held_, b.y + (b.h - 4) / 2 + held_, kDownGlyph, 4, gfx::kBlack);
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
            c.fill({2 + x, 2, bw, h}, i < lit_ ? colour_ : gfx::kWhite);
            x += bw;
            const int gw = x + kGap > w ? w - x : kGap;
            if (gw > 0) {
                c.fill({2 + x, 2, gw, h}, gfx::kWhite);
                x += gw;
            }
        }
    }

private:
    gfx::Colour colour_;
    int lit_ = 0;
};

// Icons in a grid with their names under them, 76 x 66 per entry: a
// folder's contents. One is picked; a scroll bar comes when they do not fit.
class IconView : public wm::Widget {
public:
    using Callback = void (*)(IconView &view, void *context);
    static constexpr int kCellW = 76, kCellH = 66;
    static constexpr gfx::Rgb kHighlight = {0, 0, 128};

    IconView(const gfx::Rect &r, Callback on_open, Callback on_select, Callback on_drag, void *context)
        : Widget(r), on_open_(on_open), on_select_(on_select), on_drag_(on_drag), context_(context),
          bar_({0, 0, wm::ScrollBar::kThickness, 10}, true, &IconView::scrolled, this)
    {
        bar_.set_parent(this);
    }
    ~IconView() = default;
    // The entries: the program's own tables, which must stay valid.
    void set_items(const char *const *names, int count, const gfx::Bitmap *const *icons)
    {
        names_ = names;
        icons_ = icons;
        count_ = count;
        if (selected_ >= count_) {
            selected_ = -1;
        }
        layout();
    }
    int selected() const { return selected_; }
    // The entry at a point of the view, or -1.
    int item_at(int x, int y) const
    {
        const int col = (x - 2 - 10) / kCellW, row = (y - 2 - 8) / kCellH;
        const int i = (row + top_row_) * columns() + col;
        return x >= 12 && y >= 10 && col < columns() && i < count_ ? i : -1;
    }
    void select(int i)
    {
        i = i < -1 ? -1 : (i >= count_ ? count_ - 1 : i);
        if (i == selected_) {
            return;
        }
        selected_ = i;
        if (i >= 0) {
            const int row = i / columns();
            if (row < top_row_) {
                top_row_ = row;
            } else if (row >= top_row_ + visible_rows()) {
                top_row_ = row - visible_rows() + 1;
            }
        }
        layout();
        if (on_select_ != nullptr) {
            on_select_(*this, context_);
        }
    }
    void reset()
    {
        selected_ = -1;
        top_row_ = 0;
        layout();
    }
    void layout()
    {
        bar_.rect = {rect.w - 2 - wm::ScrollBar::kThickness, 2, wm::ScrollBar::kThickness, rect.h - 4};
        const int rows = (count_ + columns() - 1) / columns();
        bar_.set_range(rows > visible_rows() ? rows - visible_rows() : 0, visible_rows());
        bar_.set_value_quietly(top_row_);
        invalidate();
    }
    int columns() const
    {
        const int c = (rect.w - 4 - wm::ScrollBar::kThickness - 8) / kCellW;
        return c < 1 ? 1 : c;
    }
    int visible_rows() const
    {
        const int r = (rect.h - 4 - 10) / kCellH;
        return r < 1 ? 1 : r;
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        const gfx::Rect area = {2, 2, rect.w - 4 - (bar_.maximum() > 0 ? wm::ScrollBar::kThickness : 0), rect.h - 4};
        c.fill(area, gfx::kWhite);
        gfx::Canvas inner = c.sub(area);
        const int cols = columns();
        for (int i = top_row_ * cols; i < count_; i++) {
            const int row = i / cols - top_row_, col = i % cols;
            if (row > visible_rows()) {
                break;
            }
            const int x = 10 + col * kCellW, y = 8 + row * kCellH;
            const gfx::Bitmap *icon = icons_[i];
            const bool chosen = i == selected_;
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
                    inner.fill_rgb({tx - 1, ty - 1, tw + 2, 13}, kHighlight);
                }
                inner.text(tx, ty, lines[l], chosen ? gfx::kWhite : gfx::kBlack);
            }
        }
        if (bar_.maximum() > 0) {
            gfx::Canvas bc = c.sub(bar_.rect);
            bar_.paint(bc);
        }
    }

    bool wants_mouse() const override { return true; }
    bool focusable() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind == wm::MouseKind::Down) {
            in_bar_ = bar_.maximum() > 0 && bar_.rect.contains(m.x, m.y);
        }
        if (in_bar_) {
            bar_.mouse({m.kind, m.x - bar_.rect.x, m.y - bar_.rect.y});
            invalidate();
            return;
        }
        if (m.kind == wm::MouseKind::Move) {
            // Held on an entry and moved a little: it is dragged.
            const int dx = m.x - press_x_, dy = m.y - press_y_;
            if (pressed_ && dx * dx + dy * dy > 16 && on_drag_ != nullptr) {
                pressed_ = false;
                on_drag_(*this, context_);
            }
            return;
        }
        if (m.kind != wm::MouseKind::Down) {
            pressed_ = false;
            return;
        }
        const int i = item_at(m.x, m.y);
        select(i);
        pressed_ = i >= 0 && !m.double_click;
        press_x_ = m.x;
        press_y_ = m.y;
        if (i >= 0 && m.double_click && on_open_ != nullptr) {
            on_open_(*this, context_);
        }
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        const int cols = columns();
        switch (k.usage) {
        case wm::key::Left:
            select(selected_ < 0 ? 0 : selected_ - 1);
            return true;
        case wm::key::Right:
            select(selected_ + 1);
            return true;
        case wm::key::Up:
            select(selected_ < 0 ? 0 : (selected_ - cols >= 0 ? selected_ - cols : selected_));
            return true;
        case wm::key::Down:
            select(selected_ < 0 ? 0 : (selected_ + cols < count_ ? selected_ + cols : selected_));
            return true;
        case wm::key::Home:
            select(0);
            return true;
        case wm::key::End:
            select(count_ - 1);
            return true;
        case wm::key::Enter:
            if (selected_ >= 0 && on_open_ != nullptr) {
                on_open_(*this, context_);
            }
            return true;
        default:
            return false;
        }
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

    static void scrolled(wm::ScrollBar &bar, void *context)
    {
        auto *v = static_cast<IconView *>(context);
        v->top_row_ = bar.value();
        v->invalidate();
    }

    const char *const *names_ = nullptr;
    const gfx::Bitmap *const *icons_ = nullptr;
    int count_ = 0;
    Callback on_open_, on_select_, on_drag_;
    void *context_;
    bool pressed_ = false;
    int press_x_ = 0, press_y_ = 0;
    int selected_ = -1;
    int top_row_ = 0;
    bool in_bar_ = false;
    wm::ScrollBar bar_;
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
            }
            os::release(controls_[i]);
        }
        g_windows[slot_] = nullptr;
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
    static void scrolled(wm::ScrollBar &b, void *context) { static_cast<Behind *>(context)->from(b, What::Changed); }
    static void picked(wm::ListBox &l, void *context) { static_cast<Behind *>(context)->from(l, What::Changed); }
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
    // The right button: to the topmost picture under it that takes the mouse.
    void right_click(int x, int y) override
    {
        for (int i = count_ - 1; i >= 0; i--) {
            if (kinds_[i] == Kind::Picture && controls_[i]->wants_mouse() && controls_[i]->rect.contains(x, y)) {
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
    for (Behind *w : g_windows) {
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
    int slot = -1;
    for (int i = 0; i < kMaxWindows && slot < 0; i++) {
        slot = g_windows[i] == nullptr ? i : -1;
    }
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
    Behind *w = behind(window);
    return w != nullptr ? w->state() : nullptr;
}

bool alive(Window window) { return behind(window) != nullptr; }

bool on_screen(Window window)
{
    Behind *w = behind(window);
    return w != nullptr && w->open();
}

void redraw(Window window, const gfx::Rect &part)
{
    if (Behind *w = behind(window)) {
        w->invalidate_client(part);
    }
}

Control plain(Window window, const gfx::Rect &place)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<PlainText>(Kind::Plain, place) : kNoControl;
}

Control label(Window window, const gfx::Rect &place, const char *text)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Label>(Kind::Label, place, text) : kNoControl;
}

Control button(Window window, const gfx::Rect &place, const char *text)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Button>(Kind::Button, place, text, &Behind::pressed, w) : kNoControl;
}

Control check_box(Window window, const gfx::Rect &place, const char *text, bool checked)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::CheckBox>(Kind::CheckBox, place, text, checked, &Behind::ticked, w) : kNoControl;
}

Control radio(Window window, const gfx::Rect &place, const char *text, int group, int value)
{
    Behind *w = behind(window);
    int *chosen = w != nullptr ? w->radio_group(group) : nullptr;
    return chosen != nullptr ? w->make<wm::RadioButton>(Kind::Radio, place, text, chosen, value) : kNoControl;
}

Control text_field(Window window, const gfx::Rect &place, const char *initial)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::TextField>(Kind::TextField, place, initial) : kNoControl;
}

Control scroll_bar(Window window, const gfx::Rect &place, bool vertical)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::ScrollBar>(Kind::ScrollBar, place, vertical, &Behind::scrolled, w) : kNoControl;
}

Control list(Window window, const gfx::Rect &place, const char *const *items, int count)
{
    Behind *w = behind(window);
    const Control c = w != nullptr ? w->make<wm::ListBox>(Kind::List, place, items, count, &Behind::picked, w) : kNoControl;
    if (c != kNoControl) {
        w->as<wm::ListBox>(c, Kind::List)->set_on_activate(&Behind::chosen);
    }
    return c;
}

Control group(Window window, const gfx::Rect &place, const char *caption)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::GroupFrame>(Kind::Group, place, caption) : kNoControl;
}

Control text_area(Window window, const gfx::Rect &place, const char *text)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::TextArea>(Kind::TextArea, place, text) : kNoControl;
}

Control picture(Window window, const gfx::Rect &place, uint8_t how)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<Picture>(Kind::Picture, place, how, &Behind::paint_picture, &Behind::picture_mouse,
                                           &Behind::picture_key, w)
                        : kNoControl;
}

Control icon_view(Window window, const gfx::Rect &place)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<IconView>(Kind::IconView, place, &Behind::icon_opened, &Behind::icon_picked,
                                            &Behind::icon_dragged, w)
                        : kNoControl;
}

void reset_view(Window window, Control control)
{
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->reset();
    }
}

int item_at(Window window, Control control, int x, int y)
{
    auto *v = control_of<IconView>(window, control, Kind::IconView);
    return v != nullptr ? v->item_at(x, y) : -1;
}

Control text_edit(Window window, const gfx::Rect &place, size_t text_bytes, size_t undo_bytes)
{
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
    auto *e = control_of<OwnedTextEdit>(window, control, Kind::TextEdit);
    return e == nullptr ? 0
                        : static_cast<uint8_t>((e->modified() ? kModified : 0) | (e->can_undo() ? kCanUndo : 0) |
                                               (e->has_selection() ? kHasSelection : 0));
}

void set_modified(Window window, Control control, bool modified)
{
    Behind *w = behind(window);
    if (auto *e = w != nullptr ? w->as<OwnedTextEdit>(control, Kind::TextEdit) : nullptr) {
        e->set_modified(modified);
        w->note_modified(modified);
    }
}

void load_begin(Window window, Control control)
{
    if (auto *e = control_of<OwnedTextEdit>(window, control, Kind::TextEdit)) {
        e->load_begin();
    }
}

bool load_append(Window window, Control control, const uint8_t *bytes, size_t size)
{
    auto *e = control_of<OwnedTextEdit>(window, control, Kind::TextEdit);
    return e != nullptr && e->load_append(bytes, size);
}

void load_finish(Window window, Control control)
{
    Behind *w = behind(window);
    if (auto *e = w != nullptr ? w->as<OwnedTextEdit>(control, Kind::TextEdit) : nullptr) {
        e->load_finish();
        w->note_modified(e->modified());
    }
}

TextReader text_reader(Window window, Control control)
{
    return control_of<OwnedTextEdit>(window, control, Kind::TextEdit);
}

size_t read_text(TextReader reader, size_t offset, uint8_t *out, size_t room)
{
    return reader != nullptr ? static_cast<const OwnedTextEdit *>(reader)->save_bytes(offset, out, room) : 0;
}

size_t clipboard_size() { return wm::clipboard::size(); }

void set_locked(Window window, bool locked)
{
    if (Behind *w = behind(window)) {
        w->set_locked(locked);
    }
}

bool locked(Window window)
{
    Behind *w = behind(window);
    return w != nullptr && w->locked();
}

void set_caret(Window window, Control control, const gfx::Rect &caret)
{
    if (auto *p = control_of<Picture>(window, control, Kind::Picture)) {
        p->set_caret(caret);
    }
}

bool control_has_keys(Window window, Control control)
{
    Behind *w = behind(window);
    const wm::Widget *c = w != nullptr ? w->any(control) : nullptr;
    return c != nullptr && c->has_focus();
}

bool caret_lit(Window window)
{
    Behind *w = behind(window);
    return w != nullptr && (w->manager() == nullptr || w->manager()->caret_visible());
}

void place_control(Window window, Control control, const gfx::Rect &place)
{
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        c->invalidate(); // where it was
        c->rect = place;
        if (w->kind(control) == Kind::TextEdit) {
            static_cast<OwnedTextEdit *>(c)->layout();
        } else if (w->kind(control) == Kind::IconView) {
            static_cast<IconView *>(c)->layout();
        }
        c->invalidate();
    }
}

gfx::Rect client_size(Window window)
{
    Behind *w = behind(window);
    return w != nullptr ? gfx::Rect{0, 0, w->client().w, w->client().h} : gfx::Rect{0, 0, 0, 0};
}

void resize(Window window, int client_width, int client_height)
{
    if (Behind *w = behind(window)) {
        w->resize_client(client_width, client_height);
    }
}

void redraw_control(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        c->invalidate();
    }
}

void redraw_control_part(Window window, Control control, const gfx::Rect &part)
{
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        c->invalidate_part(part);
    }
}

Control grid(Window window, const gfx::Rect &place, int columns, int count, int cell_w, int cell_h, int margin)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<Grid>(Kind::Grid, place, columns, count, cell_w, cell_h, margin, &Behind::paint_cell,
                                        &Behind::cell_picked, w)
                        : kNoControl;
}

Control drop_down(Window window, const gfx::Bitmap *icon, const gfx::Rect &place)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<DropDown>(Kind::DropDown, place, icon, &Behind::arrow_pressed, w) : kNoControl;
}

void menu_under(Window window, Control control, wm::Menu &menu)
{
    Behind *w = behind(window);
    if (const wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        const gfx::Rect client = w->client();
        os::open_menu(menu, client.x + c->rect.x, client.y + c->rect.bottom());
    }
}

Control meter(Window window, const gfx::Rect &place, gfx::Colour colour)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<Meter>(Kind::Meter, place, colour) : kNoControl;
}

void set_level(Window window, Control control, uint32_t value, uint32_t total)
{
    Behind *w = behind(window);
    if (Meter *m = w != nullptr ? w->as<Meter>(control, Kind::Meter) : nullptr) {
        m->set(value, total);
    }
}

bool in_front(Window window)
{
    Behind *w = behind(window);
    return w != nullptr && os::in_front(*w);
}

bool has_keys(Window window)
{
    Behind *w = behind(window);
    return w != nullptr && w->wm::Window::focused() && w->wm::Window::state() != wm::WindowState::Minimized;
}

void set_menu_bar(Window window, const wm::MenuBar *bar)
{
    Behind *w = behind(window);
    if (w != nullptr && w->has_menu_room()) {
        w->set_menu_bar(bar);
    }
}

void set_title(Window window, const char *title)
{
    if (Behind *w = behind(window)) {
        w->set_title(title);
    }
}

Control focus(Window window)
{
    Behind *w = behind(window);
    return w != nullptr ? w->focus_number() : kNoControl;
}

void set_text(Window window, Control control, const char *text)
{
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
    if (capacity > 0) {
        out[0] = 0;
    }
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->get(out, capacity);
    }
}

void select_all(Window window, Control control)
{
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->select_all();
    }
}

void set_hidden(Window window, Control control, bool hidden)
{
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->set_hidden(hidden);
    }
}

void set_enabled(Window window, Control control, bool enabled)
{
    if (auto *b = control_of<wm::Button>(window, control, Kind::Button)) {
        b->set_enabled(enabled);
    }
}

bool checked(Window window, Control control)
{
    auto *b = control_of<wm::CheckBox>(window, control, Kind::CheckBox);
    return b != nullptr && b->checked();
}

void set_checked(Window window, Control control, bool on)
{
    if (auto *b = control_of<wm::CheckBox>(window, control, Kind::CheckBox)) {
        b->set_checked(on);
    }
}

int radio_value(Window window, int group)
{
    Behind *w = behind(window);
    const int *chosen = w != nullptr ? w->radio_group(group) : nullptr;
    return chosen != nullptr ? *chosen : 0;
}

void set_radio_value(Window window, int group, int value)
{
    Behind *w = behind(window);
    if (int *chosen = w != nullptr ? w->radio_group(group) : nullptr) {
        *chosen = value;
        w->invalidate();
    }
}

void set_range(Window window, Control control, int maximum, int page)
{
    if (auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar)) {
        b->set_range(maximum, page);
    }
}

int value(Window window, Control control)
{
    auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar);
    return b != nullptr ? b->value() : 0;
}

void set_value(Window window, Control control, int value)
{
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
    if (auto *g = control_of<Grid>(window, control, Kind::Grid)) {
        return g->selected();
    }
    if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        return v->selected();
    }
    auto *l = control_of<wm::ListBox>(window, control, Kind::List);
    return l != nullptr ? l->selected() : -1;
}

void select(Window window, Control control, int index)
{
    if (auto *l = control_of<wm::ListBox>(window, control, Kind::List)) {
        l->select(index);
    } else if (auto *g = control_of<Grid>(window, control, Kind::Grid)) {
        g->select_quietly(index);
    } else if (auto *v = control_of<IconView>(window, control, Kind::IconView)) {
        v->select(index);
    }
}

void set_focus(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        w->set_focus(c);
    }
}

void set_default(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Button *b = w != nullptr ? w->button(control) : nullptr) {
        b->set_default(true);
        w->set_default(b);
    }
}

void press(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Button *b = w != nullptr ? w->button(control) : nullptr) {
        b->click();
    }
}

bool show(Window window)
{
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
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? g_drawings.add(c->sub(part)) : kNoDrawing;
}

#define DRAW(call)                                                                                                     \
    if (gfx::Canvas *c = g_drawings.at(on)) {                                                                          \
        c->call;                                                                                                       \
    }

void fill(Drawing on, const gfx::Rect &r, gfx::Colour col) { DRAW(fill(r, col)) }
void hline(Drawing on, int x, int y, int w, gfx::Colour col) { DRAW(hline(x, y, w, col)) }
void vline(Drawing on, int x, int y, int h, gfx::Colour col) { DRAW(vline(x, y, h, col)) }
void fill_rgb(Drawing on, const gfx::Rect &r, gfx::Rgb col) { DRAW(fill_rgb(r, col)) }
void fill_pattern(Drawing on, const gfx::Rect &r, gfx::Rgb paper, gfx::Rgb ink, const uint8_t rows[8])
{
    DRAW(fill_pattern(r, paper, ink, rows))
}
void fill_fade(Drawing on, const gfx::Rect &r, gfx::Rgb from, gfx::Rgb to, bool vertical, const gfx::Rect *span)
{
    DRAW(fill_fade(r, from, to, vertical, span))
}
void focus_rect(Drawing on, const gfx::Rect &r) { DRAW(focus_rect(r)) }
void checker(Drawing on, const gfx::Rect &r) { DRAW(checker(r)) }
void edge(Drawing on, const gfx::Rect &r, gfx::Colour tl, gfx::Colour br) { DRAW(edge(r, tl, br)) }
void raised(Drawing on, const gfx::Rect &r) { DRAW(raised(r)) }
void sunken(Drawing on, const gfx::Rect &r) { DRAW(sunken(r)) }
void button(Drawing on, const gfx::Rect &r, gfx::ButtonLook look) { DRAW(button(r, look)) }
void button_frame(Drawing on, const gfx::Rect &r, gfx::ButtonLook look) { DRAW(button_frame(r, look)) }
void blit(Drawing on, const gfx::Bitmap &b, int x, int y) { DRAW(blit(b, x, y)) }
void blit_selected(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour tint)
{
    DRAW(blit_selected(b, x, y, tint))
}
void blit2x(Drawing on, const gfx::Bitmap &b, int x, int y) { DRAW(blit2x(b, x, y)) }
void blit_opaque(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour bg) { DRAW(blit_opaque(b, x, y, bg)) }
void pattern(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour col)
{
    DRAW(pattern(x, y, rows, count, col))
}
void pattern_opaque(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour fg, gfx::Colour bg)
{
    DRAW(pattern_opaque(x, y, rows, count, fg, bg))
}
void text_up(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold) { DRAW(text_up(x, y, s, col, bold)) }
void fill_around(Drawing on, const gfx::Rect &area, const gfx::Rect &hole, gfx::Colour col)
{
    DRAW(fill_around(area, hole, col))
}

#undef DRAW

// Drawing nothing, a text still ends where it would have.
int text(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text(x, y, s, col, bold) : x + gfx::Canvas::text_width(s, bold);
}
int text_opaque(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg, int caret_x)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_opaque(x, y, s, fg, bg, caret_x) : x + gfx::Canvas::text_width(s);
}
int text_opaque_rgb(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Rgb bg, int caret_x)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_opaque(x, y, s, fg, bg, caret_x) : x + gfx::Canvas::text_width(s);
}

gfx::Rect bounds(Drawing on)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->limit().offset(-c->origin_x(), -c->origin_y()) : gfx::Rect{0, 0, 0, 0};
}

int text_width(const char *s, bool bold) { return gfx::Canvas::text_width(s, bold); }
int text_height() { return gfx::Canvas::text_height(); }

} // namespace draw

} // namespace ui
} // namespace os
