// The Terminal: the command shell in a window, driven with the keyboard as
// a person would. What is checked is the grid of characters the window
// shows, the files and processes behind it, and that the picture on the
// screen is what a full repaint would give.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <chrono>
#include <thread>

#include "core/fs/memory_volume.hpp"
#include "core/job/job.hpp"
#include "core/msg/queue.hpp"
#include "core/proc/table.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "../../programs/test_hooks.hpp"
#include "core/tty/screen.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"
#include "../thread_queue.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)
#define CHECK_ROW(row_, expected)                                                                          \
    do {                                                                                                   \
        const std::string got_ = row(row_), expected_ = (expected);                                        \
        if (got_ != expected_) {                                                                           \
            printf("FAILED %s:%d: row %d is \"%s\", not \"%s\"\n", __FILE__, __LINE__, row_, got_.c_str(), \
                   expected_.c_str());                                                                     \
            failures++;                                                                                    \
        }                                                                                                  \
    } while (0)

namespace {

host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

void key(uint16_t usage, uint16_t held = 0)
{
    if (held != 0) {
        desktop.key(held, true);
    }
    desktop.key(usage, true);
    desktop.key(usage, false);
    if (held != 0) {
        desktop.key(held, false);
    }
}

void type(const char *text) { desktop.text(text); }

void enter(const char *line)
{
    type(line);
    key(wm::key::Enter);
}

void wait(uint32_t us)
{
    for (uint32_t done = 0; done < us; done += 10000) {
        clock_.now += 10000;
        desktop.tick();
    }
}

const char *title() { return desktop.active() ? desktop.active()->title() : ""; }

// A row of the Terminal in front, without the spaces at its end.
std::string row(int index)
{
    const tty::Screen *s = shell::terminal_screen();
    if (s == nullptr) {
        return "<no terminal>";
    }
    std::string text(reinterpret_cast<const char *>(s->row(index)), tty::Screen::kColumns);
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

// The row the cursor is on.
int at() { return shell::terminal_screen()->cursor_row(); }
int column() { return shell::terminal_screen()->cursor_column(); }

bool no_stale_pixels()
{
    std::string before(reinterpret_cast<char *>(display.pixels()), 640 * 480);
    desktop.repaint_all();
    int x0 = 640, x1 = -1, y0 = 480, y1 = -1;
    for (int i = 0; i < 640 * 480; i++) {
        if (before[i] != static_cast<char>(display.pixels()[i])) {
            x0 = std::min(x0, i % 640), x1 = std::max(x1, i % 640);
            y0 = std::min(y0, i / 640), y1 = std::max(y1, i / 640);
        }
    }
    if (x1 >= 0) {
        printf("stale pixels within x %d..%d, y %d..%d\n", x0, x1, y0, y1);
    }
    return memcmp(before.data(), display.pixels(), before.size()) == 0;
}

int count(const char *program)
{
    int n = 0;
    for (int i = 0; i < proc::table().count(); i++) {
        n += strcmp(proc::table().at(i).name, program) == 0;
    }
    return n;
}

void test_terminal()
{
    const shell::Program *program = shell::find_program("Terminal");
    CHECK(program != nullptr && program->icon != nullptr && !program->built_in);
    CHECK(shell::terminal_screen() == nullptr);
    const int blocks = memory.in_use();

    // It opens with the greeting and a prompt, as a process of its own.
    shell::start_program("Terminal");
    desktop.update();
    CHECK(strcmp(title(), "Terminal") == 0 && count("Terminal") == 1);
    // Two processes and five blocks: the window with its grid (the emulator),
    // the grid's picture and the scroll bar, the words the program was
    // started with,
    // and the session it is the terminal of, with the TTY and the shell.
    CHECK(memory.in_use() == blocks + 5 && count("sh") == 1);
    {
        const proc::Process *sh = proc::table().find(proc::table().find_by_name("sh"));
        CHECK(sh != nullptr && sh->parent == desktop.active()->owner() && !sh->is(proc::kWindowed));
        CHECK(sh != nullptr && sh->memory > 3000 && desktop.windows_of(sh->pid) == 0);
    }
    CHECK_ROW(0, std::string("Chinchilla ") + shell::kVersion);
    CHECK_ROW(1, "help lists the commands.");
    CHECK_ROW(3, "C:\\>");
    CHECK(at() == 3 && column() == 5);
    CHECK(no_stale_pixels());

    // Typing shows; Enter runs the line and the answer is under it.
    type("echo hi");
    CHECK_ROW(3, "C:\\> echo hi");
    CHECK(column() == 12 && no_stale_pixels());
    key(wm::key::Enter);
    CHECK_ROW(4, "hi");
    CHECK_ROW(5, "C:\\>");
    CHECK(at() == 5 && no_stale_pixels());

    // The keys that correct a line: Backspace, the arrows, Home, End,
    // Delete.
    type("ecoh  there");
    key(wm::key::Home);
    key(wm::key::Right);
    key(wm::key::Right);
    key(wm::key::Delete);
    key(wm::key::Right);
    type("o");
    CHECK_ROW(5, "C:\\> echo  there");
    CHECK(column() == 9);
    key(wm::key::End);
    key(wm::key::Left);
    key(wm::key::Backspace);
    CHECK_ROW(5, "C:\\> echo  thee");
    CHECK(no_stale_pixels());
    key(wm::key::Enter);
    CHECK_ROW(6, "thee");

    // An earlier line with the up arrow.
    key(wm::key::Up);
    key(wm::key::Up);
    CHECK_ROW(7, "C:\\> echo hi");
    key(wm::key::Down);
    CHECK_ROW(7, "C:\\> echo  thee");
    // Ctrl+C gives the line up.
    key(wm::key::A + 2, wm::key::LeftCtrl);
    CHECK_ROW(7, "C:\\> echo  thee^C");
    CHECK_ROW(8, "C:\\>");
    CHECK(no_stale_pixels());

    // Files: the same drive the File manager shows. Letters beyond ASCII.
    enter("cd Documents");
    enter("type notes.txt");
    CHECK_ROW(9, "C:\\Documents> type notes.txt");
    CHECK_ROW(10, "Shopping list for the VGA board");
    enter("mkdir Se\xC3\xB1" "ales");
    fs::DirEntry made;
    CHECK(storage.stat("C:\\Documents\\Se\xC3\xB1" "ales", made) == fs::FsError::Ok && made.folder);
    enter("ls S*");
    CHECK_ROW(at() - 1, "Se\xF1" "ales\\"); // the grid keeps Latin-1
    CHECK(no_stale_pixels());

    // Processes: the Terminal sees itself.
    enter("ps");
    bool listed = false;
    for (int r = 0; r < tty::Screen::kRows; r++) {
        listed = listed || (row(r).find("---W ") != std::string::npos && row(r).find("  Terminal") != std::string::npos);
    }
    CHECK(listed);

    // More than a screen of output: the top has scrolled away, and Page Up
    // brings it back.
    enter("cls");
    CHECK_ROW(0, "C:\\Documents>");
    CHECK(at() == 0);
    enter("help");
    enter("help");
    CHECK(at() == 24 && row(24) == "C:\\Documents>");
    CHECK(shell::terminal_screen()->history() > 10);
    CHECK(no_stale_pixels());
    const std::string bottom = row(23);
    key(wm::key::PageUp);
    CHECK(shell::terminal_screen()->scrolled() == 24 && at() == -1 && row(23) != bottom);
    CHECK(no_stale_pixels());
    key(wm::key::PageUp);
    key(wm::key::PageUp);
    // All the way back is the greeting: cls empties the screen, not the
    // history.
    CHECK_ROW(0, std::string("Chinchilla ") + shell::kVersion);
    CHECK(shell::terminal_screen()->scrolled() == shell::terminal_screen()->history());
    CHECK(no_stale_pixels());
    key(wm::key::PageDown);
    // Typing goes to the line being typed, and the view goes back to it.
    type("x");
    CHECK(shell::terminal_screen()->scrolled() == 0 && row(24) == "C:\\Documents> x");
    key(wm::key::Backspace);
    CHECK(no_stale_pixels());

    // The cursor blinks; each half of the blink is painted right.
    wait(600000);
    CHECK(no_stale_pixels());
    wait(600000);
    CHECK(no_stale_pixels());

    // A second and a third Terminal, each its own session and process; a
    // fourth is refused.
    shell::start_program("Terminal");
    desktop.update();
    CHECK(count("Terminal") == 2 && count("sh") == 2 && memory.in_use() == blocks + 10);
    CHECK_ROW(3, "C:\\>"); // the new one is in front, in its own folder
    CHECK(no_stale_pixels()); // the first one's cursor is a line now
    shell::start_program("Terminal");
    desktop.update();
    CHECK(count("Terminal") == 3);
    shell::start_program("Terminal");
    desktop.update();
    // The fourth is a process only while it says so.
    CHECK(count("Terminal") == 4 && count("sh") == 3 && strcmp(title(), "Terminal") == 0);
    CHECK(desktop.active() != nullptr && desktop.active()->dialog()); // "Three Terminals are open already."
    key(wm::key::Enter);
    wait(20000);
    CHECK(count("Terminal") == 3);
    CHECK(no_stale_pixels());

    // exit closes the window and ends the process; the memory comes back.
    enter("exit");
    wait(20000);
    CHECK(count("Terminal") == 2 && count("sh") == 2 && memory.in_use() == blocks + 10);
    // Ctrl+D on an empty line does the same.
    CHECK(strcmp(title(), "Terminal") == 0);
    key(wm::key::A + 3, wm::key::LeftCtrl);
    wait(20000);
    CHECK(count("Terminal") == 1 && count("sh") == 1 && memory.in_use() == blocks + 5);
    // The first one is still where it was left.
    CHECK(row(24) == "C:\\Documents>");
    // It can end itself by number, like any other process.
    char line[32];
    snprintf(line, sizeof line, "kill %d", proc::table().find_by_name("Terminal"));
    enter(line);
    wait(20000);
    CHECK(count("Terminal") == 0 && count("sh") == 0);
    CHECK(memory.in_use() == blocks && shell::terminal_screen() == nullptr);
    CHECK(no_stale_pixels());

    // The other way round: the session is ended by its own number, from
    // inside or by anyone (TERM or KILL), and its window closes with it.
    shell::start_program("Terminal");
    desktop.update();
    snprintf(line, sizeof line, "kill %d", proc::table().find_by_name("sh"));
    enter(line);
    wait(20000);
    CHECK(count("Terminal") == 0 && count("sh") == 0 && desktop.window_count() == 0);
    CHECK(memory.in_use() == blocks);
    shell::start_program("Terminal");
    desktop.update();
    CHECK(proc::table().signal(proc::table().find_by_name("sh"), proc::Signal::Kill) == proc::Sent::Ok);
    wait(20000);
    CHECK(count("Terminal") == 0 && count("sh") == 0 && memory.in_use() == blocks);

    // The close button's way (Alt+F4), with a half-typed line.
    shell::start_program("Terminal");
    desktop.update();
    type("half a li");
    key(0x3D, wm::key::LeftAlt); // F4
    wait(20000);
    CHECK(count("Terminal") == 0 && count("sh") == 0 && memory.in_use() == blocks);
}

} // namespace

/* ---- Run, and the shell's open, run and edit ---------------------------------------- */

wm::Window *window_titled(const char *text)
{
    for (int z = 0; z < desktop.window_count(); z++) {
        if (strcasestr(desktop.window_at(z)->title(), text) != nullptr) { // the title has the name as typed
            return desktop.window_at(z);
        }
    }
    return nullptr;
}

void close_everything()
{
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    wait(20000);
}

void click(int x, int y)
{
    for (uint8_t buttons : {0, 1, 0}) {
        clock_.now += 300000;
        desktop.mouse({true, static_cast<int16_t>(x), static_cast<int16_t>(y), buttons, 0});
    }
}

// Maximised, the grid has as many columns and rows as fit and the TTY knows;
// the scroll bar goes back over what has left the top.
void test_size_and_scroll_bar()
{
    close_everything();
    shell::start_program("Terminal");
    wm::Window *w = desktop.active();
    CHECK(w != nullptr && w->resizable());
    const tty::Screen *s = shell::terminal_screen();
    CHECK(s != nullptr && s->columns() == 80 && s->rows() == 25);
    desktop.toggle_maximize(*w);
    wait(20000);
    const gfx::Rect c = w->client();
    const int columns = (c.w - wm::ScrollBar::kThickness - 4) / 6, rows = (c.h - 4) / 12;
    CHECK(columns > 80 && rows > 25);
    CHECK(s->columns() == columns && s->rows() == rows);
    // A line as wide as the new grid stays on one row.
    type("echo ");
    for (int i = 0; i < columns - 16; i++) {
        type("w");
    }
    key(wm::key::Enter);
    const std::string wide = [&] {
        std::string text(reinterpret_cast<const char *>(s->row(s->cursor_row() - 1)), columns);
        return text;
    }();
    CHECK(wide.find(' ') == std::string::npos || wide.find_first_not_of('w') >= static_cast<size_t>(columns - 16));
    // Fill more than a screen, then the bar's top arrow goes one line back
    // and its bottom arrow returns.
    for (int i = 0; i < rows + 5; i++) {
        enter("echo more");
    }
    CHECK(s->history() > 0 && s->scrolled() == 0);
    const int bar_x = c.right() - wm::ScrollBar::kThickness / 2;
    click(bar_x, c.y + 6);
    wait(20000);
    CHECK(s->scrolled() == 1);
    click(bar_x, c.bottom() - 6);
    wait(20000);
    CHECK(s->scrolled() == 0);
    // The wheel over the grid goes back an eighth of the screen a notch
    // (three lines at least), and returns.
    desktop.mouse({true, static_cast<int16_t>(c.x + 100), static_cast<int16_t>(c.y + 100), 0, 1});
    wait(20000);
    CHECK(s->scrolled() == (rows / 8 < 3 ? 3 : rows / 8));
    desktop.mouse({true, static_cast<int16_t>(c.x + 100), static_cast<int16_t>(c.y + 100), 0, -5});
    wait(20000);
    CHECK(s->scrolled() == 0);
    // The keys are still the grid's after a click on the bar.
    enter("echo after");
    CHECK(row(s->cursor_row() - 1) == "after");
    // Back to its first size: 80 by 25 again, the cursor still on it.
    desktop.toggle_maximize(*w);
    wait(20000);
    CHECK(s->columns() == 80 && s->rows() == 25 && s->cursor_row() >= 0 && s->cursor_row() <= 24);
    close_everything();
}

void test_run()
{
    const int blocks = memory.in_use();
    storage.make_folder("C:\\Documents");
    storage.write_file("C:\\Documents\\list.txt", "milk\n", 5);
    storage.write_file("C:\\Documents\\old.zip", "zip", 3);

    // The Run dialog is a program of its own. A program's name, in any
    // case, starts it; the dialog goes, and the program is the desktop's
    // child, not the dialog's.
    shell::run_dialog();
    CHECK(strcmp(title(), "Run") == 0 && count("Run") == 1);
    type("editor");
    key(wm::key::Enter);
    wait(20000);
    CHECK(strcmp(title(), "Untitled - Editor") == 0 && count("Run") == 0 && count("Editor") == 1);
    CHECK(proc::table().find(desktop.active()->owner())->parent == proc::kRoot);
    close_everything();

    // It remembers what was run, selected, so typing replaces it. The start
    // of a name is enough when only one program begins so.
    shell::run_dialog();
    key(wm::key::Enter); // "editor" again
    wait(20000);
    CHECK(strcmp(title(), "Untitled - Editor") == 0);
    close_everything();
    shell::run_dialog();
    type("term");
    key(wm::key::Enter);
    wait(20000);
    CHECK(strcmp(title(), "Terminal") == 0);
    close_everything();

    // A file opens with its program, a folder in the File manager.
    shell::run_dialog();
    type("C:\\Documents\\list.txt");
    key(wm::key::Enter);
    wait(20000);
    CHECK(strcmp(title(), "list.txt - Editor") == 0);
    close_everything();
    shell::run_dialog();
    type("documents"); // without a drive: looked for on C:
    key(wm::key::Enter);
    wait(20000);
    CHECK(window_titled("Documents") != nullptr && count("Run") == 0);
    close_everything();

    // What cannot be opened is said, and the dialog stays for another try.
    shell::run_dialog();
    type("nothing here");
    key(wm::key::Enter);
    CHECK(desktop.active()->modal() && count("Run") == 1); // the message
    key(wm::key::Enter);
    CHECK(strcmp(title(), "Run") == 0);
    type("C:\\Documents\\old.zip");
    key(wm::key::Enter);
    CHECK(desktop.active()->modal());
    key(wm::key::Enter);
    key(wm::key::Escape);
    wait(20000);
    CHECK(desktop.window_count() == 0 && count("Run") == 0);
    CHECK(memory.in_use() == blocks);

    // From a Terminal: run, open and edit put windows on the desktop.
    shell::start_program("Terminal");
    wm::Window *terminal = desktop.active();
    enter("run editor");
    CHECK(strcmp(title(), "Untitled - Editor") == 0);
    desktop.activate(*terminal);
    enter("cd Documents");
    enter("open list.txt");
    CHECK(strcmp(title(), "list.txt - Editor") == 0);
    desktop.activate(*terminal);
    enter("open old.zip");
    CHECK(desktop.active() == terminal); // no message box: the reason is on the terminal
    CHECK(row(at() - 1) == "open: old.zip: there is no program for .zip files yet");
    enter("edit new.txt"); // a name that is not there yet is a new file
    CHECK(strcmp(title(), "new.txt - Editor") == 0);
    {
        ports::DirEntry made;
        CHECK(storage.stat("C:\\Documents\\new.txt", made) == ports::FsError::Ok && made.size == 0);
    }
    desktop.activate(*terminal);
    enter("open .");
    CHECK(window_titled("Documents") != nullptr);
    desktop.activate(*terminal);
    enter("run nonsense");
    CHECK(row(at() - 1) == "run: nonsense: there is no such program; run alone lists them");
    close_everything();
    CHECK(memory.in_use() == blocks);
}

/* ---- The session on a task of its own ------------------------------------------ */

host::ThreadQueue queue;
host::ThreadTasks tasks;
host::ThreadLock storage_lock;
host::ThreadLock kit_locks[3];
host::ThreadEvent kit_events[6];
shell::SessionKit kits[3];

// The grid is the session's task's too now: read under the Terminal's lock.
// All three locks are taken, since the test does not know which kit the
// Terminal in front was given.
struct AllKits {
    AllKits()
    {
        for (host::ThreadLock &l : kit_locks) {
            l.lock();
        }
    }
    ~AllKits()
    {
        for (host::ThreadLock &l : kit_locks) {
            l.unlock();
        }
    }
};

std::string locked_row(int index)
{
    const AllKits hold;
    return row(index);
}

bool on_screen(const char *text)
{
    const AllKits hold;
    if (shell::terminal_screen() == nullptr) {
        return false;
    }
    for (int i = 0; i < tty::Screen::kRows; i++) {
        if (row(i).find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// True when the last two lines on the screen are one ending in `answer` and
// an empty prompt: the command has finished and the session is waiting again.
bool answered(const char *answer)
{
    const AllKits hold;
    int last = tty::Screen::kRows - 1;
    while (last > 0 && row(last).empty()) {
        last--;
    }
    const std::string above = last > 0 ? row(last - 1) : "";
    const size_t size = strlen(answer);
    // "ends with": after type-ahead the answer follows a prompt on its line.
    return last > 0 && row(last) == "C:\\>" && above.size() >= size &&
           above.compare(above.size() - size, size, answer) == 0;
}

// The UI task's loop, until `done` says yes: messages, timers, painting.
template <typename Done> bool settle(Done done)
{
    msg::Inbox inbox(queue);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        inbox.deliver();
        clock_.now += 1000;
        desktop.tick();
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void test_terminal_on_a_task()
{
    msg::set_queue(&queue);
    job::jobs().set_tasks(&tasks);
    storage.set_lock(&storage_lock);
    for (int i = 0; i < 3; i++) {
        kits[i].lock = &kit_locks[i];
        kits[i].typed = &kit_events[2 * i];
        kits[i].answered = &kit_events[2 * i + 1];
    }
    shell::set_session_kits(kits, 3);
    const int blocks = memory.in_use();

    // It opens at once; the greeting and the prompt come from the session's
    // task a moment later.
    shell::start_program("Terminal");
    CHECK(strcmp(title(), "Terminal") == 0 && count("Terminal") == 1);
    // The task is the session's, not the window's.
    const proc::Pid pid = proc::table().find_by_name("sh");
    CHECK(tasks.started == 1 && job::jobs().running(pid) == 1 && kits[0].in_use);
    CHECK(job::jobs().running(desktop.active()->owner()) == 0);
    CHECK(settle([] { return locked_row(3) == "C:\\>"; }));
    CHECK(locked_row(0) == std::string("Chinchilla ") + shell::kVersion);

    // A typed character is echoed by the UI task itself; the line is run by
    // the session.
    type("echo hi");
    CHECK(locked_row(3) == "C:\\> echo hi");
    key(wm::key::Enter);
    CHECK(settle([] { return locked_row(4) == "hi" && locked_row(5) == "C:\\>"; }));
    // The session is asleep at its prompt and its last message is in the
    // queue: one more round of the loop and the screen must be right.
    settle([] { return true; });
    CHECK(no_stale_pixels());

    // ps reads the process table, which is the UI task's: the session asks
    // and sleeps until the UI task has answered.
    enter("ps");
    CHECK(settle([] { return on_screen("Terminal") && on_screen("desktop"); }));

    // run asks the UI task to start the program; the Terminal's session
    // goes on to its next prompt.
    {
        wm::Window *terminal = desktop.active();
        enter("run editor");
        CHECK(settle([] { return strcmp(title(), "Untitled - Editor") == 0; }));
        desktop.close(*desktop.active());
        desktop.activate(*terminal);
        settle([] { return true; });
    }

    // A command that prints a lot does not hold the desktop, and Ctrl+C
    // ends it. What is typed before the prompt is back is kept for the next
    // line, as on any terminal.
    {
        const std::string big(300000, 'z');
        storage.write_file("C:\\big.txt", big.data(), big.size());
    }
    enter("cat big.txt");
    desktop.text("x"); // the UI task is free to take keys meanwhile
    key(static_cast<uint16_t>(wm::key::A + 2), wm::key::LeftCtrl); // Ctrl+C
    enter("echo back");
    CHECK(settle([] { return on_screen("^C") && answered("back"); }));
    CHECK(!on_screen("zzzz\\>")); // it did not run to the end of its 300 KB
    enter("echo once more");
    CHECK(settle([] { return answered("once more"); }));
    // The session is asleep at its prompt and its last message is in the
    // queue: one more round of the loop and the screen must be right.
    settle([] { return true; });
    CHECK(no_stale_pixels());

    // ted in the window: a program that draws the whole screen from the
    // session's task. Its text is the Terminal's memory while it is open.
    // Page Up and Page Down are the program's while it has the keys, not
    // the history's.
    {
        const proc::Pid pid = proc::table().find_by_name("sh");
        const uint32_t before = proc::table().find(pid)->memory;
        storage.write_file("C:\\ted.txt", "one\ntwo\n", 8);
        enter("ted ted.txt");
        CHECK(settle([] { return locked_row(0) == "one" && on_screen("Ln 1, Col 1"); }));
        CHECK(proc::table().find(pid)->memory >= before + 128 * 1024);
        key(wm::key::Down);
        type("2: ");
        CHECK(settle([] { return locked_row(1) == "2: two" && on_screen("ted.txt*") && on_screen("Ln 2, Col 4"); }));
        // For the record (docs/look/ted/): CHINCHILLA_SHOT=file writes the
        // screen as it is now, one byte a pixel.
        if (const char *shot = getenv("CHINCHILLA_SHOT")) {
            settle([] { return true; });
            if (FILE *f = fopen(shot, "wb")) {
                fwrite(display.pixels(), 1, 640 * 480, f);
                fclose(f);
            }
        }
        key(wm::key::PageDown);
        CHECK(settle([] { return on_screen("Ln 3, Col 1"); }));
        CHECK(shell::terminal_screen()->scrolled() == 0);
        key(wm::key::PageUp);
        // Up and down keep to the column the cursor last chose.
        CHECK(settle([] { return on_screen("Ln 1, Col 4"); }));
        settle([] { return true; });
        CHECK(no_stale_pixels());
        key(static_cast<uint16_t>(wm::key::A + ('s' - 'a')), wm::key::LeftCtrl);
        CHECK(settle([] { return on_screen("Saved"); }));
        key(static_cast<uint16_t>(wm::key::A + ('q' - 'a')), wm::key::LeftCtrl);
        CHECK(settle([] { return locked_row(0) == "C:\\>"; })); // a clean screen and the prompt
        char back[32] = {};
        size_t size = 0;
        CHECK(storage.read_file("C:\\ted.txt", back, sizeof back, size) == fs::FsError::Ok &&
              std::string(back, size) == "one\n2: two\n");
        CHECK(proc::table().find(pid)->memory == before);
        // Back at the prompt the keys are the history's again.
        enter("echo after ted");
        CHECK(settle([] { return answered("after ted"); }));
        settle([] { return true; });
        CHECK(no_stale_pixels());
    }

    // exit: the session ends, its task returns, the window goes and the
    // process with it; the kit and the memory are back.
    enter("exit");
    CHECK(settle([&] { return count("Terminal") == 0 && count("sh") == 0; }));
    CHECK(desktop.window_count() == 0 && job::jobs().running() == 0 && !kits[0].in_use);
    CHECK(memory.in_use() == blocks);

    // The close button while the session waits for a line: the window is
    // gone at once, the rest when the session has noticed.
    shell::start_program("Terminal");
    CHECK(settle([] { return locked_row(3) == "C:\\>"; }));
    key(wm::key::F4, wm::key::LeftAlt);
    CHECK(desktop.window_count() == 0);
    desktop.update();
    CHECK(count("Terminal") == 0); // the emulator does not wait for its session
    CHECK(settle([&] { return count("Terminal") == 0 && count("sh") == 0; }));
    CHECK(job::jobs().running() == 0 && memory.in_use() == blocks);

    // End Task with force, in the middle of a command.
    shell::start_program("Terminal");
    CHECK(settle([] { return locked_row(3) == "C:\\>"; }));
    const proc::Pid second = desktop.active()->owner();
    enter("cat big.txt");
    CHECK(proc::table().signal(second, proc::Signal::Kill) == proc::Sent::Ok);
    CHECK(settle([&] { return count("Terminal") == 0 && count("sh") == 0; }));
    CHECK(desktop.window_count() == 0 && job::jobs().running() == 0 && memory.in_use() == blocks);

    // The session ended from outside, in the middle of a command: its
    // window closes when the shell has stopped.
    shell::start_program("Terminal");
    CHECK(settle([] { return locked_row(3) == "C:\\>"; }));
    enter("cat big.txt");
    CHECK(proc::table().signal(proc::table().find_by_name("sh"), proc::Signal::Kill) == proc::Sent::Ok);
    CHECK(settle([&] { return count("Terminal") == 0 && count("sh") == 0; }));
    CHECK(desktop.window_count() == 0 && job::jobs().running() == 0 && memory.in_use() == blocks);
    shell::start_program("Terminal");
    CHECK(settle([] { return locked_row(3) == "C:\\>"; }));
    CHECK(proc::table().signal(proc::table().find_by_name("sh"), proc::Signal::Term) == proc::Sent::Ok);
    CHECK(settle([&] { return count("Terminal") == 0 && count("sh") == 0; }));
    CHECK(desktop.window_count() == 0 && !kits[0].in_use && memory.in_use() == blocks);

    // Three at once, each with its own session; a fourth is refused.
    for (int i = 0; i < 3; i++) {
        shell::start_program("Terminal");
    }
    CHECK(count("Terminal") == 3 && job::jobs().running() == 3);
    CHECK(settle([] { return locked_row(3) == "C:\\>"; }));
    enter("echo third");
    CHECK(settle([] { return locked_row(4) == "third"; }));
    shell::start_program("Terminal");
    CHECK(strcmp(title(), "Terminal") == 0 && desktop.active()->modal()); // the message
    key(wm::key::Enter);
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    CHECK(settle([&] { return count("Terminal") == 0 && count("sh") == 0; }));
    CHECK(job::jobs().running() == 0 && memory.in_use() == blocks);

    tasks.join();
    shell::set_session_kits(nullptr, 0);
    job::jobs().set_tasks(nullptr);
    msg::set_queue(nullptr);
}

int main()
{
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    while (desktop.active() != nullptr) {
        desktop.close(*desktop.active());
    }
    test_terminal();
    test_size_and_scroll_bar();
    test_run();
    test_terminal_on_a_task();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("terminal: all checks passed\n");
    return 0;
}
