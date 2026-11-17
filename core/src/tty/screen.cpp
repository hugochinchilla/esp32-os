#include "core/tty/screen.hpp"

#include <cstring>

namespace tty {

Screen::Screen() { memset(cells_, ' ', sizeof cells_); }

void Screen::clear()
{
    memset(cells_, ' ', sizeof cells_);
    top_ = history_ = scrolled_ = 0;
    column_ = row_ = 0;
    wrap_pending_ = false;
    state_ = State::Text;
    utf8_more_ = 0;
    changed(0, kRows - 1);
}

const uint8_t *Screen::row(int index) const
{
    return cells_[(top_ - scrolled_ + index + kLines) % kLines];
}

void Screen::changed(int first, int last)
{
    first_changed_ = first < first_changed_ ? first : first_changed_;
    last_changed_ = last > last_changed_ ? last : last_changed_;
}

bool Screen::take_changes(int &first, int &last)
{
    if (first_changed_ > last_changed_) {
        return false;
    }
    first = first_changed_;
    last = last_changed_;
    first_changed_ = kRows;
    last_changed_ = -1;
    return true;
}

int Screen::take_bells()
{
    const int count = bells_;
    bells_ = 0;
    return count;
}

void Screen::scroll_view(int lines)
{
    int to = scrolled_ + lines;
    to = to < 0 ? 0 : to > history_ ? history_ : to;
    if (to != scrolled_) {
        scrolled_ = to;
        changed(0, kRows - 1);
    }
}

void Screen::erase(int row, int from, int to)
{
    if (from < to) {
        memset(live(row) + from, ' ', static_cast<size_t>(to - from));
        changed(row, row);
    }
}

void Screen::line_down()
{
    if (row_ < kRows - 1) {
        changed(row_, row_ + 1); // the cursor leaves one row for the other
        row_++;
        return;
    }
    // The top row goes into the history and a new, empty row comes in at
    // the bottom: only top_ moves, no line is copied.
    top_ = (top_ + 1) % kLines;
    history_ = history_ < kHistory ? history_ + 1 : kHistory;
    memset(live(kRows - 1), ' ', kColumns);
    changed(0, kRows - 1);
}

void Screen::character(uint8_t c)
{
    if (wrap_pending_) {
        wrap_pending_ = false;
        column_ = 0;
        line_down();
    }
    live(row_)[column_] = c;
    changed(row_, row_);
    if (column_ == kColumns - 1) {
        wrap_pending_ = true;
    } else {
        column_++;
    }
}

void Screen::control(uint8_t c)
{
    switch (c) {
    case '\r':
        column_ = 0;
        wrap_pending_ = false;
        changed(row_, row_);
        break;
    case '\n':
        wrap_pending_ = false;
        line_down();
        break;
    case '\b':
        if (wrap_pending_) {
            wrap_pending_ = false; // back onto the last column
        } else if (column_ > 0) {
            column_--;
        } else if (row_ > 0) {
            changed(row_ - 1, row_);
            row_--;
            column_ = kColumns - 1;
        }
        changed(row_, row_);
        break;
    case '\t': {
        const int stop = (column_ / 8 + 1) * 8;
        column_ = stop < kColumns ? stop : kColumns - 1;
        changed(row_, row_);
        break;
    }
    case '\a': bells_++; break;
    case 0x1B: state_ = State::Escape; break;
    default: break;
    }
}

int Screen::number(int index, int otherwise) const
{
    return index < number_count_ && numbers_[index] > 0 ? numbers_[index] : otherwise;
}

void Screen::sequence(uint8_t last)
{
    const int n = number(0, 1);
    const int before = row_;
    switch (last) {
    case 'A': row_ = row_ - n < 0 ? 0 : row_ - n; break;
    case 'B': row_ = row_ + n > kRows - 1 ? kRows - 1 : row_ + n; break;
    case 'C': column_ = column_ + n > kColumns - 1 ? kColumns - 1 : column_ + n; break;
    case 'D': column_ = column_ - n < 0 ? 0 : column_ - n; break;
    case 'H':
    case 'f': {
        const int row = number(0, 1), column = number(1, 1);
        row_ = row > kRows ? kRows - 1 : row - 1;
        column_ = column > kColumns ? kColumns - 1 : column - 1;
        break;
    }
    case 'J':
        if (number(0, 0) == 2) {
            for (int r = 0; r < kRows; r++) {
                erase(r, 0, kColumns);
            }
        } else if (number(0, 0) == 0) {
            erase(row_, column_, kColumns);
            for (int r = row_ + 1; r < kRows; r++) {
                erase(r, 0, kColumns);
            }
        }
        return; // the cursor stays
    case 'K':
        if (number(0, 0) == 2) {
            erase(row_, 0, kColumns);
        } else if (number(0, 0) == 0) {
            erase(row_, column_, kColumns);
        }
        return;
    default: return; // colours and the rest: not understood, not shown
    }
    wrap_pending_ = false;
    changed(before < row_ ? before : row_, before > row_ ? before : row_);
}

void Screen::write(const uint8_t *data, size_t size)
{
    if (size > 0 && scrolled_ != 0) {
        scrolled_ = 0;
        changed(0, kRows - 1);
    }
    for (size_t i = 0; i < size; i++) {
        const uint8_t byte = data[i];
        if (state_ == State::Escape) {
            if (byte == '[') {
                state_ = State::Sequence;
                number_count_ = 0;
                number_started_ = false;
            } else {
                state_ = State::Text; // ESC and one more byte: nothing we know
            }
            continue;
        }
        if (state_ == State::Sequence) {
            if (byte >= '0' && byte <= '9') {
                if (!number_started_ && number_count_ < kMaxNumbers) {
                    numbers_[number_count_++] = 0;
                    number_started_ = true;
                }
                if (number_started_ && numbers_[number_count_ - 1] < 1000) {
                    numbers_[number_count_ - 1] = numbers_[number_count_ - 1] * 10 + (byte - '0');
                }
            } else if (byte == ';') {
                if (!number_started_ && number_count_ < kMaxNumbers) {
                    numbers_[number_count_++] = 0; // an empty place: the default
                }
                number_started_ = false;
            } else if (byte >= 0x40 && byte <= 0x7E) {
                state_ = State::Text;
                sequence(byte);
            } else if (byte < 0x20) {
                state_ = State::Text; // broken off
                control(byte);
            }
            continue;
        }
        // UTF-8 into the characters the fonts have.
        if (utf8_more_ > 0) {
            if ((byte & 0xC0) == 0x80) {
                utf8_code_ = (utf8_code_ << 6) | (byte & 0x3F);
                if (--utf8_more_ == 0) {
                    character(utf8_code_ >= 0xA0 && utf8_code_ <= 0xFF ? static_cast<uint8_t>(utf8_code_) : '?');
                }
                continue;
            }
            utf8_more_ = 0;
            character('?');
        }
        if (byte < 0x20) {
            control(byte);
        } else if (byte < 0x7F) {
            character(byte);
        } else if (byte >= 0xC0) {
            utf8_more_ = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : 1;
            utf8_code_ = byte & (0x3F >> utf8_more_);
        } else if (byte != 0x7F) {
            character('?');
        }
    }
}

void Screen::write(const char *text) { write(reinterpret_cast<const uint8_t *>(text), strlen(text)); }

} // namespace tty
