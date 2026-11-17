// The colour dialog, for any program: all of the board's 256 colours on one
// sheet of 16 by 16, to pick one. Modal to the window that asked, which
// gets the colour when OK is pressed (Display's "Other..." is its first
// user). Made when asked for and gone once closed, as the file dialogs.
//
// The board keeps a colour as red and green in eight levels and blue in
// four, so there are four squares of 8 by 8, one for each level of blue.
// Red grows to the right in the left-hand squares and back to the left in
// the right-hand ones; green grows downwards in the upper squares and back
// up in the lower ones. Blue goes round: none top left, then top right,
// bottom right, and full bottom left. So each square starts where its
// neighbour ended: dark at the top, the light colours across the middle,
// the blues and purples at the bottom. How it came to this, with the
// sheets that were set aside: docs/journal/2026-11-17-colour-chooser.md.
#include <cstdio>

#include "core/app/app.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

class ColourDialog : public app::Window {
public:
    static constexpr int kColumns = 16, kRows = 16, kCellW = 18, kCellH = 14;
    static constexpr int kGridW = kColumns * kCellW, kBelow = 8 + kRows * kCellH;

    ColourDialog(gfx::Colour start, ColourFn picked, void *context)
        : grid_(grid({8, 8, kGridW, kRows * kCellH}, kColumns, kColumns * kRows, kCellW, kCellH, 1)),
          levels_(plain({8, kBelow + 6, kGridW, 14})),
          ok_(button({8 + kGridW - 156, kBelow + 30, 75, 23}, "OK")),
          cancel_(button({8 + kGridW - 75, kBelow + 30, 75, 23}, "Cancel")),
          picked_(picked), context_(context)
    {
        set_default(ok_);
        grid_.select(cell_of(start));
        describe();
        set_focus(grid_);
    }

    void command(app::Control which) override
    {
        if (which == ok_) {
            const gfx::Colour colour = colour_of(grid_.selected());
            const ColourFn picked = picked_;
            void *owner = context_;
            close();
            picked(colour, owner);
        } else if (which == cancel_) {
            close();
        }
    }
    void changed(app::Control) override { describe(); }
    void paint_cell(app::Control, int index, const gfx::Rect &cell, app::Canvas &c) override
    {
        c.fill(cell, colour_of(index));
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

    // The board colour in a cell, and the cell of a board colour.
    static gfx::Colour colour_of(int cell)
    {
        const int x = cell % kColumns, y = cell / kColumns;
        const int r = x < 8 ? x : 15 - x, g = y < 8 ? y : 15 - y;
        const int b = y < 8 ? (x < 8 ? 0 : 1) : (x < 8 ? 3 : 2);
        return gfx::rgb(static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b));
    }
    static int cell_of(gfx::Colour c)
    {
        const int r = c >> 5, g = (c >> 2) & 7, b = c & 3;
        return (b < 2 ? g : 15 - g) * kColumns + (b == 0 || b == 3 ? r : 15 - r);
    }

private:
    void describe()
    {
        const gfx::Colour c = colour_of(grid_.selected());
        const gfx::Rgb rgb = gfx::shown(c);
        char text[64];
        snprintf(text, sizeof(text), "Red %d of 7   Green %d of 7   Blue %d of 3   #%02X%02X%02X", c >> 5, (c >> 2) & 7,
                 c & 3, rgb.r, rgb.g, rgb.b);
        levels_.set(text);
    }

    app::Grid grid_;
    app::Plain levels_;
    app::Button ok_, cancel_;
    ColourFn picked_;
    void *context_;
};

} // namespace

void colour_dialog(int owner, gfx::Colour start, ColourFn chosen, void *context)
{
    // A window that already has a dialog up gets that one brought forward.
    if (has_dialog(owner)) {
        return;
    }
    app::Look look;
    look.title = "Colour";
    look.width = ColourDialog::kGridW + 16;
    look.height = ColourDialog::kBelow + 68;
    look.flags = app::kDialog | app::kClientSize;
    look.over = owner;
    if (app::open<ColourDialog>(look, start, chosen, context) == app::kNone) {
        no_memory_for("Colour");
    }
}

} // namespace shell
