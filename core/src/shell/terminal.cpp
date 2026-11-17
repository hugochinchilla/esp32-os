// The Terminal: the command shell in a window (backlog item 15;
// docs/journal/2026-11-17-terminal-window.md). The window is the device of
// a TTY: keys go in as the bytes a terminal sends, and what the TTY shows is
// written into a grid of 80 by 25 characters (tty::Screen) that the window
// paints. The session (sh::Session) is the same one the serial console and
// telnet will run.
//
// Where the machine has tasks, the session is a job of the Terminal's
// process, on a task of its own (docs/journal/2026-11-17-terminal-task.md):
// it sleeps until a line is typed, the desktop goes on while a command
// works, and Ctrl+C stops the command. Two tasks then use the grid, the
// session writing and the UI task painting, so both hold the TTY's lock for
// it; the session's task tells the UI task that the grid has changed with a
// message. The lock and the two events a session sleeps on come from a small
// pool the machine hands in (shell::set_session_kits), since the portable
// code cannot make them.
//
// Where it has none (the browser, the PC desktop program), the session runs
// on the UI task, a line at a time: when Enter completes a line it is run
// there and then, and the desktop waits for it.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include "core/gfx/art.hpp"
#include "core/job/job.hpp"
#include "core/msg/queue.hpp"
#include "core/net/wifi.hpp"
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

SessionKit *g_kits = nullptr;
int g_kit_count = 0;

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

    TerminalView(const gfx::Rect &r, TerminalWindow &owner, Screen &screen, ports::Lock *lock)
        : Widget(r), owner_(owner), screen_(screen), lock_(lock)
    {
    }

    // The UI task's own note of where the cursor is, taken under the lock
    // (TerminalWindow::show_changes): the window manager asks for the caret
    // at times of its own.
    void set_cursor(int row, int column)
    {
        cursor_row_ = row;
        cursor_column_ = column;
    }

    static gfx::Rect rows_rect(int first, int last)
    {
        return {kPad, kPad + first * kCellH, Screen::kColumns * kCellW, (last - first + 1) * kCellH};
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
        c.fill_around(whole, rows_rect(0, Screen::kRows - 1), kPaper);
        const gfx::Rect limit = c.limit().offset(-c.origin_x(), -c.origin_y());
        // The session's task may be writing: the grid is read under the
        // TTY's lock, and the cursor is where the grid says it is now.
        const Hold hold(lock_);
        const int cursor_row = screen_.cursor_row();
        const gfx::Rect caret = cursor_row < 0
                                    ? gfx::Rect{0, 0, 0, 0}
                                    : gfx::Rect{kPad + screen_.cursor_column() * kCellW, kPad + cursor_row * kCellH,
                                                kCellW, kCellH};
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
    ports::Lock *lock_;
    int cursor_row_ = 0, cursor_column_ = 0;
};

// What the TTY shows goes into the grid, and, when the session has a task of
// its own, the UI task is told that there is something new to paint: one
// message until it has looked, however much is written meanwhile. Called
// with the TTY's lock held, so the message is posted without waiting; if the
// queue is full the next write tries again, and the window looks once a
// second in any case.
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

// Large memory for a command of the session (ted's text): program memory,
// counted as the Terminal's own. Called on the UI task (Session::on_ui),
// where no process is current, so it says whose it is.
class SessionMemory : public ports::Memory {
public:
    void *allocate(size_t bytes) override
    {
        const proc::Scope as(&proc::table(), owner);
        return program_memory(bytes);
    }
    void release(void *block) override { free_program_memory(block); }
    proc::Pid owner = proc::kNone;
};

class TerminalWindow : public wm::Window {
public:
    // `kit`: the lock and events for a session on a task of its own;
    // nullptr for a session on the UI task.
    TerminalWindow(int place, SessionKit *kit)
        : Window("Terminal",
                 {40 + place * 22, 30 + place * 22, TerminalView::kWidth + 2 * kBorder,
                  TerminalView::kHeight + kClientTop + kBorder},
                 &gfx::terminal_icon_16()),
          kit_(kit), shown_(screen_),
          view_({0, 0, TerminalView::kWidth, TerminalView::kHeight}, *this, screen_, lock()),
          tty_(shown_, lock(), kit != nullptr ? kit->typed : nullptr), session_(tty_, environment())
    {
        set_resizable(false);
        add(view_);
        set_focus(&view_);
        tty_.set_size(Screen::kColumns, Screen::kRows);
        memory_.owner = proc::table().current(); // the Terminal being opened
    }

