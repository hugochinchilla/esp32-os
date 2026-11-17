// The Display dialog: the desktop's colour and the pattern over it, with a
// preview. Opened from Start > Settings > Display and by a right click on
// the desktop. Kept in C:\.config\desktop.ini, [background].
#include <cstring>

#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

using gfx::Rect;
using gfx::Rgb;

// The colours offered. The first is the desktop as it always was. They are
// kept in the settings by name.
struct NamedColour {
    const char *key;
    const char *name;
    Rgb rgb;
};
constexpr NamedColour kColours[] = {
    {"chinchilla-blue", "Chinchilla blue", {58, 110, 165}},
    {"teal", "Teal", {0, 128, 128}},
    {"sea-green", "Sea green", {46, 139, 87}},
    {"forest", "Forest", {0, 96, 48}},
    {"navy", "Navy", {0, 0, 128}},
    {"purple", "Purple", {96, 48, 144}},
    {"plum", "Plum", {128, 0, 72}},
    {"brick", "Brick", {168, 64, 48}},
    {"olive", "Olive", {128, 128, 0}},
    {"sand", "Sand", {208, 184, 128}},
    {"sky", "Sky", {128, 176, 224}},
    {"lilac", "Lilac", {176, 160, 208}},
    {"rose", "Rose", {224, 152, 176}},
    {"silver", "Silver", {192, 192, 192}},
    {"slate", "Slate", {80, 96, 112}},
    {"black", "Black", {0, 0, 0}},
};
constexpr int kColourCount = sizeof(kColours) / sizeof(kColours[0]);

// The patterns, 8x8, the top bit of a row its left pixel. All original.
struct NamedPattern {
    const char *key;
    const char *name;
    uint8_t rows[8];
};
constexpr NamedPattern kPatterns[] = {
    {"none", "(None)", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"dots", "Dots", {0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00}},
    {"diagonal", "Diagonal", {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01}},
    {"grid", "Grid", {0xFF, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80}},
    {"bricks", "Bricks", {0xFF, 0x80, 0x80, 0x80, 0xFF, 0x08, 0x08, 0x08}},
    {"weave", "Weave", {0xF8, 0x74, 0x22, 0x47, 0x8F, 0x17, 0x22, 0x71}},
    {"diamonds", "Diamonds", {0x10, 0x28, 0x44, 0x82, 0x44, 0x28, 0x10, 0x00}},
    {"waves", "Waves", {0x00, 0x18, 0x24, 0x42, 0x81, 0x00, 0x00, 0x00}},
    {"zigzag", "Zigzag", {0x81, 0x42, 0x24, 0x18, 0x00, 0x00, 0x00, 0x00}},
    {"crosses", "Crosses", {0x00, 0x20, 0x70, 0x20, 0x00, 0x02, 0x07, 0x02}},
};
constexpr int kPatternCount = sizeof(kPatterns) / sizeof(kPatterns[0]);

int g_colour = 0, g_pattern = 0;

// The pattern's ink: the colour a good deal darker, or lighter when the
// colour is too dark for that to show.
Rgb ink_for(Rgb c)
{
    if (c.r + c.g + c.b < 160) {
        return {static_cast<uint8_t>(c.r + 88), static_cast<uint8_t>(c.g + 88), static_cast<uint8_t>(c.b + 88)};
    }
    return {static_cast<uint8_t>(c.r * 5 / 8), static_cast<uint8_t>(c.g * 5 / 8), static_cast<uint8_t>(c.b * 5 / 8)};
}

wm::Wm::Backdrop backdrop_for(int colour, int pattern)
{
    wm::Wm::Backdrop b = {kColours[colour].rgb, ink_for(kColours[colour].rgb), {}};
    memcpy(b.pattern, kPatterns[pattern].rows, 8);
    return b;
}

void apply() { manager().set_backdrop(backdrop_for(g_colour, g_pattern)); }

void read_settings()
{
    fs::Ini ini;
    if (!load_settings("desktop", ini)) {
        return;
    }
    char value[32];
    if (ini.get("background", "colour", value, sizeof(value))) {
        for (int i = 0; i < kColourCount; i++) {
            if (strcmp(value, kColours[i].key) == 0) {
                g_colour = i;
            }
        }
    }
    if (ini.get("background", "pattern", value, sizeof(value))) {
        for (int i = 0; i < kPatternCount; i++) {
            if (strcmp(value, kPatterns[i].key) == 0) {
                g_pattern = i;
            }
        }
    }
}

// `ini` is the dialog's own (2 KB, too much for the stack this deep).
void write_settings(fs::Ini &ini)
{
    // Read again first: the desktop keeps other things in the same file.
    load_settings("desktop", ini);
    ini.set("background", "colour", kColours[g_colour].key);
    ini.set("background", "pattern", kPatterns[g_pattern].key);
    save_settings("desktop", ini);
}

