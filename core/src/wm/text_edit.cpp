#include "core/wm/text_edit.hpp"

#include <algorithm>
#include <cstring>

#include "core/wm/wm.hpp"

namespace wm {

using gfx::Canvas;

namespace {

constexpr gfx::Rgb kHighlight = {0, 0, 128};
constexpr int kBar = ScrollBar::kThickness;

uint8_t *g_clip = nullptr;
size_t g_clip_cap = 0, g_clip_size = 0;

bool word_char(uint8_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
           (c >= 0xC0 && c != 0xD7 && c != 0xF7);
}

} // namespace

/* ---- Clipboard ---------------------------------------------------------------- */

void clipboard::init(uint8_t *buffer, size_t capacity)
{
    g_clip = buffer;
    g_clip_cap = buffer != nullptr ? capacity : 0;
    g_clip_size = 0;
}

void clipboard::set(const uint8_t *latin1, size_t size)
{
    if (g_clip == nullptr) {
        return;
    }
    g_clip_size = size < g_clip_cap ? size : g_clip_cap;
    memcpy(g_clip, latin1, g_clip_size);
}

const uint8_t *clipboard::data()
{
    return g_clip;
}

size_t clipboard::size()
{
    return g_clip_size;
}

/* ---- Set-up -------------------------------------------------------------------- */

TextEdit::TextEdit(const Rect &r)
    : Widget(r), vbar_({0, 0, kBar, 10}, true, &TextEdit::v_scrolled, this),
      hbar_({0, 0, 10, kBar}, false, &TextEdit::h_scrolled, this)
{
    vbar_.set_parent(this);
    hbar_.set_parent(this);
    layout();
}

void TextEdit::attach(uint8_t *text, size_t text_capacity, uint8_t *undo, size_t undo_capacity)
{
    buf_ = text;
    cap_ = text != nullptr ? text_capacity : 0;
    undo_ = undo;
    undo_cap_ = undo != nullptr ? undo_capacity : 0;
    clear();
}

void TextEdit::layout()
{
    vbar_.rect = {rect.w - 2 - kBar, 2, kBar, rect.h - 4 - kBar};
    hbar_.rect = {2, rect.h - 2 - kBar, rect.w - 4 - kBar, kBar};
    sync_bars();
    invalidate();
}

Rect TextEdit::text_rect() const
{
    return {2, 2, rect.w - 4 - kBar, rect.h - 4 - kBar};
}

int TextEdit::visible_rows() const
{
    const int rows = (text_rect().h - 2) / kLineHeight;
    return rows < 1 ? 1 : rows;
}

int TextEdit::visible_columns() const
{
    const int cols = (text_rect().w - 4) / 6;
    return cols < 1 ? 1 : cols;
}

bool TextEdit::load(const uint8_t *bytes, size_t size)
{
    if (buf_ == nullptr) {
        return false;
    }
    size_t n = 0;
    bool saw_cr = false, saw_lf = false;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] == '\r' && i + 1 < size && bytes[i + 1] == '\n') {
            saw_cr = true;
            continue; // CRLF becomes '\n'
        }
        if (bytes[i] == '\n') {
            saw_lf = true;
        }
        if (n == cap_) {
            return false;
        }
        buf_[n++] = bytes[i];
    }
    crlf_ = saw_cr || !saw_lf; // new and one-line files get CRLF, as in 98
    gap_start_ = n;
    gap_end_ = cap_;
    caret_ = anchor_ = 0;
    top_ = 0;
    top_line_ = 0;
    left_col_ = 0;
    want_column_ = -1;
    line_count_ = 1 + count_lines(0, n);
    longest_ = 0;
    for (size_t p = 0; p < n; p = next_line(p)) {
        const int cols = column_of(line_end(p));
        longest_ = cols > longest_ ? cols : longest_;
        if (line_end(p) == n) {
            break;
        }
    }
    undo_valid_ = false;
    modified_ = false;
    sync_bars();
    invalidate();
    return true;
}

size_t TextEdit::file_size() const
{
    return length() + (crlf_ ? static_cast<size_t>(line_count_ - 1) : 0);
}

size_t TextEdit::save_bytes(size_t offset, uint8_t *out, size_t room) const
{
    // Walk from the start to the text position of `offset`. ponytail:
    // O(n) per call; a file is saved in a few large pieces.
    size_t file = 0, i = 0;
    const size_t len = length();
    bool half = false; // offset falls between the CR and LF of a line end
    while (i < len && file < offset) {
        const size_t width = (crlf_ && at(i) == '\n') ? 2 : 1;
        if (file + width > offset) {
            half = true;
            break;
        }
        file += width;
        i++;
    }
    size_t n = 0;
    if (half && n < room) {
        out[n++] = '\n';
        i++;
    }
    for (; i < len && n < room; i++) {
        const uint8_t c = at(i);
        if (c == '\n' && crlf_) {
            if (n + 2 > room) {
                break;
            }
            out[n++] = '\r';
        }
        out[n++] = c;
    }
    return n;
}

