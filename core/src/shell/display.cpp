// The Display dialog: the theme (the look of windows, buttons and the
// taskbar), and the desktop's colour and the pattern over it, with a
// preview. Opened from Start > Settings > Display and by a right click on
// the desktop. Kept in C:\.config\desktop.ini, [look] and [background].
//
// The colour is one of a short list with names, or, through Other..., any
// of the board's 256. All of the list but the second are colours the board
// has exactly, so the desktop is one flat colour with no dither: that is
// what a remote screen (VNC) packs best.
#include <cstdio>
#include <cstring>

#include "core/app/app.hpp"
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

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

// What the desktop shows: a colour, a place in the table of patterns, and a
// place in the table of themes. Nothing is kept in memory while the dialog
// is closed; the settings file has it.
struct Choice {
    Rgb colour = kColours[0].rgb;
    int pattern = 0;
    int theme = 0;
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

void apply(const Choice &c)
{
    os::set_theme(c.theme);
    os::set_backdrop(backdrop_for(c.colour, c.pattern));
}

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
    if (ini.get("look", "theme", value, sizeof(value)) && gfx::theme_named(value) >= 0) {
        c.theme = gfx::theme_named(value);
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
    ini.set("look", "theme", gfx::theme_at(c.theme).key);
    save_settings("desktop", ini);
}

// The preview: a small screen with the colour and pattern picked, and a
// window and a taskbar in the colours of the theme picked. `size`: of the
// picture it is painted in.
void paint_preview(app::Canvas &c, const Rect &size, Rgb colour, int pattern, const gfx::Theme &look)
{
    c.fill({0, 0, size.w, size.h}, gfx::face());
    // The monitor: a case, the screen, a foot.
    const Rect box = {(size.w - 132) / 2, 0, 132, 100};
    c.button(box, gfx::ButtonLook::Raised);
    const Rect screen = box.inset(8);
    c.dent(screen);
    const Rect glass = screen.inset(1);
    const wm::Wm::Backdrop b = backdrop_for(colour, pattern);
    if (pattern == 0) {
        c.fill_rgb(glass, b.colour);
    } else {
        c.fill_pattern(glass, b.colour, b.ink, b.pattern);
    }
    // The taskbar, with its Start button.
    const bool flat = look.flat();
    c.fill({glass.x, glass.bottom() - 7, glass.w, 7}, flat ? look.bar : look.face);
    c.hline(glass.x, glass.bottom() - 7, glass.w, look.bar_line);
    c.fill({glass.x + 2, glass.bottom() - 5, 14, 4}, flat ? look.start : gfx::kDarkGrey);
    // The window: its frame, its title bar, what it holds.
    const Rect window = {glass.x + 34, glass.y + 14, 52, 38};
    c.fill(window, flat ? look.frame_on : look.face);
    if (flat) {
        c.edge(window, look.rim_on, look.rim_on);
    } else {
        c.edge(window, gfx::kWhite, gfx::kBlack);
    }
    c.fill({window.x + 2, window.y + 2, window.w - 4, 6}, look.swatch);
    c.fill({window.x + 4, window.y + 11, window.w - 8, window.h - 15}, look.paper);
    c.fill({box.x + 50, box.bottom(), 32, 4}, gfx::dim());
    c.button({box.x + 30, box.bottom() + 4, 72, 8}, gfx::ButtonLook::Raised);
}

// The names of the themes, for the list.
struct ThemeNames {
    const char *names[8];
    int count;
    ThemeNames() : count(gfx::theme_count() < 8 ? gfx::theme_count() : 8)
    {
        for (int i = 0; i < count; i++) {
            names[i] = gfx::theme_at(i).name;
        }
    }
};

constexpr Rect kPreview = {8, 8, 148, 114};

class DisplayDialog : public app::Window {
public:
    DisplayDialog()
        : preview_(picture(kPreview)),
          theme_label_(label({168, 10, 60, 14}, "&Theme:")),
          themes_(list({168, 28, 188, 60}, theme_names_.names, theme_names_.count)),
          group_(group({8, 128, 348, 190}, "Desktop background")),
          colour_label_(label({18, 148, 60, 14}, "&Colour:")),
          colour_name_(plain({80, 148, 180, 14})),
          colours_(grid({18, 164, 328, 52}, 8, kColourCount, 41, 26)),
          other_(button({271, 143, 75, 21}, "&Other...")),
          pattern_label_(label({18, 222, 60, 14}, "&Pattern:")),
          pattern_name_(plain({80, 222, 266, 14})),
          patterns_(grid({18, 238, 328, 72}, 5, kPatternCount, 65, 36)),
          ok_(button({118, 328, 75, 23}, "OK")),
          cancel_(button({199, 328, 75, 23}, "Cancel")),
          apply_(button({280, 328, 75, 23}, "&Apply"))
    {
        set_default(ok_);
        // What the desktop shows now, to start from and to go back to.
        before_ = read_settings(settings_);
        colour_ = before_.colour;
        colours_.select(named(colour_)); // if it is one of the list's own, else no cell
        patterns_.select(before_.pattern);
        themes_.select(before_.theme);
        sync();
        set_focus(colours_);
    }

