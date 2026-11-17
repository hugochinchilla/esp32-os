// The Terminal: the command shell in a window, driven with the keyboard as
// a person would. What is checked is the grid of characters the window
// shows, the files and processes behind it, and that the picture on the
// screen is what a full repaint would give.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/fs/memory_volume.hpp"
#include "core/proc/table.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "core/tty/screen.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"

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
    const shell::App *app = shell::find_app("Terminal");
    CHECK(app != nullptr && app->icon != nullptr && app->many);
    CHECK(shell::terminal_screen() == nullptr);
    const int blocks = memory.in_use();

    // It opens with the greeting and a prompt, as a process of its own.
    shell::launch(*app);
    desktop.update();
    CHECK(strcmp(title(), "Terminal") == 0 && count("Terminal") == 1);
    CHECK(memory.in_use() == blocks + 1); // the window, with the grid, the TTY and the session in it
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
        listed = listed || row(r).find("---W   Terminal") != std::string::npos;
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
    shell::launch(*app);
    desktop.update();
    CHECK(count("Terminal") == 2 && memory.in_use() == blocks + 2);
    CHECK_ROW(3, "C:\\>"); // the new one is in front, in its own folder
    CHECK(no_stale_pixels()); // the first one's cursor is a line now
    shell::launch(*app);
    desktop.update();
    CHECK(count("Terminal") == 3);
    shell::launch(*app);
    desktop.update();
    CHECK(count("Terminal") == 3 && strcmp(title(), "Terminal") == 0);
    CHECK(desktop.active() != nullptr && desktop.active()->dialog()); // "Three Terminals are open already."
    key(wm::key::Enter);
    CHECK(no_stale_pixels());

    // exit closes the window and ends the process; the memory comes back.
    enter("exit");
    wait(20000);
    CHECK(count("Terminal") == 2 && memory.in_use() == blocks + 2);
    // Ctrl+D on an empty line does the same.
    CHECK(strcmp(title(), "Terminal") == 0);
    key(wm::key::A + 3, wm::key::LeftCtrl);
    wait(20000);
    CHECK(count("Terminal") == 1 && memory.in_use() == blocks + 1);
    // The first one is still where it was left.
    CHECK(row(24) == "C:\\Documents>");
    // It can end itself by number, like any other process.
    char line[32];
    snprintf(line, sizeof line, "kill %d", proc::table().find_by_name("Terminal"));
    enter(line);
    wait(20000);
    CHECK(count("Terminal") == 0 && memory.in_use() == blocks && shell::terminal_screen() == nullptr);
    CHECK(no_stale_pixels());

    // The close button's way (Alt+F4), with a half-typed line.
    shell::launch(*app);
    desktop.update();
    type("half a li");
    key(0x3D, wm::key::LeftAlt); // F4
    wait(20000);
    CHECK(count("Terminal") == 0 && memory.in_use() == blocks);
}

} // namespace

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
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("terminal: all checks passed\n");
    return 0;
}
