// The Display dialog, in two tabs. Appearance: the theme (the look of
// windows, buttons and the taskbar), and the desktop's colour and the
// pattern over it, and a picture file shown over them (backlog item 87;
// the desktop's half of that is wallpaper.cpp). Resolution: the screen's
// mode, 640 x 480 or 800 x 600 (backlog item 68). A preview stands over both. Opened from Start >
// Settings > Display and by a right click on the desktop. Kept in
// C:\.config\desktop.ini, [look], [background] and [screen].
//
// A change of mode is made at once and then asked about: "Keep this
// setting?", with fifteen seconds to answer. Only Keep writes it to the
// settings; no answer, Go back, or the box closed any other way puts the
// old mode back, so a monitor that shows nothing in the new mode cannot
// leave the machine there.
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
#include "core/image/image.hpp"
#include "core/job/job.hpp"
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
    char picture[fs::kMaxPath] = ""; // the file shown over the colour; "": none
    os::Wallpaper position = os::Wallpaper::Centre; // and where it stands
};

// Where the picture stands, as the settings file names it, in the order of
// os::Wallpaper.
constexpr const char *kPositions[] = {"centre", "tile", "stretch"};
constexpr int kPositionCount = 3;

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
    os::set_wallpaper(c.picture, c.position);
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
    if (!ini.get("background", "picture", c.picture, sizeof(c.picture))) {
        c.picture[0] = '\0';
    }
    if (ini.get("background", "position", value, sizeof(value))) {
        for (int i = 0; i < kPositionCount; i++) {
            if (strcmp(value, kPositions[i]) == 0) {
                c.position = static_cast<os::Wallpaper>(i);
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
    ini.set("background", "picture", c.picture);
    ini.set("background", "position", kPositions[static_cast<int>(c.position)]);
    ini.set("look", "theme", gfx::theme_at(c.theme).key);
    save_settings("desktop", ini);
}

// The preview: a small screen with the colour and pattern picked, and a
// window and a taskbar in the colours of the theme picked. `size`: of the
// picture it is painted in. `fine`: the screen is one of more pixels than the
// first mode, so everything on it is drawn smaller. `picture`: the desktop's,
// made as small as that screen, nullptr for none; `tiled`: side by side.
constexpr int kGlassWidth = 114; // the small screen's, inside its case and edge
void paint_preview(app::Canvas &c, const Rect &size, Rgb colour, int pattern, const gfx::Theme &look, bool fine,
                   const gfx::Bitmap *picture, bool tiled)
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
    if (picture != nullptr) {
        app::Canvas inside = c.sub(glass); // one a little too tall loses its edges
        if (tiled) {
            for (int y = 0; y < glass.h; y += picture->height) {
                for (int x = 0; x < glass.w; x += picture->width) {
                    inside.blit(*picture, x, y);
                }
            }
        } else {
            inside.blit(*picture, (glass.w - picture->width) / 2, (glass.h - picture->height) / 2);
        }
    }
    // The taskbar, with its Start button.
    const bool flat = look.flat();
    const int bar = fine ? 6 : 7;
    c.fill({glass.x, glass.bottom() - bar, glass.w, bar}, flat ? look.bar : look.face);
    c.hline(glass.x, glass.bottom() - bar, glass.w, look.bar_line);
    c.fill({glass.x + 2, glass.bottom() - bar + 2, fine ? 11 : 14, bar - 3}, flat ? look.start : gfx::kDarkGrey);
    // The window: its frame, its title bar, what it holds.
    const Rect window = fine ? Rect{glass.x + 27, glass.y + 11, 42, 30} : Rect{glass.x + 34, glass.y + 14, 52, 38};
    c.fill(window, flat ? look.frame_on : look.face);
    if (flat) {
        c.edge(window, look.rim_on, look.rim_on);
    } else {
        c.edge(window, gfx::kWhite, gfx::kBlack);
    }
    const int title = fine ? 5 : 6;
    c.fill({window.x + 2, window.y + 2, window.w - 4, title}, look.swatch);
    c.fill({window.x + 4, window.y + title + 5, window.w - 8, window.h - title - 9}, look.paper);
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

/* ---- The screen's mode ----------------------------------------------------------- */

// A mode as the settings file names it and the list shows it: "800x600",
// "800 x 600".
void mode_key(const ports::VideoMode &m, char *out, size_t capacity)
{
    snprintf(out, capacity, "%dx%d", m.width, m.height);
}
void mode_name(const ports::VideoMode &m, char *out, size_t capacity)
{
    snprintf(out, capacity, "%d x %d", m.width, m.height);
}

app::Number g_dialog = app::kNone;   // the one there may be: a number, dead once it has closed
app::Number g_question = app::kNone; // "Keep this setting?", while it is asked
void screen_went_back();

// "Keep this setting?" after the mode has changed. The answer has to come
// from someone who can see the screen: with none in kSeconds the old mode
// comes back, as it does for Go back, Escape and the close button.
class KeepQuestion : public app::Window {
public:
    static constexpr int kSeconds = 15;
    static constexpr int kWidth = 300, kHeight = 104;

    KeepQuestion(int from, int to)
        : from_(from), to_(to),
          now_(plain({12, 10, kWidth - 24, 14})),
          ask_(label({12, 28, kWidth - 24, 14}, "Do you want to keep this setting?")),
          back_in_(plain({12, 46, kWidth - 24, 14})),
          keep_(button({kWidth / 2 - 81, 72, 75, 23}, "&Keep")),
          back_(button({kWidth / 2 + 6, 72, 75, 23}, "&Go back"))
    {
        // Enter on a screen that shows nothing must not keep it.
        set_default(back_);
        set_focus(back_);
        char size[24], text[64];
        mode_name(os::video_mode_at(to_), size, sizeof(size));
        snprintf(text, sizeof(text), "The screen is now %s.", size);
        now_.set(text);
        count();
        timer_ = os::start_timer(1000000, 1000000, &KeepQuestion::second, this);
    }
    ~KeepQuestion() override { os::stop_timer(timer_); }

    void command(app::Control which) override
    {
        if (which == keep_) {
            // Written only now: until here a restart came back in the old mode.
            char key[24];
            mode_key(os::video_mode_at(to_), key, sizeof(key));
            load_settings("desktop", settings_);
            settings_.set("screen", "mode", key);
            save_settings("desktop", settings_);
            kept_ = true;
        }
        close();
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }
    void closed() override
    {
        if (!kept_) {
            os::set_video_mode(from_);
            screen_went_back();
        }
    }

private:
    static void second(void *self)
    {
        auto *q = static_cast<KeepQuestion *>(self);
        if (--q->left_ <= 0) {
            q->close();
            return;
        }
        q->count();
    }
    void count()
    {
        char size[24], text[80];
        mode_name(os::video_mode_at(from_), size, sizeof(size));
        snprintf(text, sizeof(text), "Going back to %s in %d second%s.", size, left_, left_ == 1 ? "" : "s");
        back_in_.set(text);
    }

    int from_, to_;
    int left_ = kSeconds;
    bool kept_ = false;
    int timer_ = 0;
    fs::Ini settings_;
    app::Plain now_;
    app::Label ask_;
    app::Plain back_in_;
    app::Button keep_, back_;
};

// Changes the screen to another mode and asks whether to keep it. `over`:
// the window the question is asked over, if it stays open.
void change_screen(int to, app::Number over)
{
    if (app::bring_forward(g_question)) {
        return; // one change at a time: the last one is still being asked about
    }
    const int from = os::video_mode();
    if (to < 0 || to == from || !os::set_video_mode(to)) {
        return;
    }
    app::Look look;
    look.title = "Display";
    look.width = KeepQuestion::kWidth;
    look.height = KeepQuestion::kHeight;
    look.icon = &gfx::display_icon_16();
    look.flags = app::kDialog | app::kClientSize;
    look.over = over;
    g_question = app::open<KeepQuestion>(look, from, to);
    if (g_question == app::kNone) {
        os::set_video_mode(from); // nobody to ask: not changed
        no_memory_for("Display");
    }
}

/* ---- The dialog ------------------------------------------------------------------ */

// The names of the modes, for the list.
struct ModeNames {
    char text[4][24];
    const char *names[4];
    int count;
    ModeNames() : count(os::video_mode_count() < 4 ? os::video_mode_count() : 4)
    {
        for (int i = 0; i < count; i++) {
            mode_name(os::video_mode_at(i), text[i], sizeof(text[i]));
            names[i] = text[i];
        }
    }
};

const char *const kTabs[] = {"Appearance", "Resolution"};
enum Page { kAppearance, kResolution };

// Where everything is. The tabs' frame holds the pages; the preview is on
// both, and the three buttons are under the frame.
constexpr Rect kTabsPlace = {6, 6, 360, 374};
constexpr Rect kPreview = {14, 32, 148, 114};
constexpr Rect kNowhere = {0, 0, 0, 0}; // of a control on the page that is not shown
// Appearance.
constexpr Rect kThemeLabel = {174, 34, 60, 14}, kThemes = {174, 52, 188, 60};
constexpr Rect kGroup = {14, 152, 348, 220};
constexpr Rect kColourLabel = {24, 172, 60, 14}, kColourName = {86, 172, 180, 14};
constexpr Rect kColourGrid = {24, 188, 328, 52}, kOther = {277, 167, 75, 21};
constexpr Rect kPatternLabel = {24, 246, 60, 14}, kPatternName = {86, 246, 266, 14};
constexpr Rect kPatternGrid = {24, 262, 328, 52};
constexpr Rect kPictureLabel = {24, 325, 60, 14}, kPictureName = {86, 325, 106, 14};
constexpr Rect kBrowse = {196, 320, 75, 21}, kNoPicture = {277, 320, 75, 21};
constexpr Rect kPositionLabel = {24, 348, 60, 14};
constexpr Rect kCentre = {86, 346, 80, 18}, kTile = {176, 346, 70, 18}, kStretch = {256, 346, 90, 18};
constexpr int kPositionGroup = 0; // of the radio buttons
// Resolution.
constexpr Rect kModeLabel = {174, 34, 180, 14}, kModes = {174, 52, 188, 60};
constexpr Rect kFacts = {14, 156, 348, 62}, kNote = {14, 226, 348, 14};

class DisplayDialog : public app::Window {
public:
    DisplayDialog()
        : tabs_(tabs(kTabsPlace, kTabs, 2)), // first: the pages' controls lie over it
          preview_(picture(kPreview)),
          theme_label_(label(kThemeLabel, "&Theme:")),
          themes_(list(kThemes, theme_names_.names, theme_names_.count)),
          group_(group(kGroup, "Desktop background")),
          colour_label_(label(kColourLabel, "&Colour:")),
          colour_name_(plain(kColourName)),
          colours_(grid(kColourGrid, 8, kColourCount, 41, 26)),
          other_(button(kOther, "&Other...")),
          pattern_label_(label(kPatternLabel, "&Pattern:")),
          pattern_name_(plain(kPatternName)),
          patterns_(grid(kPatternGrid, 5, kPatternCount, 65, 26)),
          picture_label_(label(kPictureLabel, "Pict&ure:")),
          picture_name_(plain(kPictureName)),
          browse_(button(kBrowse, "&Browse...")),
          no_picture_(button(kNoPicture, "&None")),
          position_label_(label(kPositionLabel, "Pos&ition:")),
          centre_(radio(kCentre, "Centre", kPositionGroup, static_cast<int>(os::Wallpaper::Centre))),
          tile_(radio(kTile, "Tile", kPositionGroup, static_cast<int>(os::Wallpaper::Tile))),
          stretch_(radio(kStretch, "Stretch", kPositionGroup, static_cast<int>(os::Wallpaper::Stretch))),
          mode_label_(label(kNowhere, "&Screen area:")),
          modes_(list(kNowhere, mode_names_.names, mode_names_.count)),
          facts_(text_area(kNowhere, facts_text_)),
          note_(label(kNowhere, "A change asks to be kept. With no answer it is undone.")),
          ok_(button({129, 388, 75, 23}, "OK")),
          cancel_(button({210, 388, 75, 23}, "Cancel")),
          apply_(button({291, 388, 75, 23}, "&Apply"))
    {
        set_default(ok_);
        // What the desktop shows now, to start from and to go back to.
        before_ = read_settings(settings_);
        colour_ = before_.colour;
        keep_text(picture_, before_.picture);
        position_ = before_.position;
        set_radio_value(kPositionGroup, static_cast<int>(position_));
        colours_.select(named(colour_)); // if it is one of the list's own, else no cell
        patterns_.select(before_.pattern);
        themes_.select(before_.theme);
        modes_.select(os::video_mode());
        sync();
        set_focus(colours_);
        show_small_picture(false);
    }
    // After the job, if one was at work: the process waits for it.
    ~DisplayDialog() override
    {
        release_fitted(small_);
        release_fitted(job_.out);
    }

    void command(app::Control which) override
    {
        if (which == other_) {
            // Other...: the 256 colours, in the colour dialog that any
            // program can ask for (colour_dialog.cpp), over this one.
            colour_dialog(number(), gfx::nearest(colour_), &DisplayDialog::other_picked, this);
        } else if (which == browse_) {
            // Browse...: the Open dialog that any program asks for, over
            // this one, in the folder of the picture named or of the samples.
            char folder[fs::kMaxPath] = "C:\\pictures";
            if (picture_[0] != '\0') {
                fs::path::parent(picture_, folder, sizeof(folder));
            }
            open_file_dialog(number(), folder, &DisplayDialog::picture_picked, this);
        } else if (which == no_picture_) {
            picture_[0] = '\0';
            sync();
            show_small_picture(false);
        } else if (which == ok_) {
            before_ = picked();
            write_settings(settings_, before_);
            const int mode = modes_.selected();
            close(); // applies
            change_screen(mode, app::kNone);
        } else if (which == cancel_) {
            close();
        } else if (which == apply_) {
            apply(picked()); // shown on the real desktop; only OK keeps it
            change_screen(modes_.selected(), number());
        }
    }

    // A cell or a row was picked, or another tab.
    void changed(app::Control which) override
    {
        if (which == tabs_) {
            show_page(tabs_.selected());
            return;
        }
        if (which == colours_) {
            colour_ = kColours[colours_.selected()].rgb;
        }
        const auto position = static_cast<os::Wallpaper>(radio_value(kPositionGroup));
        const bool moved = position != position_;
        position_ = position;
        sync();
        if (which == modes_ || moved) {
            show_small_picture(false); // as that screen would show it, standing so
        }
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        const int mode = modes_.selected() < 0 ? 0 : modes_.selected();
        const gfx::Bitmap small = {small_.width, small_.height, small_.pixels, -1};
        paint_preview(c, kPreview, colour_, patterns_.selected(), gfx::theme_at(themes_.selected()),
                      os::video_mode_at(mode).width > os::video_mode_at(0).width,
                      small_.pixels != nullptr ? &small : nullptr, small_tiled_);
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
            const bool appearance = tabs_.selected() == kAppearance;
            if (appearance && (letter == 't' || letter == 'T')) {
                set_focus(themes_);
                return true;
            }
            if (appearance && (letter == 'c' || letter == 'C')) {
                set_focus(colours_);
                return true;
            }
            if (appearance && (letter == 'o' || letter == 'O')) {
                other_.press();
                return true;
            }
            if (appearance && (letter == 'p' || letter == 'P')) {
                set_focus(patterns_);
                return true;
            }
            if (appearance && (letter == 'u' || letter == 'U' || letter == 'b' || letter == 'B')) {
                browse_.press();
                return true;
            }
            if (appearance && (letter == 'i' || letter == 'I')) {
                set_focus(position_ == os::Wallpaper::Tile ? tile_ : position_ == os::Wallpaper::Stretch ? stretch_ : centre_);
                return true;
            }
            if (appearance && (letter == 'n' || letter == 'N')) {
                no_picture_.press();
                return true;
            }
            if (!appearance && (letter == 's' || letter == 'S')) {
                set_focus(modes_);
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
    void closed() override
    {
        closed_ = true;
        apply(before_);
    }

    // The mode was not kept: the list shows the one the screen is in again.
    void mode_went_back()
    {
        modes_.select(os::video_mode());
        sync();
    }

private:
    static void other_picked(gfx::Colour colour, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->colour_ = gfx::shown(colour);
        d->colours_.select(named(d->colour_));
        d->sync();
    }
    static void picture_picked(const char *path, void *context)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        keep_text(d->picture_, path);
        d->sync();
        d->show_small_picture(true);
    }

    // The picture named, as small as the small screen, made by a job (a
    // photograph is seconds of decoding on the board). `say`: tell if it
    // cannot be shown, as for a file just picked; the one the settings name
    // may be gone without a word, as on the desktop.
    struct SmallJob {
        char file[fs::kMaxPath];
        int width, height; // of the screen it is for
        uint8_t background[3];
        os::Wallpaper position;
        FittedPicture out;
        image::Error error;
    };
    void show_small_picture(bool say)
    {
        say_ = say_ || say;
        if (job_busy_) {
            job_again_ = true;
            return;
        }
        release_fitted(small_);
        preview_.redraw();
        if (picture_[0] == '\0') {
            say_ = false;
            return;
        }
        const ports::VideoMode m = os::video_mode_at(modes_.selected() < 0 ? 0 : modes_.selected());
        wallpaper_file(picture_, m.width, m.height, job_.file, sizeof(job_.file));
        job_.width = m.width;
        job_.height = m.height;
        job_.position = position_;
        job_.background[0] = colour_.r;
        job_.background[1] = colour_.g;
        job_.background[2] = colour_.b;
        job::Spec spec;
        spec.name = "display";
        spec.work = &DisplayDialog::small_work;
        spec.done = &DisplayDialog::small_done;
        spec.context = this;
        spec.stack_bytes = 12 * 1024;
        job_busy_ = true; // without tasks the job is over when start_job() returns
        if (!os::start_job(spec)) {
            job_busy_ = false;
            say_ = false;
        }
    }
    static uint32_t small_work(const job::Job &job, void *context)
    {
        SmallJob &j = static_cast<DisplayDialog *>(context)->job_;
        j.error = decode_fitted(j.file, j.width, j.height, j.position == os::Wallpaper::Stretch, kGlassWidth, j.width,
                                j.background, &job, j.out);
        return 0;
    }
    static void small_done(void *context, uint32_t, bool)
    {
        auto *d = static_cast<DisplayDialog *>(context);
        d->job_busy_ = false;
        if (d->closed_) {
            return; // the destructor gives the picture back
        }
        if (d->job_again_) {
            d->job_again_ = false;
            release_fitted(d->job_.out);
            d->show_small_picture(false);
            return;
        }
        d->small_ = d->job_.out;
        d->small_tiled_ = d->job_.position == os::Wallpaper::Tile;
        d->job_.out = {};
        d->preview_.redraw();
        if (d->say_ && d->job_.error != image::Error::None) {
            // Not a picture the desktop can show: not taken.
            char text[160];
            snprintf(text, sizeof(text), "Cannot show %s:\n%s.", fs::path::name(d->picture_),
                     image::error_text(d->job_.error));
            d->picture_[0] = '\0';
            d->sync();
            message_box("Display", text);
        }
        d->say_ = false;
    }
    // The controls of the page in front get their places; the others none.
    void show_page(int page)
    {
        const bool a = page == kAppearance;
        theme_label_.place(a ? kThemeLabel : kNowhere);
        themes_.place(a ? kThemes : kNowhere);
        group_.place(a ? kGroup : kNowhere);
        colour_label_.place(a ? kColourLabel : kNowhere);
        colour_name_.place(a ? kColourName : kNowhere);
        colours_.place(a ? kColourGrid : kNowhere);
        other_.place(a ? kOther : kNowhere);
        pattern_label_.place(a ? kPatternLabel : kNowhere);
        pattern_name_.place(a ? kPatternName : kNowhere);
        patterns_.place(a ? kPatternGrid : kNowhere);
        picture_label_.place(a ? kPictureLabel : kNowhere);
        picture_name_.place(a ? kPictureName : kNowhere);
        browse_.place(a ? kBrowse : kNowhere);
        no_picture_.place(a ? kNoPicture : kNowhere);
        position_label_.place(a ? kPositionLabel : kNowhere);
        centre_.place(a ? kCentre : kNowhere);
        tile_.place(a ? kTile : kNowhere);
        stretch_.place(a ? kStretch : kNowhere);
        mode_label_.place(a ? kNowhere : kModeLabel);
        modes_.place(a ? kNowhere : kModes);
        facts_.place(a ? kNowhere : kFacts);
        note_.place(a ? kNowhere : kNote);
        set_focus(tabs_); // not left on a control that is no longer shown
    }
    void sync()
    {
        char text[32];
        if (named(colour_) >= 0) {
            keep_text(text, kColours[named(colour_)].name);
        } else {
            snprintf(text, sizeof(text), "#%02X%02X%02X%s", colour_.r, colour_.g, colour_.b,
                     is_exact(colour_) ? "" : " (dithered)");
        }
        colour_name_.set(text);
        pattern_name_.set(kPatterns[patterns_.selected()].name);
        picture_name_.set(picture_[0] != '\0' ? fs::path::name(picture_) : "(None)");
        no_picture_.set_enabled(picture_[0] != '\0');
        patterns_.redraw(); // the patterns are shown in the colour picked
        // What the mode picked costs, in the machine's own figures.
        const ports::VideoMode m = os::video_mode_at(modes_.selected() < 0 ? 0 : modes_.selected());
        int n = snprintf(facts_text_, sizeof(facts_text_), "%d x %d pixels in 256 colours\n%d KB of memory for the picture",
                         m.width, m.height, (m.width * m.height + 512) / 1024);
        if (m.frame_us != 0) {
            const int tenths = static_cast<int>((10000000 + m.frame_us / 2) / m.frame_us);
            const int ms_tenths = static_cast<int>((m.frame_us + 50) / 100);
            snprintf(facts_text_ + n, sizeof(facts_text_) - static_cast<size_t>(n),
                     "\n%d.%d pictures a second, a pixel clock of %d MHz\n%d.%d ms to draw each picture in",
                     tenths / 10, tenths % 10, static_cast<int>(m.pixel_clock_hz / 1000000),
                     ms_tenths / 10, ms_tenths % 10);
        }
        facts_.set_text(facts_text_);
        preview_.redraw();
    }
    Choice picked() const
    {
        Choice c = {colour_, patterns_.selected(), themes_.selected() < 0 ? 0 : themes_.selected(), "", position_};
        keep_text(c.picture, picture_);
        return c;
    }

    Rgb colour_ = kColours[0].rgb;
    char picture_[fs::kMaxPath] = "";
    os::Wallpaper position_ = os::Wallpaper::Centre;
    bool small_tiled_ = false;
    FittedPicture small_; // it on the small screen; the Memory port's
    SmallJob job_ = {};   // the job's while job_busy_
    bool job_busy_ = false, job_again_ = false, say_ = false, closed_ = false;
    Choice before_; // what the settings file holds
    fs::Ini settings_;
    ThemeNames theme_names_; // before the list that shows them
    ModeNames mode_names_;
    char facts_text_[200] = "";
    app::Tabs tabs_;
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
    app::Label picture_label_;
    app::Plain picture_name_;
    app::Button browse_, no_picture_;
    app::Label position_label_;
    app::RadioButton centre_, tile_, stretch_;
    app::Label mode_label_;
    app::ListBox modes_;
    app::TextArea facts_;
    app::Label note_;
    app::Button ok_, cancel_, apply_;
};

void screen_went_back()
{
    if (auto *dialog = app::program<DisplayDialog>(g_dialog)) {
        dialog->mode_went_back();
    }
}

} // namespace

void display_dialog()
{
    const AsProgram as("Display");
    if (app::bring_forward(g_question) || app::bring_forward(g_dialog)) {
        return;
    }
    app::Look look;
    look.title = "Display";
    look.width = 380;
    look.height = 446;
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
    const Choice choice = read_settings(ini);
    // The screen's mode, if one was kept and this screen has it. The desktop
    // changes to it before its first picture. Before the desktop's picture
    // is asked for, which is made for the mode asked.
    char want[24], key[24];
    if (ini.get("screen", "mode", want, sizeof(want))) {
        for (int i = 0; i < os::video_mode_count(); i++) {
            mode_key(os::video_mode_at(i), key, sizeof(key));
            if (strcmp(key, want) == 0) {
                os::set_video_mode(i);
            }
        }
    }
    apply(choice);
}

} // namespace shell