    void command(app::Control which) override
    {
        if (which == other_) {
            // Other...: the 256 colours, in the colour dialog that any
            // program can ask for (colour_dialog.cpp), over this one.
            colour_dialog(number(), gfx::nearest(colour_), &DisplayDialog::other_picked, this);
        } else if (which == ok_) {
            before_ = picked();
            write_settings(settings_, before_);
            close(); // applies
        } else if (which == cancel_) {
            close();
        } else if (which == apply_) {
            apply(picked()); // shown on the real desktop; only OK keeps it
        }
    }

    // A cell was picked.
    void changed(app::Control which) override
    {
        if (which == colours_) {
            colour_ = kColours[colours_.selected()].rgb;
        }
        sync();
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        paint_preview(c, kPreview, colour_, patterns_.selected(), gfx::theme_at(themes_.selected()));
    }
    void paint_cell(app::Control which, int index, const Rect &cell, app::Canvas &c) override
    {
        if (which == colours_) {
            c.fill_rgb(cell, kColours[index].rgb);
            return;
        }
        const wm::Wm::Backdrop b = backdrop_for(colour_, index);
        if (index == 0) {
            c.fill_rgb(cell, b.colour);
        } else {
            c.fill_pattern(cell, b.colour, b.ink, b.pattern);
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt)) {
            const char letter = wm::key::letter(k.usage);
            if (letter == 't' || letter == 'T') {
                set_focus(themes_);
                return true;
            }
            if (letter == 'c' || letter == 'C') {
                set_focus(colours_);
                return true;
            }
            if (letter == 'o' || letter == 'O') {
                other_.press();
                return true;
            }
            if (letter == 'p' || letter == 'P') {
                set_focus(patterns_);
                return true;
            }
            if (letter == 'a' || letter == 'A') {
                apply_.press();
                return true;
            }
        }
        return false;
    }

    // Closed without OK: what Apply showed is taken back.
    void closed() override { apply(before_); }

private:
    static void other_picked(gfx::Colour colour, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->colour_ = gfx::shown(colour);
        d->colours_.select(named(d->colour_));
        d->sync();
    }
    void sync()
    {
        char text[32];
        if (named(colour_) >= 0) {
            snprintf(text, sizeof(text), "%s", kColours[named(colour_)].name);
        } else {
            snprintf(text, sizeof(text), "#%02X%02X%02X%s", colour_.r, colour_.g, colour_.b,
                     is_exact(colour_) ? "" : " (dithered)");
        }
        colour_name_.set(text);
        pattern_name_.set(kPatterns[patterns_.selected()].name);
        patterns_.redraw(); // the patterns are shown in the colour picked
        preview_.redraw();
    }
    Choice picked() const { return {colour_, patterns_.selected(), themes_.selected() < 0 ? 0 : themes_.selected()}; }

    Rgb colour_ = kColours[0].rgb;
    Choice before_; // what the settings file holds
    fs::Ini settings_;
    ThemeNames theme_names_; // before the list that shows them
    app::Picture preview_;
    app::Label theme_label_;
    app::ListBox themes_;
    app::GroupFrame group_;
    app::Label colour_label_;
    app::Plain colour_name_;
    app::Grid colours_;
    app::Button other_;
    app::Label pattern_label_;
    app::Plain pattern_name_;
    app::Grid patterns_;
    app::Button ok_, cancel_, apply_;
};

app::Number g_dialog = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

void display_dialog()
{
    const AsProgram as("Display");
    if (app::bring_forward(g_dialog)) {
        return;
    }
    app::Look look;
    look.title = "Display";
    look.width = 372;
    look.height = 388;
    look.icon = &gfx::display_icon_16();
    look.flags = app::kDialog;
    g_dialog = app::open<DisplayDialog>(look);
    if (g_dialog == app::kNone) {
        no_memory_for("Display");
    }
}

void install_display()
{
    fs::Ini ini;
    apply(read_settings(ini));
}

} // namespace shell
