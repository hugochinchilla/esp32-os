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

#include "core/app/app.hpp"
#include "core/msg/queue.hpp"
#include "core/os/os.hpp"
#include "core/sh/terminal_session.hpp"
#include "core/shell/shell.hpp"
#include "core/tty/screen.hpp"
#include "core/tty/tty.hpp"
#include "art.hpp"

namespace shell {

namespace {

using tty::Screen;

constexpr int kCellW = 6, kCellH = 12; // Spleen 6x12
constexpr int kPad = 2;                // dark edge around the characters
constexpr gfx::Colour kPaper = gfx::kBlack;
constexpr gfx::Colour kInk = gfx::rgb(5, 5, 2); // light grey, as on a DOS screen

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

class TerminalWindow : public app::Window {
public:
    static constexpr int kBar = os::ui::kScrollBarThickness;
    // The grid of characters at its first size.
    static constexpr int kWidth = Screen::kColumns * kCellW + 2 * kPad;
    static constexpr int kHeight = Screen::kRows * kCellH + 2 * kPad;

    // The grid is a picture that takes the keys, every pixel of it written
    // once when it is painted; beside it, the scroll bar.
    TerminalWindow()
        : shown_(screen_), view_(picture({0, 0, kWidth, kHeight}, app::kOnce | app::kKeys)),
          bar_(scroll_bar({kWidth, 0, kBar, kHeight}, true))
    {
        set_focus(view_);
        show_history();
    }

    ~TerminalWindow() override { leave(); } // one that was never opened has no closed()

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

    // However the window went (its close button, End Task): the session is
    // hung up, and from here on is no business of this window. The window
    // and its process go at once; the `sh` process goes when its shell has
    // stopped, which is a moment.
    void closed() override { leave(); }

    // Made larger or smaller, or maximised: the grid takes as many columns
    // and rows as fit, and the session is told, so what runs next (ls, more,
    // ted, top) uses the new size. A program already on the screen keeps
    // the size it started with.
    void resized(int w, int h) override
    {
        view_w_ = w - kBar;
        view_h_ = h;
        view_.place({0, 0, view_w_, view_h_});
        bar_.place({view_w_, 0, kBar, h});
        int columns, rows;
        {
            const Hold hold(lock_);
            screen_.set_size((view_w_ - 2 * kPad) / kCellW, (view_h_ - 2 * kPad) / kCellH);
            columns = screen_.columns();
            rows = screen_.rows();
        }
        if (session_ != nullptr) {
            session_->set_size(columns, rows);
        }
        view_.redraw();
        show_changes();
    }

    const Screen &screen() const { return screen_; }

    // The scroll bar: its top is the oldest line kept, its bottom the
    // screen itself.
    void changed(app::Control) override
    {
        int back;
        {
            const Hold hold(lock_);
            back = screen_.history() - bar_.value() - screen_.scrolled();
        }
        look_back(back);
        set_focus(view_); // the keys stay with the grid
    }

    void paint_in(app::Control, app::Canvas &c) override
    {
        const gfx::Rect whole = {0, 0, view_w_, view_h_};
        c.fill_around(whole, rows_rect(0, screen_.rows() - 1), kPaper);
        const gfx::Rect limit = c.limit();
        const bool keys = view_.has_keys();
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
            if (keys && caret_lit()) {
                run(c, cells, at, at + 1, r.y, kPaper, kInk); // a block, the character cut out of it
            } else if (keys) {
                run(c, cells, at, at + 1, r.y, kInk, kPaper); // the blink's dark half
            } else {
                // Another window has the keyboard: a line under the cell.
                c.sub({caret.x, caret.y, kCellW, kCellH - 1}).text_opaque(0, 0, one(cells[at]), kInk, kPaper);
                c.hline(caret.x, caret.y + kCellH - 1, kCellW, kInk);
            }
            run(c, cells, at + 1, columns, r.y, kInk, kPaper);
        }
    }

    // The keyboard, as the bytes a terminal sends.
    bool key_in(app::Control, const wm::KeyMsg &k) override
    {
        if (!k.pressed || (k.mods & wm::kAlt) != 0) {
            return false;
        }
        const auto send = [this](const char *bytes) {
            typed(bytes, strlen(bytes));
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
            if (program_has_keys()) {
                return send("\x1b[5~");
            }
            look_back(screen_.rows() - 1);
            return true;
        case wm::key::PageDown:
            if (program_has_keys()) {
                return send("\x1b[6~");
            }
            look_back(-(screen_.rows() - 1));
            return true;
        default: return false; // a character: it comes back through text_in()
        }
    }
    void text_in(app::Control, const char *utf8) override { typed(utf8, strlen(utf8)); }

    // Which of the three places on the screen it opened at.
    int place() const { return place_; }
    void take_place(int place) { place_ = place; }

private:
    // Rows of the grid, in the picture. The size of the grid changes only
    // on the UI task (resized), so it is read without the lock.
    gfx::Rect rows_rect(int first, int last) const
    {
        return {kPad, kPad + first * kCellH, screen_.columns() * kCellW, (last - first + 1) * kCellH};
    }

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
    static void run(app::Canvas &c, const uint8_t *cells, int from, int to, int y, gfx::Colour ink, gfx::Colour paper)
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

