// Pincel: a drawing program. A sheet of the board's 256 colours, eight
// tools down the left, the colours along the bottom. It saves BMP files of
// 8 bits a pixel and opens every kind of picture the system decodes.
//
// What the tools do to the sheet is in sheet.cpp; this file is the window.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/app/app.hpp"
#include "core/image/convert.hpp"
#include "core/image/image.hpp"
#include "core/job/job.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "art.hpp"
#include "sheet.hpp"

namespace pincel {

namespace {

enum Tool : uint8_t { kPencil, kBrush, kEraser, kLine, kBox, kEllipse, kFill, kPicker, kTools };
const char *const *const kToolArt[kTools] = {art::kPencil, art::kBrush, art::kEraser, art::kLine,
                                             art::kBox,    art::kEllipse, art::kFill, art::kPicker};
const char *const kToolNames[kTools] = {"Pencil", "Brush", "Eraser", "Line", "Rectangle", "Ellipse", "Fill",
                                        "Pick a colour"};

// The four sizes of the options box: of the brush, of a line and of the
// edge of a shape; the eraser's are larger.
constexpr int kSizes = 4;
constexpr int kSize[kSizes] = {1, 2, 4, 7};
constexpr int kEraserSize[kSizes] = {4, 6, 10, 16};

// The colours on offer when the program is first used: a dark row and a
// light one. A double click on one changes it; they are kept in pincel.ini.
constexpr int kPaletteColumns = 14, kPaletteRows = 2, kPalette = kPaletteColumns * kPaletteRows;
constexpr gfx::Colour kFirstPalette[kPalette] = {
    gfx::rgb(0, 0, 0), gfx::rgb(2, 2, 1), gfx::rgb(4, 0, 0), gfx::rgb(4, 4, 0), gfx::rgb(0, 4, 0),
    gfx::rgb(0, 4, 2), gfx::rgb(0, 0, 2), gfx::rgb(4, 0, 2), gfx::rgb(4, 2, 0), gfx::rgb(6, 3, 0),
    gfx::rgb(2, 4, 1), gfx::rgb(0, 2, 1), gfx::rgb(2, 2, 3), gfx::rgb(4, 2, 2),
    gfx::rgb(7, 7, 3), gfx::rgb(5, 5, 2), gfx::rgb(7, 0, 0), gfx::rgb(7, 7, 0), gfx::rgb(0, 7, 0),
    gfx::rgb(0, 7, 3), gfx::rgb(0, 0, 3), gfx::rgb(7, 0, 3), gfx::rgb(7, 5, 2), gfx::rgb(7, 4, 0),
    gfx::rgb(5, 7, 2), gfx::rgb(4, 6, 3), gfx::rgb(5, 4, 3), gfx::rgb(7, 4, 3),
};

constexpr int kFirstWidth = 320, kFirstHeight = 240; // a new sheet

// Where things are in the window.
constexpr int kMargin = 4;
constexpr int kToolCell = 26;                   // a tool's button
constexpr int kLeft = 2 * kToolCell + 2 * kMargin; // where the sheet's area begins
constexpr int kSizeRow = 12, kStyleCell = 16;
constexpr int kOptionsH = kSizes * kSizeRow + kStyleCell + 12;
constexpr int kSwatch = 16;                     // a colour of the palette
constexpr int kPaletteH = kPaletteRows * kSwatch + 2;
constexpr int kStatusH = 20;
constexpr int kBar = os::ui::kScrollBarThickness;

// Image > Size: the sheet's width and height.
class SizeDialog : public app::Window {
public:
    using Done = void (*)(app::Number owner, int width, int height);

    SizeDialog(app::Number owner, int width, int height, Done done)
        : owner_(owner), done_(done),
          width_label_(label({12, 15, 60, 14}, "&Width:")),
          width_(text_field({76, 11, 60, 22}, number_text(width))),
          height_label_(label({12, 43, 60, 14}, "&Height:")),
          height_(text_field({76, 39, 60, 22}, number_text(height))),
          limit_(label({12, 68, 220, 14}, "In dots, 800 by 600 at most.")),
          ok_(button({76, 92, 75, 23}, "OK")),
          cancel_(button({157, 92, 75, 23}, "Cancel"))
    {
        set_default(ok_);
        set_focus(width_);
        width_.select_all();
    }