/* ---- Gap buffer --------------------------------------------------------------- */

void TextEdit::move_gap(size_t pos)
{
    if (pos < gap_start_) {
        const size_t n = gap_start_ - pos;
        memmove(buf_ + gap_end_ - n, buf_ + pos, n);
        gap_start_ = pos;
        gap_end_ -= n;
    } else if (pos > gap_start_) {
        const size_t n = pos - gap_start_;
        memmove(buf_ + gap_start_, buf_ + gap_end_, n);
        gap_start_ += n;
        gap_end_ += n;
    }
}

void TextEdit::raw_insert(size_t pos, const uint8_t *data, size_t size)
{
    move_gap(pos);
    memcpy(buf_ + gap_start_, data, size);
    gap_start_ += size;
}

void TextEdit::raw_erase(size_t from, size_t to)
{
    move_gap(from);
    gap_end_ += to - from;
}

void TextEdit::replace(size_t from, size_t to, const uint8_t *data, size_t size, bool typing)
{
    if (buf_ == nullptr || (size > 0 && size > (gap_end_ - gap_start_) + (to - from))) {
        return; // does not fit
    }
    const size_t removed = to - from;
    // Undo: typing goes on the end of the step; anything else starts one.
    if (typing && undo_valid_ && undo_typing_ && removed == 0 && from == undo_pos_ + undo_inserted_ &&
        undo_removed_ + undo_inserted_ + size <= undo_cap_) {
        memcpy(undo_ + undo_removed_ + undo_inserted_, data, size);
        undo_inserted_ += size;
    } else if (removed + size <= undo_cap_) {
        for (size_t i = 0; i < removed; i++) {
            undo_[i] = at(from + i);
        }
        if (size > 0) {
            memcpy(undo_ + removed, data, size);
        }
        undo_valid_ = true;
        undo_typing_ = typing;
        undo_pos_ = from;
        undo_removed_ = removed;
        undo_inserted_ = size;
    } else {
        undo_valid_ = false;
    }

    const int lines_removed = count_lines(from, to);
    int lines_added = 0;
    for (size_t i = 0; i < size; i++) {
        lines_added += data[i] == '\n';
    }
    raw_erase(from, to);
    if (size > 0) {
        raw_insert(from, data, size);
    }
    line_count_ += lines_added - lines_removed;
    // Keep the first line shown where it was.
    if (top_ > from) {
        if (top_ >= to) {
            top_ = top_ - removed + size;
            top_line_ += lines_added - lines_removed;
        } else {
            top_ = line_start(from);
            top_line_ = count_lines(0, top_);
        }
    }
    caret_ = anchor_ = from + size;
    want_column_ = -1;
    const int cols = column_of(line_end(caret_));
    longest_ = cols > longest_ ? cols : longest_;
    modified_ = true;
    changed();
}

void TextEdit::changed()
{
    keep_caret_visible();
    sync_bars();
    invalidate();
    if (on_change_ != nullptr) {
        on_change_(*this, on_change_context_);
    }
}

void TextEdit::insert(const uint8_t *latin1, size_t size)
{
    // Typed text with no selection joins the previous typing in one undo step.
    replace(selection_start(), selection_end(), latin1, size, !has_selection());
}

void TextEdit::delete_selection()
{
    if (has_selection()) {
        replace(selection_start(), selection_end(), nullptr, 0, false);
    }
}

void TextEdit::undo()
{
    if (!undo_valid_) {
        return;
    }
    // Put the removed text back in place of the inserted text; the step
    // turns into its opposite, so a second undo redoes.
    const size_t pos = undo_pos_, removed = undo_removed_, inserted = undo_inserted_;
    const int lines_out = count_lines(pos, pos + inserted);
    int lines_in = 0;
    for (size_t i = 0; i < removed; i++) {
        lines_in += undo_[i] == '\n';
    }
    raw_erase(pos, pos + inserted);
    raw_insert(pos, undo_, removed);
    line_count_ += lines_in - lines_out;
    std::rotate(undo_, undo_ + removed, undo_ + removed + inserted);
    undo_removed_ = inserted;
    undo_inserted_ = removed;
    undo_typing_ = false;
    if (top_ > pos) {
        top_ = line_start(pos);
        top_line_ = count_lines(0, top_);
    }
    caret_ = anchor_ = pos + removed;
    modified_ = true;
    changed();
}

