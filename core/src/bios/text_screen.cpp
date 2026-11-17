#include "core/bios/text_screen.hpp"

#include <cstring>

#include "core/gfx/font.hpp"

namespace bios {

void TextScreen::clear()
{
    for (auto &row : cells_) {
        for (auto &cell : row) {
            cell = {' ', gfx::kBlack, static_cast<uint8_t>(kDirty | (cell.flags & kReserved))};
        }
    }
    row_ = column_ = 0;
}

void TextScreen::reserve(int column, int row, int columns, int rows)
{
    for (int r = row; r < row + rows && r < kRows; r++) {
        for (int q = column; q < column + columns && q < kColumns; q++) {
            if (r >= 0 && q >= 0) {
                cells_[r][q].flags |= kReserved;
            }
        }
    }
}

void TextScreen::set(int column, int row, char ch, gfx::Colour colour)
{
    if (row < 0 || row >= kRows || column < 0 || column >= kColumns) {
        return;
    }
    Cell &cell = cells_[row][column];
    if (ch < 0x20 || ch > 0x7E) {
        ch = '?';
    }
    if (cell.ch != ch || cell.colour != colour) {
        cell.ch = ch;
        cell.colour = colour;
        cell.flags |= kDirty;
    }
}

void TextScreen::put(int column, int row, const char *text, gfx::Colour colour)
{
    for (; *text != 0; text++, column++) {
        set(column, row, *text, colour);
    }
}

void TextScreen::send(const char *bytes, size_t n)
{
    if (out_ != nullptr && n > 0) {
        out_(bytes, n, out_context_);
    }
}

void TextScreen::send_only(const char *text) { send(text, strlen(text)); }

void TextScreen::move_to(int row)
{
    if (row >= kRows) {
        row = kRows - 1;
    }
    while (row_ < row) {
        send("\r\n", 2);
        row_++;
        column_ = 0;
    }
}

void TextScreen::print(const char *text, gfx::Colour colour)
{
    const char *start = text;
    for (; *text != 0 && column_ < kColumns; text++, column_++) {
        set(column_, row_, *text, colour);
    }
    send(start, static_cast<size_t>(text - start));
}

void TextScreen::restart_line()
{
    for (int q = 0; q < kColumns; q++) {
        set(q, row_, ' ', gfx::kBlack);
    }
    column_ = 0;
    send("\r\x1b[K", 4);
}

void TextScreen::paint_all(gfx::Canvas &c)
{
    for (auto &row : cells_) {
        for (auto &cell : row) {
            cell.flags |= kDirty;
        }
    }
    paint(c);
}

void TextScreen::paint(gfx::Canvas &c)
{
    const gfx::WideFont &f = gfx::kSpleen8x16;
    for (int r = 0; r < kRows; r++) {
        for (int q = 0; q < kColumns; q++) {
            Cell &cell = cells_[r][q];
            if ((cell.flags & kDirty) == 0) {
                continue;
            }
            cell.flags &= static_cast<uint8_t>(~kDirty);
            if (cell.flags & kReserved) {
                continue;
            }
            const uint8_t *g = cell.ch != ' ' ? f.glyph(cell.ch) : nullptr;
            const int x = q * kCellWidth, y = r * kCellHeight;
            if (g == nullptr) {
                c.fill({x, y, kCellWidth, kCellHeight}, gfx::kBlack);
                continue;
            }
            for (int row = 0; row < kCellHeight; row++) {
                // Runs of the same colour, each written once.
                const uint8_t bits = g[row];
                for (int p = 0; p < kCellWidth;) {
                    const bool on = (bits & (0x80 >> p)) != 0;
                    int n = 1;
                    while (p + n < kCellWidth && ((bits & (0x80 >> (p + n))) != 0) == on) {
                        n++;
                    }
                    c.fill({x + p, y + row, n, 1}, on ? cell.colour : gfx::kBlack);
                    p += n;
                }
            }
        }
    }
}

void TextScreen::row_text(int row, char *out, size_t capacity) const
{
    if (capacity == 0) {
        return;
    }
    int end = kColumns;
    while (end > 0 && cells_[row][end - 1].ch == ' ') {
        end--;
    }
    size_t n = 0;
    for (int q = 0; q < end && n + 1 < capacity; q++) {
        out[n++] = cells_[row][q].ch;
    }
    out[n] = 0;
}

} // namespace bios
