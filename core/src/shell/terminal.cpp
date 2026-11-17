// The Terminal: the command shell in a window (backlog item 15;
// docs/journal/2026-11-17-terminal-window.md). The window is the device of
// a TTY: keys go in as the bytes a terminal sends, and what the TTY shows is
// written into a grid of 80 by 25 characters (tty::Screen) that the window
// paints. The session (sh::Session) is the same one the serial console and
// telnet will run.
//
// The session runs on the UI task, a line at a time: when Enter completes a
// line it is run there and then, and the desktop waits for it, as it does
// for a save in the Editor. Nothing here is a task of its own.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/sh/sh.hpp"
#include "core/shell/shell.hpp"
#include "core/tty/screen.hpp"
#include "core/tty/tty.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "internal.hpp"

namespace shell {

namespace {

using tty::Screen;

constexpr int kCellW = 6, kCellH = 12; // Spleen 6x12
constexpr int kPad = 2;                // dark edge around the characters
constexpr gfx::Colour kPaper = gfx::kBlack;
constexpr gfx::Colour kInk = gfx::rgb(5, 5, 2); // light grey, as on a DOS screen

class TerminalWindow;

// The grid of characters, and the keyboard.
class TerminalView : public wm::Widget {
public:
    static constexpr int kWidth = Screen::kColumns * kCellW + 2 * kPad;
    static constexpr int kHeight = Screen::kRows * kCellH + 2 * kPad;

    TerminalView(const gfx::Rect &r, TerminalWindow &owner, Screen &screen) : Widget(r), owner_(owner), screen_(screen) {}

    static gfx::Rect rows_rect(int first, int last)
    {
        return {kPad, kPad + first * kCellH, Screen::kColumns * kCellW, (last - first + 1) * kCellH};
    }

    bool focusable() const override { return true; }
    bool layered() const override { return false; } // every pixel is written once below
    gfx::Rect caret_rect() const override
    {
        const int row = screen_.cursor_row();
        if (row < 0) {
            return {0, 0, 0, 0};
        }
        return {kPad + screen_.cursor_column() * kCellW, kPad + row * kCellH, kCellW, kCellH};
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect whole = {0, 0, rect.w, rect.h};
        c.fill_around(whole, rows_rect(0, Screen::kRows - 1), kPaper);
        const gfx::Rect limit = c.limit().offset(-c.origin_x(), -c.origin_y());
        const gfx::Rect caret = caret_rect();
        for (int row = 0; row < Screen::kRows; row++) {
            const gfx::Rect r = rows_rect(row, row);
            if (!r.overlaps(limit)) {
                continue;
            }
            const uint8_t *cells = screen_.row(row);
            if (caret.w == 0 || caret.y != r.y) {
                run(c, cells, 0, Screen::kColumns, r.y, kInk, kPaper);
                continue;
            }
            // The row with the cursor: the text before it, the cursor's own
            // cell, the text after it.
            const int at = screen_.cursor_column();
            run(c, cells, 0, at, r.y, kInk, kPaper);
            if (has_focus() && caret_shown()) {
                run(c, cells, at, at + 1, r.y, kPaper, kInk); // a block, the character cut out of it
            } else if (has_focus()) {
                run(c, cells, at, at + 1, r.y, kInk, kPaper); // the blink's dark half
            } else {
                // Another window has the keyboard: a line under the cell.
                c.sub({caret.x, caret.y, kCellW, kCellH - 1}).text_opaque(0, 0, one(cells[at]), kInk, kPaper);
                c.hline(caret.x, caret.y + kCellH - 1, kCellW, kInk);
            }
            run(c, cells, at + 1, Screen::kColumns, r.y, kInk, kPaper);
        }
    }

    bool key(const wm::KeyMsg &k) override;
    void text(const char *utf8) override;

private:
    // One Latin-1 character as the UTF-8 the canvas takes.
    static const char *one(uint8_t c)
    {
        static char out[3];
        if (c < 0x80) {
            out[0] = static_cast<char>(c);
            out[1] = 0;
        } else {
            out[0] = static_cast<char>(0xC0 | (c >> 6));
            out[1] = static_cast<char>(0x80 | (c & 0x3F));
            out[2] = 0;
        }
        return out;
    }

    // Cells [from, to) of a row, each pixel written once.
    static void run(gfx::Canvas &c, const uint8_t *cells, int from, int to, int y, gfx::Colour ink, gfx::Colour paper)
    {
        if (from >= to) {
            return;
        }
        char text[2 * Screen::kColumns + 1];
        int used = 0;
        for (int i = from; i < to; i++) {
            if (cells[i] < 0x80) {
                text[used++] = static_cast<char>(cells[i]);
            } else {
                text[used++] = static_cast<char>(0xC0 | (cells[i] >> 6));
                text[used++] = static_cast<char>(0x80 | (cells[i] & 0x3F));
            }
        }
        text[used] = 0;
        c.text_opaque(kPad + from * kCellW, y, text, ink, paper);
    }