// A grid of cells to pick one from: the colours, or the patterns. The cell
// chosen has a black and white frame; arrows move it when it has the focus.
class Picker : public wm::Widget {
public:
    using PaintFn = void (*)(gfx::Canvas &c, const Rect &cell, int index, void *context);
    using ChangeFn = void (*)(void *context);

    Picker(const Rect &r, int columns, int count, int cell_w, int cell_h, PaintFn paint_cell, ChangeFn changed,
           void *context)
        : Widget(r), columns_(columns), count_(count), cell_w_(cell_w), cell_h_(cell_h), paint_cell_(paint_cell),
          changed_(changed), context_(context)
    {
    }

    int selected() const { return selected_; }
    void select(int index)
    {
        if (index < 0 || index >= count_ || index == selected_) {
            return;
        }
        selected_ = index;
        invalidate();
        if (changed_ != nullptr) {
            changed_(context_);
        }
    }
    Rect cell(int index) const
    {
        return {(index % columns_) * cell_w_, (index / columns_) * cell_h_, cell_w_, cell_h_};
    }

    void paint(gfx::Canvas &c) override
    {
        c.fill({0, 0, rect.w, rect.h}, gfx::kGrey);
        for (int i = 0; i < count_; i++) {
            const Rect r = cell(i);
            if (i == selected_) {
                c.edge(r, gfx::kBlack, gfx::kBlack);
                c.edge(r.inset(1), gfx::kWhite, gfx::kWhite);
            }
            const Rect inner = r.inset(3);
            c.edge(inner, gfx::kDarkGrey, gfx::kWhite);
            paint_cell_(c, inner.inset(1), i, context_);
        }
        if (has_focus() && selected_ >= 0) {
            c.focus_rect(cell(selected_).inset(2));
        }
    }
    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind != wm::MouseKind::Down || m.x < 0 || m.y < 0 || m.x >= columns_ * cell_w_) {
            return;
        }
        select(m.y / cell_h_ * columns_ + m.x / cell_w_);
    }
    bool focusable() const override { return true; }
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        switch (k.usage) {
        case wm::key::Left:
            select(selected_ - 1);
            return true;
        case wm::key::Right:
            select(selected_ + 1);
            return true;
        case wm::key::Up:
            select(selected_ - columns_);
            return true;
        case wm::key::Down:
            select(selected_ + columns_);
            return true;
        default:
            return false;
        }
    }

private:
    int columns_, count_, cell_w_, cell_h_;
    PaintFn paint_cell_;
    ChangeFn changed_;
    void *context_;
    int selected_ = 0;
};

// The preview: a small screen with the colour and pattern picked, a window
// and a taskbar on it.
class Preview : public wm::Widget {
public:
    explicit Preview(const Rect &r) : Widget(r) {}
    void set(int colour, int pattern)
    {
        colour_ = colour;
        pattern_ = pattern;
        invalidate();
    }
    void paint(gfx::Canvas &c) override
    {
        const Rect all = {0, 0, rect.w, rect.h};
        c.fill(all, gfx::kGrey);
        // The monitor: a case, the screen, a foot.
        const Rect box = {(rect.w - 132) / 2, 0, 132, 100};
        c.button(box, gfx::ButtonLook::Raised);
        const Rect screen = box.inset(8);
        c.edge(screen, gfx::kDarkGrey, gfx::kWhite);
        const Rect glass = screen.inset(1);
        const wm::Wm::Backdrop b = backdrop_for(colour_, pattern_);
        if (pattern_ == 0) {
            c.fill_rgb(glass, b.colour);
        } else {
            c.fill_pattern(glass, b.colour, b.ink, b.pattern);
        }
        c.fill({glass.x, glass.bottom() - 7, glass.w, 7}, gfx::kGrey);
        c.hline(glass.x, glass.bottom() - 7, glass.w, gfx::kWhite);
        c.fill({glass.x + 2, glass.bottom() - 5, 14, 4}, gfx::kDarkGrey);
        const Rect window = {glass.x + 34, glass.y + 14, 52, 38};
        c.fill(window, gfx::kGrey);
        c.edge(window, gfx::kWhite, gfx::kBlack);
        c.fill({window.x + 2, window.y + 2, window.w - 4, 6}, gfx::rgb(0, 0, 2));
        c.fill({window.x + 4, window.y + 11, window.w - 8, window.h - 15}, gfx::kWhite);
        c.fill({box.x + 50, box.bottom(), 32, 4}, gfx::kDarkGrey);
        c.button({box.x + 30, box.bottom() + 4, 72, 8}, gfx::ButtonLook::Raised);
    }

private:
    int colour_ = 0, pattern_ = 0;
};

