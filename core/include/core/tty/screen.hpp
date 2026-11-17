// A terminal's screen as a grid of characters: what the terminal window
// shows (backlog item 15; docs/journal/2026-11-17-terminal-window.md). It is
// the device end of a TTY: bytes written to it are text for the grid and the
// few control codes and escape sequences a terminal has to know.
//
//     characters   UTF-8; one cell each, kept as Latin-1 like the fonts
//     CR LF BS     start of line, one line down, one cell left
//     TAB BEL      the next column that is a multiple of 8; counted (bells())
//     ESC [ 2 J    erase the screen           ESC [ J   from the cursor on
//     ESC [ r;c H  cursor to row r, column c  ESC [ H   to the top left
//     ESC [ K      erase to the end of the line
//     ESC [ n A/B/C/D   cursor up, down, right, left
//
// Other escape sequences are read to their end and ignored (colours).
//
// A line that reaches the right edge carries on at the start of the next
// one, and a backspace at the start of a line goes back to the end of the
// one above, so the TTY's line editing works on lines longer than the
// screen is wide.
//
// Lines pushed off the top are kept, kHistory of them, and can be looked at
// again (scroll_view). No heap: the whole grid is in the object, 16 KB.
#pragma once

#include <cstdint>

#include "core/ports.hpp"

namespace tty {

class Screen : public ports::ByteSink {
public:
    static constexpr int kColumns = 80, kRows = 25;
    static constexpr int kHistory = 175; // lines kept above the screen
    static constexpr int kLines = kRows + kHistory;

    Screen();

    void write(const uint8_t *data, size_t size) override;
    void write(const char *text); // up to the 0 (tests)

    // A row of what is in view, top to bottom: kColumns Latin-1 characters,
    // no 0 at the end.
    const uint8_t *row(int index) const;
    // Where the cursor is in the view; the row is -1 when the view has been
    // scrolled so far back that the cursor is not in it.
    int cursor_column() const { return column_; }
    int cursor_row() const { return row_ + scrolled_ < kRows ? row_ + scrolled_ : -1; }

    // Looking back: how many lines above the screen exist, how many of them
    // the view has been moved up by, and moving it (positive: further back).
    // Anything written brings the view back to the screen itself.
    int history() const { return history_; }
    int scrolled() const { return scrolled_; }
    void scroll_view(int lines);

    // What changed since this was last asked: the first and last row of the
    // view that need painting again. False if nothing did.
    bool take_changes(int &first, int &last);
    // Bells rung since this was last asked.
    int take_bells();

    void clear(); // an empty screen and no history

private:
    uint8_t *live(int row) { return cells_[(top_ + row) % kLines]; }
    void changed(int first, int last);
    void character(uint8_t c);
    void control(uint8_t c);
    void sequence(uint8_t last);
    void line_down();
    void erase(int row, int from, int to);
    int number(int index, int otherwise) const;

    uint8_t cells_[kLines][kColumns];
    int top_ = 0;      // which line of cells_ is the screen's first row
    int history_ = 0;
    int scrolled_ = 0;
    int column_ = 0, row_ = 0;
    bool wrap_pending_ = false; // the last column was written: the next character starts a new line

    enum class State : uint8_t { Text, Escape, Sequence };
    State state_ = State::Text;
    static constexpr int kMaxNumbers = 4;
    int numbers_[kMaxNumbers] = {};
    int number_count_ = 0;
    bool number_started_ = false;
    int utf8_more_ = 0;
    uint32_t utf8_code_ = 0;

    int first_changed_ = 0, last_changed_ = kRows - 1; // a new screen wants painting
    int bells_ = 0;
};

} // namespace tty
