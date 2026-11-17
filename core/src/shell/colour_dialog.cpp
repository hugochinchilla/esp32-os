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

#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "picker.hpp"

namespace shell {

namespace {

class ColourDialog : public wm::Window {
public:
    static constexpr int kColumns = 16, kRows = 16, kCellW = 18, kCellH = 14;
    static constexpr int kGridW = kColumns * kCellW, kBelow = 8 + kRows * kCellH;

    ColourDialog(wm::Window *owner, gfx::Colour start, ColourFn picked, void *context)
        : Window("Colour", {0, 0, kGridW + 16 + 2 * kBorder, kBelow + 68 + kClientTop + kBorder}, nullptr),
          grid_({8, 8, kGridW, kRows * kCellH}, kColumns, kColumns * kRows, kCellW, kCellH,
                &ColourDialog::paint_cell, &ColourDialog::changed, this, 1),
          levels_({8, kBelow + 6, kGridW, 14}, text_),
          ok_({8 + kGridW - 156, kBelow + 30, 75, 23}, "OK", &ColourDialog::ok, this),
          cancel_({8 + kGridW - 75, kBelow + 30, 75, 23}, "Cancel", &ColourDialog::cancel, this),
          picked_(picked), context_(context)
    {
        set_dialog(true);
        add(grid_);
        add(levels_);
        add(ok_);
        add(cancel_);
        ok_.set_default(true);
        set_default(&ok_);
        grid_.select_quietly(cell_of(start));
        describe();
        set_modal(owner);
        set_frame(os::centred(frame().w, frame().h));
        set_focus(&grid_);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
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
    static void paint_cell(gfx::Canvas &c, const gfx::Rect &cell, int index, void *) { c.fill(cell, colour_of(index)); }
    static void changed(void *context) { static_cast<ColourDialog *>(context)->describe(); }
    void describe()
    {
        const gfx::Colour c = colour_of(grid_.selected());
        const gfx::Rgb rgb = gfx::shown(c);
        snprintf(text_, sizeof(text_), "Red %d of 7   Green %d of 7   Blue %d of 3   #%02X%02X%02X", c >> 5, (c >> 2) & 7,
                 c & 3, rgb.r, rgb.g, rgb.b);
        levels_.invalidate();
    }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<ColourDialog *>(context);
        const gfx::Colour colour = colour_of(d->grid_.selected());
        const ColourFn picked = d->picked_;
        void *owner = d->context_;
        d->close();
        picked(colour, owner);
    }
    static void cancel(wm::Button &, void *context) { static_cast<ColourDialog *>(context)->close(); }

    char text_[64] = "";
    Picker grid_;
    wm::Label levels_;
    wm::Button ok_, cancel_;
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
    if (ColourDialog *d = make_window<ColourDialog>(window_numbered(owner), start, chosen, context)) {
        os::show(*d);
        discard(d); // if the window manager had no room for it
    } else {
        no_memory_for("Colour");
    }
}

} // namespace shell
