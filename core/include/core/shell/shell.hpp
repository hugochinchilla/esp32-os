// The desktop shell: built-in programs, the Start menu, desktop icons and
// the small dialogs. How programs are delivered is still the owner's open
// question (docs/DECISIONS.md); for now they are built in, behind this table,
// so the Start menu, the desktop and later a file manager launch them the
// same way.
#pragma once

#include "core/fs/fs.hpp"
#include "core/gfx/canvas.hpp"
#include "core/ports.hpp"
#include "core/wm/wm.hpp"

namespace net {
class Manager;
}

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

// A small window with a message and an OK button. It is modal to the window
// in use when it opens. One at a time: a new message replaces the one shown.
void message_box(const char *title, const char *text);
// The same with a question and up to three buttons; `answer` gets the index
// of the button chosen (closing it some other way picks the last button).
using AnswerFn = void (*)(int choice, void *context);
void ask(const char *title, const char *text, const char *const *buttons, int count, AnswerFn answer,
         void *context);
// The Shut Down dialog: Restart (through the System port) or Cancel.
void shut_down_dialog();

// File dialogs, for any program. Modal to `owner`. `chosen` gets the full
// path ("C:\docs\notes.txt") when the user picks one; nothing on Cancel.
// Save As asks before replacing an existing file.
using PathFn = void (*)(const char *path, void *context);
void open_file_dialog(wm::Window *owner, const char *folder, PathFn chosen, void *context);
void save_file_dialog(wm::Window *owner, const char *folder, const char *name, PathFn chosen,
                      void *context);
// A question with a text answer (a new name).
using TextFn = void (*)(const char *text, void *context);
void input_dialog(wm::Window *owner, const char *title, const char *prompt, const char *initial,
                  TextFn done, void *context);

// Opens a file with the program for its kind (by extension). Returns
// false, after saying so, when there is none.
bool open_file(const char *path);
// A File manager window on a folder; nullptr for My Board (the drives).
void open_folder(const char *path);
// The Editor with a file (nullptr: a new, empty one).
void open_in_editor(const char *path);
// The Picture viewer with a picture (nullptr: asks for one).
void open_picture(const char *path);
// Tests: decode the picture shown to the end now. Timings of the last
// picture (µs): decoding, opening to last row, longest timer slice.
void finish_picture();
const uint32_t *picture_timings();
constexpr int kPictureTimings = 3;

// Wi-Fi, if the machine has it: call before start(). The shell then shows
// the signal icon in the tray, keeps the manager running and sets the clock
// from the network. Without it, Network says it is not available.
void set_network(net::Manager *wifi);
net::Manager *network();
// The Network panel: networks in range, connect, forget.
void open_network();

// Sets up the desktop icons and the Start menu, and opens the first windows.
// `storage` holds the drives; `memory` gives the large buffers (editor,
// clipboard, folder lists), once.
void start(wm::Wm &wm, ports::System &system, fs::Storage &storage, ports::Memory &memory);

} // namespace shell
