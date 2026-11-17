// The Terminal: a terminal emulator in a window (backlog item 15;
// docs/journal/2026-11-17-terminal-window.md, and for what it is today
// docs/journal/2026-11-17-terminal-split.md).
//
// The window is a device and nothing else. It has a grid of characters
// (tty::Screen), 80 by 25 at first, that it paints; the keyboard, whose keys
// it turns into the bytes a terminal sends; and a scroll bar that goes back
// over what has left the top. It runs no shell and has no TTY. When it
// opens it asks the system for a terminal session (os::open_session), which
// is a process of its own, `sh`, child of this one: the TTY and the command
// shell live there (core/sh/terminal_session.hpp). The window forwards what
// is typed to the session and shows what the session writes. Made larger or
// maximised, the grid takes as many columns and rows as fit and the session
// is told.
//
// Closing the window hangs the session up, as closing xterm does (the
// owner's choice): a command that is running is stopped, the shell ends and
// the `sh` process goes. The other way round, when the shell ends (exit,
// Ctrl+D, `kill`), the window closes.
//
// Where the machine has tasks the session writes from a task of its own, so
// two tasks use the grid: both hold the lock the session lends for it, and
// the session's writing tells the UI task with a message that there is
// something new to paint. Where it has none (the browser, the PC desktop
// program), the session runs inside typed(), a line at a time.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include "core/gfx/art.hpp"
#include "core/msg/queue.hpp"
#include "core/os/os.hpp"
#include "core/sh/terminal_session.hpp"
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

// Holds a lock, if there is one, until the end of the block.
class Hold {
public:
    explicit Hold(ports::Lock *lock) : lock_(lock)
    {
        if (lock_ != nullptr) {
            lock_->lock();
        }
    }
    ~Hold()
    {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }
    Hold(const Hold &) = delete;
    Hold &operator=(const Hold &) = delete;

private:
    ports::Lock *lock_;
};

// The grid of characters, and the keyboard.
class TerminalView : public wm::Widget {
public:
    static constexpr int kWidth = Screen::kColumns * kCellW + 2 * kPad;
    static constexpr int kHeight = Screen::kRows * kCellH + 2 * kPad;

    TerminalView(const gfx::Rect &r, TerminalWindow &owner, Screen &screen)
        : Widget(r), owner_(owner), screen_(screen)
    {
    }

    // The lock the session lends for the grid, once there is a session.
    void set_lock(ports::Lock *lock) { lock_ = lock; }

    // The UI task's own note of where the cursor is, taken under the lock
    // (TerminalWindow::show_changes): the window manager asks for the caret
    // at times of its own.
    void set_cursor(int row, int column)
    {
        cursor_row_ = row;
        cursor_column_ = column;
    }

    // Rows of the grid, in the widget. The size of the grid changes only on
    // the UI task (TerminalWindow::layout), so it is read without the lock.
    gfx::Rect rows_rect(int first, int last) const
    {
        return {kPad, kPad + first * kCellH, screen_.columns() * kCellW, (last - first + 1) * kCellH};
    }

