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

enum class What : uint8_t {
    Command, // a button was pressed: `control`
    Changed, // a check box, a scroll bar or a list's selection changed: `control`
    Chosen,  // a row of a list was double-clicked, or Enter on it: `control`
    Key,     // a key the controls did not use: `key`. True: the program used it
    Painting, // the window is about to be painted: the last moment to change what the controls show
    Paint,   // the client area, after the face and the controls are painted: `canvas`
    Closed,  // the window has closed, or never opened; once, and it and its numbers are dead after this
};

struct Event {
    What what;
    Control control = kNoControl;
    const wm::KeyMsg *key = nullptr;
    gfx::Canvas *canvas = nullptr;
};

using Events = bool (*)(Window window, const Event &event, void *state);

enum Flag : uint8_t {
    kDialog = 1,       // fixed size, only a close button
    kWholeDesktop = 2, // nothing else can be used while it is open
    kPicture = 4,      // the program paints too (What::Paint)
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

// Controls, in the window's client area. kNoControl: no room (24 a window).
Control label(Window window, const gfx::Rect &place, const char *text);
Control button(Window window, const gfx::Rect &place, const char *text);
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

// What a control shows: a label, a button, a text area, a text field.
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
void set_items(Window window, Control list, const char *const *items, int count);
int selected(Window window, Control list); // -1: none
void select(Window window, Control list, int index);
// Which control the keys go to.
void set_focus(Window window, Control control);
// The button Enter presses.
void set_default(Window window, Control button);
// As if it had been clicked.
void press(Window window, Control button);

// On the screen, or to the front if it is there already. False: the window
// manager is full, and the window is gone.
bool show(Window window);
void close(Window window);

} // namespace ui
} // namespace os