void TextEdit::copy()
{
    if (!has_selection()) {
        return;
    }
    const size_t s = selection_start(), e = selection_end();
    move_gap(e); // makes [s, e) one piece
    clipboard::set(buf_ + s, e - s);
}

void TextEdit::cut()
{
    copy();
    delete_selection();
}

void TextEdit::paste()
{
    if (clipboard::size() > 0) {
        replace(selection_start(), selection_end(), clipboard::data(), clipboard::size(), false);
    }
}

void TextEdit::select_all()
{
    select(0, length());
}

void TextEdit::select(size_t from, size_t to)
{
    anchor_ = from < length() ? from : length();
    caret_ = to < length() ? to : length();
    keep_caret_visible();
    invalidate();
}

/* ---- Lines -------------------------------------------------------------------- */

size_t TextEdit::line_start(size_t pos) const
{
    while (pos > 0 && at(pos - 1) != '\n') {
        pos--;
    }
    return pos;
}

size_t TextEdit::line_end(size_t pos) const
{
    const size_t len = length();
    while (pos < len && at(pos) != '\n') {
        pos++;
    }
    return pos;
}

size_t TextEdit::next_line(size_t pos) const
{
    const size_t e = line_end(pos);
    return e < length() ? e + 1 : length();
}

size_t TextEdit::previous_line(size_t pos) const
{
    const size_t s = line_start(pos);
    return s == 0 ? 0 : line_start(s - 1);
}

int TextEdit::column_of(size_t pos) const
{
    int col = 0;
    for (size_t i = line_start(pos); i < pos; i++) {
        col = at(i) == '\t' ? (col / kTab + 1) * kTab : col + 1;
    }
    return col;
}

size_t TextEdit::pos_at_column(size_t line, int column) const
{
    const size_t end = line_end(line);
    int col = 0;
    size_t i = line;
    while (i < end) {
        const int next = at(i) == '\t' ? (col / kTab + 1) * kTab : col + 1;
        if (next > column) {
            // Nearer to the end of the character: after it.
            if (column - col > next - column) {
                i++;
            }
            break;
        }
        col = next;
        i++;
    }
    return i;
}

int TextEdit::count_lines(size_t from, size_t to) const
{
    int n = 0;
    for (size_t i = from; i < to; i++) {
        n += at(i) == '\n';
    }
    return n;
}

int TextEdit::caret_line() const
{
    return caret_ >= top_ ? top_line_ + count_lines(top_, caret_) : top_line_ - count_lines(caret_, top_);
}

void TextEdit::scroll_to_line(int line)
{
    line = line < 0 ? 0 : (line >= line_count_ ? line_count_ - 1 : line);
    if (line == top_line_) {
        return;
    }
    if (line < top_line_ / 2) {
        top_ = 0; // nearer the start than the current top: count from there
        top_line_ = 0;
    }
    while (top_line_ < line) {
        top_ = next_line(top_);
        top_line_++;
    }
    while (top_line_ > line) {
        top_ = previous_line(top_);
        top_line_--;
    }
    invalidate();
}

void TextEdit::keep_caret_visible()
{
    const int rows = visible_rows();
    const int line = caret_line();
    if (line < top_line_) {
        scroll_to_line(line);
    } else if (line >= top_line_ + rows) {
        scroll_to_line(line - rows + 1);
    }
    const int col = column_of(caret_), cols = visible_columns();
    if (col < left_col_) {
        left_col_ = col;
        invalidate();
    } else if (col >= left_col_ + cols) {
        left_col_ = col - cols + 1;
        invalidate();
    }
    sync_bars();
}

void TextEdit::sync_bars()
{
    const int rows = visible_rows(), cols = visible_columns();
    vbar_.set_range(line_count_ > rows ? line_count_ - rows : 0, rows);
    hbar_.set_range(longest_ + 1 > cols ? longest_ + 1 - cols : 0, cols);
    // Quietly: setting the value here must not scroll again.
    vbar_.set_value_quietly(top_line_);
    hbar_.set_value_quietly(left_col_);
}

void TextEdit::v_scrolled(ScrollBar &bar, void *context)
{
    auto *t = static_cast<TextEdit *>(context);
    t->scroll_to_line(bar.value());
    t->invalidate();
}

void TextEdit::h_scrolled(ScrollBar &bar, void *context)
{
    auto *t = static_cast<TextEdit *>(context);
    t->left_col_ = bar.value();
    t->invalidate();
}

/* ---- Caret movement ----------------------------------------------------------- */

