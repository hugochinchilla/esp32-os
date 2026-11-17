// The BIOS's screen: a true text screen, 80 by 30 characters of 8x16
// (Spleen 8x16) on 640x480, each cell a character and a colour on black.
// What is printed through the cursor also goes out as plain text, so a
// serial terminal shows the same lines (docs/journal/2026-11-17-bios-stage.md).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"

namespace bios {

constexpr int kColumns = 80, kRows = 30;
constexpr int kCellWidth = 8, kCellHeight = 16;

class TextScreen {
public:
    // Plain text for a serial terminal: printable ASCII, "\r\n" between
    // lines, and "\r" with ESC [ K when a line is written again in place.
    using Out = void (*)(const char *bytes, size_t n, void *context);

    TextScreen() { clear(); }
    void set_out(Out out, void *context)
    {
        out_ = out;
        out_context_ = context;
    }

    // Every cell empty, the cursor at the top left. Nothing is sent.
    void clear();

    // Cells no text is ever painted on: a picture sits there (the mascot).
    void reserve(int column, int row, int columns, int rows);

    // Writes into the grid only, not through the cursor and not to serial:
    // for text at a fixed place, like the bottom lines.
    void put(int column, int row, const char *text, gfx::Colour colour);
    // Sends text to serial only, where the serial cursor is (for what put()
    // placed).
    void send_only(const char *text);

    // The cursor, as on a terminal. move_to() goes down to the start of
    // `row` (never up); print() writes at the cursor on the screen and on
    // serial; restart_line() empties the cursor's row and goes back to its
    // first column, to write it again.
    void move_to(int row);
    void print(const char *text, gfx::Colour colour);
    void restart_line();
    int cursor_row() const { return row_; }
    int cursor_column() const { return column_; }

    // Paints the cells that changed since the last paint (paint_all: every
    // cell). A cell is written whole, character and background, so nothing
    // flashes. Reserved cells are left alone.
    void paint(gfx::Canvas &c);
    void paint_all(gfx::Canvas &c);

    char at(int column, int row) const { return cells_[row][column].ch; }
    gfx::Colour colour_at(int column, int row) const { return cells_[row][column].colour; }
    // A row as text, without the spaces at its end.
    void row_text(int row, char *out, size_t capacity) const;

private:
    struct Cell {
        char ch = ' ';
        gfx::Colour colour = gfx::kBlack;
        uint8_t flags = 0; // kDirty, kReserved
    };
    static constexpr uint8_t kDirty = 1, kReserved = 2;
    void set(int column, int row, char ch, gfx::Colour colour);
    void send(const char *bytes, size_t n);

    Cell cells_[kRows][kColumns];
    int row_ = 0, column_ = 0;
    Out out_ = nullptr;
    void *out_context_ = nullptr;
};

} // namespace bios
