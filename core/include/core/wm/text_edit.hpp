// Multi-line text editing (the Editor's page), and the clipboard.
//
// The text lives in a gap buffer: one block of memory (external RAM on the
// board) with the free space kept at the caret, so typing moves no more
// than the distance from the previous edit and nothing is allocated per
// keystroke. Text is Latin-1, lines end in '\n' inside; files with CRLF are
// read and written back with CRLF.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/wm/widgets.hpp"

namespace wm {

namespace clipboard {
// The clipboard's memory, given once (64 KB is plenty). Without it, copying
// does nothing.
void init(uint8_t *buffer, size_t capacity);
void set(const uint8_t *latin1, size_t size);
const uint8_t *data();
size_t size();
} // namespace clipboard

class TextEdit : public Widget {
public:
    using ChangeFn = void (*)(TextEdit &edit, void *context);
    static constexpr int kLineHeight = 13;
    static constexpr int kTab = 8;

    explicit TextEdit(const Rect &r);

    // The memory for the text and for undo, given once before use.
    void attach(uint8_t *text, size_t text_capacity, uint8_t *undo, size_t undo_capacity);
    bool attached() const { return buf_ != nullptr; }
    size_t capacity() const { return cap_; }

    // Replaces the whole text (a file's bytes: CRLF becomes '\n').
    // Returns false if it does not fit.
    bool load(const uint8_t *bytes, size_t size);
    void clear() { load(nullptr, 0); }
    // The same in pieces, for files read a part at a time: begin, append
    // each piece (false when it does not fit), finish.
    void load_begin();
    bool load_append(const uint8_t *bytes, size_t size);
    void load_finish();
    // The file's bytes again, in pieces: `offset` counts bytes of the file
    // (with CRLF if the file had it). Returns how many were written to `out`.
    size_t save_bytes(size_t offset, uint8_t *out, size_t room) const;
    size_t file_size() const; // length of the file save_bytes() produces

    size_t length() const { return cap_ - (gap_end_ - gap_start_); }
    uint8_t at(size_t i) const { return i < gap_start_ ? buf_[i] : buf_[i + (gap_end_ - gap_start_)]; }
    bool crlf() const { return crlf_; }
    void set_crlf(bool on) { crlf_ = on; }

    bool modified() const { return modified_; }
    void set_modified(bool on) { modified_ = on; }
    void set_on_change(ChangeFn fn, void *context)
    {
        on_change_ = fn;
        on_change_context_ = context;
    }

    // Editing, for menus as well as keys.
    void insert(const uint8_t *latin1, size_t size);
    void delete_selection();
    void undo();
    bool can_undo() const { return undo_valid_; }
    void cut();
    void copy();
    void paste();
    void select_all();
    void select(size_t from, size_t to); // caret ends at `to`

    size_t caret() const { return caret_; }
    size_t selection_start() const { return caret_ < anchor_ ? caret_ : anchor_; }
    size_t selection_end() const { return caret_ < anchor_ ? anchor_ : caret_; }
    bool has_selection() const { return caret_ != anchor_; }
    int line_count() const { return line_count_; }
    int top_line() const { return top_line_; }
    int caret_line() const; // 0-based
    int visible_rows() const;
    int visible_columns() const;
    int left_column() const { return left_col_; }

    // After the widget's rect changed.
    void layout();

    void paint(gfx::Canvas &c) override;
    bool wants_mouse() const override { return true; }
    void mouse(const MouseMsg &m) override;
    bool focusable() const override { return true; }
    bool key(const KeyMsg &k) override;
    void text(const char *utf8) override;
    Rect caret_rect() const override;

private:
    // What to repaint: the text area, some lines (numbers, clipped to what
    // shows), a scroll bar if it changed.
    void invalidate_text();
    void invalidate_lines(int first, int last);
    int line_of(size_t pos) const; // line number of a position
    // Gap buffer.
    void move_gap(size_t pos);
    void raw_insert(size_t pos, const uint8_t *data, size_t size);
    void raw_erase(size_t from, size_t to);
    // An edit with undo: replaces [from, to) with `data`.
    void replace(size_t from, size_t to, const uint8_t *data, size_t size, bool typing);
    // After an edit starting on `first_line`: keeps the caret in view and
    // repaints that line, or down to the bottom when lines came or went.
    void changed(int first_line, bool lines_moved);

    // Lines.
    size_t line_start(size_t pos) const;
    size_t line_end(size_t pos) const;
    size_t next_line(size_t pos) const;     // start of the next line, or length()
    size_t previous_line(size_t pos) const; // start of the line before pos's line
    int column_of(size_t pos) const;        // with tabs expanded
    size_t pos_at_column(size_t line, int column) const;
    int count_lines(size_t from, size_t to) const; // '\n' in [from, to)
    void scroll_to_line(int line);
    void keep_caret_visible();
    void sync_bars();
    void move_caret(size_t to, bool extend, bool keep_column = false);
    size_t word_left(size_t pos) const;
    size_t word_right(size_t pos) const;
    size_t pos_at(int x, int y) const; // widget coordinates
    Rect text_rect() const;

    static void v_scrolled(ScrollBar &, void *context);
    static void h_scrolled(ScrollBar &, void *context);

    uint8_t *buf_ = nullptr;
    size_t cap_ = 0;
    size_t gap_start_ = 0, gap_end_ = 0;
    bool crlf_ = true;
    bool modified_ = false;

    size_t caret_ = 0, anchor_ = 0;
    int want_column_ = -1; // remembered column for Up and Down
    size_t top_ = 0;       // start of the first line shown
    int top_line_ = 0;
    int left_col_ = 0;
    int line_count_ = 1;
    int longest_ = 0; // columns of the longest line seen (only grows)
    bool dragging_ = false;
    int in_bar_ = 0;  // mouse captured by: 1 vertical bar, 2 horizontal bar

    // One step of undo: at undo_pos_, `removed` bytes were replaced by
    // `inserted` bytes; both are kept in undo_ (removed first).
    uint8_t *undo_ = nullptr;
    size_t undo_cap_ = 0;
    bool undo_valid_ = false;
    bool undo_typing_ = false;
    size_t undo_pos_ = 0, undo_removed_ = 0, undo_inserted_ = 0;

    // Where the last save_bytes() piece ended, so the next one carries on.
    mutable size_t save_file_ = 0, save_text_ = 0;
    // While loading in pieces.
    bool load_cr_ = false, load_saw_cr_ = false, load_saw_lf_ = false;

    ChangeFn on_change_ = nullptr;
    void *on_change_context_ = nullptr;
    ScrollBar vbar_, hbar_;
};

} // namespace wm
