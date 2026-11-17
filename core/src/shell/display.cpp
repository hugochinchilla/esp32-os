// The Display dialog: the desktop's colour and the pattern over it, with a
// preview. Opened from Start > Settings > Display and by a right click on
// the desktop. Kept in C:\.config\desktop.ini, [background].
//
// The colour is one of a short list with names, or, through Other..., any
// of the board's 256. All of the list but the second are colours the board
// has exactly, so the desktop is one flat colour with no dither: that is
// what a remote screen (VNC) packs best.
#include <cstdio>
#include <cstring>

#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "picker.hpp"

namespace shell {

namespace {

using gfx::Rect;
using gfx::Rgb;

// The colours offered, kept in the settings by name. The first is the
// default.
struct NamedColour {
    const char *key;
    const char *name;
    Rgb rgb;
};
// A colour the board has: red and green 0..7, blue 0..3.
constexpr Rgb exact(unsigned r, unsigned g, unsigned b)
{
    constexpr uint8_t k3[8] = {0, 36, 73, 109, 146, 182, 219, 255}, kB[4] = {0, 73, 182, 255};
    return {k3[r], k3[g], kB[b]};
}
constexpr NamedColour kColours[] = {
    // The first is the default (the owner's choice, 2026-11-17): the board
    // colour nearest to the blue the desktop had until then, which is next.
    {"steel-blue", "Steel blue", exact(2, 3, 2)},
    {"chinchilla-blue", "Chinchilla blue (dithered)", {58, 110, 165}}, // between board colours
    {"teal", "Teal", exact(0, 4, 2)},
    {"sea-green", "Sea green", exact(1, 4, 1)},
    {"forest", "Forest", exact(0, 3, 0)},
    {"navy", "Navy", exact(0, 0, 1)},
    {"purple", "Purple", exact(3, 1, 2)},
    {"plum", "Plum", exact(4, 0, 1)},
    {"brick", "Brick", exact(5, 2, 1)},
    {"olive", "Olive", exact(4, 4, 0)},
    {"sand", "Sand", exact(6, 5, 1)},
    {"sky", "Sky", exact(4, 5, 3)},
    {"rose", "Rose", exact(7, 4, 2)},
    {"silver", "Silver", exact(5, 5, 2)},
    {"slate", "Slate", exact(2, 2, 1)},
    {"black", "Black", exact(0, 0, 0)},
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

bool same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
// Whether the board has this colour (it is then shown with no dither).
bool is_exact(Rgb c) { return same(gfx::shown(gfx::nearest(c)), c); }
// Its place in the list of names; -1: it is not one of them.
int named(Rgb c)
{
    for (int i = 0; i < kColourCount; i++) {
        if (same(kColours[i].rgb, c)) {
            return i;
        }
    }
    return -1;
}

// What the desktop shows: a colour, and a place in the table of patterns.
// Nothing is kept in memory while the dialog is closed; the settings file
// has it.
struct Choice {
    Rgb colour = kColours[0].rgb;
    int pattern = 0;
};

// The pattern's ink: the colour a good deal darker, or lighter when the
// colour is too dark for that to show.
Rgb ink_for(Rgb c)
{
    if (c.r + c.g + c.b < 160) {
        return {static_cast<uint8_t>(c.r + 88), static_cast<uint8_t>(c.g + 88), static_cast<uint8_t>(c.b + 88)};
    }
    return {static_cast<uint8_t>(c.r * 5 / 8), static_cast<uint8_t>(c.g * 5 / 8), static_cast<uint8_t>(c.b * 5 / 8)};
}

wm::Wm::Backdrop backdrop_for(Rgb colour, int pattern)
{
    Rgb ink = ink_for(colour);
    if (is_exact(colour)) {
        // A flat colour gets a flat ink: the nearest the board has, and if
        // that is the colour itself (blue has few levels), white or black.
        ink = gfx::shown(gfx::nearest(ink));
        if (same(ink, colour)) {
            ink = colour.r + colour.g + colour.b < 384 ? Rgb{255, 255, 255} : Rgb{0, 0, 0};
        }
    }
    wm::Wm::Backdrop b = {colour, ink, {}};
    memcpy(b.pattern, kPatterns[pattern].rows, 8);
    return b;
}

void apply(const Choice &c) { os::set_backdrop(backdrop_for(c.colour, c.pattern)); }

// "#RRGGBB", as a colour that has no name is kept.
bool from_hex(const char *text, Rgb &out)
{
    unsigned r, g, b;
    if (text[0] != '#' || strlen(text) != 7 || sscanf(text + 1, "%2x%2x%2x", &r, &g, &b) != 3) {
        return false;
    }
    out = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    return true;
}

// `ini` is the caller's (2 KB, too much for the stack this deep).
Choice read_settings(fs::Ini &ini)
{
    Choice c;
    if (!load_settings("desktop", ini)) {
        return c;
    }
    char value[32];
    if (ini.get("background", "colour", value, sizeof(value))) {
        for (int i = 0; i < kColourCount; i++) {
            if (strcmp(value, kColours[i].key) == 0) {
                c.colour = kColours[i].rgb;
            }
        }
        from_hex(value, c.colour); // a colour with no name
    }
    if (ini.get("background", "pattern", value, sizeof(value))) {
        for (int i = 0; i < kPatternCount; i++) {
            if (strcmp(value, kPatterns[i].key) == 0) {
                c.pattern = i;
            }
        }
    }
    return c;
}

void write_settings(fs::Ini &ini, const Choice &c)
{
    // Read again first: the desktop keeps other things in the same file.
    load_settings("desktop", ini);
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", c.colour.r, c.colour.g, c.colour.b);
    ini.set("background", "colour", named(c.colour) >= 0 ? kColours[named(c.colour)].key : hex);
    ini.set("background", "pattern", kPatterns[c.pattern].key);
    save_settings("desktop", ini);
}

// The preview: a small screen with the colour and pattern picked, a window
// and a taskbar on it.
class Preview : public wm::Widget {
public:
    explicit Preview(const Rect &r) : Widget(r) {}
    void set(Rgb colour, int pattern)
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
    Rgb colour_ = {0, 0, 0};
    int pattern_ = 0;
};

class DisplayDialog : public wm::Window {
public:
    DisplayDialog()
        : Window("Display", {0, 0, 372, 388}, &gfx::display_icon_16()),
          preview_({8, 8, 348, 114}),
          group_({8, 128, 348, 190}, "Desktop background"),
          colour_label_({18, 148, 60, 14}, "&Colour:"),
          colour_name_({80, 148, 180, 14}, colour_text_),
          colours_({18, 164, 328, 52}, 8, kColourCount, 41, 26, &DisplayDialog::paint_colour,
                   &DisplayDialog::colour_changed, this),
          other_({271, 143, 75, 21}, "&Other...", &DisplayDialog::other, this),
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
        add(other_);
        add(pattern_label_);
        add(pattern_name_);
        add(patterns_);
        add(ok_);
        add(cancel_);
        add(apply_);
        ok_.set_default(true);
        set_default(&ok_);
        // What the desktop shows now, to start from and to go back to.
        before_ = read_settings(settings_);
        colour_ = before_.colour;
        show_colour();
        patterns_.select_quietly(before_.pattern);
        sync();
        set_frame(os::centred(frame().w, frame().h));
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
            if (letter == 'o' || letter == 'O') {
                other_.click();
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
    void closed() override { apply(before_); }

private:
    static void paint_colour(gfx::Canvas &c, const Rect &cell, int index, void *)
    {
        c.fill_rgb(cell, kColours[index].rgb);
    }
    static void paint_pattern(gfx::Canvas &c, const Rect &cell, int index, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        const wm::Wm::Backdrop b = backdrop_for(d->colour_, index);
        if (index == 0) {
            c.fill_rgb(cell, b.colour);
        } else {
            c.fill_pattern(cell, b.colour, b.ink, b.pattern);
        }
    }
    // A cell of the list was picked.
    static void colour_changed(void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->colour_ = kColours[d->colours_.selected()].rgb;
        d->sync();
    }
    static void changed(void *context) { static_cast<DisplayDialog *>(context)->sync(); }
    // Other...: the 256 colours, in the colour dialog that any program can
    // ask for (colour_dialog.cpp), over this one.
    static void other(wm::Button &, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        colour_dialog(d->id(), gfx::nearest(d->colour_), &DisplayDialog::other_picked, d);
    }
    static void other_picked(gfx::Colour colour, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->colour_ = gfx::shown(colour);
        d->show_colour();
        d->sync();
    }
    // The list shows the colour if it is one of its own, else no cell.
    void show_colour()
    {
        if (named(colour_) >= 0) {
            colours_.select_quietly(named(colour_));
        } else {
            colours_.clear();
        }
    }
    void sync()
    {
        if (named(colour_) >= 0) {
            snprintf(colour_text_, sizeof(colour_text_), "%s", kColours[named(colour_)].name);
        } else {
            snprintf(colour_text_, sizeof(colour_text_), "#%02X%02X%02X%s", colour_.r, colour_.g, colour_.b,
                     is_exact(colour_) ? "" : " (dithered)");
        }
        snprintf(pattern_text_, sizeof(pattern_text_), "%s", kPatterns[patterns_.selected()].name);
        colour_name_.invalidate();
        pattern_name_.invalidate();
        patterns_.invalidate(); // the patterns are shown in the colour picked
        preview_.set(colour_, patterns_.selected());
    }
    Choice picked() const { return {colour_, patterns_.selected()}; }
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->before_ = d->picked();
        write_settings(d->settings_, d->before_);
        d->close(); // applies
    }
    static void cancel(wm::Button &, void *context) { static_cast<DisplayDialog *>(context)->close(); }
    // Apply shows the choice on the real desktop; only OK keeps it.
    static void apply_now(wm::Button &, void *context)
    {
        apply(static_cast<DisplayDialog *>(context)->picked());
    }

    char colour_text_[32] = "";
    Rgb colour_ = kColours[0].rgb;
    char pattern_text_[24] = "";
    Choice before_; // what the settings file holds
    fs::Ini settings_;
    Preview preview_;
    wm::GroupFrame group_;
    wm::Label colour_label_, colour_name_;
    Picker colours_;
    wm::Button other_;
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
    fs::Ini ini;
    apply(read_settings(ini));
}

} // namespace shell