class DisplayDialog : public wm::Window {
public:
    DisplayDialog()
        : Window("Display", {0, 0, 372, 388}, &gfx::display_icon_16()),
          preview_({8, 8, 348, 114}),
          group_({8, 128, 348, 190}, "Desktop background"),
          colour_label_({18, 148, 60, 14}, "&Colour:"),
          colour_name_({80, 148, 266, 14}, colour_text_),
          colours_({18, 164, 328, 52}, 8, kColourCount, 41, 26, &DisplayDialog::paint_colour, &DisplayDialog::changed,
                   this),
          pattern_label_({18, 222, 60, 14}, "&Pattern:"),
          pattern_name_({80, 222, 266, 14}, pattern_text_),
          patterns_({18, 238, 328, 72}, 5, kPatternCount, 65, 36, &DisplayDialog::paint_pattern,
                    &DisplayDialog::changed, this),
          ok_({118, 328, 75, 23}, "OK", &DisplayDialog::ok, this),
          cancel_({199, 328, 75, 23}, "Cancel", &DisplayDialog::cancel, this),
          apply_({280, 328, 75, 23}, "&Apply", &DisplayDialog::apply_now, this)
    {
        set_dialog(true);
        add(preview_);
        add(group_);
        add(colour_label_);
        add(colour_name_);
        add(colours_);
        add(pattern_label_);
        add(pattern_name_);
        add(patterns_);
        add(ok_);
        add(cancel_);
        add(apply_);
        ok_.set_default(true);
        set_default(&ok_);
        // What the desktop shows now, to start from and to go back to.
        before_colour_ = g_colour;
        before_pattern_ = g_pattern;
        colours_.select(g_colour);
        patterns_.select(g_pattern);
        sync();
        set_frame(centred(shell::manager(), frame().w, frame().h));
        set_focus(&colours_);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt)) {
            const char letter = wm::key::letter(k.usage);
            if (letter == 'c' || letter == 'C') {
                set_focus(&colours_);
                return true;
            }
            if (letter == 'p' || letter == 'P') {
                set_focus(&patterns_);
                return true;
            }
            if (letter == 'a' || letter == 'A') {
                apply_.click();
                return true;
            }
        }
        return Window::key(k);
    }

    // Closed without OK: what Apply showed is taken back.
    void closed() override
    {
        if (!kept_) {
            g_colour = before_colour_;
            g_pattern = before_pattern_;
        }
        kept_ = false;
        apply();
    }

private:
    static void paint_colour(gfx::Canvas &c, const Rect &cell, int index, void *)
    {
        c.fill_rgb(cell, kColours[index].rgb);
    }
    static void paint_pattern(gfx::Canvas &c, const Rect &cell, int index, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        const wm::Wm::Backdrop b = backdrop_for(d->colours_.selected(), index);
        if (index == 0) {
            c.fill_rgb(cell, b.colour);
        } else {
            c.fill_pattern(cell, b.colour, b.ink, b.pattern);
        }
    }
    static void changed(void *context) { static_cast<DisplayDialog *>(context)->sync(); }
    void sync()
    {
        strncpy(colour_text_, kColours[colours_.selected()].name, sizeof(colour_text_) - 1);
        strncpy(pattern_text_, kPatterns[patterns_.selected()].name, sizeof(pattern_text_) - 1);
        colour_name_.invalidate();
        pattern_name_.invalidate();
        patterns_.invalidate(); // the patterns are shown in the colour picked
        preview_.set(colours_.selected(), patterns_.selected());
    }
    void take()
    {
        g_colour = colours_.selected();
        g_pattern = patterns_.selected();
    }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->take();
        write_settings(d->settings_);
        d->kept_ = true;
        d->close(); // applies
    }
    static void cancel(wm::Button &, void *context) { static_cast<DisplayDialog *>(context)->close(); }
    // Apply shows the choice on the real desktop; only OK keeps it.
    static void apply_now(wm::Button &, void *context)
    {
        static_cast<DisplayDialog *>(context)->take();
        apply();
    }

    char colour_text_[24] = "";
    char pattern_text_[24] = "";
    int before_colour_ = 0, before_pattern_ = 0;
    bool kept_ = false;
    fs::Ini settings_;
    Preview preview_;
    wm::GroupFrame group_;
    wm::Label colour_label_, colour_name_;
    Picker colours_;
    wm::Label pattern_label_, pattern_name_;
    Picker patterns_;
    wm::Button ok_, cancel_, apply_;
};

} // namespace

void display_dialog()
{
    const AsProgram as("Display");
    open_one<DisplayDialog>("Display");
}

void install_display()
{
    read_settings();
    apply();
}

} // namespace shell
