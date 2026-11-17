// The look of windows, buttons, menus and the taskbar: a table of colours and
// a few styles, one row per theme. Everything that draws a part of the user
// interface asks here, so the Display dialog can change the look while the
// desktop works the same (backlog item 47).
//
// The sizes and places of things are not in the table: every theme has the
// same borders, title height and buttons in the same places.
//
// Included by canvas.hpp, so whatever draws has it.
#pragma once

#include <cstdint>

#include "core/gfx/canvas.hpp"

namespace gfx {

// How edges are drawn: the two-pixel light and shadow bevels of the classic
// look, or one flat outline.
enum class Edges : uint8_t { Bevel, Flat };
// The title bar: a fade from left to right (classic); a fade from top to
// bottom; one flat colour.
enum class TitleBar : uint8_t { FadeAcross, FadeDown, Solid };
// Its buttons: grey buttons like any other (classic); small coloured tiles
// with a light rim; the bare shapes on the bar.
enum class TitleButtons : uint8_t { Buttons, Tiles, Bare };

struct Theme {
    const char *key;  // in the settings file
    const char *name; // in the Display dialog
    Edges edges;
    TitleBar title_bar;
    TitleButtons title_buttons;

    // Everywhere.
    Colour face;       // windows, dialogs, menus
    Colour ink;        // text
    Colour paper;      // inside fields and lists
    Colour dim;        // greyed text, separators, group frames
    Rgb select;        // the chosen item
    Colour select_ink; // its text

    // Flat edges only (a bevelled theme draws these from its face colour).
    Colour outline;     // around buttons, fields, menus
    Colour button;      // a button's face
    Colour button_down; // held down, or switched on
    Colour track;       // a scroll bar's track
    Colour thumb;       // and its thumb

    // The window frame and title bar; `on` is the active window.
    Rgb title_from, title_to;         // the fade of the active window
    Rgb title_off_from, title_off_to; // FadeAcross: of the others
    Colour frame_on, frame_off;       // flat: the border, and the bar where it is not a fade
    Colour rim_on, rim_off;           // flat: the one-pixel line around the window
    Colour title_ink_on, title_ink_off;
    Colour title_shadow; // FadeDown: under the title's letters
    bool title_rule;     // a line in the rim's colour under the bar
    Colour title_dim;    // a button that cannot be used
    // Tiles: the tile of the active window and of the others, the close
    // tile of each, the rim, the shape.
    Colour tile_on, tile_off, close_on, close_off, tile_rim, tile_ink;

    // The taskbar (flat only).
    Colour bar, bar_line, bar_ink;
    Colour start, start_ink;
    Colour task, task_line;                       // a window's button, the line under it
    Colour task_on, task_on_line, task_on_ink;    // of the active window

    // One colour that stands for the title bar in small pictures.
    Colour swatch;

    bool flat() const { return edges == Edges::Flat; }
};

int theme_count();
const Theme &theme_at(int index);
// The place of the theme with this key in the table; -1: there is none.
int theme_named(const char *key);

// The theme in use. Changing it paints nothing: os::set_theme does that.
extern const Theme *g_theme;
inline const Theme &theme() { return *g_theme; }
int theme_index();
void use_theme(int index);

inline Colour face() { return g_theme->face; }
inline Colour ink() { return g_theme->ink; }
inline Colour paper() { return g_theme->paper; }
inline Colour dim() { return g_theme->dim; }
// A button's face: the window's where edges are bevels.
inline Colour button_face(bool down = false)
{
    return g_theme->flat() ? (down ? g_theme->button_down : g_theme->button) : g_theme->face;
}

} // namespace gfx