void TextEdit::move_caret(size_t to, bool extend, bool keep_column)
{
    caret_ = to > length() ? length() : to;
    if (!extend) {
        anchor_ = caret_;
    }
    if (!keep_column) {
        want_column_ = -1;
    }
    keep_caret_visible();
    invalidate();
}

size_t TextEdit::word_left(size_t pos) const
{
    while (pos > 0 && !word_char(at(pos - 1))) {
        pos--;
    }
    while (pos > 0 && word_char(at(pos - 1))) {
        pos--;
    }
    return pos;
}

size_t TextEdit::word_right(size_t pos) const
{
    const size_t len = length();
    while (pos < len && word_char(at(pos))) {
        pos++;
    }
    while (pos < len && !word_char(at(pos))) {
        pos++;
    }
    return pos;
}

size_t TextEdit::pos_at(int x, int y) const
{
    const Rect t = text_rect();
    int row = (y - t.y - 1) / kLineHeight;
    if (y < t.y) {
        row = -1;
    }
    size_t line = top_;
    for (int r = 0; r < row && next_line(line) != line && line_end(line) < length(); r++) {
        line = next_line(line);
    }
    if (row < 0) {
        line = previous_line(top_);
    }
    const int col = left_col_ + (x - t.x - 2 + 3) / 6;
    return pos_at_column(line, col < 0 ? 0 : col);
}

/* ---- Input -------------------------------------------------------------------- */

bool TextEdit::key(const KeyMsg &k)
{
    if (!k.pressed) {
        return false;
    }
    const bool shift = k.mods & kShift, ctrl = k.mods & kCtrl;
    const int rows = visible_rows();
    switch (k.usage) {
    case key::Left:
        if (!shift && has_selection()) {
            move_caret(selection_start(), false);
        } else {
            move_caret(ctrl ? word_left(caret_) : (caret_ > 0 ? caret_ - 1 : 0), shift);
        }
        return true;
    case key::Right:
        if (!shift && has_selection()) {
            move_caret(selection_end(), false);
        } else {
            move_caret(ctrl ? word_right(caret_) : caret_ + 1, shift);
        }
        return true;
    case key::Up:
    case key::Down:
    case key::PageUp:
    case key::PageDown: {
        const int col = want_column_ >= 0 ? want_column_ : column_of(caret_);
        const bool down = k.usage == key::Down || k.usage == key::PageDown;
        const int steps = (k.usage == key::Up || k.usage == key::Down) ? 1 : rows - 1;
        size_t line = line_start(caret_);
        for (int i = 0; i < steps; i++) {
            const size_t next = down ? (line_end(line) < length() ? line_end(line) + 1 : line)
                                     : previous_line(line);
            if (next == line) {
                break;
            }
            line = next;
        }
        if (steps > 1) {
            scroll_to_line(top_line_ + (down ? steps : -steps));
        }
        move_caret(pos_at_column(line, col), shift, true);
        want_column_ = col;
        return true;
    }
    case key::Home:
        move_caret(ctrl ? 0 : line_start(caret_), shift);
        return true;
    case key::End:
        move_caret(ctrl ? length() : line_end(caret_), shift);
        return true;
    case key::Backspace:
        if (has_selection()) {
            delete_selection();
        } else if (caret_ > 0) {
            replace(caret_ - 1, caret_, nullptr, 0, false);
        }
        return true;
    case key::Delete:
        if (shift) {
            cut();
        } else if (has_selection()) {
            delete_selection();
        } else if (caret_ < length()) {
            replace(caret_, caret_ + 1, nullptr, 0, false);
        }
        return true;
    case key::Enter: {
        const uint8_t nl = '\n';
        insert(&nl, 1);
        return true;
    }
    case key::Tab: {
        const uint8_t tab = '\t';
        insert(&tab, 1);
        return true;
    }
    default:
        break;
    }
    if (ctrl) {
        switch (k.usage) {
        case key::A:
            select_all();
            return true;
        case key::A + ('c' - 'a'):
            copy();
            return true;
        case key::A + ('x' - 'a'):
            cut();
            return true;
        case key::A + ('v' - 'a'):
            paste();
            return true;
        case key::A + ('z' - 'a'):
            undo();
            return true;
        default:
            break;
        }
    }
    return false;
}

void TextEdit::text(const char *utf8)
{
    uint8_t chars[64];
    size_t n = 0;
    for (uint8_t c; (c = gfx::next_latin1(utf8)) != 0;) {
        if (c >= 0x20 || c == '\t' || c == '\n') {
            chars[n++] = c;
        }
        if (n == sizeof(chars)) {
            insert(chars, n);
            n = 0;
        }
    }
    if (n > 0) {
        insert(chars, n);
    }
}

