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

using Window = int;  // 0: none
using Control = int; // 0: none
constexpr Window kNoWindow = 0;
constexpr Control kNoControl = 0;

enum class What : uint8_t {
    Command, // a button was pressed: `control`
    Key,     // a key the controls did not use: `key`. True: the program used it
    Paint,   // the client area, after the face and the controls are painted: `canvas`
    Closed,  // the window has closed; it and its numbers are dead after this
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
