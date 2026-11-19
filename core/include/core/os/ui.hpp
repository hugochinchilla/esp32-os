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
    Key,     // a key, before the controls see it: `key`. True: the program used it. With a `control`,
             // a key for the picture that has the keys (kKeys)
    Text,    // characters typed into the picture that has the keys: `control`, `text` (UTF-8)
    Painting, // the window is about to be painted: the last moment to change what the controls show
    Paint,   // the client area, after the face and the controls are painted: `drawing`. With a
             // `control`, that picture alone, its corner as (0, 0)
    Cell,    // a cell of a grid is to be painted: `control`, `index`, `part` (the cell), `drawing`
    Mouse,   // the left button in a picture that asked for it (kMouse): `control`, `mouse`, in the
             // picture's coordinates. From Down to Up they all go to the picture pressed
    RightClick, // the right button went down in a picture that asked for the mouse, or in an icon
                // view: `control`, `x`, `y` in its coordinates
    Resized, // the client area has another size (the user pulled an edge, or maximised): `x`, `y` are
             // its width and height. The program puts its controls where they go now
    Drag,    // an entry of an icon view was held and moved a little: `control`. The program starts
             // the drag of what it stands for
    DragOver, // a file is dragged over the client area: `text` its path, `x`, `y`. True: it would be
              // taken there. `text` is nullptr when the drag has left the window or ended
    Dropped, // the file was let go there, after DragOver said yes: `text`, `x`, `y`
    Closing, // the close button, Alt+F4, a TERM signal. True: the program deals with it (asks about
             // unsaved work, closes itself later); otherwise the window closes
    Menu,    // a menu of the menu bar is about to open: `index`, which. A chance to grey items out
    MouseMoved, // only while the window has the whole screen (take_screen): the mouse moved by
                // `x`, `y`, and `index` says which buttons are held (bit 0 left, 1 right, 2 middle)
    Closed,  // the window has closed, or never opened; once, and it and its numbers are dead after this
};

struct Event {
    What what;
    Control control = kNoControl;
    const wm::KeyMsg *key = nullptr;
    Drawing drawing = kNoDrawing;
    int index = 0;
    gfx::Rect part = {};
    const wm::MouseMsg *mouse = nullptr;
    const char *text = nullptr;
    int x = 0, y = 0;
};

using Events = bool (*)(Window window, const Event &event, void *state);

enum Flag : uint8_t {
    kDialog = 1,       // fixed size, only a close button
    kWholeDesktop = 2, // nothing else can be used while it is open
    kPicture = 4,      // the program paints too (What::Paint)
    kClientSize = 8,   // `width` and `height` are of the client area, not of the frame
    kFixedSize = 16,   // cannot be made bigger or smaller (a dialog never can)
    kMenuBar = 32,     // it will have a menu bar (set_menu_bar): with kClientSize, room for it
};