    bool focusable() const override { return true; }
    bool layered() const override { return false; } // every pixel is written once below
    gfx::Rect caret_rect() const override
    {
        if (cursor_row_ < 0) {
            return {0, 0, 0, 0};
        }
        return {kPad + cursor_column_ * kCellW, kPad + cursor_row_ * kCellH, kCellW, kCellH};
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect whole = {0, 0, rect.w, rect.h};
        c.fill_around(whole, rows_rect(0, screen_.rows() - 1), kPaper);
        const gfx::Rect limit = c.limit().offset(-c.origin_x(), -c.origin_y());
        // The session's task may be writing: the grid is read under the
        // session's lock, and the cursor is where the grid says it is now.
        const Hold hold(lock_);
        const int cursor_row = screen_.cursor_row();
        const gfx::Rect caret = cursor_row < 0
                                    ? gfx::Rect{0, 0, 0, 0}
                                    : gfx::Rect{kPad + screen_.cursor_column() * kCellW, kPad + cursor_row * kCellH,
                                                kCellW, kCellH};
        const int columns = screen_.columns();
        for (int row = 0; row < screen_.rows(); row++) {
            const gfx::Rect r = rows_rect(row, row);
            if (!r.overlaps(limit)) {
                continue;
            }
            const uint8_t *cells = screen_.row(row);
            if (caret.w == 0 || caret.y != r.y) {
                run(c, cells, 0, columns, r.y, kInk, kPaper);
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
            run(c, cells, at + 1, columns, r.y, kInk, kPaper);
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
        char text[2 * Screen::kMaxColumns + 1];
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
    ports::Lock *lock_ = nullptr;
    int cursor_row_ = 0, cursor_column_ = 0;
};

// The device the session writes to. What it shows goes into the grid, and,
// when the session has a task of its own, the UI task is told that there is
// something new to paint: one message until it has looked, however much is
// written meanwhile. Called with the session's lock held, so the message is
// posted without waiting; if the queue is full the next write tries again,
// and the window looks once a second in any case.
class Shown : public ports::ByteSink {
public:
    explicit Shown(Screen &screen) : screen_(screen) {}
    void tell(proc::Pid pid, msg::Handler changed, void *window)
    {
        pid_ = pid;
        changed_ = changed;
        window_ = window;
    }
    void write(const uint8_t *data, size_t size) override
    {
        screen_.write(data, size);
        if (changed_ != nullptr && !told_.exchange(true)) {
            if (!msg::post(pid_, changed_, window_)) {
                told_ = false;
            }
        }
    }
    void looked() { told_ = false; }

private:
    Screen &screen_;
    proc::Pid pid_ = proc::kNone;
    msg::Handler changed_ = nullptr;
    void *window_ = nullptr;
    std::atomic<bool> told_{false};
};

constexpr int kMaxTerminals = 3;
TerminalWindow *g_terminals[kMaxTerminals] = {};

class TerminalWindow : public wm::Window {
public:
    explicit TerminalWindow(int place)
        : Window("Terminal",
                 {40 + place * 22, 30 + place * 22, TerminalView::kWidth + wm::ScrollBar::kThickness + 2 * kBorder,
                  TerminalView::kHeight + kClientTop + kBorder},
                 &gfx::terminal_icon_16()),
          shown_(screen_), view_({0, 0, TerminalView::kWidth, TerminalView::kHeight}, *this, screen_),
          bar_({TerminalView::kWidth, 0, wm::ScrollBar::kThickness, TerminalView::kHeight}, true,
               &TerminalWindow::bar_moved, this)
    {
        add(view_);
        add(bar_);
        set_focus(&view_);
        show_history();
    }

    ~TerminalWindow()
    {
        leave(); // one that was never opened has no closed()
        for (TerminalWindow *&t : g_terminals) {
            if (t == this) {
                t = nullptr;
            }
        }
    }

    // Asks the system for the session this window is the terminal of.
    // nullptr: it runs. Otherwise why it does not, for a message box.
    const char *begin()
    {
        const char *why = nullptr;
        session_ = os::open_session(shown_, why);
        if (session_ == nullptr) {
            return why;
        }
        lock_ = session_->lock();
        view_.set_lock(lock_);
        if (lock_ != nullptr) {
            // The session has a task of its own, and may be writing already.
            const Hold hold(lock_);
            shown_.tell(os::self(), &TerminalWindow::grid_changed, this);
        }
        session_->on_ended(&TerminalWindow::session_ended, this);
        session_->set_size(screen_.columns(), screen_.rows());
        show_changes();
        return nullptr;
    }

    // Keys from the window, as the bytes a terminal sends for them: straight
    // to the session. Its echo is on the grid when this returns; what the
    // shell answers comes with a message, or, without tasks, is there too.
    void typed(const char *bytes, size_t size)
    {
        if (session_ == nullptr) {
            return;
        }
        session_->typed(reinterpret_cast<const uint8_t *>(bytes), size); // `exit` closes this window in here
        show_changes();
    }

    // A program has taken the keys for itself (ted, top): Page Up and
    // Page Down are its own then, not the history's.
    bool program_has_keys() const { return session_ != nullptr && session_->raw(); }

    void look_back(int lines)
    {
        {
            const Hold hold(lock_);
            screen_.scroll_view(lines);
        }
        show_changes();
    }

    const wm::ScrollBar &scroll_bar() const { return bar_; }

    // However the window went (its close button, End Task): the session is
    // hung up, and from here on is no business of this window. The window
    // and its process go at once; the `sh` process goes when its shell has
    // stopped, which is a moment.
    void closed() override { leave(); }

    // Made larger or smaller, or maximised: the grid takes as many columns
    // and rows as fit, and the session is told, so what runs next (ls, more,
    // ted, top) uses the new size. A program already on the screen keeps
    // the size it started with.
    void resized() override
    {
        const gfx::Rect c = client();
        const int bar = wm::ScrollBar::kThickness;
        view_.rect = {0, 0, c.w - bar, c.h};
        bar_.rect = {c.w - bar, 0, bar, c.h};
        int columns, rows;
        {
            const Hold hold(lock_);
            screen_.set_size((view_.rect.w - 2 * kPad) / kCellW, (view_.rect.h - 2 * kPad) / kCellH);
            columns = screen_.columns();
            rows = screen_.rows();
        }
        if (session_ != nullptr) {
            session_->set_size(columns, rows);
        }
        view_.invalidate();
        show_changes();
    }

    const Screen &screen() const { return screen_; }

private:
    void leave()
    {
        if (safety_ != 0) {
            os::stop_timer(safety_);
            safety_ = 0;
        }
        if (sh::TerminalSession *const session = session_) {
            session_ = nullptr;
            session->on_ended(nullptr, nullptr);
            session->hang_up(); // nothing is written to the grid after this
        }
    }

    // The session ended by itself (exit, Ctrl+D, a signal): it is gone when
    // this returns.
    static void session_ended(void *context)
    {
        auto *w = static_cast<TerminalWindow *>(context);
        w->session_ = nullptr;
        w->close();
    }
    // The UI task: the session's task has written to the grid.
    static void grid_changed(const msg::Message &m)
    {
        auto *w = static_cast<TerminalWindow *>(m.context);
        w->shown_.looked();
        w->show_changes();
    }
    static void look(void *context) { static_cast<TerminalWindow *>(context)->show_changes(); }

    // The scroll bar: its top is the oldest line kept, its bottom the
    // screen itself.
    static void bar_moved(wm::ScrollBar &bar, void *context)
    {
        auto *w = static_cast<TerminalWindow *>(context);
        int back;
        {
            const Hold hold(w->lock_);
            back = w->screen_.history() - bar.value() - w->screen_.scrolled();
        }
        w->look_back(back);
        w->set_focus(&w->view_); // the keys stay with the grid
    }
    void show_history()
    {
        if (history_ != bar_.maximum() || rows_ != shown_rows_) {
            bar_.set_range(history_, rows_);
            shown_rows_ = rows_;
        }
        bar_.set_value_quietly(history_ - scrolled_);
    }

    void show_changes()
    {
        int first = 0, last = 0;
        bool changed;
        {
            const Hold hold(lock_);
            changed = screen_.take_changes(first, last);
            screen_.take_bells(); // no sound yet
            view_.set_cursor(screen_.cursor_row(), screen_.cursor_column());
            history_ = screen_.history();
            scrolled_ = screen_.scrolled();
            rows_ = screen_.rows();
        }
        show_history();
        if (!open()) {
            return; // not on the screen yet, or closed
        }
        if (lock_ != nullptr && session_ != nullptr && safety_ == 0) {
            safety_ = os::start_timer(1000000, 1000000, &TerminalWindow::look, this);
        }
        if (changed) {
            view_.invalidate_part(view_.rows_rect(first, last));
        }
        os::caret_moved(); // the cursor is shown at once where it now is
    }

    sh::TerminalSession *session_ = nullptr; // the system's; nullptr once either side has gone
    ports::Lock *lock_ = nullptr;            // the session's, for the grid; nullptr: one task only
    int safety_ = 0;
    Screen screen_;
    Shown shown_;
    TerminalView view_;
    wm::ScrollBar bar_;
    int history_ = 0, scrolled_ = 0, rows_ = Screen::kRows, shown_rows_ = 0; // as last seen under the lock
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
    case wm::key::PageUp:
        if (owner_.program_has_keys()) {
            return send("\x1b[5~");
        }
        owner_.look_back(screen_.rows() - 1);
        return true;
    case wm::key::PageDown:
        if (owner_.program_has_keys()) {
            return send("\x1b[6~");
        }
        owner_.look_back(-(screen_.rows() - 1));
        return true;
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
    const proc::Pid pid = os::spawn("Terminal");
    if (pid == proc::kNone) {
        message_box("Terminal", "The Terminal could not start:\ntoo many programs are running.");
        return;
    }
    const os::As as(pid);
    TerminalWindow *w = make_window<TerminalWindow>(place);
    if (w == nullptr) {
        no_memory_for("Terminal");
        return;
    }
    if (const char *why = w->begin()) {
        discard(w);
        message_box("Terminal", why);
        return;
    }
    g_terminals[place] = w;
    os::show(*w);
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