void TextEdit::mouse(const MouseMsg &m)
{
    if (m.kind == MouseKind::Down) {
        in_bar_ = vbar_.rect.contains(m.x, m.y) ? 1 : (hbar_.rect.contains(m.x, m.y) ? 2 : 0);
    }
    if (in_bar_ != 0) {
        ScrollBar &bar = in_bar_ == 1 ? vbar_ : hbar_;
        bar.mouse({m.kind, m.x - bar.rect.x, m.y - bar.rect.y});
        if (m.kind == MouseKind::Up) {
            in_bar_ = 0;
        }
        invalidate();
        return;
    }
    switch (m.kind) {
    case MouseKind::Down: {
        const size_t pos = pos_at(m.x, m.y);
        if (m.double_click) {
            size_t s = pos, e = pos;
            while (s > 0 && word_char(at(s - 1))) {
                s--;
            }
            while (e < length() && word_char(at(e))) {
                e++;
            }
            select(s, e);
            dragging_ = false;
        } else {
            move_caret(pos, false);
            dragging_ = true;
        }
        break;
    }
    case MouseKind::Move:
        if (dragging_) {
            move_caret(pos_at(m.x, m.y), true);
        }
        break;
    case MouseKind::Up:
        dragging_ = false;
        break;
    }
}

/* ---- Painting ------------------------------------------------------------------ */

void TextEdit::paint(Canvas &c)
{
    const Rect r = {0, 0, rect.w, rect.h};
    c.sunken(r);
    const Rect t = text_rect();
    c.fill(t, gfx::kWhite);
    c.fill({t.right(), t.bottom(), kBar, kBar}, gfx::kGrey); // corner between the bars
    {
        Canvas vb = c.sub(vbar_.rect);
        vbar_.paint(vb);
        Canvas hb = c.sub(hbar_.rect);
        hbar_.paint(hb);
    }
    if (buf_ == nullptr) {
        return;
    }
    Canvas inner = c.sub(t);
    const bool focus = has_focus();
    const size_t s0 = selection_start(), s1 = selection_end();
    const int rows = visible_rows() + 1, cols = visible_columns() + 1;
    size_t line = top_;
    const size_t len = length();
    for (int row = 0; row < rows; row++) {
        const int y = 1 + row * kLineHeight;
        // The visible cells of this line: one character each, tabs as spaces.
        uint8_t cells[160];
        bool chosen[160];
        int count = 0;
        int col = 0;
        size_t i = line;
        int caret_col = -1;
        for (; i < len && at(i) != '\n' && col < left_col_ + cols; i++) {
            if (i == caret_) {
                caret_col = col;
            }
            const uint8_t ch = at(i);
            const int next = ch == '\t' ? (col / kTab + 1) * kTab : col + 1;
            for (int k = col; k < next; k++) {
                if (k >= left_col_ && count < cols && count < 160) {
                    cells[count] = ch == '\t' ? ' ' : ch;
                    chosen[count] = i >= s0 && i < s1;
                    count++;
                }
            }
            col = next;
        }
        if (i == caret_ && caret_col < 0) {
            caret_col = col;
        }
        // A selected line end shows as one highlighted space.
        const bool eol_chosen = i < len && i >= s0 && i < s1;
        for (int a = 0; a < count;) {
            int b = a;
            while (b < count && chosen[b] == chosen[a]) {
                b++;
            }
            char run[330];
            int n = 0;
            for (int k = a; k < b; k++) {
                const uint8_t ch = cells[k];
                if (ch < 0x80) {
                    run[n++] = static_cast<char>(ch < 0x20 ? ' ' : ch);
                } else {
                    run[n++] = static_cast<char>(0xC0 | (ch >> 6));
                    run[n++] = static_cast<char>(0x80 | (ch & 0x3F));
                }
            }
            run[n] = 0;
            const int x = 2 + a * 6;
            if (chosen[a]) {
                inner.fill_rgb({x, y, (b - a) * 6, kLineHeight}, kHighlight);
            }
            inner.text(x, y, run, chosen[a] ? gfx::kWhite : gfx::kBlack);
            a = b;
        }
        if (eol_chosen && col >= left_col_) {
            inner.fill_rgb({2 + (col - left_col_) * 6, y, 6, kLineHeight}, kHighlight);
        }
        if (focus && caret_col >= left_col_ && caret_shown()) {
            inner.vline(1 + (caret_col - left_col_) * 6, y, kLineHeight - 1, gfx::kBlack);
        }
        if (i >= len) {
            break;
        }
        line = line_end(line) + 1;
    }
}

} // namespace wm
