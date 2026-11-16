#include "core/wm/widgets.hpp"

#include "core/wm/wm.hpp"

#include <cstring>

namespace wm {

using gfx::Canvas;
using gfx::kBlack;
using gfx::kDarkGrey;
using gfx::kGrey;
using gfx::kWhite;

namespace {

constexpr gfx::Rgb kHighlight = {0, 0, 128};
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
    for (int r = 0; r < 12; r++) {
        for (int q = 0; q < 12; q++) {
            gfx::Colour col;
            switch (kRadio[r][q]) {
            case 'D':
                col = kDarkGrey;
                break;
            case 'W':
                col = kWhite;
                break;
            case 'K':
                col = kBlack;
                break;
            case 'G':
                col = kGrey;
                break;
            case 'F':
                col = kWhite;
                break;
            default:
                continue;
            }
            c.fill({x + q, y + r, 1, 1}, col);
        }
    }
    if (selected) {
        c.pattern(x + 4, y + 4, kRadioDot, 4, kBlack);
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

// Caption next to a check box or radio button, with the focus rectangle.
void caption(Canvas &c, int x, int h, const char *text, bool focus)
{
    const int y = (h - kTextH) / 2;
    const int w = label(c, x, y, text, kBlack);
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
    c.fill({0, 0, rect.w, rect.h}, kGrey);
    label(c, 0, (rect.h - kTextH) / 2, text_, kBlack, bold_);
}

/* ---- Button ----------------------------------------------------------------- */

void Button::paint(Canvas &c)
{
    Rect r = {0, 0, rect.w, rect.h};
    if (default_) {
        c.edge(r, kBlack, kBlack);
        r = r.inset(1);
    }
    c.button(r, pressed_ ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
    const int d = pressed_ ? 1 : 0;
    Canvas inner = c.sub(r.inset(2));
    const int tw = label_width(text_);
    label(inner, (r.w - 4 - tw) / 2 + d, (r.h - 4 - kTextH) / 2 + d, text_, kBlack);
    if (has_focus()) {
        c.focus_rect(r.inset(4).offset(d, d));
    }
}

void Button::mouse(const MouseMsg &m)
{
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
    if (on_click_ != nullptr) {
        on_click_(*this, context_);
    }
}

/* ---- Check box -------------------------------------------------------------- */

void CheckBox::paint(Canvas &c)
{
    c.fill({0, 0, rect.w, rect.h}, kGrey);
    const int y = (rect.h - 13) / 2;
    c.sunken({0, y, 13, 13});
    c.fill({2, y + 2, 9, 9}, held_ ? kGrey : kWhite);
    if (checked_) {
        c.pattern(3, y + 3, kCheckGlyph, 7, kBlack);
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
    c.fill({0, 0, rect.w, rect.h}, kGrey);
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

void TextField::paint(Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    c.fill(r.inset(2), kWhite);
    Canvas inner = c.sub(r.inset(2));
    const int y = (rect.h - 4 - kTextH) / 2;
    const bool focus = has_focus();
    const int s0 = selection_start(), s1 = selection_end();
    for (int i = scroll_; i < length_; i++) {
        const int x = 2 + (i - scroll_) * 6;
        if (x >= rect.w) {
            break;
        }
        char one[3];
        to_utf8(&chars_[i], 1, one, sizeof(one));
        const bool chosen = focus && i >= s0 && i < s1;
        if (chosen) {
            inner.fill_rgb({x, y, 6, kTextH}, kHighlight);
        }
        inner.text(x, y, one, chosen ? kWhite : kBlack);
    }
    if (focus && caret_shown()) {
        inner.vline(1 + (caret_ - scroll_) * 6, y, kTextH, kBlack);
    }
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
    const Rect r = {0, 0, rect.w, rect.h};
    c.checker(r);
    const Rect first = vertical_ ? Rect(0, 0, rect.w, kThickness) : Rect(0, 0, kThickness, rect.h);
    const Rect last = vertical_ ? Rect(0, rect.h - kThickness, rect.w, kThickness)
                                : Rect(rect.w - kThickness, 0, kThickness, rect.h);
    c.button(first, held_ == -1 ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
    c.button(last, held_ == 1 ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
    const int d1 = held_ == -1 ? 1 : 0, d2 = held_ == 1 ? 1 : 0;
    if (vertical_) {
        c.pattern(first.x + 4 + d1, first.y + 6 + d1, kUp, 4, kBlack);
        c.pattern(last.x + 4 + d2, last.y + 6 + d2, kDown, 4, kBlack);
    } else {
        c.pattern(first.x + 6 + d1, first.y + 4 + d1, kLeftGlyph, 7, kBlack);
        c.pattern(last.x + 6 + d2, last.y + 4 + d2, kRightGlyph, 7, kBlack);
    }
    if (maximum_ > 0) {
        c.button(thumb(), gfx::ButtonLook::Raised);
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
    c.fill(rows, kWhite);
    Canvas inner = c.sub(rows);
    const int top = bar_.value();
    const int rh = row_height();
    for (int i = 0; i <= visible_rows() && top + i < count_; i++) {
        const int index = top + i;
        Rect row = {0, i * rh, rows.w, rh};
        const bool chosen = index == selected_;
        int tx = 2;
        if (icon_ != nullptr) {
            if (const gfx::Bitmap *b = icon_(index, context_)) {
                inner.blit(*b, 2, row.y + 1);
            }
            tx = 22;
            row = {20, row.y, Canvas::text_width(items_[index]) + 4, rh};
        }
        if (chosen) {
            inner.fill_rgb(row, kHighlight);
        }
        inner.text(tx, row.y + (rh - kTextH) / 2, items_[index], chosen ? kWhite : kBlack);
        if (chosen && has_focus()) {
            inner.focus_rect(row);
        }
    }
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
    c.edge({0, top, rect.w - 1, rect.h - top - 1}, kDarkGrey, kDarkGrey);
    c.edge({1, top + 1, rect.w - 1, rect.h - top - 1}, kWhite, kWhite);
    // The caption sits on the top line, with a little face colour around it.
    const int tw = Canvas::text_width(caption_);
    c.fill({6, 0, tw + 4, kTextH}, kGrey);
    c.text(8, 0, caption_, kBlack);
}

/* ---- Text area -------------------------------------------------------------- */

void TextArea::paint(Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    c.fill(r.inset(2), kWhite);
    Canvas inner = c.sub(r.inset(2));
    // One line at a time, copied out so it can end in a zero.
    char line[128];
    const char *p = text_;
    int y = 3;
    while (*p != 0 && y < rect.h) {
        int n = 0;
        while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(line)) - 1) {
            line[n] = p[n];
            n++;
        }
        line[n] = 0;
        inner.text(4, y, line, kBlack);
        p += n;
        while (*p != 0 && *p != '\n') {
            p++; // the rest of an over-long line
        }
        if (*p == '\n') {
            p++;
        }
        y += 13;
    }
}

} // namespace wm