    ~TerminalWindow()
    {
        if (kit_ != nullptr) {
            kit_->in_use = false;
        }
        for (TerminalWindow *&t : g_terminals) {
            if (t == this) {
                t = nullptr;
            }
        }
    }

    // Starts the session. False if its task could not be started.
    bool begin()
    {
        if (kit_ == nullptr) {
            session_.greet();
            pump();
            return true;
        }
        shown_.tell(proc::table().current(), &TerminalWindow::grid_changed, this);
        job::Spec spec;
        spec.name = "terminal";
        spec.work = &TerminalWindow::run_session;
        spec.done = &TerminalWindow::session_over;
        spec.stop = &TerminalWindow::leave_now;
        spec.context = this;
        spec.stack_bytes = kSessionStack;
        running_ = job::jobs().start(spec);
        return running_;
    }

    // Keys from the window, as the bytes a terminal sends for them.
    void typed(const char *bytes, size_t size)
    {
        tty_.input(reinterpret_cast<const uint8_t *>(bytes), size);
        if (kit_ != nullptr) {
            show_changes(); // the echo; what the session answers comes with a message
            return;
        }
        pump();
        if (session_.ended()) {
            close(); // exit, or Ctrl+D
        }
    }

    // A program has taken the keys for itself (ted, top): Page Up and
    // Page Down are its own then, not the history's.
    bool program_has_keys() const { return tty_.raw(); }

    void look_back(int lines)
    {
        {
            const Hold hold(lock());
            screen_.scroll_view(lines);
        }
        show_changes();
    }

    // However the window went (its close button, End Task): the session is
    // told that its terminal is gone. A command that is running is stopped
    // as Ctrl+C would stop it. The window is off the screen at once; the
    // object and the process stay until the session's task has returned
    // (job::Jobs), which is a moment.
    void closed() override { leave(); }

    const Screen &screen() const { return screen_; }

private:
    // ponytail: the stack of a session's task. The commands keep their large
    // buffers in the session, not on the stack; the deepest is a copy or a
    // removal of nested folders. To be measured on the board.
    static constexpr uint32_t kSessionStack = 12288;

    ports::Lock *lock() const { return kit_ != nullptr ? kit_->lock : nullptr; }

    sh::Env environment()
    {
        sh::Env env;
        env.storage = &storage();
        env.procs = &proc::table();
        env.system = &system_port();
        env.desktop = &desktop_for_sessions(); // open, run, edit
        env.zone = network() != nullptr ? &network()->zone() : nullptr;
        env.memory = &memory_; // whose it is, is set in the constructor
        // On the UI task the session calls straight in. From a task of its
        // own it asks the UI task, through the queue, for what belongs to it
        // (the process table).
        if (kit_ != nullptr) {
            env.ui = new (call_) sh::QueueCall(*msg::queue(), *kit_->answered);
        }
        return env;
    }

    void leave()
    {
        if (kit_ != nullptr && running_) {
            tty_.input("\x03");
            tty_.hang_up();
        }
    }

    // The session's task.
    static uint32_t run_session(const job::Job &, void *context)
    {
        static_cast<TerminalWindow *>(context)->session_.run();
        return 0;
    }
    // The UI task: the session has ended (exit, Ctrl+D, or its terminal went).
    static void session_over(void *context, uint32_t, bool)
    {
        auto *w = static_cast<TerminalWindow *>(context);
        w->running_ = false;
        w->close();
    }
    static void leave_now(void *context) { static_cast<TerminalWindow *>(context)->leave(); }
    // The UI task: the session's task has written to the grid.
    static void grid_changed(const msg::Message &m)
    {
        auto *w = static_cast<TerminalWindow *>(m.context);
        w->shown_.looked();
        w->show_changes();
    }
    static void look(void *context) { static_cast<TerminalWindow *>(context)->show_changes(); }

