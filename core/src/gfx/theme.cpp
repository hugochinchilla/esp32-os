// The themes. Every flat colour here is one the board has exactly (red and
// green 0..7, blue 0..3), so nothing but the title fades is dithered.
#include "core/gfx/theme.hpp"

#include <cstring>

namespace gfx {

namespace {

constexpr Colour kNavy = rgb(0, 1, 2);
constexpr Colour kSlate = rgb(3, 3, 2);
constexpr Colour kIndigo = rgb(2, 2, 2);
constexpr Colour kAmber = rgb(7, 5, 0);
constexpr Colour kNight = rgb(1, 1, 1);

// The look the desktop has had from the start: grey, bevelled, a blue fade
// in the title bar. It never reads the fields for flat edges; they hold
// what the others start from.
constexpr Theme classic()
{
    Theme t = {};
    t.key = "classic";
    t.name = "Classic";
    t.edges = Edges::Bevel;
    t.title_bar = TitleBar::FadeAcross;
    t.title_buttons = TitleButtons::Buttons;
    t.face = kGrey;
    t.ink = kBlack;
    t.paper = kWhite;
    t.dim = kDarkGrey;
    t.select = {0, 0, 128};
    t.select_ink = kWhite;
    t.outline = kBlack;
    t.button = t.button_down = t.track = t.thumb = kGrey;
    t.title_from = {0, 0, 128};
    t.title_to = {16, 132, 208};
    t.title_off_from = {128, 128, 128};
    t.title_off_to = {192, 192, 192};
    t.frame_on = t.frame_off = kGrey;
    t.rim_on = t.rim_off = kBlack;
    t.title_ink_on = kWhite;
    t.title_ink_off = kGrey;
    t.title_shadow = kBlack;
    t.title_rule = false;
    t.title_dim = kDarkGrey;
    t.tile_on = t.tile_off = t.close_on = t.close_off = kGrey;
    t.tile_rim = kWhite;
    t.tile_ink = kBlack;
    t.bar = t.task = t.task_line = t.task_on = t.task_on_line = t.start = kGrey;
    t.bar_line = kWhite;
    t.bar_ink = t.start_ink = t.task_on_ink = kBlack;
    t.swatch = rgb(0, 0, 2);
    return t;
}

// After the blue and green desktops of the early 2000s: a blue bar that
// fades down, a red close tile, cream faces, a green Start.
constexpr Theme sky()
{
    Theme t = classic();
    t.key = "sky";
    t.name = "Sky";
    t.edges = Edges::Flat;
    t.title_bar = TitleBar::FadeDown;
    t.title_buttons = TitleButtons::Tiles;
    t.face = rgb(6, 6, 2);
    t.dim = rgb(4, 4, 2);
    t.select = {36, 109, 182};
    t.outline = kNavy;
    t.button = kWhite;
    t.button_down = rgb(5, 6, 3);
    t.track = kWhite;
    t.thumb = rgb(5, 6, 3);
    t.title_from = {70, 150, 255};
    t.title_to = {0, 60, 235};
    t.frame_on = rgb(0, 2, 3);
    t.frame_off = rgb(3, 4, 3);
    t.rim_on = t.rim_off = kNavy;
    t.title_ink_on = t.title_ink_off = kWhite;
    t.title_shadow = kNavy;
    t.title_dim = rgb(4, 5, 3);
    t.tile_on = rgb(1, 3, 3);
    t.tile_off = rgb(3, 4, 3);
    t.close_on = rgb(6, 2, 0);
    t.close_off = rgb(6, 4, 2);
    t.tile_rim = t.tile_ink = kWhite;
    t.bar = rgb(1, 2, 3);
    t.bar_line = rgb(3, 5, 3);
    t.bar_ink = kWhite;
    t.start = rgb(1, 4, 0);
    t.start_ink = kWhite;
    t.task = t.task_line = rgb(1, 3, 3);
    t.task_on = t.task_on_line = kNavy;
    t.task_on_ink = kWhite;
    t.swatch = rgb(0, 2, 3);
    return t;
}

// Flat and light: pale faces, one indigo for the active window's frame and
// bar, the title buttons as bare shapes.
constexpr Theme glacier()
{
    Theme t = classic();
    t.key = "glacier";
    t.name = "Glacier";
    t.edges = Edges::Flat;
    t.title_bar = TitleBar::Solid;
    t.title_buttons = TitleButtons::Bare;
    t.face = rgb(6, 6, 3);
    t.dim = kSlate;
    t.select = {73, 73, 182};
    t.outline = kSlate;
    t.button = kWhite;
    t.button_down = t.track = rgb(5, 5, 3);
    t.thumb = kSlate;
    t.frame_on = t.rim_on = kIndigo;
    t.frame_off = t.rim_off = rgb(4, 4, 2);
    t.title_ink_on = kWhite;
    t.title_ink_off = rgb(6, 6, 3);
    t.title_dim = rgb(4, 4, 3);
    t.bar = t.bar_line = t.task = kNight;
    t.bar_ink = kWhite;
    t.start = kIndigo;
    t.start_ink = kWhite;
    t.task_line = kSlate;
    t.task_on = t.task_on_line = rgb(6, 6, 3);
    t.task_on_ink = kBlack;
    t.swatch = kIndigo;
    return t;
}

// Dark: navy faces, black fields, white text, and amber for whatever is
// active. The title bar is the face colour, not black, so the black
// outlines of the program icons still show on it.
constexpr Theme midnight()
{
    Theme t = classic();
    t.key = "midnight";
    t.name = "Midnight";
    t.edges = Edges::Flat;
    t.title_bar = TitleBar::Solid;
    t.title_buttons = TitleButtons::Bare;
    t.face = kNight;
    t.ink = kWhite;
    t.paper = kBlack;
    t.dim = kSlate;
    t.select = {255, 182, 0};
    t.select_ink = kBlack;
    t.outline = kSlate;
    t.button = rgb(2, 2, 1);
    t.button_down = kSlate;
    t.track = kBlack;
    t.thumb = kSlate;
    t.frame_on = t.frame_off = kNight;
    t.rim_on = kAmber;
    t.rim_off = kSlate;
    t.title_ink_on = kAmber;
    t.title_ink_off = rgb(5, 5, 2);
    t.title_rule = true;
    t.title_dim = rgb(2, 2, 1);
    t.bar = t.task = kNight;
    t.bar_line = t.task_line = kSlate;
    t.bar_ink = rgb(5, 5, 2);
    t.start = kAmber;
    t.start_ink = kBlack;
    t.task_on = rgb(2, 2, 1);
    t.task_on_line = t.task_on_ink = kAmber;
    t.swatch = kAmber;
    return t;
}

constexpr Theme kThemes[] = {classic(), sky(), glacier(), midnight()};
constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

} // namespace

const Theme *g_theme = &kThemes[0];

int theme_count() { return kThemeCount; }

const Theme &theme_at(int index) { return kThemes[index >= 0 && index < kThemeCount ? index : 0]; }

int theme_named(const char *key)
{
    for (int i = 0; i < kThemeCount; i++) {
        if (strcmp(kThemes[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int theme_index() { return static_cast<int>(g_theme - kThemes); }

void use_theme(int index) { g_theme = &theme_at(index); }

} // namespace gfx