struct Look {
    const char *title = "";
    int width = 200, height = 100; // of the whole frame; centred on the screen
    int x = -1, y = -1;            // where the frame goes; -1: centred
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
// Another icon for the title bar and the taskbar (16x16; nullptr: none).
void set_icon(Window window, const gfx::Bitmap *icon);
bool in_front(Window window);                     // it is the window in use
// The size of the client area, as `w` and `h`.
gfx::Rect client_size(Window window);
// Another size for the client area. A window on the screen stays where it
// is, but inside the screen; one not yet shown is centred again unless it
// was given a place.
void resize(Window window, int client_width, int client_height);
bool has_keys(Window window);                     // the keys go to it, and it is not minimised
// The menu bar: the program's own table, which must stay valid; the
// window must have been made with kMenuBar. What::Menu comes before one
// of its menus opens.
void set_menu_bar(Window window, const wm::MenuBar *bar);
// Asks for a part of the client area to be painted again.
void redraw(Window window, const gfx::Rect &part);

// The whole screen for one window, for a game that fills it (backlog item
// 81). While the window has it the system paints nothing and shows no
// pointer; every key reaches the window's Key event as it goes down and
// up, the modifier keys too, and none repeats; the mouse comes as
// MouseMoved. The picture is one RRRGGGBB byte per pixel (gfx::rgb),
// `width` by `height`, rows one after the other, and stays the window's to
// write until the loan ends: with give_screen_back(), when the window
// closes, when the screen changes its size, or when the user presses
// Ctrl+Alt+Del, the way out that is always there. The program is not told:
// it asks has_screen() before it writes each picture. Afterwards the
// desktop is painted again.
struct Screen {
    uint8_t *pixels = nullptr;
    int width = 0, height = 0;
};
// False: the window is not on the screen, or the screen is not free.
bool take_screen(Window window, Screen &out);
bool has_screen(Window window);
void give_screen_back(Window window);
// Which part the program has changed, for whoever watches the screen from
// afar (the remote screen). The monitor shows it anyway.
void screen_changed(Window window, const gfx::Rect &part);

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
constexpr int kScrollBarThickness = 16;
Control scroll_bar(Window window, const gfx::Rect &place, bool vertical);
// The rows are the program's texts and must stay valid while they are shown.
Control list(Window window, const gfx::Rect &place, const char *const *items, int count);
Control group(Window window, const gfx::Rect &place, const char *caption); // a frame with a caption
Control text_area(Window window, const gfx::Rect &place, const char *text); // several lines, read only
// Tabs: a row of titles (the program's texts, which must stay valid; six at
// most) and under it the frame of the page they choose. Make it before the
// controls of its pages, which lie over it. It draws no page: when Changed
// tells of another title (a click, the arrows, Ctrl+Tab), the program
// gives the controls of that page their places and the others an empty
// one (place_control). selected() and select() are a list's; select()
// tells of the change. The row is kTabStrip high.
constexpr int kTabStrip = 20;
Control tabs(Window window, const gfx::Rect &place, const char *const *titles, int count);

// A rectangle the program paints (What::Paint with this control).
enum PictureFlag : uint8_t {
    kOnce = 1,  // the program writes every pixel of it exactly once when it paints (a game's board,
                // a picture), so the system need not paint it through a buffer
    kMouse = 2, // What::Mouse and What::RightClick for it
    kKeys = 4,  // it can have the keys: What::Key and What::Text with its number
};
Control picture(Window window, const gfx::Rect &place, uint8_t how = 0);
// Where the text cursor is in a picture that takes the keys, in its
// coordinates: all that a blink paints again. An empty one: none.
void set_caret(Window window, Control picture, const gfx::Rect &caret);
// For a picture that paints its own text cursor: whether the keys go to
// it, and whether the cursor is in the lit half of its blink.
bool control_has_keys(Window window, Control control);
bool caret_lit(Window window);
// A control moved or made another size, in the client area. An empty
// place: it is not shown.
void place_control(Window window, Control control, const gfx::Rect &place);
// Asks for a control to be painted again.
void redraw_control(Window window, Control control);
void redraw_control_part(Window window, Control control, const gfx::Rect &part); // in the control's coordinates
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
// The same at a point of a control (where the right button went down).
void menu_at(Window window, Control control, int x, int y, wm::Menu &menu);

/* ---- The text editor: a control that holds a whole text file ----
 *
 * The text and the undo memory are the control's, asked for when it is
 * made (kNoControl if there is none) and given back with it. Changed
 * tells of the first change after set_modified(false), and of the text
 * going back to unchanged.
 */
Control text_edit(Window window, const gfx::Rect &place, size_t text_bytes, size_t undo_bytes);
enum class Edit : uint8_t { Clear, Undo, Cut, Copy, Paste, Delete, SelectAll };
void edit(Window window, Control text_edit, Edit what);
enum EditState : uint8_t { kModified = 1, kCanUndo = 2, kHasSelection = 4 };
uint8_t edit_state(Window window, Control text_edit);
void set_modified(Window window, Control text_edit, bool modified);
// A file's bytes into the text, in pieces (CRLF becomes a line end):
// begin, append each piece (false when it does not fit), finish.
void load_begin(Window window, Control text_edit);
bool load_append(Window window, Control text_edit, const uint8_t *bytes, size_t size);
void load_finish(Window window, Control text_edit);
// The file's bytes again, for a job of the program that writes them out
// on another task. The reader is taken on the UI task and is good while
// the window stays locked (set_locked), which is what keeps the text and
// the window as they are; read_text may then be called from the job.
using TextReader = const void *;
TextReader text_reader(Window window, Control text_edit);
size_t read_text(TextReader reader, size_t offset, uint8_t *out, size_t room);

// How many bytes of text the clipboard holds (0: Paste has nothing).
size_t clipboard_size();

// Locked: the window is shown and can be moved, but takes no keys, no
// clicks and no request to close, and the pointer is an hourglass over
// it. For a program whose job is using what the window holds.
void set_locked(Window window, bool locked);
bool locked(Window window);

// The hourglass on demand: while it is on, the pointer is an hourglass over
// the window's client area. Nothing else changes: the window takes its keys
// and clicks, can be closed, and the pointer is the arrow over its title
// bar and menu bar. For a program that is working something out and wants
// the user to see it (Chess while the engine thinks). The program turns it
// off when it has finished; a window that closes takes it with it.
void set_hourglass(Window window, bool on);
bool hourglass(Window window);

// Icons in a grid with their names under them: what a folder holds. The
// names and the pictures (32 by 32) are the program's tables and must stay
// valid; set_items() is a list's. Several entries can be picked at once
// (backlog item 55): a click picks one alone, Ctrl+click adds or takes away
// one, Shift+click picks from the last one clicked to this one, a rectangle
// pulled from an empty place picks what it touches, Ctrl+A picks all, and
// the arrow keys move as in a list (with Shift they stretch the range).
// selected_count() and is_selected() say which; selected() is the one
// picked last (the one Enter opens), or -1. select() picks one alone and
// tells of it (Changed), as every change by the mouse or the keys does.
// set_items() keeps what is picked while the number of entries stays the
// same; otherwise only the one picked last stays. highlight() shows one
// more entry as picked without picking it (the folder a dragged file would
// go into); -1: none. Chosen: Enter or a double click. Drag: a picked entry
// held and moved; all the picked ones go together. The first 256 entries
// can be picked.
// RightClick: the right button went down in it; the program asks item_at()
// what is there and picks it, unless it is one of several picked already.
// It shows them one of three ways (backlog item 56), which the program
// sets with set_view_mode(): Icons, the grid, as it is made; List, small
// pictures with the names beside them, filled down the columns and going on
// to the right; Details, one entry per line under a row of titles. What is
// picked stays through a change of view. The List and the Details show the
// 16 x 16 pictures of set_small_icons(), one per entry. The Details show the
// program's columns, set_columns(): the first is the name's (its cells are
// not read, its width is the least it gets: it takes what the others
// leave), the others have a text per entry. All these tables are the
// program's and must stay valid.
enum class ViewMode : uint8_t { Icons, List, Details };
struct Column {
    const char *title;
    int width;                // pixels
    bool right;               // numbers: against the right edge
    const char *const *cells; // a text per entry; nullptr: none
};
Control icon_view(Window window, const gfx::Rect &place);
void set_view_mode(Window window, Control icon_view, ViewMode mode);
void set_small_icons(Window window, Control icon_view, const gfx::Bitmap *const *icons);
void set_columns(Window window, Control icon_view, const Column *columns, int count);
void reset_view(Window window, Control icon_view); // nothing picked, back at the top
// The entry whose picture or name is at a point of it, or -1 (the empty
// space between two entries is no entry).
int item_at(Window window, Control icon_view, int x, int y);
int selected_count(Window window, Control icon_view);
bool is_selected(Window window, Control icon_view, int index);
void highlight(Window window, Control icon_view, int index);

// A bar that fills from the left in blocks, in a sunken box: progress, or
// how full something is.
Control meter(Window window, const gfx::Rect &place, gfx::Colour colour);
void set_level(Window window, Control meter, uint32_t value, uint32_t total); // shown in whole blocks

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
// pictures a window. For an icon view too, where they are not optional.
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
// One that was never shown is destroyed, and its program hears Closed.
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
void dent(Drawing on, const gfx::Rect &r);
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
// The font `scale` times as large, leaning if `slanted` (a web page's headings and italics).
int text_scaled(Drawing on, int x, int y, const char *s, gfx::Colour c, bool bold, bool slanted, int scale);
int text_opaque(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg, int caret_x);
int text_opaque_rgb(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Rgb bg, int caret_x);
void text_up(Drawing on, int x, int y, const char *s, gfx::Colour c, bool bold);
void fill_around(Drawing on, const gfx::Rect &area, const gfx::Rect &hole, gfx::Colour c);

// What of the drawing may be drawn now, in its coordinates: a program can
// skip what lies outside.
gfx::Rect bounds(Drawing on);

// Needs no drawing: the size of a text in the system's font.
int text_width(const char *s, bool bold);
int text_height();

} // namespace draw

} // namespace ui
} // namespace os