    void command(app::Control which) override
    {
        char text[16];
        width_.get(text, sizeof(text));
        const int w = atoi(text);
        height_.get(text, sizeof(text));
        const int h = atoi(text);
        const Done done = done_;
        const app::Number owner = owner_;
        if (which == ok_ && (w < 1 || w > kMaxWidth || h < 1 || h > kMaxHeight)) {
            shell::message_box("Pincel", "The sheet can be from 1 by 1 to 800 by 600 dots.");
            return;
        }
        close();
        if (which == ok_) {
            done(owner, w, h);
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

private:
    const char *number_text(int n)
    {
        snprintf(text_, sizeof(text_), "%d", n);
        return text_;
    }

    char text_[16] = "";
    app::Number owner_;
    Done done_;
    app::Label width_label_;
    app::TextField width_;
    app::Label height_label_;
    app::TextField height_;
    app::Label limit_;
    app::Button ok_, cancel_;
};

enum class Then : uint8_t { Nothing, New, Open, OpenPath, Exit };

// More than one Pincel can run at once, each a process of its own with its
// own sheet, as the Editor. A sheet takes its size twice over in external
// RAM: the picture, and the copy Undo goes back to.
class PincelWindow : public app::Window {
public:
    PincelWindow()
        : tools_(picture({kMargin, kMargin, 2 * kToolCell, (kTools / 2) * kToolCell}, app::kMouse)),
          options_(picture({kMargin, 2 * kMargin + (kTools / 2) * kToolCell, 2 * kToolCell, kOptionsH}, app::kMouse)),
          view_(picture({kLeft, kMargin, 10, 10}, app::kOnce | app::kMouse)),
          vbar_(scroll_bar({0, 0, 0, 0}, true)),
          hbar_(scroll_bar({0, 0, 0, 0}, false)),
          palette_(picture({kMargin, 0, 10, kPaletteH}, app::kMouse)),
          status_(picture({0, 0, 10, kStatusH}, app::kOnce))
    {
        set_document(path_);
        file_items_[0] = {"&New", &PincelWindow::menu_new, this, nullptr, nullptr, 0};
        file_items_[1] = {"&Open...", &PincelWindow::menu_open, this, nullptr, nullptr, 0};
        file_items_[2] = {"&Save", &PincelWindow::menu_save, this, nullptr, nullptr, 0};
        file_items_[3] = {"Save &As...", &PincelWindow::menu_save_as, this, nullptr, nullptr, 0};
        file_items_[4] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[5] = {"E&xit", &PincelWindow::menu_exit, this, nullptr, nullptr, 0};
        edit_items_[0] = {"&Undo", &PincelWindow::menu_undo, this, nullptr, nullptr, 0};
        image_items_[0] = {"&Size...", &PincelWindow::menu_size, this, nullptr, nullptr, 0};
        image_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        image_items_[2] = {"Flip &horizontal", &PincelWindow::menu_flip_h, this, nullptr, nullptr, 0};
        image_items_[3] = {"Flip &vertical", &PincelWindow::menu_flip_v, this, nullptr, nullptr, 0};
        image_items_[4] = {"&Invert colours", &PincelWindow::menu_invert, this, nullptr, nullptr, 0};
        image_items_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        image_items_[6] = {"&Clear", &PincelWindow::menu_clear, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        memcpy(colours_, kFirstPalette, sizeof(colours_));
        read_settings();
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
    }

    // The sheet and its copy go back with the window.
    ~PincelWindow() override
    {
        decoder_.release(os::memory());
        converter_.release(os::memory());
        os::release(sheet_.pixels);
        os::release(undo_);
    }

    // From the program's way in: the file to open, or a new sheet.
    void open_path(const char *path)
    {
        if (!new_sheet(kFirstWidth, kFirstHeight)) {
            shell::message_box("Pincel", "There is not enough memory for Pincel.");
            return;
        }
        retitle();
        app::show(number());
        if (path != nullptr) {
            load(path);
        }
    }

    void resized(int w, int h) override
    {
        width_ = w - kLeft - kMargin;
        height_ = h - 3 * kMargin - kPaletteH - kStatusH;
        palette_.place({kMargin, h - kStatusH - kMargin - kPaletteH, w - 2 * kMargin, kPaletteH});
        status_.place({0, h - kStatusH, w, kStatusH});
        status_w_ = w;
        layout();
    }

    void changed(app::Control) override { view_.redraw(); } // a scroll bar moved

    bool closing() override
    {
        then(Then::Exit);
        return true; // then() closes it once any changes are dealt with
    }

    void menu_opening(int entry) override
    {
        if (entry == 1) {
            edit_items_[0].flags = can_undo_ ? 0 : wm::kMenuDisabled;
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed || (k.mods & wm::kCtrl) == 0 || drawing_) {
            return false;
        }
        switch (k.usage - wm::key::A + 'A') {
        case 'Z': undo(); return true;
        case 'S': save(false); return true;
        case 'O': then(Then::Open); return true;
        case 'N': then(Then::New); return true;
        default: return false;
        }
    }

    void paint_in(app::Control which, app::Canvas &c) override
    {
        if (which == view_) {
            paint_view(c);
        } else if (which == tools_) {
            paint_tools(c);
        } else if (which == options_) {
            paint_options(c);
        } else if (which == palette_) {
            paint_palette(c);
        } else if (which == status_) {
            const gfx::Rect r = {0, 0, status_w_, kStatusH};
            c.dent(r);
            const int ty = (kStatusH - 12) / 2;
            c.fill_around(r.inset(1), {4, ty, app::Canvas::text_width(status_text_), 12}, gfx::face());
            c.text_opaque(4, ty, status_text_, gfx::ink(), gfx::face());
        }
    }

    void mouse(app::Control which, const wm::MouseMsg &m) override
    {
        if (which == view_) {
            mouse_on_sheet(m);
        } else if (m.kind != wm::MouseKind::Down) {
            return;
        } else if (which == tools_) {
            const int i = (m.y / kToolCell) * 2 + m.x / kToolCell;
            if (m.x >= 0 && m.y >= 0 && m.x < 2 * kToolCell && i < kTools) {
                choose_tool(static_cast<Tool>(i));
            }
        } else if (which == options_) {
            if (m.y >= 4 && m.y < 4 + kSizes * kSizeRow && sized(tool_)) {
                size_[tool_] = static_cast<uint8_t>((m.y - 4) / kSizeRow);
            } else if (m.y >= style_y() && m.y < style_y() + kStyleCell && shaped(tool_)) {
                const int i = (m.x - 2) / kStyleCell;
                if (m.x >= 2 && i < 3) {
                    style_ = static_cast<Style>(i);
                }
            }
            options_.redraw();
        } else if (which == palette_) {
            const int i = swatch_at(m.x, m.y);
            if (i < 0) {
                return;
            }
            ink_ = colours_[i];
            palette_.redraw();
            if (m.double_click) {
                editing_ = i;
                shell::colour_dialog(number(), colours_[i], &PincelWindow::colour_chosen, this);
            }
        }
    }

    // The right button on a colour: the paper, which the eraser leaves and
    // a shape is filled with.
    void right_click(app::Control which, int x, int y) override
    {
        const int i = which == palette_ ? swatch_at(x, y) : -1;
        if (i >= 0) {
            paper_ = colours_[i];
            palette_.redraw();
        }
    }

    // For Image > Size's dialog: the sheet made another size. What was
    // drawn stays, from the top left corner; new room is paper.
    void set_size(int w, int h)
    {
        if (w == sheet_.width && h == sheet_.height) {
            return;
        }
        Sheet old = sheet_;
        uint8_t *old_undo = undo_;
        sheet_ = Sheet();
        undo_ = nullptr;
        if (!new_sheet(w, h)) {
            sheet_ = old;
            undo_ = old_undo;
            shell::message_box("Pincel", "There is not enough memory for a sheet of that size.");
            return;
        }
        const int rows = old.height < h ? old.height : h, columns = old.width < w ? old.width : w;
        for (int y = 0; y < rows; y++) {
            memcpy(sheet_.pixels + y * w, old.pixels + y * old.width, static_cast<size_t>(columns));
        }
        os::release(old.pixels);
        os::release(old_undo);
        set_modified(true);
        sheet_changed();
    }

#ifdef APP_LINKED
    // For the tests: the sheet, and the colours in use.
    const Sheet &sheet() const { return sheet_; }
    gfx::Colour ink() const { return ink_; }
    gfx::Colour paper() const { return paper_; }
#endif

private:
    /* ---- The sheet ---- */

    // A sheet of paper of this size in place of the one there is, with its
    // copy for Undo. False, and nothing changed, if there is no memory.
    bool new_sheet(int w, int h)
    {
        const size_t bytes = static_cast<size_t>(w) * h;
        uint8_t *pixels = static_cast<uint8_t *>(os::allocate(bytes));
        uint8_t *undo = static_cast<uint8_t *>(os::allocate(bytes));
        if (pixels == nullptr || undo == nullptr) {
            os::release(pixels);
            os::release(undo);
            return false;
        }
        os::release(sheet_.pixels);
        os::release(undo_);
        sheet_ = {pixels, w, h};
        undo_ = undo;
        memset(pixels, paper_, bytes);
        can_undo_ = false;
        return true;
    }

    size_t sheet_bytes() const { return static_cast<size_t>(sheet_.width) * sheet_.height; }

    // Before anything is drawn: what Undo goes back to.
    void keep_for_undo()
    {
        memcpy(undo_, sheet_.pixels, sheet_bytes());
        can_undo_ = true;
        set_modified(true);
    }

    // Undo, and Undo again to have it back: the sheet and its copy change
    // places.
    void undo()
    {
        if (!can_undo_ || drawing_) {
            return;
        }
        uint8_t *t = sheet_.pixels;
        sheet_.pixels = undo_;
        undo_ = t;
        set_modified(true);
        view_.redraw();
    }

    // The sheet is another one, or another size.
    void sheet_changed()
    {
        vbar_.set_value(0);
        hbar_.set_value(0);
        layout();
        view_.redraw();
        say_size();
    }

    void set_modified(bool on)
    {
        if (modified_ != on) {
            modified_ = on;
            retitle();
        }
    }

    /* ---- The window's parts ---- */

    // The sheet's area and its scroll bars: a bar only where the sheet is
    // larger than the area.
    void layout()
    {
        need_h_ = sheet_.width > width_;
        need_v_ = sheet_.height > height_ - (need_h_ ? kBar : 0);
        need_h_ = sheet_.width > width_ - (need_v_ ? kBar : 0);
        const gfx::Rect a = area();
        view_.place({kLeft, kMargin, a.w, a.h});
        vbar_.place(need_v_ ? gfx::Rect{kLeft + a.w, kMargin, kBar, a.h} : gfx::Rect{0, 0, 0, 0});
        hbar_.place(need_h_ ? gfx::Rect{kLeft, kMargin + a.h, a.w, kBar} : gfx::Rect{0, 0, 0, 0});
        vbar_.set_range(sheet_.height > a.h ? sheet_.height - a.h : 0, a.h);
        hbar_.set_range(sheet_.width > a.w ? sheet_.width - a.w : 0, a.w);
    }

    // The size of the sheet's area, as w and h.
    gfx::Rect area() const { return {0, 0, width_ - (need_v_ ? kBar : 0), height_ - (need_h_ ? kBar : 0)}; }
    int scroll_x() const { return need_h_ ? hbar_.value() : 0; }
    int scroll_y() const { return need_v_ ? vbar_.value() : 0; }

    // The sheet from its top left corner, and the desk around it: each
    // pixel once.
    void paint_view(app::Canvas &c)
    {
        const gfx::Rect a = area();
        const gfx::Rect at = {-scroll_x(), -scroll_y(), sheet_.width, sheet_.height};
        const gfx::Rect shown = at.intersect(a);
        if (sheet_.pixels != nullptr && !shown.empty()) {
            const gfx::Bitmap b = {sheet_.width, sheet_.height, sheet_.pixels, -1};
            c.blit(b, at.x, at.y);
        }
        c.fill_around(a, shown, gfx::dim());
    }

    void paint_tools(app::Canvas &c)
    {
        for (int i = 0; i < kTools; i++) {
            const gfx::Rect r = {(i % 2) * kToolCell, (i / 2) * kToolCell, kToolCell, kToolCell};
            const bool on = i == tool_;
            c.button(r, on ? gfx::ButtonLook::Checked : gfx::ButtonLook::Raised);
            const int at = (kToolCell - 16) / 2 + (on ? 1 : 0);
            c.pattern(r.x + at, r.y + at, kToolArt[i], 16, gfx::ink());
        }
    }

    static bool sized(Tool t) { return t == kBrush || t == kEraser || t == kLine || t == kBox || t == kEllipse; }
    static bool shaped(Tool t) { return t == kBox || t == kEllipse; }
    static int style_y() { return 4 + kSizes * kSizeRow + 4; }

    // The options of the tool in use: how thick it draws, and for a shape
    // whether it is an outline, an outline filled with the paper, or solid.
    void paint_options(app::Canvas &c)
    {
        const gfx::Rect r = {0, 0, 2 * kToolCell, kOptionsH};
        c.fill(r.inset(1), gfx::face());
        c.dent(r);
        const bool sizes = sized(tool_);
        for (int i = 0; i < kSizes; i++) {
            const gfx::Rect row = {2, 4 + i * kSizeRow, r.w - 4, kSizeRow};
            const bool on = sizes && i == size_[tool_];
            if (on) {
                c.fill_rgb(row, gfx::theme().select);
            }
            const int thick = kSize[i];
            c.fill({row.x + 6, row.y + (kSizeRow - thick) / 2, row.w - 12, thick},
                   on ? gfx::theme().select_ink : sizes ? gfx::ink() : gfx::dim());
        }
        const gfx::Colour line = shaped(tool_) ? gfx::ink() : gfx::dim();
        for (int i = 0; i < 3; i++) {
            const gfx::Rect cell = {2 + i * kStyleCell, style_y(), kStyleCell, kStyleCell};
            if (shaped(tool_) && i == static_cast<int>(style_)) {
                c.fill_rgb(cell, gfx::theme().select);
            }
            const gfx::Rect shape = {cell.x + 3, cell.y + 4, kStyleCell - 6, kStyleCell - 8};
            if (i == 2) {
                c.fill(shape, line);
            } else {
                if (i == 1) {
                    c.fill(shape.inset(1), gfx::paper());
                }
                c.edge(shape, line, line);
            }
        }
    }

    gfx::Rect swatch_rect(int i) const
    {
        return {kLeft - kMargin + (i % kPaletteColumns) * kSwatch, 1 + (i / kPaletteColumns) * kSwatch, kSwatch,
                kSwatch};
    }
    int swatch_at(int x, int y) const
    {
        for (int i = 0; i < kPalette; i++) {
            if (swatch_rect(i).contains(x, y)) {
                return i;
            }
        }
        return -1;
    }

    // The two colours in use, the ink over the paper, and the palette.
    void paint_palette(app::Canvas &c)
    {
        const gfx::Rect box = {0, 0, 2 * kToolCell, kPaletteH};
        c.fill({0, 0, status_w_, kPaletteH}, gfx::face()); // a picture paints all of itself
        c.dent(box);
        const gfx::Rect paper = {22, 12, 18, 18}, ink = {11, 4, 18, 18};
        c.fill(paper.inset(1), paper_);
        c.raised(paper);
        c.fill(paper.inset(2), paper_);
        c.fill(ink.inset(1), ink_);
        c.raised(ink);
        c.fill(ink.inset(2), ink_);
        for (int i = 0; i < kPalette; i++) {
            const gfx::Rect r = swatch_rect(i);
            c.fill(r.inset(2), colours_[i]);
            c.sunken({r.x, r.y, r.w - 1, r.h - 1});
        }
    }

    void set_status(const char *text)
    {
        if (strcmp(status_text_, text) != 0) {
            snprintf(status_text_, sizeof(status_text_), "%s", text);
            status_.redraw();
        }
    }

    void say_size()
    {
        char text[64];
        snprintf(text, sizeof(text), "%s   %d x %d", kToolNames[tool_], sheet_.width, sheet_.height);
        set_status(text);
    }

    void choose_tool(Tool t)
    {
        tool_ = t;
        tools_.redraw();
        options_.redraw();
        say_size();
    }

    /* ---- Drawing ---- */

    Tip tip() const
    {
        switch (tool_) {
        case kEraser: return {kEraserSize[size_[tool_]], true};
        case kPencil: return {1, false};
        default: return {kSize[size_[tool_]], false};
        }
    }

    // The part of the sheet that has changed, painted again.
    void show(const gfx::Rect &part)
    {
        if (!part.empty()) {
            view_.redraw(part.offset(-scroll_x(), -scroll_y()).intersect(area()));
        }
    }

    // A line or a shape from where the button went down to (x, y), drawn
    // over the sheet as it was: what it covered before is put back first.
    void stretch(int x, int y)
    {
        const gfx::Rect before = shape_;
        for (int row = before.y; row < before.bottom(); row++) {
            const size_t at = static_cast<size_t>(row) * sheet_.width + before.x;
            memcpy(sheet_.pixels + at, undo_ + at, static_cast<size_t>(before.w));
        }
        shape_ = gfx::Rect();
        const int edge = kSize[size_[tool_]];
        if (tool_ == kLine) {
            line(sheet_, from_x_, from_y_, x, y, tip(), ink_, shape_);
        } else if (tool_ == kBox) {
            box(sheet_, from_x_, from_y_, x, y, edge, style_, ink_, paper_, shape_);
        } else {
            ellipse(sheet_, from_x_, from_y_, x, y, edge, style_, ink_, paper_, shape_);
        }
        show(gfx::bounding(before, shape_));
    }

    void pick(int x, int y)
    {
        if (x >= 0 && y >= 0 && x < sheet_.width && y < sheet_.height) {
            ink_ = sheet_.pixels[y * sheet_.width + x];
            palette_.redraw();
        }
    }

    void fill_from(int x, int y)
    {
        if (x < 0 || y < 0 || x >= sheet_.width || y >= sheet_.height) {
            return;
        }
        keep_for_undo();
        // Working memory for as long as the fill takes; with none, a few
        // places on the stack do, slowly.
        constexpr int kSeeds = 8192;
        Seed few[32];
        Seed *seeds = static_cast<Seed *>(os::allocate(kSeeds * sizeof(Seed)));
        gfx::Rect part;
        {
            const os::Busy hourglass;
            flood(sheet_, x, y, ink_, undo_, seeds != nullptr ? seeds : few, seeds != nullptr ? kSeeds : 32, part);
        }
        os::release(seeds);
        show(part);
    }

    void mouse_on_sheet(const wm::MouseMsg &m)
    {
        if (sheet_.pixels == nullptr) {
            return;
        }
        const int x = m.x + scroll_x(), y = m.y + scroll_y();
        if (m.kind == wm::MouseKind::Down) {
            drawing_ = true;
            from_x_ = last_x_ = x;
            from_y_ = last_y_ = y;
            shape_ = gfx::Rect();
            if (tool_ == kPicker) {
                pick(x, y);
            } else if (tool_ == kFill) {
                fill_from(x, y);
            } else {
                keep_for_undo();
                if (tool_ == kPencil || tool_ == kBrush || tool_ == kEraser) {
                    gfx::Rect part;
                    stamp(sheet_, x, y, tip(), tool_ == kEraser ? paper_ : ink_, part);
                    show(part);
                } else {
                    stretch(x, y);
                }
            }
        } else if (!drawing_) {
            return;
        } else if (tool_ == kPicker) {
            pick(x, y);
        } else if (tool_ == kPencil || tool_ == kBrush || tool_ == kEraser) {
            gfx::Rect part;
            line(sheet_, last_x_, last_y_, x, y, tip(), tool_ == kEraser ? paper_ : ink_, part);
            show(part);
        } else if (tool_ != kFill && (x != last_x_ || y != last_y_)) {
            stretch(x, y);
        }
        last_x_ = x;
        last_y_ = y;
        if (m.kind == wm::MouseKind::Up) {
            drawing_ = false;
            say_size();
            return;
        }
        // Where the pointer is on the sheet, and how large the shape is.
        char text[64];
        if (tool_ == kLine || tool_ == kBox || tool_ == kEllipse) {
            snprintf(text, sizeof(text), "%d, %d   %d x %d", x, y, abs(x - from_x_) + 1, abs(y - from_y_) + 1);
        } else {
            snprintf(text, sizeof(text), "%d, %d", x, y);
        }
        set_status(text);
    }

    static void colour_chosen(gfx::Colour colour, void *context)
    {
        auto *w = static_cast<PincelWindow *>(context);
        w->colours_[w->editing_] = colour;
        w->ink_ = colour;
        w->palette_.redraw();
        w->write_settings();
    }

    /* ---- Settings: C:\.config\pincel.ini ---- */

    // The palette, as two hexadecimal digits a colour.
    void read_settings()
    {
        fs::Ini ini;
        char text[2 * kPalette + 1];
        if (!os::load_settings("pincel", ini) || !ini.get("palette", "colours", text, sizeof(text)) ||
            strlen(text) != 2 * kPalette) {
            return;
        }
        for (int i = 0; i < kPalette; i++) {
            const char pair[3] = {text[2 * i], text[2 * i + 1], 0};
            colours_[i] = static_cast<gfx::Colour>(strtol(pair, nullptr, 16));
        }
    }

    void write_settings()
    {
        fs::Ini ini;
        os::load_settings("pincel", ini);
        char text[2 * kPalette + 1];
        for (int i = 0; i < kPalette; i++) {
            snprintf(text + 2 * i, 3, "%02x", colours_[i]);
        }
        ini.set("palette", "colours", text);
        os::save_settings("pincel", ini);
    }

    /* ---- Files ---- */

    // Does `what` once any unsaved changes have been saved or let go.
    void then(Then what)
    {
        if (locked()) {
            return; // it is being saved
        }
        if (modified_ && on_screen()) {
            next_ = what;
            static const char *const buttons[] = {"&Yes", "&No", "Cancel"};
            snprintf(message_, sizeof(message_), "The picture %s has changed.\nDo you want to save the changes?",
                     name());
            shell::ask("Pincel", message_, buttons, 3, &PincelWindow::save_answer, this);
            return;
        }
        perform(what);
    }

    static void save_answer(int choice, void *context)
    {
        auto *w = static_cast<PincelWindow *>(context);
        if (choice == 0) {
            w->save(true); // performs next_ when saved
        } else if (choice == 1) {
            w->perform(w->next_);
        }
    }

    void perform(Then what)
    {
        next_ = Then::Nothing;
        switch (what) {
        case Then::New:
            if (!new_sheet(kFirstWidth, kFirstHeight)) {
                shell::message_box("Pincel", "There is not enough memory for a new sheet.");
                return;
            }
            path_[0] = 0;
            modified_ = false;
            retitle();
            sheet_changed();
            break;
        case Then::Open:
            shell::open_file_dialog(number(), folder(), &PincelWindow::chosen_to_open, this);
            break;
        case Then::Exit:
            close();
            break;
        default:
            break;
        }
    }

    // The picture's file, as the decoder reads it: a handle of the OS.
    struct FromHandle : image::Source {
        bool read(void *data, size_t size, size_t &got) override
        {
            return os::file::read(file, data, size, got) == ports::FsError::Ok;
        }
        bool seek(uint32_t offset) override { return os::file::seek(file, offset) == ports::FsError::Ok; }
        os::file::Handle file = os::file::kNoFile;
    };

    // Opens a picture of any kind the system decodes, as a sheet of its
    // size, or smaller in the same shape if it is larger than a sheet can
    // be. Colours the board does not have are dithered; those it has stay.
    void load(const char *path)
    {
        const os::Busy hourglass;
        FromHandle source;
        image::Info info;
        image::Error e = image::Error::NotFound;
        if (os::file::open(path, ports::OpenMode::Read, source.file) == ports::FsError::Ok) {
            const gfx::Rgb back = gfx::shown(paper_);
            const uint8_t background[3] = {back.r, back.g, back.b};
            e = decoder_.open(source, os::memory(), info, background);
        }
        if (e == image::Error::None && !converter_.ready(os::memory())) {
            e = image::Error::NoMemory;
        }
        int w = info.width, h = info.height;
        if (e == image::Error::None) {
            if (w > kMaxWidth || h > kMaxHeight) {
                if (static_cast<long>(w) * kMaxHeight > static_cast<long>(h) * kMaxWidth) {
                    h = static_cast<int>(static_cast<long>(h) * kMaxWidth / w);
                    w = kMaxWidth;
                } else {
                    w = static_cast<int>(static_cast<long>(w) * kMaxHeight / h);
                    h = kMaxHeight;
                }
            }
            w = w < 1 ? 1 : w;
            h = h < 1 ? 1 : h;
            if (!new_sheet(w, h)) {
                e = image::Error::NoMemory;
            }
        }
        if (e == image::Error::None) {
            converter_.begin(info.width, info.height, w, h, sheet_.pixels, image::Dither::FloydSteinberg);
            while (e == image::Error::None && !decoder_.done()) {
                e = decoder_.step(converter_, 16);
            }
            // A damaged file: what was read of it is on the sheet.
            snprintf(path_, sizeof(path_), "%s", path);
            modified_ = false;
            retitle();
            sheet_changed();
        }
        decoder_.release(os::memory());
        converter_.release(os::memory());
        if (source.file != os::file::kNoFile) {
            os::file::close(source.file);
        }
        if (e != image::Error::None) {
            snprintf(message_, sizeof(message_), "Cannot open %s:\n%s.", fs::path::name(path), image::error_text(e));
            shell::message_box("Pincel", message_);
        }
    }

    // Pincel writes BMP files only: a picture opened from another kind of
    // file is saved under a name of its own.
    bool is_bmp() const
    {
        char ext[8];
        fs::path::extension(path_, ext, sizeof(ext));
        return strcmp(ext, "bmp") == 0;
    }

    void save(bool continue_after)
    {
        if (path_[0] == 0 || !is_bmp()) {
            save_as(continue_after);
            return;
        }
        if (locked()) {
            return; // a save is already running
        }
        // The file is written by a job on a task of its own, as the Editor
        // does: the window is locked until it has finished, so the sheet
        // stays as it is and the window stays open.
        continue_after_save_ = continue_after;
        bmp_header(sheet_.width, sheet_.height, header_);
        set_locked(true);
        job::Spec spec;
        spec.name = "save";
        spec.work = &PincelWindow::write_out;
        spec.done = &PincelWindow::saved;
        spec.context = this;
        spec.stack_bytes = 8192; // the file system's calls and the paths
        const os::Busy hourglass;
        if (!os::start_job(spec)) {
            set_locked(false);
            next_ = Then::Nothing;
            snprintf(message_, sizeof(message_), "Cannot save %s:\nthe system is too busy. Try again in a moment.",
                     name());
            shell::message_box("Pincel", message_);
        }
    }

    // The job: on its own task. It reads the header, the sheet and the
    // path, which the locked window keeps as they are. The rows go from the
    // last to the first, each filled up to a multiple of four bytes.
    static uint32_t write_out(const job::Job &job, void *context)
    {
        auto *w = static_cast<PincelWindow *>(context);
        os::file::Handle save;
        ports::FsError e = os::file::begin_save(w->path_, save, &job);
        if (e == ports::FsError::Ok) {
            e = os::file::write(save, w->header_, sizeof(w->header_));
        }
        const uint8_t fill[4] = {};
        const int extra = bmp_row(w->sheet_.width) - w->sheet_.width;
        for (int y = w->sheet_.height - 1; y >= 0 && e == ports::FsError::Ok; y--) {
            e = os::file::write(save, w->sheet_.pixels + static_cast<size_t>(y) * w->sheet_.width,
                                static_cast<size_t>(w->sheet_.width));
            if (extra > 0 && e == ports::FsError::Ok) {
                e = os::file::write(save, fill, static_cast<size_t>(extra));
            }
        }
        if (save != os::file::kNoFile) {
            const ports::FsError c = os::file::finish_save(save, e == ports::FsError::Ok);
            e = e != ports::FsError::Ok ? e : c;
        }
        return static_cast<uint32_t>(e);
    }

    // Back where the program's events run, when the job has finished.
    static void saved(void *context, uint32_t result, bool /*stopped*/)
    {
        auto *w = static_cast<PincelWindow *>(context);
        w->set_locked(false);
        const ports::FsError e = static_cast<ports::FsError>(result);
        if (e != ports::FsError::Ok) {
            w->next_ = Then::Nothing;
            snprintf(w->message_, sizeof(w->message_), "Cannot save %s:\n%s.", w->name(), fs::error_text(e));
            shell::message_box("Pincel", w->message_);
            return;
        }
        w->set_modified(false);
        if (w->continue_after_save_) {
            w->perform(w->next_);
        }
    }

    void save_as(bool continue_after)
    {
        continue_after_save_as_ = continue_after;
        // The name it has, as a BMP file.
        char suggested[fs::kMaxPath] = "untitled.bmp";
        if (path_[0] != 0) {
            snprintf(suggested, sizeof(suggested), "%s", fs::path::name(path_));
            as_bmp(suggested, sizeof(suggested));
        }
        shell::save_file_dialog(number(), folder(), suggested, &PincelWindow::chosen_to_save, this);
    }

    // "tree.png" -> "tree.bmp", "tree" -> "tree.bmp".
    static void as_bmp(char *name, size_t capacity)
    {
        char ext[8];
        fs::path::extension(name, ext, sizeof(ext));
        if (strcmp(ext, "bmp") == 0) {
            return;
        }
        char *dot = strrchr(name, '.');
        if (dot != nullptr && ext[0] != 0) {
            *dot = 0;
        }
        const size_t n = strlen(name);
        snprintf(name + n, capacity - n, ".bmp");
    }

    static void chosen_to_save(const char *path, void *context)
    {
        auto *w = static_cast<PincelWindow *>(context);
        snprintf(w->path_, sizeof(w->path_), "%s", path);
        as_bmp(w->path_, sizeof(w->path_));
        w->retitle();
        w->save(w->continue_after_save_as_);
    }

    static void chosen_to_open(const char *path, void *context) { static_cast<PincelWindow *>(context)->load(path); }

    const char *name() const { return path_[0] ? fs::path::name(path_) : "Untitled"; }

    // The folder of the file, for the file dialogs (C:\pictures by default).
    const char *folder()
    {
        if (path_[0] == 0) {
            return "C:\\pictures";
        }
        fs::path::parent(path_, folder_, sizeof(folder_));
        return folder_;
    }

    // "tree.bmp - Pincel", with a * after the name while there are changes.
    void retitle()
    {
        snprintf(title_, sizeof(title_), "%s%s - Pincel", name(), modified_ ? "*" : "");
        set_title(title_);
    }

    /* ---- The menus ---- */

    // Something done to the whole sheet, which Undo takes back.
    void whole(void (*what)(Sheet &))
    {
        keep_for_undo();
        what(sheet_);
        view_.redraw();
    }

    static void menu_new(void *c) { static_cast<PincelWindow *>(c)->then(Then::New); }
    static void menu_open(void *c) { static_cast<PincelWindow *>(c)->then(Then::Open); }
    static void menu_save(void *c) { static_cast<PincelWindow *>(c)->save(false); }
    static void menu_save_as(void *c) { static_cast<PincelWindow *>(c)->save_as(false); }
    static void menu_exit(void *c) { static_cast<PincelWindow *>(c)->then(Then::Exit); }
    static void menu_undo(void *c) { static_cast<PincelWindow *>(c)->undo(); }
    static void menu_flip_h(void *c) { static_cast<PincelWindow *>(c)->whole(&flip_horizontal); }
    static void menu_flip_v(void *c) { static_cast<PincelWindow *>(c)->whole(&flip_vertical); }
    static void menu_invert(void *c) { static_cast<PincelWindow *>(c)->whole(&invert); }
    static void menu_clear(void *c)
    {
        auto *w = static_cast<PincelWindow *>(c);
        w->keep_for_undo();
        memset(w->sheet_.pixels, w->paper_, w->sheet_bytes());
        w->view_.redraw();
    }
    static void menu_size(void *c)
    {
        auto *w = static_cast<PincelWindow *>(c);
        if (os::ui::has_dialog(w->number())) {
            return;
        }
        app::Look look;
        look.title = "Size";
        look.width = 252;
        look.height = 152;
        look.flags = app::kDialog;
        look.over = w->number();
        if (app::open<SizeDialog>(look, w->number(), w->sheet_.width, w->sheet_.height, &PincelWindow::size_chosen) ==
            app::kNone) {
            shell::message_box("Pincel", "There is not enough memory for Size.");
        }
    }
    static void size_chosen(app::Number owner, int width, int height)
    {
        if (PincelWindow *w = app::program<PincelWindow>(owner)) {
            w->set_size(width, height);
        }
    }

    Sheet sheet_;
    uint8_t *undo_ = nullptr; // the sheet before the last change, of the same size
    bool can_undo_ = false;
    bool modified_ = false;
    Tool tool_ = kPencil;
    uint8_t size_[kTools] = {0, 2, 2, 0, 0, 0, 0, 0}; // each tool's own, a place in kSize
    Style style_ = Style::Outline;
    gfx::Colour ink_ = gfx::kBlack, paper_ = gfx::kWhite;
    gfx::Colour colours_[kPalette];
    int editing_ = 0; // the colour the colour dialog is open for
    // While the button is held on the sheet: where it went down, where the
    // pointer was last, and what the line or shape being pulled covers.
    bool drawing_ = false;
    int from_x_ = 0, from_y_ = 0, last_x_ = 0, last_y_ = 0;
    gfx::Rect shape_;
    int width_ = 10, height_ = 10; // of the sheet's area with its scroll bars
    int status_w_ = 10;
    bool need_v_ = false, need_h_ = false;
    char path_[fs::kMaxPath] = "";
    char folder_[fs::kMaxPath] = "";
    char title_[96] = "";
    char message_[200] = "";
    char status_text_[64] = "";
    Then next_ = Then::Nothing;
    bool continue_after_save_as_ = false;
    bool continue_after_save_ = false;
    uint8_t header_[kBmpHeader]; // of the file being saved, for the job
    image::Decoder decoder_;
    image::Converter converter_;
    app::Picture tools_, options_, view_;
    app::ScrollBar vbar_, hbar_;
    app::Picture palette_, status_;
    wm::MenuItem file_items_[6];
    wm::MenuItem edit_items_[1];
    wm::MenuItem image_items_[7];
    wm::Menu file_ = {file_items_, 6};
    wm::Menu edit_menu_ = {edit_items_, 1};
    wm::Menu image_menu_ = {image_items_, 7};
    const wm::MenuBarEntry entries_[3] = {{"&File", &file_}, {"&Edit", &edit_menu_}, {"&Image", &image_menu_}};
    const wm::MenuBar bar_ = {entries_, 3};
};

} // namespace

#ifdef APP_LINKED
// For the tests, which have the program linked into them
// (programs/test_hooks.hpp): the sheet of the Pincel in front, or of any
// that is open, and its two colours.
namespace {
PincelWindow *open_one()
{
    return app::program<PincelWindow>(os::main_window(os::find("Pincel")));
}
} // namespace
const Sheet *test_sheet()
{
    PincelWindow *w = open_one();
    return w != nullptr ? &w->sheet() : nullptr;
}
gfx::Colour test_ink() { return open_one() != nullptr ? open_one()->ink() : 0; }
gfx::Colour test_paper() { return open_one() != nullptr ? open_one()->paper() : 0; }
#endif

} // namespace pincel

// Pincel is a program kept in a file: not part of the system. Each start is
// another copy with its own process. 8 KB of stack: it reads files on its
// own task, and the file system's code and the picture decoders run on it.
APP_INFO(app::many(app::file_info("Pincel", 8 * 1024, art::kSmall, art::kSmallInks, art::kLarge, art::kLargeInks)));

namespace {
// The window's icon: the header's own pixels are not in memory with the
// program, so they are made again here from the same rows.
constexpr app::Icon16 kWindowIcon = app::icon16(art::kSmall, art::kSmallInks);
} // namespace

// argv[1], if there is one, is the picture to open.
APP_MAIN(int argc, char **argv)
{
    static const gfx::Bitmap icon = {16, 16, kWindowIcon.pixels, app::kClearInk};
    // Each new one a step down and to the right of where the last began.
    int place = 0;
    for (int i = 0; i < os::process_count(); i++) {
        const proc::Process &p = os::process_at(i);
        if (p.pid != os::self() && strcmp(p.name, "Pincel") == 0 && os::main_window(p.pid) != os::kNoWindow) {
            place++;
        }
    }
    place %= 6;
    app::Look look;
    look.title = "Untitled - Pincel";
    look.x = 60 + place * 20;
    look.y = 20 + place * 20;
    look.width = 480;
    look.height = 400;
    look.icon = &icon;
    look.flags = app::kMenuBar;
    // Shown once it has its sheet.
    const app::Number number = app::make<pincel::PincelWindow>(look);
    if (number == app::kNone) {
        shell::message_box("Pincel", "There is not enough memory to open Pincel.");
        return 0; // said: the system has nothing to add
    }
    app::program<pincel::PincelWindow>(number)->open_path(argc > 1 ? argv[1] : nullptr);
    if (!os::ui::on_screen(number)) {
        os::ui::close(number); // no memory for the sheet, or no room
    }
    return 0;
}
