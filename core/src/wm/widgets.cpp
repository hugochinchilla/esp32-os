#include "core/wm/widgets.hpp"

#include "core/wm/wm.hpp"

#include <cstring>

namespace wm {

using gfx::Canvas;

namespace {

constexpr int kTextH = 12;

const char *const kCheckGlyph[] = {"      X", "     XX", "X   XXX", "XX XXX ",
                                   "XXXXX  ", " XXX   ", "  X    "};
const char *const kUp[] = {"   X   ", "  XXX  ", " XXXXX ", "XXXXXXX"};
const char *const kDown[] = {"XXXXXXX", " XXXXX ", "  XXX  ", "   X   "};
const char *const kLeftGlyph[] = {"   X", "  XX", " XXX", "XXXX", " XXX", "  XX", "   X"};
const char *const kRightGlyph[] = {"X   ", "XX  ", "XXX ", "XXXX", "XXX ", "XX  ", "X   "};

// The round radio button, 12x12: outer ring dark above the diagonal and
// white below, inner ring black and grey, white inside, dot when selected.
// D dark, W white, K black, G grey, F fill, '.' clear.
const char *const kRadio[12] = {
    "....DDDD....", "..DDKKKKDD..", ".DKKFFFFKKW.", ".DKFFFFFFGW.",
    "DKFFFFFFFFGW", "DKFFFFFFFFGW", "DKFFFFFFFFGW", "DKFFFFFFFFGW",
    ".DKFFFFFFGW.", ".WGGFFFFGGW.", "..WWGGGGWW..", "....WWWW....",
};
const char *const kRadioDot[] = {" XX ", "XXXX", "XXXX", " XX "};

// Latin-1 text to UTF-8, for drawing.
void to_utf8(const uint8_t *chars, int count, char *out, int capacity)
{
    int n = 0;
    for (int i = 0; i < count && n < capacity - 2; i++) {
        uint8_t c = chars[i];
        if (c < 0x80) {
            out[n++] = static_cast<char>(c);
        } else {
            out[n++] = static_cast<char>(0xC0 | (c >> 6));
            out[n++] = static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    out[n] = 0;
}

void draw_radio(Canvas &c, int x, int y, bool selected)
{
    // Bevelled: a ring of shadow above and left, light below and right.
    // Flat: the outer ring in the outline colour, the rest paper.
    const gfx::Theme &t = gfx::theme();
    const bool flat = t.flat();
    for (int r = 0; r < 12; r++) {
        for (int q = 0; q < 12; q++) {
            gfx::Colour col;
            switch (kRadio[r][q]) {
            case 'D':
                col = flat ? t.outline : gfx::kDarkGrey;
                break;
            case 'W':
                col = flat ? t.outline : gfx::kWhite;
                break;
            case 'K':
                col = flat ? t.paper : gfx::kBlack;
                break;
            case 'G':
                col = flat ? t.paper : gfx::kGrey;
                break;
            case 'F':
                col = t.paper;
                break;
            default:
                continue;
            }
            c.fill({x + q, y + r, 1, 1}, col);
        }
    }
    if (selected) {
        c.pattern(x + 4, y + 4, kRadioDot, 4, gfx::ink());
    }
}

// Text with its access letter ('&') underlined. Returns its width.
int label(Canvas &c, int x, int y, const char *text, gfx::Colour colour, bool bold = false)
{
    char plain[96];
    const int at = strip_access(text, plain, sizeof(plain));
    c.text(x, y, plain, colour, bold);
    if (at >= 0) {
        char before[96];
        memcpy(before, plain, at);
        before[at] = 0;
        const char one[2] = {plain[at], 0};
        c.hline(x + Canvas::text_width(before, bold), y + 11, Canvas::text_width(one, bold), colour);
    }
    return Canvas::text_width(plain, bold);
}

int label_width(const char *text)
{
    char plain[96];
    strip_access(text, plain, sizeof(plain));
    return Canvas::text_width(plain);
}

// The same with the background in one pass (no erase before drawing).
int label_opaque(Canvas &c, int x, int y, const char *text, gfx::Colour fg, gfx::Colour bg)
{
    char plain[96];
    const int at = strip_access(text, plain, sizeof(plain));
    c.text_opaque(x, y, plain, fg, bg);
    if (at >= 0) {
        char before[96];
        memcpy(before, plain, at);
        before[at] = 0;
        const char one[2] = {plain[at], 0};
        c.hline(x + Canvas::text_width(before), y + 11, Canvas::text_width(one), fg);
    }
    return Canvas::text_width(plain);
}

// Calls fn for the parts of `area` outside `hole` (up to four rectangles).
template <typename Fn> void around(const Rect &area, const Rect &hole, Fn fn)
{
    const Rect h = hole.intersect(area);
    if (h.empty()) {
        fn(area);
        return;
    }
    fn(Rect(area.x, area.y, area.w, h.y - area.y));
    fn(Rect(area.x, h.bottom(), area.w, area.bottom() - h.bottom()));
    fn(Rect(area.x, h.y, h.x - area.x, h.h));
    fn(Rect(h.right(), h.y, area.right() - h.right(), h.h));
}

// Caption next to a check box or radio button, with the focus rectangle.
void caption(Canvas &c, int x, int h, const char *text, bool focus)
{
    const int y = (h - kTextH) / 2;
    const int w = label(c, x, y, text, gfx::ink());
    if (focus) {
        c.focus_rect({x - 2, y - 1, w + 4, kTextH + 2});
    }
}

bool inside(const Rect &r, const MouseMsg &m)
{
    return m.x >= 0 && m.y >= 0 && m.x < r.w && m.y < r.h;
}

} // namespace

/* ---- Label ------------------------------------------------------------------ */

void Label::paint(Canvas &c)
{
    const int y = (rect.h - kTextH) / 2;
    if (bold_) {
        c.fill({0, 0, rect.w, rect.h}, gfx::face()); // ponytail: opaque text has no bold yet
        label(c, 0, y, text_, gfx::ink(), true);
        return;
    }
    around({0, 0, rect.w, rect.h}, {0, y, label_width(text_), kTextH}, [&](const Rect &r) { c.fill(r, gfx::face()); });
    label_opaque(c, 0, y, text_, gfx::ink(), gfx::face());
}

/* ---- Button ----------------------------------------------------------------- */

void Button::paint(Canvas &c)
{
    Rect r = {0, 0, rect.w, rect.h};
    if (default_) {
        c.edge(r, gfx::ink(), gfx::ink());
        r = r.inset(1);
    }
    c.button_frame(r, pressed_ ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
    const int d = pressed_ ? 1 : 0;
    const gfx::Colour face = gfx::button_face(pressed_);
    Canvas inner = c.sub(r.inset(2));
    const int tw = label_width(text_);
    const int tx = (r.w - 4 - tw) / 2 + d, ty = (r.h - 4 - kTextH) / 2 + d;
    around({0, 0, r.w - 4, r.h - 4}, {tx, ty, tw, kTextH}, [&](const Rect &a) { inner.fill(a, face); });
    if (!enabled_) {
        // Etched, as greyed-out menu items are: dark grey over a white copy
        // one pixel down and right.
        inner.fill({tx, ty, tw + 1, kTextH + 1}, face);
        label(inner, tx + 1, ty + 1, text_, gfx::paper());
        label(inner, tx, ty, text_, gfx::dim());
        return;
    }
    label_opaque(inner, tx, ty, text_, gfx::ink(), face);
    if (has_focus()) {
        c.focus_rect(r.inset(4).offset(d, d));
    }
}

void Button::set_enabled(bool on)
{
    if (on != enabled_) {
        enabled_ = on;
        pressed_ = false;
        invalidate();
    }
}

void Button::mouse(const MouseMsg &m)
{
    if (!enabled_) {
        return;
    }
    const bool in = inside(rect, m);
    const bool was = pressed_;
    switch (m.kind) {
    case MouseKind::Down:
        pressed_ = true;
        break;
    case MouseKind::Move:
        pressed_ = in;
        break;
    case MouseKind::Up:
        pressed_ = false;
        if (was) {
            invalidate();
        }
        if (in) {
            click();
        }
        return;
    }
    if (was != pressed_) {
        invalidate();
    }
}

bool Button::key(const KeyMsg &k)
{
    if (!enabled_) {
        return false;
    }
    if (k.usage == key::Space || k.usage == key::Enter) {
        if (k.pressed) {
            click();
        }
        return true;
    }
    return false;
}

void Button::click()
{
    if (enabled_ && on_click_ != nullptr) {
        on_click_(*this, context_);
    }
}

/* ---- Check box -------------------------------------------------------------- */

void CheckBox::paint(Canvas &c)
{
    c.fill({0, 0, rect.w, rect.h}, gfx::face());
    const int y = (rect.h - 13) / 2;
    c.sunken({0, y, 13, 13});
    c.fill({2, y + 2, 9, 9}, held_ ? gfx::face() : gfx::paper());
    if (checked_) {
        c.pattern(3, y + 3, kCheckGlyph, 7, gfx::ink());
    }
    caption(c, 19, rect.h, text_, has_focus());
}

void CheckBox::mouse(const MouseMsg &m)
{
    const bool was = held_;
    held_ = m.kind != MouseKind::Up && inside(rect, m);
    if (m.kind == MouseKind::Up && inside(rect, m)) {
        set_checked(!checked_);
    }
    if (was != held_) {
        invalidate();
    }
}

bool CheckBox::key(const KeyMsg &k)
{
    if (k.usage == key::Space) {
        if (k.pressed) {
            set_checked(!checked_);
        }
        return true;
    }
    return false;
}

void CheckBox::set_checked(bool on)
{
    if (on == checked_) {
        return;
    }
    checked_ = on;
    invalidate();
    if (on_change_ != nullptr) {
        on_change_(*this, context_);
    }
}

/* ---- Radio button ----------------------------------------------------------- */

void RadioButton::paint(Canvas &c)
{
    c.fill({0, 0, rect.w, rect.h}, gfx::face());
    draw_radio(c, 0, (rect.h - 12) / 2, selected());
    caption(c, 19, rect.h, text_, has_focus());
}

void RadioButton::mouse(const MouseMsg &m)
{
    if (m.kind == MouseKind::Up && inside(rect, m)) {
        select();
    }
}

bool RadioButton::key(const KeyMsg &k)
{
    if (k.usage == key::Space) {
        if (k.pressed) {
            select();
        }
        return true;
    }
    return false;
}

void RadioButton::select()
{
    if (*group_ == value_) {
        return;
    }
    *group_ = value_;
    // The others in the group change too: repaint the whole window client.
    if (window() != nullptr) {
        window()->invalidate_client({0, 0, 4000, 4000});
    }
}

/* ---- Text field ------------------------------------------------------------- */

TextField::TextField(const Rect &r, const char *initial) : Widget(r)
{
    set(initial);
}

void TextField::set(const char *utf8)
{
    length_ = 0;
    caret_ = anchor_ = scroll_ = 0;
    text(utf8);
    anchor_ = caret_;
}

void TextField::get(char *out, int capacity) const
{
    to_utf8(chars_, length_, out, capacity);
}

Rect TextField::caret_rect() const
{
    return {2 + 1 + (caret_ - scroll_) * 6, 2 + (rect.h - 4 - kTextH) / 2, 1, kTextH};
}

void TextField::paint(Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    const Rect area = {0, 0, rect.w - 4, rect.h - 4};
    Canvas inner = c.sub(r.inset(2));
    const int y = (rect.h - 4 - kTextH) / 2;
    const bool focus = has_focus();
    const int s0 = selection_start(), s1 = selection_end();
    const int caret_x = focus && caret_shown() ? 1 + (caret_ - scroll_) * 6 : -1;
    // Above and below the text, and the two pixels left of it (the caret may
    // be in the second).
    inner.fill({0, 0, area.w, y}, gfx::paper());
    inner.fill({0, y + kTextH, area.w, area.h - y - kTextH}, gfx::paper());
    inner.fill({0, y, caret_x == 1 ? 1 : 2, kTextH}, gfx::paper());
    if (caret_x == 1) {
        inner.fill({1, y, 1, kTextH}, gfx::ink());
    }
    // The characters, in runs of the same look, each pixel once.
    int x = 2;
    for (int i = scroll_; i < length_ && x < area.w;) {
        const bool chosen = focus && i >= s0 && i < s1;
        char run[2 * kCapacity + 1];
        int n = 0, count = 0;
        while (i < length_ && (focus && i >= s0 && i < s1) == chosen && x + count * 6 < area.w) {
            const uint8_t ch = hidden_ ? '*' : chars_[i];
            i++;
            if (ch < 0x80) {
                run[n++] = static_cast<char>(ch);
            } else {
                run[n++] = static_cast<char>(0xC0 | (ch >> 6));
                run[n++] = static_cast<char>(0x80 | (ch & 0x3F));
            }
            count++;
        }
        run[n] = 0;
        const int cx = caret_x > x && caret_x < x + count * 6 ? caret_x : -1;
        if (chosen) {
            inner.text_opaque(x, y, run, gfx::theme().select_ink, gfx::theme().select, cx);
        } else {
            inner.text_opaque(x, y, run, gfx::ink(), gfx::paper(), cx);
        }
        x += count * 6;
    }
    // The caret just after the last character sits in that character's
    // cell (drawn above); right of the text is white.
    inner.fill({x, y, area.w - x, kTextH}, gfx::paper());
}

int TextField::index_at(int x) const
{
    int i = scroll_ + (x - 4 + 3) / 6;
    return i < 0 ? 0 : (i > length_ ? length_ : i);
}

void TextField::keep_caret_visible()
{
    const int shown = (rect.w - 8) / 6;
    if (caret_ < scroll_) {
        scroll_ = caret_;
    } else if (caret_ > scroll_ + shown) {
        scroll_ = caret_ - shown;
    }
}

void TextField::move_caret(int to, bool extend)
{
    to = to < 0 ? 0 : (to > length_ ? length_ : to);
    caret_ = to;
    if (!extend) {
        anchor_ = to;
    }
    keep_caret_visible();
    invalidate();
}

void TextField::erase_selection()
{
    const int s0 = selection_start(), s1 = selection_end();
    if (s0 == s1) {
        return;
    }
    memmove(chars_ + s0, chars_ + s1, length_ - s1);
    length_ -= s1 - s0;
    caret_ = anchor_ = s0;
}

void TextField::insert(const uint8_t *chars, int count)
{
    erase_selection();
    if (count > kCapacity - length_) {
        count = kCapacity - length_;
    }
    memmove(chars_ + caret_ + count, chars_ + caret_, length_ - caret_);
    memcpy(chars_ + caret_, chars, count);
    length_ += count;
    caret_ += count;
    anchor_ = caret_;
    keep_caret_visible();
    invalidate();
}

void TextField::text(const char *utf8)
{
    uint8_t buffer[kCapacity];
    int n = 0;
    for (uint8_t c; (c = gfx::next_latin1(utf8)) != 0 && n < kCapacity;) {
        if (c >= 0x20 && c != 0x7F) {
            buffer[n++] = c;
        }
    }
    if (n > 0) {
        insert(buffer, n);
    }
}

bool TextField::key(const KeyMsg &k)
{
    const bool shift = k.mods & kShift;
    if (!k.pressed) {
        return false;
    }
    switch (k.usage) {
    case key::Left:
        if (!shift && selection_start() != selection_end()) {
            move_caret(selection_start(), false);
        } else {
            move_caret(caret_ - 1, shift);
        }
        return true;
    case key::Right:
        if (!shift && selection_start() != selection_end()) {
            move_caret(selection_end(), false);
        } else {
            move_caret(caret_ + 1, shift);
        }
        return true;
    case key::Home:
        move_caret(0, shift);
        return true;
    case key::End:
        move_caret(length_, shift);
        return true;
    case key::Backspace:
        if (selection_start() == selection_end() && caret_ > 0) {
            anchor_ = caret_ - 1;
        }
        erase_selection();
        keep_caret_visible();
        invalidate();
        return true;
    case key::Delete:
        if (selection_start() == selection_end() && caret_ < length_) {
            anchor_ = caret_ + 1;
        }
        erase_selection();
        invalidate();
        return true;
    default:
        break;
    }
    if ((k.mods & kCtrl) && k.usage == key::A) {
        anchor_ = 0;
        caret_ = length_;
        keep_caret_visible();
        invalidate();
        return true;
    }
    return false;
}

void TextField::mouse(const MouseMsg &m)
{
    switch (m.kind) {
    case MouseKind::Down:
        dragging_ = true;
        move_caret(index_at(m.x), false);
        break;
    case MouseKind::Move:
        if (dragging_) {
            move_caret(index_at(m.x), true);
        }
        break;
    case MouseKind::Up:
        dragging_ = false;
        break;
    }
}

/* ---- Scroll bar ------------------------------------------------------------- */

void ScrollBar::set_range(int maximum, int page)
{
    maximum_ = maximum < 0 ? 0 : maximum;
    page_ = page < 1 ? 1 : page;
    if (value_ > maximum_) {
        value_ = maximum_;
    }
    invalidate();
}

void ScrollBar::set_value(int value)
{
    value = value < 0 ? 0 : (value > maximum_ ? maximum_ : value);
    if (value != value_) {
        value_ = value;
        changed();
    }
}

void ScrollBar::changed()
{
    invalidate();
    if (on_change_ != nullptr) {
        on_change_(*this, context_);
    }
}

Rect ScrollBar::thumb() const
{
    const int track = length() - 2 * kThickness;
    int size = maximum_ > 0 ? track * page_ / (maximum_ + page_) : track;
    size = size < 8 ? 8 : (size > track ? track : size);
    const int at = kThickness + (maximum_ > 0 ? (track - size) * value_ / maximum_ : 0);
    return vertical_ ? Rect(0, at, rect.w, size) : Rect(at, 0, size, rect.h);
}

void ScrollBar::paint(Canvas &c)
{
    const Rect first = vertical_ ? Rect(0, 0, rect.w, kThickness) : Rect(0, 0, kThickness, rect.h);
    const Rect last = vertical_ ? Rect(0, rect.h - kThickness, rect.w, kThickness)
                                : Rect(rect.w - kThickness, 0, kThickness, rect.h);
    // The track between the arrows, around the thumb.
    const Rect track = vertical_ ? Rect(0, kThickness, rect.w, rect.h - 2 * kThickness)
                                 : Rect(kThickness, 0, rect.w - 2 * kThickness, rect.h);
    if (maximum_ > 0) {
        around(track, thumb(), [&](const Rect &r) { c.checker(r); });
        if (gfx::theme().flat()) { // a block of one colour
            c.edge(thumb(), gfx::theme().outline, gfx::theme().outline);
            c.fill(thumb().inset(1), gfx::theme().thumb);
        } else {
            c.button(thumb(), gfx::ButtonLook::Raised);
        }
    } else {
        c.checker(track);
    }
    // The arrow buttons: edges, then the arrow with the face around it, so
    // no pixel is drawn twice.
    auto arrow = [&](const Rect &b, bool held, const char *const *glyph, int rows, int gx, int gy) {
        c.button_frame(b, held ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
        const int d = held ? 1 : 0;
        const Rect g = {b.x + gx + d, b.y + gy + d, static_cast<int>(strlen(glyph[0])), rows};
        const gfx::Colour face = gfx::button_face(held);
        around(b.inset(2), g, [&](const Rect &a) { c.fill(a, face); });
        c.pattern_opaque(g.x, g.y, glyph, rows, gfx::ink(), face);
    };
    if (vertical_) {
        arrow(first, held_ == -1, kUp, 4, 4, 6);
        arrow(last, held_ == 1, kDown, 4, 4, 6);
    } else {
        arrow(first, held_ == -1, kLeftGlyph, 7, 6, 4);
        arrow(last, held_ == 1, kRightGlyph, 7, 6, 4);
    }
}

// One step for the held arrow or track. A track press stops once the
// thumb reaches the pointer.
void ScrollBar::step()
{
    const Rect t = thumb();
    const int t0 = vertical_ ? t.y : t.x, t1 = t0 + (vertical_ ? t.h : t.w);
    switch (held_) {
    case -1:
        set_value(value_ - 1);
        break;
    case 1:
        set_value(value_ + 1);
        break;
    case -2:
        if (last_along_ < t0) {
            set_value(value_ - page_);
        }
        break;
    case 2:
        if (last_along_ >= t1) {
            set_value(value_ + page_);
        }
        break;
    default:
        break;
    }
}

bool ScrollBar::wheel(const WheelMsg &m)
{
    const int notches = vertical_ ? m.down : m.right;
    if (notches == 0 || maximum_ == 0 || held_ != 0) {
        return false;
    }
    set_value(value_ + notches * wheel_step());
    return true;
}

void ScrollBar::repeat(void *self)
{
    static_cast<ScrollBar *>(self)->step();
}

void ScrollBar::mouse(const MouseMsg &m)
{
    const int along = vertical_ ? m.y : m.x;
    const Rect t = thumb();
    const int t0 = vertical_ ? t.y : t.x, t1 = t0 + (vertical_ ? t.h : t.w);
    last_along_ = along;
    switch (m.kind) {
    case MouseKind::Down:
        if (along < kThickness) {
            held_ = -1;
        } else if (along >= length() - kThickness) {
            held_ = 1;
        } else if (maximum_ > 0 && along >= t0 && along < t1) {
            held_ = 3;
            grab_ = along;
            grab_value_ = value_;
        } else {
            held_ = along < t0 ? -2 : 2;
        }
        if (held_ != 3) {
            step();
            // Held down, it repeats: after a pause, then quickly.
            if (Wm *wm = host()) {
                timer_ = wm->start_timer(400000, 50000, &ScrollBar::repeat, this);
            }
        }
        invalidate();
        break;
    case MouseKind::Move:
        if (held_ == 3) {
            const int room = length() - 2 * kThickness - (t1 - t0);
            if (room > 0) {
                set_value(grab_value_ + (along - grab_) * maximum_ / room);
            }
        }
        break;
    case MouseKind::Up:
        held_ = 0;
        if (Wm *wm = host()) {
            wm->stop_timer(timer_);
        }
        timer_ = 0;
        invalidate();
        break;
    }
}

/* ---- List box --------------------------------------------------------------- */

ListBox::ListBox(const Rect &r, const char *const *items, int count, Callback on_select,
                 void *context)
    : Widget(r), items_(items), count_(count), on_select_(on_select), context_(context),
      bar_(Rect(r.w - 2 - ScrollBar::kThickness, 2, ScrollBar::kThickness, r.h - 4), true,
           &ListBox::scrolled, this)
{
    bar_.set_parent(this);
    layout();
}

void ListBox::layout()
{
    bar_.rect = {rect.w - 2 - ScrollBar::kThickness, 2, ScrollBar::kThickness, rect.h - 4};
    const int rows = visible_rows();
    bar_.set_range(count_ > rows ? count_ - rows : 0, rows);
    invalidate();
}

void ListBox::set_items(const char *const *items, int count)
{
    items_ = items;
    count_ = count;
    selected_ = -1;
    bar_.set_value_quietly(0);
    layout();
}

Rect ListBox::bar_rect() const
{
    return bar_.rect;
}

void ListBox::scrolled(ScrollBar &, void *context)
{
    static_cast<ListBox *>(context)->invalidate();
}

void ListBox::paint(Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    const bool bar = bar_.maximum() > 0;
    const Rect rows = {2, 2, rect.w - 4 - (bar ? ScrollBar::kThickness : 0), rect.h - 4};
    Canvas inner = c.sub(rows);
    const int top = bar_.value();
    const int rh = row_height();
    int y = 0;
    for (int i = 0; i <= visible_rows() && top + i < count_; i++, y += rh) {
        const int index = top + i;
        const Rect row = {0, y, rows.w, rh};
        const bool chosen = index == selected_;
        const int tw = Canvas::text_width(items_[index]);
        int tx = 2;
        Rect hl = row; // the highlighted part when chosen
        if (icon_ != nullptr) {
            const Rect cell = {0, y, 20, rh};
            const gfx::Bitmap *b = icon_(index, context_);
            if (b != nullptr) {
                const Rect at = {2, y + 1, b->width, b->height};
                around(cell, at, [&](const Rect &a) { inner.fill(a, gfx::paper()); });
                inner.blit_opaque(*b, 2, y + 1, gfx::paper());
            } else {
                inner.fill(cell, gfx::paper());
            }
            tx = 22;
            hl = {20, y, tw + 4, rh};
            inner.fill({hl.right(), y, rows.w - hl.right(), rh}, gfx::paper());
        }
        const int ty = y + (rh - kTextH) / 2;
        const Rect cells = {tx, ty, tw, kTextH};
        if (chosen) {
            around(hl, cells, [&](const Rect &a) { inner.fill_rgb(a, gfx::theme().select); });
            inner.text_opaque(tx, ty, items_[index], gfx::theme().select_ink, gfx::theme().select);
            if (has_focus()) {
                inner.focus_rect(hl);
            }
        } else {
            around(hl, cells, [&](const Rect &a) { inner.fill(a, gfx::paper()); });
            inner.text_opaque(tx, ty, items_[index], gfx::ink(), gfx::paper());
        }
    }
    inner.fill({0, y, rows.w, rows.h - y}, gfx::paper());
    if (bar) {
        Canvas bc = c.sub(bar_.rect);
        bar_.paint(bc);
    }
}

void ListBox::select(int index)
{
    index = index < 0 ? 0 : (index >= count_ ? count_ - 1 : index);
    if (index == selected_) {
        return;
    }
    selected_ = index;
    const int rows = visible_rows();
    if (index < bar_.value()) {
        bar_.set_value(index);
    } else if (index >= bar_.value() + rows) {
        bar_.set_value(index - rows + 1);
    }
    invalidate();
    if (on_select_ != nullptr) {
        on_select_(*this, context_);
    }
}

void ListBox::mouse(const MouseMsg &m)
{
    if (m.kind == MouseKind::Down) {
        in_bar_ = bar_.maximum() > 0 && bar_.rect.contains(m.x, m.y);
    }
    if (in_bar_) {
        bar_.mouse({m.kind, m.x - bar_.rect.x, m.y - bar_.rect.y});
        invalidate();
        return;
    }
    if (m.kind != MouseKind::Up && m.y >= 2) {
        const int row = (m.y - 2) / row_height();
        if (row < visible_rows() && bar_.value() + row < count_) {
            select(bar_.value() + row);
            if (m.double_click && on_activate_ != nullptr) {
                on_activate_(*this, context_);
            }
        }
    }
}

bool ListBox::key(const KeyMsg &k)
{
    if (!k.pressed) {
        return false;
    }
    const int rows = visible_rows();
    switch (k.usage) {
    case key::Enter:
        if (selected_ >= 0 && on_activate_ != nullptr) {
            on_activate_(*this, context_);
            return true;
        }
        return false;
    case key::Up:
        select(selected_ - 1);
        return true;
    case key::Down:
        select(selected_ + 1);
        return true;
    case key::Home:
        select(0);
        return true;
    case key::End:
        select(count_ - 1);
        return true;
    case key::PageUp:
        select(selected_ - rows);
        return true;
    case key::PageDown:
        select(selected_ + rows);
        return true;
    default:
        return false;
    }
}

/* ---- Group frame ------------------------------------------------------------ */

void GroupFrame::paint(Canvas &c)
{
    const int top = kTextH / 2;
    c.edge({0, top, rect.w - 1, rect.h - top - 1}, gfx::dim(), gfx::dim());
    c.edge({1, top + 1, rect.w - 1, rect.h - top - 1}, gfx::paper(), gfx::paper());
    // The caption sits on the top line, with a little face colour around it.
    const int tw = Canvas::text_width(caption_);
    c.fill({6, 0, tw + 4, kTextH}, gfx::face());
    c.text(8, 0, caption_, gfx::ink());
}

/* ---- Tabs --------------------------------------------------------------------- */

Rect Tabs::tab(int index) const
{
    int x = 2;
    for (int i = 0; i < index; i++) {
        x += label_width(titles_[i]) + 14;
    }
    return {x, 2, label_width(titles_[index]) + 14, kStrip - 2};
}

void Tabs::paint(Canvas &c)
{
    const gfx::Theme &t = gfx::theme();
    const bool flat = t.flat();
    const Rect body = {0, kStrip, rect.w, rect.h - kStrip};
    // The chosen title last: it stands in front of its neighbours and of
    // the page's top edge, so that title and page read as one sheet.
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < count_; i++) {
            const bool chosen = i == selected_;
            if (chosen != (pass == 1)) {
                continue;
            }
            Rect r = tab(i);
            if (chosen) {
                r = {r.x - 2, 0, r.w + 4, kStrip + (flat ? 1 : 2)};
            }
            if (flat) {
                c.fill(r, chosen ? gfx::face() : t.button);
                c.edge(r, t.outline, t.outline);
                if (chosen) {
                    c.hline(r.x + 1, kStrip, r.w - 2, gfx::face());
                }
            } else {
                // Light along the left and the top, shadow down the right,
                // the two top corners cut. The sides stop where the page
                // begins; the chosen one covers the page's top edge between.
                const int lines = kStrip - r.y;
                if (chosen) {
                    c.fill({r.x, r.y, r.w, lines}, gfx::face());
                    c.fill({r.x + 1, kStrip, r.w - 3, 2}, gfx::face());
                }
                c.hline(r.x + 2, r.y, r.w - 4, gfx::paper());
                c.hline(r.x + 1, r.y + 1, 1, gfx::paper());
                c.vline(r.x, r.y + 2, lines - 2, gfx::paper());
                c.hline(r.right() - 2, r.y + 1, 1, gfx::ink());
                c.vline(r.right() - 2, r.y + 2, lines - 2, gfx::dim());
                c.vline(r.right() - 1, r.y + 2, lines - 2, gfx::ink());
            }
            const int tw = label_width(titles_[i]);
            const int tx = r.x + (r.w - tw) / 2, ty = chosen ? 4 : 5;
            label(c, tx, ty, titles_[i], gfx::ink());
            if (chosen && has_focus()) {
                c.focus_rect({tx - 2, ty - 1, tw + 4, kTextH + 2});
            }
        }
        if (pass == 0) {
            if (flat) {
                c.edge(body, t.outline, t.outline);
            } else {
                c.raised(body);
            }
        }
    }
}

void Tabs::mouse(const MouseMsg &m)
{
    if (m.kind != MouseKind::Down || m.y < 0 || m.y >= kStrip) {
        return;
    }
    for (int i = 0; i < count_; i++) {
        const Rect r = tab(i);
        if (m.x >= r.x && m.x < r.right()) {
            select(i);
            return;
        }
    }
}

bool Tabs::key(const KeyMsg &k)
{
    if (k.usage != key::Left && k.usage != key::Right) {
        return false;
    }
    if (k.pressed) {
        const int to = selected_ + (k.usage == key::Left ? -1 : 1);
        select(to < 0 ? 0 : (to >= count_ ? count_ - 1 : to));
    }
    return true;
}

bool Tabs::turn_page(int step)
{
    if (count_ < 2 || rect.empty()) {
        return false;
    }
    select((selected_ + step + count_) % count_);
    return true;
}

void Tabs::select(int index)
{
    if (index < 0 || index >= count_ || index == selected_) {
        return;
    }
    selected_ = index;
    // The page changes with it: the whole client area is painted again.
    if (window() != nullptr) {
        window()->invalidate_client({0, 0, 4000, 4000});
    }
    if (on_change_ != nullptr) {
        on_change_(*this, context_);
    }
}

/* ---- Text area -------------------------------------------------------------- */

void TextArea::paint(Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    const Rect area = {0, 0, rect.w - 4, rect.h - 4};
    Canvas inner = c.sub(r.inset(2));
    // One band of 13 rows per line, each pixel once; copied out so the line
    // can end in a zero.
    char line[128];
    const char *p = text_;
    int y = 3;
    inner.fill({0, 0, area.w, y}, gfx::paper());
    while (*p != 0 && y < area.h) {
        int n = 0;
        while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(line)) - 1) {
            line[n] = p[n];
            n++;
        }
        line[n] = 0;
        const Rect band = {0, y, area.w, 13};
        around(band, {4, y, Canvas::text_width(line), kTextH}, [&](const Rect &a) { inner.fill(a, gfx::paper()); });
        inner.text_opaque(4, y, line, gfx::ink(), gfx::paper());
        p += n;
        while (*p != 0 && *p != '\n') {
            p++; // the rest of an over-long line
        }
        if (*p == '\n') {
            p++;
        }
        y += 13;
    }
    inner.fill({0, y, area.w, area.h - y}, gfx::paper());
}

} // namespace wm