    // Runs what has been typed: every finished line, then the prompt for
    // the next one. Only for a session on the UI task.
    void pump()
    {
        for (int guard = 0; guard < 64 && !session_.ended() && (tty_.readable() || !session_.waiting()); guard++) {
            session_.step();
        }
        show_changes();
    }

    void show_changes()
    {
        int first = 0, last = 0;
        bool changed;
        {
            const Hold hold(lock());
            changed = screen_.take_changes(first, last);
            screen_.take_bells(); // no sound yet
            view_.set_cursor(screen_.cursor_row(), screen_.cursor_column());
        }
        wm::Wm *m = manager();
        if (m == nullptr) {
            return; // closed, and waiting for its session
        }
        if (kit_ != nullptr && safety_ == 0) {
            safety_ = m->start_timer(1000000, 1000000, &TerminalWindow::look, this);
        }
        if (changed) {
            view_.invalidate_part(TerminalView::rows_rect(first, last));
        }
        m->caret_moved(); // the cursor is shown at once where it now is
    }

    SessionKit *kit_;
    bool running_ = false; // the session's task has been started and has not returned
    int safety_ = 0;
    alignas(sh::QueueCall) unsigned char call_[sizeof(sh::QueueCall)];
    SessionMemory memory_;
    Screen screen_;
    Shown shown_;
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
    case wm::key::PageUp:
        if (owner_.program_has_keys()) {
            return send("\x1b[5~");
        }
        owner_.look_back(Screen::kRows - 1);
        return true;
    case wm::key::PageDown:
        if (owner_.program_has_keys()) {
            return send("\x1b[6~");
        }
        owner_.look_back(-(Screen::kRows - 1));
        return true;
    default: return false; // a character: it comes back through text()
    }
}

void TerminalView::text(const char *utf8) { owner_.typed(utf8, strlen(utf8)); }

} // namespace

void set_session_kits(SessionKit *kits, int count)
{
    g_kits = kits;
    g_kit_count = count;
}

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
    // A session on a task of its own where the machine can give one.
    SessionKit *kit = nullptr;
    if (g_kit_count > 0 && job::jobs().threaded()) {
        for (int i = 0; i < g_kit_count && kit == nullptr; i++) {
            kit = !g_kits[i].in_use ? &g_kits[i] : nullptr;
        }
        if (kit == nullptr) {
            // Every kit is held, some by Terminals that have closed and are
            // waiting for a command to stop.
            message_box("Terminal", "A Terminal is still closing.\nTry again in a moment.");
            return;
        }
    }
    proc::Table &t = proc::table();
    const proc::Pid pid = t.spawn("Terminal", t.current(), proc::kWindowed);
    // With no room in the table it is the caller's window, as elsewhere.
    const proc::Scope as(&t, pid != proc::kNone ? pid : t.current());
    if (kit != nullptr) {
        kit->in_use = true; // given back by the window's destructor
    }
    TerminalWindow *w = make_window<TerminalWindow>(place, kit);
    if (w == nullptr) {
        if (kit != nullptr) {
            kit->in_use = false;
        }
        no_memory_for("Terminal");
        return;
    }
    if (!w->begin()) {
        discard(w);
        message_box("Terminal", "The Terminal could not start:\nthe system is too busy.");
        return;
    }
    g_terminals[place] = w;
    show(*w);
    discard(w); // not opened: the window manager was full
}

sh::Env session_environment(sh::UiCall *ui, proc::Pid owner)
{
    // One for each caller (telnet, the serial console): each charges its
    // own process.
    static SessionMemory memories[4];
    static int used = 0;
    SessionMemory &memory = memories[used < 3 ? used++ : 3];
    memory.owner = owner;
    sh::Env env;
    env.storage = &storage();
    env.procs = &proc::table();
    env.system = &system_port();
    env.ui = ui;
    env.desktop = &desktop_for_sessions();
    env.zone = network() != nullptr ? &network()->zone() : nullptr;
    env.memory = &memory;
    return env;
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
