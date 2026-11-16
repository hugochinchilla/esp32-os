// The desktop shell: built-in programs, the Start menu, desktop icons and
// the small dialogs. How programs are delivered is still the owner's open
// question (docs/DECISIONS.md); for now they are built in, behind this table,
// so the Start menu, the desktop and later a file manager launch them the
// same way.
#pragma once

#include "core/gfx/canvas.hpp"
#include "core/ports.hpp"
#include "core/wm/wm.hpp"

namespace shell {

struct App {
    const char *name;
    const gfx::Bitmap *icon; // 16x16
    void (*open)();          // opens its window, or brings it to the front
};

int app_count();
const App &app(int index);
const App *find_app(const char *name);
void launch(const App &app);

// A small window with a message and an OK button. One at a time: a new
// message replaces the one shown.
void message_box(const char *title, const char *text);
// The Shut Down dialog: Restart (through the System port) or Cancel.
void shut_down_dialog();

// Sets up the desktop icons and the Start menu, and opens the first windows.
void start(wm::Wm &wm, ports::System &system);

} // namespace shell