    TerminalWindow &owner_;
    Screen &screen_;
};

constexpr int kMaxTerminals = 3;
TerminalWindow *g_terminals[kMaxTerminals] = {};

class TerminalWindow : public wm::Window {
public:
    explicit TerminalWindow(int place)
        : Window("Terminal",
                 {40 + place * 22, 30 + place * 22, TerminalView::kWidth + 2 * kBorder,
                  TerminalView::kHeight + kClientTop + kBorder},
                 &gfx::terminal_icon_16()),
          view_({0, 0, TerminalView::kWidth, TerminalView::kHeight}, *this, screen_), tty_(screen_),
          session_(tty_, environment())
    {
        set_resizable(false);
        add(view_);
        set_focus(&view_);
        tty_.set_size(Screen::kColumns, Screen::kRows);
        session_.greet();
        pump();
    }

    ~TerminalWindow()
    {
        for (TerminalWindow *&t : g_terminals) {
            if (t == this) {
                t = nullptr;
            }
        }
    }

    // Keys from the window, as the bytes a terminal sends for them.
    void typed(const char *bytes, size_t size)
    {
        tty_.input(reinterpret_cast<const uint8_t *>(bytes), size);
        pump();
        if (session_.ended()) {
            close(); // exit, or Ctrl+D
        }
    }

    void look_back(int lines)
    {
        screen_.scroll_view(lines);
        show_changes();
    }

    const Screen &screen() const { return screen_; }

private:
    static sh::Env environment()
    {
        sh::Env env;
        env.storage = &storage();
        env.procs = &proc::table();
        env.system = &system_port();
        // No UiCall: this is the UI task, the session calls straight in.
        return env;
    }

    // Runs what has been typed: every finished line, then the prompt for
    // the next one.
    void pump()
    {
        for (int guard = 0; guard < 64 && !session_.ended() && (tty_.readable() || !session_.waiting()); guard++) {
            session_.step();
        }
        show_changes();
    }

    void show_changes()
    {
        int first, last;
        if (screen_.take_changes(first, last)) {
            view_.invalidate_part(TerminalView::rows_rect(first, last));
        }
        screen_.take_bells(); // no sound yet
        if (wm::Wm *m = manager()) {
            m->caret_moved(); // the cursor is shown at once where it now is
        }
    }

    Screen screen_;
    TerminalView view_;
    tty::Tty tty_;
    sh::Session session_;
};

bool TerminalView::key(const wm::KeyMsg &k)
{
    if (!k.pressed || (k.mods & wm::kAlt) != 0) {
        return false;
    }
    const auto send = [this](const char *bytes) {
        owner_.typed(bytes, strlen(bytes));
        return true;
    };
    if ((k.mods & wm::kCtrl) != 0) {
        // Ctrl and a letter is that letter's control code: Ctrl+C is 3.
        if (k.usage >= wm::key::A && k.usage <= wm::key::Z) {
            const char code[2] = {static_cast<char>(k.usage - wm::key::A + 1), 0};
            return send(code);
        }
        return false;
    }
    switch (k.usage) {
    case wm::key::Enter: return send("\r");
    case wm::key::Backspace: return send("\x7f");
    case wm::key::Tab: return send("\t");
    case wm::key::Up: return send("\x1b[A");
    case wm::key::Down: return send("\x1b[B");
    case wm::key::Right: return send("\x1b[C");
    case wm::key::Left: return send("\x1b[D");
    case wm::key::Home: return send("\x1b[H");
    case wm::key::End: return send("\x1b[F");
    case wm::key::Delete: return send("\x1b[3~");
    // What has scrolled off the top, a screen at a time.
    case wm::key::PageUp: owner_.look_back(Screen::kRows - 1); return true;
    case wm::key::PageDown: owner_.look_back(-(Screen::kRows - 1)); return true;
    default: return false; // a character: it comes back through text()
    }
}

void TerminalView::text(const char *utf8) { owner_.typed(utf8, strlen(utf8)); }

} // namespace

// Every call opens another Terminal, as a process of its own.
void open_terminal()
{
    int place = -1;
    for (int i = kMaxTerminals - 1; i >= 0; i--) {
        if (g_terminals[i] == nullptr || !g_terminals[i]->open()) {
            place = i; // free, or closed and about to go
        }
    }
    if (place < 0) {
        message_box("Terminal", "Three Terminals are open already.\nClose one of them first.");
        return;
    }
    proc::Table &t = proc::table();
    const proc::Pid pid = t.spawn("Terminal", t.current(), proc::kWindowed);
    // With no room in the table it is the caller's window, as elsewhere.
    const proc::Scope as(&t, pid != proc::kNone ? pid : t.current());
    TerminalWindow *w = make_window<TerminalWindow>(place);
    if (w == nullptr) {
        no_memory_for("Terminal");
        return;
    }
    g_terminals[place] = w;
    show(*w);
    discard(w); // not opened: the window manager was full
}

const tty::Screen *terminal_screen()
{
    // The one the keyboard goes to, else any that is open.
    const TerminalWindow *found = nullptr;
    for (const TerminalWindow *t : g_terminals) {
        if (t != nullptr && t->open() && (found == nullptr || t->focused())) {
            found = t;
        }
    }
    return found != nullptr ? &found->screen() : nullptr;
}

} // namespace shell