    void show_history()
    {
        if (history_ != shown_history_ || rows_ != shown_rows_) {
            bar_.set_range(history_, rows_);
            shown_history_ = history_;
            shown_rows_ = rows_;
        }
        bar_.set_value(history_ - scrolled_);
    }

    void show_changes()
    {
        int first = 0, last = 0;
        bool changed;
        int bells;
        {
            const Hold hold(lock_);
            changed = screen_.take_changes(first, last);
            bells = screen_.take_bells();
            // The window manager asks for the caret at times of its own:
            // where the cursor is, as last seen under the lock.
            view_.set_caret(screen_.cursor_row() < 0
                                ? gfx::Rect{0, 0, 0, 0}
                                : gfx::Rect{kPad + screen_.cursor_column() * kCellW,
                                            kPad + screen_.cursor_row() * kCellH, kCellW, kCellH});
            history_ = screen_.history();
            scrolled_ = screen_.scrolled();
            rows_ = screen_.rows();
        }
        if (bells > 0) {
            os::play_sound(sound::Id::Bell);
        }
        show_history();
        if (!on_screen()) {
            return; // not on the screen yet, or closed
        }
        if (lock_ != nullptr && session_ != nullptr && safety_ == 0) {
            safety_ = os::start_timer(1000000, 1000000, &TerminalWindow::look, this);
        }
        if (changed) {
            view_.redraw(rows_rect(first, last));
        }
        os::caret_moved(); // the cursor is shown at once where it now is
    }

    sh::TerminalSession *session_ = nullptr; // the system's; nullptr once either side has gone
    ports::Lock *lock_ = nullptr;            // the session's, for the grid; nullptr: one task only
    int safety_ = 0;
    int place_ = 0;
    Screen screen_;
    Shown shown_;
    app::Picture view_;
    app::ScrollBar bar_;
    int view_w_ = kWidth, view_h_ = kHeight;
    int history_ = 0, scrolled_ = 0, rows_ = Screen::kRows; // as last seen under the lock
    int shown_history_ = -1, shown_rows_ = 0;               // what the scroll bar was last told
};

} // namespace

#ifdef APP_LINKED
// Tests: the grid of the Terminal the keyboard goes to, else of any that
// is open (programs/test_hooks.hpp).
const tty::Screen *terminal_screen()
{
    const TerminalWindow *found = nullptr;
    for (int i = 0; i < os::process_count(); i++) {
        const proc::Process &p = os::process_at(i);
        if (strcmp(p.name, "Terminal") != 0) {
            continue;
        }
        const TerminalWindow *t = app::program<TerminalWindow>(os::main_window(p.pid));
        if (t != nullptr && t->on_screen() && (found == nullptr || t->has_keys())) {
            found = t;
        }
    }
    return found != nullptr ? &found->screen() : nullptr;
}
#endif

} // namespace shell

// No task of its own (stack 0): the window is a device, it reads no files,
// and its session already has a task. It ran on the UI task before it was
// a file; the 8 KB it asked for at first were 24 KB of internal memory
// with three Terminals open (the board, 2026-11-20).
APP_INFO(app::many(app::file_info("Terminal", 0, art::kSmall, art::kSmallInks)));

namespace {
// The window's icon: the header's own pixels are not in memory with the
// program, so they are made again here from the same rows.
constexpr app::Icon16 kWindowIcon = app::icon16(art::kSmall, art::kSmallInks);
constexpr int kMaxTerminals = 3; // the machine has that many session tasks
} // namespace

// Every start is another Terminal, a process of its own, up to three.
APP_MAIN(int, char **)
{
    static const gfx::Bitmap icon = {16, 16, kWindowIcon.pixels, app::kClearInk};
    // The places the others took, by where their windows are.
    bool taken[kMaxTerminals] = {};
    int others = 0;
    for (int i = 0; i < os::process_count(); i++) {
        const proc::Process &p = os::process_at(i);
        const os::ui::Window window = os::main_window(p.pid);
        if (p.pid == os::self() || strcmp(p.name, "Terminal") != 0 || window == os::kNoWindow) {
            continue;
        }
        others++;
        if (const auto *t = app::program<shell::TerminalWindow>(window)) {
            taken[t->place()] = true;
        }
    }
    if (others >= kMaxTerminals) {
        shell::message_box("Terminal", "Three Terminals are open already.\nClose one of them first.");
        return 0; // said: the system has nothing to add
    }
    int place = 0;
    while (place < kMaxTerminals - 1 && taken[place]) {
        place++;
    }
    app::Look look;
    look.title = "Terminal";
    look.x = 40 + place * 22;
    look.y = 30 + place * 22;
    look.width = shell::TerminalWindow::kWidth + shell::TerminalWindow::kBar;
    look.height = shell::TerminalWindow::kHeight;
    look.icon = &icon;
    look.flags = app::kClientSize;
    // Not shown until it has its session.
    const app::Number number = app::make<shell::TerminalWindow>(look);
    if (number == app::kNone) {
        shell::message_box("Terminal", "There is not enough memory to open the Terminal.");
        return 0;
    }
    shell::TerminalWindow *terminal = app::program<shell::TerminalWindow>(number);
    terminal->take_place(place);
    if (const char *why = terminal->begin()) {
        os::ui::close(number);
        shell::message_box("Terminal", why);
        return 0;
    }
    app::show(number);
    return 0;
}
