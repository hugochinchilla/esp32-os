// Windows and controls by number (backlog item 37, the owner's choice:
// "ventanas y widgets por número"; docs/journal/2026-11-17-ui-handles.md).
//
// A program does not inherit from the window class or hold a button object.
// It asks for a window and for controls in it and is given numbers; the
// objects are the system's, made in memory counted as the program's and
// destroyed by the system when the window closes. What happens in the
// window reaches the program as events, through one function it gives when
// it asks for the window:
//
//     bool events(os::ui::Window window, const os::ui::Event &event, void *state);
//
// The program's own memory for the window (`state`) is asked for with the
// window and goes with it, so nothing of a closed program stays.
//
// This is the first cut, with what the smallest program needs. The numbers
// are the border a program built apart from the system would use (item 27).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/gfx/canvas.hpp"
#include "core/wm/window.hpp"

namespace os {
namespace ui {

// A window's number is the one the whole system knows it by (os::WindowId,
// what os::main_window() answers): given when the window is made, never
// given again.
using Window = int;  // 0: none
using Control = int; // 0: none
constexpr Window kNoWindow = 0;
constexpr Control kNoControl = 0;
// What a program draws on while it paints: a number good until the Paint
// event it came with returns (namespace draw, below).
using Drawing = int; // 0: none
constexpr Drawing kNoDrawing = 0;

enum class What : uint8_t {
    Command, // a button was pressed: `control`
    Changed, // a check box, a scroll bar or a list's selection changed: `control`
    Chosen,  // a row of a list was double-clicked, or Enter on it: `control`. False: press the default button
    Key,     // a key the controls did not use: `key`. True: the program used it
    Painting, // the window is about to be painted: the last moment to change what the controls show
    Paint,   // the client area, after the face and the controls are painted: `drawing`. With a
             // `control`, that picture alone, its corner as (0, 0)
    Cell,    // a cell of a grid is to be painted: `control`, `index`, `part` (the cell), `drawing`
    Closed,  // the window has closed, or never opened; once, and it and its numbers are dead after this
};

struct Event {
    What what;
    Control control = kNoControl;
    const wm::KeyMsg *key = nullptr;
    Drawing drawing = kNoDrawing;
    int index = 0;
    gfx::Rect part = {};
};

using Events = bool (*)(Window window, const Event &event, void *state);

enum Flag : uint8_t {
    kDialog = 1,       // fixed size, only a close button
    kWholeDesktop = 2, // nothing else can be used while it is open
    kPicture = 4,      // the program paints too (What::Paint)
    kClientSize = 8,   // `width` and `height` are of the client area, not of the frame
};

struct Look {
    const char *title = "";
    int width = 200, height = 100; // of the whole frame; centred on the screen
    const gfx::Bitmap *icon = nullptr;
    uint8_t flags = 0;
    // A dialog over this window, which cannot be used while the dialog is
    // open: any window's number, the asking program's or another's.
    Window over = kNoWindow;
};

// A new window of the process asking, not yet on the screen. `state_bytes`
// of zeroed memory come with it and are handed to `events` every time.
// kNoWindow: there is no memory. Texts given here and to the controls must
// stay valid while the window lives (literals, or text in the state).
Window create(const Look &look, Events events, size_t state_bytes = 0);
void *state(Window window);
bool alive(Window window);
bool on_screen(Window window); // shown, and not closed
void set_title(Window window, const char *title); // the text must stay valid
// Asks for a part of the client area to be painted again.
void redraw(Window window, const gfx::Rect &part);

// Controls, in the window's client area. kNoControl: no room (24 a window).
Control label(Window window, const gfx::Rect &place, const char *text);
Control button(Window window, const gfx::Rect &place, const char *text);
// A line of text shown exactly as given ('&' is not an access letter), of
// which the control keeps its own copy (95 bytes at most): for text made
// up as the program runs, a name read from somewhere.
Control plain(Window window, const gfx::Rect &place);
Control check_box(Window window, const gfx::Rect &place, const char *text, bool checked = false);
// One of a group of which one is chosen. `group` is 0 to 3, the window's
// own; `value` is what radio_value() answers while this one is chosen.
Control radio(Window window, const gfx::Rect &place, const char *text, int group, int value);
Control text_field(Window window, const gfx::Rect &place, const char *initial = "");
Control scroll_bar(Window window, const gfx::Rect &place, bool vertical);
// The rows are the program's texts and must stay valid while they are shown.
Control list(Window window, const gfx::Rect &place, const char *const *items, int count);
Control group(Window window, const gfx::Rect &place, const char *caption); // a frame with a caption
Control text_area(Window window, const gfx::Rect &place, const char *text); // several lines, read only

// A rectangle the program paints (What::Paint with this control).
Control picture(Window window, const gfx::Rect &place);
// Asks for a control to be painted again.
void redraw_control(Window window, Control control);
// A grid of cells to pick one from, each painted by the program
// (What::Cell): colours, patterns. The one picked has a frame, the arrows
// move it, and Changed tells of it. `margin`: from the edge of a cell to
// its sunken frame. selected() and select() are a list's; select() tells
// nobody, and -1 leaves none picked.
Control grid(Window window, const gfx::Rect &place, int columns, int count, int cell_w, int cell_h, int margin = 3);

// A box showing a text and a picture, with an arrow to press (Command):
// what a "Look in" box is. The program opens what drops (menu_under).
Control drop_down(Window window, const gfx::Bitmap *icon, const gfx::Rect &place);
// A menu of the program's just under a control. The menu and its items
// are the program's and must stay valid while it is open.
void menu_under(Window window, Control control, wm::Menu &menu);

// What a control shows: a label, a button, a text area, a text field, a
// drop-down box (whose text must stay valid, as a label's).
void set_text(Window window, Control control, const char *text);
// What is in a text field, as UTF-8.
void get_text(Window window, Control control, char *out, int capacity);
void select_all(Window window, Control text_field);
void set_hidden(Window window, Control text_field, bool hidden); // dots for the characters
void set_enabled(Window window, Control button, bool enabled);
bool checked(Window window, Control check_box);
void set_checked(Window window, Control check_box, bool checked);
int radio_value(Window window, int group);
void set_radio_value(Window window, int group, int value);
// A scroll bar: from 0 to `maximum`, `page` of it in view.
void set_range(Window window, Control scroll_bar, int maximum, int page);
int value(Window window, Control scroll_bar);
void set_value(Window window, Control scroll_bar, int value); // no Changed for it
// A list.
// `icons`: a picture for each row, or nullptr for none. One list with
// pictures a window.
void set_items(Window window, Control list, const char *const *items, int count,
               const gfx::Bitmap *const *icons = nullptr);
int selected(Window window, Control list); // -1: none. A grid's too
void select(Window window, Control list, int index);
// Which control the keys go to.
Control focus(Window window); // kNoControl: none
void set_focus(Window window, Control control);
// The button Enter presses.
void set_default(Window window, Control button);
// As if it had been clicked.
void press(Window window, Control button);

// On the screen, or to the front if it is there already. False: the window
// manager is full, and the window is gone.
bool show(Window window);
void close(Window window);

/* ---- Drawing, while a Paint event is being answered ----
 *
 * The owner's choice (2026-11-17): a program draws by calls on a number,
 * as it makes its controls, and does not hold the system's canvas object.
 * The calls are the canvas's own (core/gfx/canvas.hpp), one for one, in
 * the window's client coordinates and cut to what may be drawn. A number
 * that is not a drawing in progress draws nothing.
 */
namespace draw {

// A part of a drawing, with its own corner as (0, 0) and cut to `part`.
// Good until the same Paint event returns; kNoDrawing when there are too
// many at once (kMaxParts).
constexpr int kMaxParts = 8;
Drawing part(Drawing on, const gfx::Rect &part);

void fill(Drawing on, const gfx::Rect &r, gfx::Colour c);
void hline(Drawing on, int x, int y, int w, gfx::Colour c);
void vline(Drawing on, int x, int y, int h, gfx::Colour c);
void fill_rgb(Drawing on, const gfx::Rect &r, gfx::Rgb c);
void fill_pattern(Drawing on, const gfx::Rect &r, gfx::Rgb paper, gfx::Rgb ink, const uint8_t rows[8]);
void fill_fade(Drawing on, const gfx::Rect &r, gfx::Rgb from, gfx::Rgb to, bool vertical, const gfx::Rect *span);
void focus_rect(Drawing on, const gfx::Rect &r);
void checker(Drawing on, const gfx::Rect &r);
void edge(Drawing on, const gfx::Rect &r, gfx::Colour tl, gfx::Colour br);
void raised(Drawing on, const gfx::Rect &r);
void sunken(Drawing on, const gfx::Rect &r);
void button(Drawing on, const gfx::Rect &r, gfx::ButtonLook look);
void button_frame(Drawing on, const gfx::Rect &r, gfx::ButtonLook look);
void blit(Drawing on, const gfx::Bitmap &b, int x, int y);
void blit_selected(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour tint);
void blit2x(Drawing on, const gfx::Bitmap &b, int x, int y);
void blit_opaque(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour bg);
void pattern(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour c);
void pattern_opaque(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour fg, gfx::Colour bg);
// The text calls answer the x after the last character, as the canvas does.
int text(Drawing on, int x, int y, const char *s, gfx::Colour c, bool bold);
int text_opaque(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg, int caret_x);
int text_opaque_rgb(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Rgb bg, int caret_x);
void text_up(Drawing on, int x, int y, const char *s, gfx::Colour c, bool bold);
void fill_around(Drawing on, const gfx::Rect &area, const gfx::Rect &hole, gfx::Colour c);

// Needs no drawing: the size of a text in the system's font.
int text_width(const char *s, bool bold);
int text_height();

} // namespace draw

} // namespace ui
} // namespace os
