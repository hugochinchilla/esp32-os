// The desktop shell: built-in programs, the Start menu, desktop icons and
// the small dialogs. How programs are delivered is still the owner's open
// question (docs/DECISIONS.md); for now they are built in, behind this table,
// so the Start menu, the desktop and later a file manager launch them the
// same way.
#pragma once

#include "core/fs/fs.hpp"
#include "core/fs/ini.hpp"
#include "core/games/cactus.hpp"
#include "core/games/snake.hpp"
#include "core/gfx/canvas.hpp"
#include "core/ports.hpp"
#include "core/wm/wm.hpp"

namespace tty {
class Screen;
}

namespace sh {
class Desktop;
}

namespace net {
class Manager;
}

namespace shell {

struct App {
    const char *name;
    const gfx::Bitmap *icon; // 16x16
    void (*open)();          // opens its window, or brings it to the front
    // Each launch is another copy, in a process of its own that open() makes
    // (the Editor). Otherwise the program runs once at a time.
    bool many = false;
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
// The Shut Down dialog: Shut Down, Restart (through the System port) or
// Cancel.
void shut_down_dialog();
// Shut Down: asks every program to close, then hands the screen to the
// power-down screen for good (Ctrl+Alt+Del there restarts). Returns false,
// with nothing shut down, when a program has a question open instead.
bool power_down();
bool powered_down();
// The Task Manager (also Ctrl+Alt+Del): opens it or brings it to the front.
void open_task_manager();

// The screensaver's settings (Start > Settings > Screen Saver). It starts
// after kScreenSaverMinutes without input unless changed there.
constexpr int kScreenSaverMinutes = 5, kScreenSaverMaxMinutes = 99;
void screen_saver_dialog();

// The Display dialog (Start > Settings > Display, or a right click on the
// desktop): the desktop's colour and pattern.
void display_dialog();

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
// True if open_file() has a program for this kind of file.
bool has_program_for(const char *path);
// What Start > Run... does with what was typed, and the shell's `open` and
// `run` commands with their words: the name of a program (in any case, or
// the start of it if only one begins so) starts it; a path opens the folder
// in the File manager or the file with its program. A name without a drive
// is looked for from `folder`. False, with the reason in `why`, if nothing
// was opened. `programs` and `paths` say which of the two to try.
bool run_text(const char *typed, const char *folder, char *why, size_t capacity, bool programs = true,
              bool paths = true);
// The Run dialog (Start > Run...).
void run_dialog();
// The desktop as the command shell sees it (core/sh/sh.hpp): for the
// sessions the desktop starts.
sh::Desktop &desktop_for_sessions();
// Welcome and About, as the desktop used to start: the PC picture tests and
// the simulator's tool tests use them (the board starts with no windows).
void open_demo_windows();
// A File manager window on a folder; nullptr for My Board (the drives).
void open_folder(const char *path);
// Another Editor, with a file (nullptr: a new, empty one). A file that an
// Editor already has open comes to the front instead.
void open_in_editor(const char *path);
// A program's own settings file, C:\.config\<program>.ini. load: false
// when there is none (the defaults apply). save: makes C:\.config if needed.
bool load_settings(const char *program, fs::Ini &ini);
bool save_settings(const char *program, const fs::Ini &ini);
// The Picture viewer with a picture (nullptr: asks for one).
void open_picture(const char *path);
// Tests: decode the picture shown to the end now. Timings of the last
// picture (µs): decoding, opening to last row, longest timer slice.
void finish_picture();
const uint32_t *picture_timings();
constexpr int kPictureTimings = 3;
// The Terminal: a command shell (core/sh) in a window. Every call opens
// another one, up to three.
void open_terminal();
// What a Terminal's session needs to run on a task of its own, and the
// portable code cannot make: a lock for its TTY and two things to sleep on.
// The machine hands in a few (one per Terminal that may be open at once),
// before any Terminal opens. With none, or without tasks (job::Jobs), a
// Terminal's session runs on the UI task.
struct SessionKit {
    ports::Lock *lock = nullptr;
    ports::Event *typed = nullptr;    // a line has been typed
    ports::Event *answered = nullptr; // the UI task has done what was asked
    bool in_use = false;
};
void set_session_kits(SessionKit *kits, int count);
// Tests: the grid of the Terminal in front (nullptr: none is open).
const tty::Screen *terminal_screen();

// Culebra, the snake game. Tests: the game in its window.
void open_culebra();
const games::Snake &culebra_game();

// Wi-Fi, if the machine has it: call before start(). The shell then shows
// the signal icon in the tray, keeps the manager running and sets the clock
// from the network. Without it, Network says it is not available.
void set_network(net::Manager *wifi);
net::Manager *network();
// The Network panel: networks in range, connect, forget.
void open_network();

// Tests and reference pictures: the game in the Cactus Patch window, and a
// repaint after changing it directly.
games::CactusField &cactus_patch_field();
void cactus_patch_refresh();
int cactus_patch_seconds();

// Sets up the desktop icons and the Start menu, and opens the first windows.
// `storage` holds the drives; `memory` gives the large buffers (editor,
// clipboard, folder lists), once.
void start(wm::Wm &wm, ports::System &system, fs::Storage &storage, ports::Memory &memory);

} // namespace shell
