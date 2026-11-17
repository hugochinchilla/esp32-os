// The TTY: what a person at a terminal sees while typing (the echo is played
// into a small screen here and the screen is what is checked), what the
// program reads, Ctrl+C and Ctrl+D, earlier lines, raw mode, and a session
// on a thread of its own that sleeps until a line is typed.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/tty/screen.hpp"
#include "core/tty/tty.hpp"
#include "../host_adapters.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

// The least a terminal does: characters at the cursor, backspace one to the
// left, CR to the start of the line, LF one line down. Anything else that
// arrives is counted, so a test fails if the TTY sends something a plain
// terminal would not understand.
class Screen : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override
    {
        raw.append(reinterpret_cast<const char *>(data), size);
        for (size_t i = 0; i < size; i++) {
            const uint8_t c = data[i];
            if (c == '\b') {
                if (column_ > 0) {
                    column_--;
                } else {
                    off_the_edge++;
                }
            } else if (c == '\r') {
                column_ = 0;
            } else if (c == '\n') {
                lines_.emplace_back();
            } else if (c == '\a') {
                bells++;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < size && (data[i + 1] & 0xC0) == 0x80) {
                // Two bytes of UTF-8 are one character in one column; the
                // screen keeps it as Latin-1.
                cell(static_cast<char>(((c & 0x1F) << 6) | (data[i + 1] & 0x3F)));
                i++;
            } else if (c >= 0x20 && c < 0x7F) {
                cell(static_cast<char>(c));
            } else {
                unknown++;
            }
        }
    }

    // The line the cursor is on, without the spaces left at its end.
    std::string line() const { return trimmed(lines_.back()); }
    std::string line(size_t index) const { return index < lines_.size() ? trimmed(lines_[index]) : "?"; }
    size_t line_count() const { return lines_.size(); }
    size_t column() const { return column_; }

    std::string raw;
    int bells = 0, unknown = 0, off_the_edge = 0;

private:
    void cell(char c)
    {
        std::string &line = lines_.back();
        if (line.size() <= column_) {
            line.resize(column_ + 1, ' ');
        }
        line[column_++] = c;
    }
    static std::string trimmed(std::string s)
    {
        while (!s.empty() && s.back() == ' ') {
            s.pop_back();
        }
        return s;
    }
    std::vector<std::string> lines_{1};
    size_t column_ = 0;
};

// What one read() gave, as text: the line, or a name for the other answers.
std::string read_one(tty::Tty &t, size_t capacity = 200, uint32_t wait_ms = 0)
{
    char buffer[256];
    const int n = t.read(buffer, capacity, wait_ms);
    if (n == tty::kNothing) {
        return "<nothing>";
    }
    if (n == tty::kEnd) {
        return "<end>";
    }
    if (n == tty::kInterrupted) {
        return "<interrupted>";
    }
    return std::string(buffer, static_cast<size_t>(n));
}

#define LEFT "\x1b[D"
#define RIGHT "\x1b[C"
#define UP "\x1b[A"
#define DOWN "\x1b[B"

/* ---- Typing a line ---------------------------------------------------------------- */

void test_echo_and_lines()
{
    Screen screen;
    tty::Tty t(screen);
    CHECK(read_one(t) == "<nothing>");

    // The program's prompt, then typing: shown as typed, read only at Enter.
    t.print("C:\\> ");
    t.input("dir");
    CHECK(screen.line() == "C:\\> dir" && screen.column() == 8);
    CHECK(read_one(t) == "<nothing>");
    t.input("\r");
    CHECK(read_one(t) == "dir\n" && read_one(t) == "<nothing>");
    CHECK(screen.line_count() == 2 && screen.line(0) == "C:\\> dir" && screen.column() == 0);

    // What the program writes: a '\n' takes the cursor to the start of the
    // next line (CR LF on the wire).
    t.print("one\ntwo\n");
    CHECK(screen.line(1) == "one" && screen.line(2) == "two" && screen.column() == 0);
    CHECK(screen.raw.find("one\r\ntwo\r\n") != std::string::npos);
    t.printf("%d files, %s\n", 3, "ok");
    CHECK(screen.line(3) == "3 files, ok");

    // Every kind of Enter is one Enter: CR, LF, CR LF (telnet), CR NUL
    // (telnet), and two CRs are two.
    t.input("a\rb\nc\r\nd\r");
    t.input(reinterpret_cast<const uint8_t *>("\0e\r\r"), 4);
    CHECK(read_one(t) == "a\n" && read_one(t) == "b\n" && read_one(t) == "c\n" && read_one(t) == "d\n");
    CHECK(read_one(t) == "e\n" && read_one(t) == "\n" && read_one(t) == "<nothing>");

    // Typed ahead: lines wait in order until they are read. A read with a
    // small buffer takes a line in pieces.
    t.input("first\rsecond line\r");
    CHECK(read_one(t) == "first\n");
    CHECK(read_one(t, 6) == "second" && read_one(t, 6) == " line\n" && read_one(t) == "<nothing>");

    // Control codes with no meaning here are dropped.
    t.input("a\x02\x1F" "b\r");
    CHECK(read_one(t) == "ab\n");
    CHECK(screen.unknown == 0 && screen.off_the_edge == 0 && screen.bells == 0);
}

void test_editing()
{
    Screen screen;
    tty::Tty t(screen);

    // Backspace, both codes terminals send for it. At the start of the line
    // there is nothing to erase: a bell.
    t.input("helo\x7flo\x08\x08lo");
    CHECK(screen.line() == "hello" && screen.column() == 5);
    t.input("\r");
    CHECK(read_one(t) == "hello\n");
    t.input("\x7f");
    CHECK(screen.bells == 1);

    // Left and right, typing in the middle, Delete under the cursor.
    t.input("ct file" LEFT LEFT LEFT LEFT LEFT LEFT "a");
    CHECK(screen.line() == "cat file" && screen.column() == 2);
    t.input(RIGHT RIGHT "my\x1b[3~");
    CHECK(screen.line() == "cat myile" && screen.column() == 6);
    t.input(RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT "s"); // right stops at the end
    CHECK(screen.line() == "cat myiles");
    t.input("\r");
    CHECK(read_one(t) == "cat myiles\n");

    // Home and End in the three ways terminals send them, and Ctrl+A, Ctrl+E.
    t.input("bc\x1b[Ha\x1b[Fd\x1b[1~0\x1b[4~e\x1bOH<\x1bOF>\x01[\x05]\r");
    CHECK(read_one(t) == "[<0abcde>]\n");

    // Ctrl+W takes the word before the cursor, Ctrl+U everything before it,
    // Ctrl+K everything after it.
    t.input("copy one  two  \x17");
    CHECK(screen.line() == "copy one");
    t.input("\x17x\r");
    CHECK(read_one(t) == "copy x\n");
    t.input("abcdef" LEFT LEFT "\x15");
    CHECK(screen.line() == "ef" && screen.column() == 0);
    t.input("\r");
    CHECK(read_one(t) == "ef\n");
    t.input("abcdef" LEFT LEFT "\x0b");
    CHECK(screen.line() == "abcd" && screen.column() == 4);
    t.input("\r");
    CHECK(read_one(t) == "abcd\n");

    // Enter with the cursor in the middle sends the whole line.
    t.input("whole" LEFT LEFT "\r");
    CHECK(read_one(t) == "whole\n");

    // Keys the line has no use for leave no litter: F5, Ctrl+Right, Alt+x,
    // Page Up.
    t.input("a\x1b[15~b\x1b[1;5Cc\x1bxd\x1b[5~\r");
    CHECK(read_one(t) == "abcd\n");

    // The longest line is 159 characters; more only rings the bell.
    const int bells = screen.bells;
    const std::string many(200, 'x');
    t.input(many.c_str());
    CHECK(screen.bells == bells + 41);
    t.input("\r");
    CHECK(read_one(t) == std::string(159, 'x') + "\n");
    CHECK(screen.unknown == 0 && screen.off_the_edge == 0);
}

/* ---- Letters beyond ASCII ---------------------------------------------------------- */

void test_utf8()
{
    Screen screen;
    tty::Tty t(screen);

    // Typed as UTF-8, shown as UTF-8, read as UTF-8; each is one column and
    // one press of backspace or of an arrow.
    t.input("a\xC3\xB1o caf\xC3\xA9");
    CHECK(screen.line() == "a\xF1o caf\xE9" && screen.column() == 8);
    CHECK(screen.raw == "a\xC3\xB1o caf\xC3\xA9");
    t.input("\x7f" "e" LEFT LEFT LEFT LEFT LEFT LEFT "\x7f" "n\r");
    CHECK(screen.line(0) == "ano cafe");
    CHECK(read_one(t) == "ano cafe\n");
    t.input("ni\xC3\xB1o" LEFT LEFT "\x1b[3~" "n\r");
    CHECK(read_one(t) == "nino\n");
    t.input("\xC2\xA1hola, se\xC3\xB1or!\r");
    CHECK(read_one(t) == "\xC2\xA1hola, se\xC3\xB1or!\n" && screen.line(2) == "\xA1hola, se\xF1or!");

    // A character that arrives in two pieces, as a serial port may cut it.
    t.input("\xC3");
    CHECK(screen.column() == 0);
    t.input("\xBC\r");
    CHECK(read_one(t) == "\xC3\xBC\n");

    // What Latin-1 lacks (the euro sign, three bytes; a face, four) is a
    // question mark, and so are bytes that are not UTF-8 at all.
    t.input("5 \xE2\x82\xAC \xF0\x9F\x98\x80 \xF1 \xB1 \xC3x\r");
    CHECK(read_one(t) == "5 ? ? ? ? ?x\n");

    // An earlier line comes back as it was.
    t.input(UP UP UP);
    CHECK(screen.line() == "\xA1hola, se\xF1or!");
    t.input("\r");
    CHECK(read_one(t) == "\xC2\xA1hola, se\xC3\xB1or!\n");

    // The longest line is 159 characters whatever they are: 318 bytes and
    // the '\n' when read.
    std::string many;
    for (int i = 0; i < 170; i++) {
        many += "\xC3\xB1";
    }
    t.input(many.c_str());
    t.input("\r");
    char line[tty::Tty::kReadSize];
    CHECK(t.read(line, sizeof line) == 319 && line[318] == '\n');

    // Raw mode hands bytes over untouched.
    t.set_raw(true);
    t.input("\xC3\xB1\xFF");
    CHECK(read_one(t) == "\xC3\xB1\xFF");
    CHECK(screen.unknown == 0 && screen.off_the_edge == 0);
}

/* ---- Ctrl+C and Ctrl+D ------------------------------------------------------------ */

int g_interrupts = 0;
void count_interrupt(void *context) { g_interrupts += *static_cast<int *>(context); }

void test_interrupt_and_end()
{
    Screen screen;
    tty::Tty t(screen);
    int step = 1;
    g_interrupts = 0;
    t.on_interrupt(&count_interrupt, &step);

    // Ctrl+C throws the half-typed line away, shows ^C, and the program's
    // next read says so, once.
    t.input("rm everyth\x03");
    CHECK(screen.line(0) == "rm everyth^C" && screen.line_count() == 2 && screen.column() == 0);
    CHECK(t.interrupted() && g_interrupts == 1);
    CHECK(read_one(t) == "<interrupted>" && !t.interrupted() && read_one(t) == "<nothing>");

    // Lines typed ahead go with it; what is typed after it is new.
    t.input("one\rtwo\r\x03three\r");
    CHECK(read_one(t) == "<interrupted>" && read_one(t) == "three\n" && read_one(t) == "<nothing>");

    // A program that is busy looks at the flag instead of reading. Until
    // it has, what it writes is not shown: nothing lands under the ^C.
    t.input("\x03");
    const size_t shown_at_interrupt = screen.raw.size();
    t.print("the rest of a long listing\n");
    CHECK(screen.raw.size() == shown_at_interrupt);
    CHECK(t.interrupted() && t.take_interrupt() && !t.take_interrupt());
    t.print("after\n");
    CHECK(screen.line(screen.line_count() - 2) == "after");
    CHECK(read_one(t) == "<nothing>" && g_interrupts == 3);

    // Ctrl+D on an empty line is the end of the input, after the lines
    // before it; in a line it is nothing (a bell).
    t.input("last\r\x04");
    CHECK(read_one(t) == "last\n" && read_one(t) == "<end>" && read_one(t) == "<nothing>");
    const int bells = screen.bells;
    t.input("ab\x04" "c\r");
    CHECK(screen.bells == bells + 1 && read_one(t) == "abc\n");

    // The device going away: the end, for ever, and nothing more is shown.
    t.input("half");
    t.hang_up();
    const size_t shown = screen.raw.size();
    t.print("lost");
    t.input("more\r");
    CHECK(t.hung_up() && read_one(t) == "<end>" && read_one(t) == "<end>" && screen.raw.size() == shown);
}

/* ---- Earlier lines ---------------------------------------------------------------- */

void test_history()
{
    Screen screen;
    tty::Tty t(screen);
    const auto type = [&](const char *line) {
        t.input(line);
        t.input("\r");
        read_one(t);
    };

    // Nothing earlier yet: a bell.
    t.input(UP);
    CHECK(screen.bells == 1);

    type("dir");
    type("type notes.txt");
    type("type notes.txt"); // the same again is kept once
    type("");               // an empty line is not kept
    type("ps");

    // Up walks back, down walks forward, and below the newest is what was
    // being typed before the walk began.
    t.input("ki" UP);
    CHECK(screen.line() == "ps");
    t.input(UP);
    CHECK(screen.line() == "type notes.txt" && screen.column() == 14);
    t.input(UP);
    CHECK(screen.line() == "dir");
    t.input(UP); // the oldest: a bell, and it stays
    CHECK(screen.line() == "dir" && screen.bells == 2);
    t.input(DOWN DOWN DOWN);
    CHECK(screen.line() == "ki" && screen.column() == 2);
    t.input(DOWN);
    CHECK(screen.bells == 3);

    // An earlier line can be changed and sent; it becomes the newest.
    t.input(UP UP "\x17" "todo.txt\r");
    CHECK(read_one(t) == "type todo.txt\n");
    t.input(UP);
    CHECK(screen.line() == "type todo.txt");
    t.input("\x03");
    read_one(t);

    // What is typed with the echo off is neither shown nor kept.
    t.set_echo(false);
    t.input("secret\r");
    CHECK(read_one(t) == "secret\n" && screen.line() == "");
    t.set_echo(true);
    t.input(UP);
    CHECK(screen.line() == "type todo.txt");
    t.input("\x15");

    // The store is 384 bytes: the oldest lines make room for new ones.
    for (int i = 0; i < 40; i++) {
        char line[40];
        snprintf(line, sizeof line, "command number %02d with words", i);
        type(line);
    }
    int walked = 0;
    const int bells = screen.bells;
    while (screen.bells == bells && walked < 100) {
        t.input(UP);
        walked++;
    }
    CHECK(walked == 13 + 1); // 13 lines of 29 bytes fit, then the bell
    CHECK(screen.line() == "command number 27 with words");
    t.input("\x03");
    read_one(t);

    t.clear_history();
    const int before = screen.bells;
    t.input(UP);
    CHECK(screen.bells == before + 1);
    CHECK(screen.unknown == 0 && screen.off_the_edge == 0);
}

/* ---- Raw mode --------------------------------------------------------------------- */

void test_raw()
{
    Screen screen;
    tty::Tty t(screen);
    CHECK(t.columns() == 80 && t.rows() == 24);
    t.set_size(100, 37);
    CHECK(t.columns() == 100 && t.rows() == 37);

    // A half-typed line is dropped when a program takes the keys.
    t.input("half");
    t.set_raw(true);
    const size_t shown = screen.raw.size();

    // Keys come as they are, at once, with no echo; arrows are the
    // program's to understand.
    t.input("q" UP "\r\x7f");
    CHECK(read_one(t) == "q" UP "\r\x7f" && screen.raw.size() == shown);
    // Written as it is: no CR is added.
    t.print("a\nb");
    CHECK(screen.raw.substr(shown) == "a\nb");

    // Ctrl+C still interrupts, and takes the unread keys with it.
    t.input("xy\x03z");
    CHECK(read_one(t) == "<interrupted>" && read_one(t) == "z");

    // Back to lines: unread keys are thrown away.
    t.input("unread");
    t.set_raw(false);
    CHECK(read_one(t) == "<nothing>");
    t.input("ok\r");
    CHECK(read_one(t) == "ok\n");
}

/* ---- The program is not reading ---------------------------------------------------- */

void test_full_queue()
{
    Screen screen;
    tty::Tty t(screen);
    // 512 bytes of finished lines wait; the line that does not fit stays on
    // the screen with a bell, and Enter works again once there is room.
    const std::string line(99, 'z');
    for (int i = 0; i < 5; i++) {
        t.input(line.c_str());
        t.input("\r");
    }
    CHECK(screen.bells == 0);
    t.input(line.c_str());
    t.input("\r");
    CHECK(screen.bells == 1 && screen.line() == line);
    for (int i = 0; i < 5; i++) {
        CHECK(read_one(t) == line + "\n");
    }
    CHECK(read_one(t) == "<nothing>");
    t.input("\r");
    CHECK(read_one(t) == line + "\n" && screen.bells == 1);
}

/* ---- Two tasks -------------------------------------------------------------------- */

void test_session_on_a_thread()
{
    using Clock = std::chrono::steady_clock;
    Screen screen;
    host::ThreadLock lock;
    host::ThreadEvent event;
    tty::Tty t(screen, &lock, &event);

    // A wait that nothing ends takes its time and answers "nothing".
    const auto start = Clock::now();
    CHECK(read_one(t, 200, 50) == "<nothing>");
    CHECK(Clock::now() - start >= std::chrono::milliseconds(45));

    // A session like the shell's: it sleeps in read(), echoes each line
    // back, counts the interrupts and ends at the end of the input.
    std::vector<std::string> lines;
    std::atomic<int> interrupted{0}, answers{0};
    std::thread session([&] {
        for (;;) {
            char buffer[200];
            const int n = t.read(buffer, sizeof buffer, 0xFFFFFFFF);
            if (n == tty::kEnd) {
                break;
            }
            if (n == tty::kInterrupted) {
                interrupted++;
                continue;
            }
            lines.emplace_back(buffer, static_cast<size_t>(n));
            t.printf("got %d\n", n);
            answers++;
        }
    });

    // The device's task types 300 lines a few bytes at a time, as a serial
    // port delivers them, with a Ctrl+C on an empty line now and then. It
    // waits for each answer before the next line, as a person does.
    int expected_interrupts = 0;
    for (int i = 0; i < 300; i++) {
        char line[40];
        const int length = snprintf(line, sizeof line, "line %d of the test\r", i);
        for (int at = 0; at < length; at += 3) {
            t.input(reinterpret_cast<const uint8_t *>(line + at), static_cast<size_t>(length - at < 3 ? length - at : 3));
        }
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (answers != i + 1 && Clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (i % 50 == 49) {
            const int before = interrupted;
            t.input("\x03");
            expected_interrupts++;
            while (interrupted == before && Clock::now() < deadline) {
                std::this_thread::yield();
            }
        }
    }
    t.input("\x04");
    session.join();

    CHECK(lines.size() == 300 && interrupted == expected_interrupts);
    // Echo and answers did not run into each other: every line on the
    // screen is a whole typed line, a whole answer, or a ^C.
    CHECK(screen.line_count() == 300 * 2 + 6 + 1);
    for (size_t i = 0; i + 1 < screen.line_count(); i++) {
        const std::string line = screen.line(i);
        CHECK(line.rfind("line ", 0) == 0 || line.rfind("got ", 0) == 0 || line == "^C");
    }
    for (size_t i = 0; i < lines.size(); i++) {
        char line[40];
        snprintf(line, sizeof line, "line %d of the test\n", static_cast<int>(i));
        CHECK(lines[i] == line);
    }

    // hang_up() wakes a sleeping reader too.
    std::atomic<int> answer{0};
    std::thread sleeper([&] {
        char buffer[8];
        answer = t.read(buffer, sizeof buffer, 0xFFFFFFFF);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    t.hang_up();
    sleeper.join();
    CHECK(answer == tty::kEnd);
}

/* ---- The grid a terminal window shows ------------------------------------------------ */

std::string grid_row(const tty::Screen &s, int row)
{
    std::string text(reinterpret_cast<const char *>(s.row(row)), tty::Screen::kColumns);
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

void test_grid()
{
    using tty::Screen;
    static Screen s; // 16 KB: not on the stack
    int first = -1, last = -1;
    // A new screen is empty and wants painting, once.
    CHECK(s.take_changes(first, last) && first == 0 && last == Screen::kRows - 1 && !s.take_changes(first, last));
    CHECK(grid_row(s, 0) == "" && s.cursor_row() == 0 && s.cursor_column() == 0);

    // Text, line ends, and only the rows touched are reported.
    s.write("one\r\ntwo");
    CHECK(grid_row(s, 0) == "one" && grid_row(s, 1) == "two" && s.cursor_row() == 1 && s.cursor_column() == 3);
    CHECK(s.take_changes(first, last) && first == 0 && last == 1);
    s.write("!");
    CHECK(s.take_changes(first, last) && first == 1 && last == 1);
    // A LF alone goes down and keeps the column; CR alone goes back.
    s.write("\nx\ry");
    CHECK(grid_row(s, 2) == "y   x" && s.cursor_column() == 1);
    // Backspace moves and erases nothing; tab stops are every 8.
    s.write("\r\nabc\b\bX\tT");
    CHECK(grid_row(s, 3) == "aXc     T");
    // UTF-8 is one cell; what Latin-1 lacks is '?'; a bell is counted.
    s.write("\r\na\xC3\xB1o \xE2\x82\xAC\a\a");
    CHECK(grid_row(s, 4) == "a\xF1o ?" && s.take_bells() == 2 && s.take_bells() == 0);

    // The right edge: the 80th character stays on its line, the 81st starts
    // the next; a backspace walks back over the join.
    s.clear();
    const std::string eighty(80, 'w');
    s.write(eighty.c_str());
    CHECK(grid_row(s, 0) == eighty && grid_row(s, 1) == "" && s.cursor_row() == 0 && s.cursor_column() == 79);
    s.write("12");
    CHECK(grid_row(s, 1) == "12" && s.cursor_row() == 1 && s.cursor_column() == 2);
    s.write("\b\b\b#");
    CHECK(grid_row(s, 0) == std::string(79, 'w') + "#" && s.cursor_row() == 0);
    s.write("\b\b=");
    CHECK(grid_row(s, 0) == std::string(78, 'w') + "=#");

    // The TTY's own line editing on a line longer than the screen is wide:
    // a letter put in near the start pushes the rest along, over the join.
    s.clear();
    {
        tty::Tty t(s);
        const std::string start(100, 'a');
        t.input(start.c_str());
        t.input("\x1b[H" "Z");
        CHECK(grid_row(s, 0) == "Z" + std::string(79, 'a') && grid_row(s, 1) == std::string(21, 'a'));
        CHECK(s.cursor_row() == 0 && s.cursor_column() == 1);
        t.input("\x7f");
        CHECK(grid_row(s, 0) == std::string(80, 'a') && grid_row(s, 1) == std::string(20, 'a'));
        t.input("\x1b[F" "!");
        CHECK(grid_row(s, 1) == std::string(20, 'a') + "!" && s.cursor_row() == 1 && s.cursor_column() == 21);
    }

    // Escape sequences: erase the screen, place the cursor, erase to the
    // end of the line, move; the ones not understood show nothing.
    s.clear();
    s.write("junk\r\nmore junk\x1b[2J\x1b[H" "top");
    CHECK(grid_row(s, 0) == "top" && grid_row(s, 1) == "");
    s.write("\x1b[5;10Hhere\x1b[3D\x1b[K");
    CHECK(grid_row(s, 4) == "         h" && s.cursor_row() == 4 && s.cursor_column() == 10);
    s.write("\x1b[2A\x1b[5C" "up\x1b[B\x1b[10D" "down");
    CHECK(grid_row(s, 2) == "               up" && grid_row(s, 3) == "       down");
    s.write("\x1b[99;99H@\x1b[;5H5\x1b[1;31mred\x1b[0m\x1b[?25l");
    CHECK(grid_row(s, 24) == std::string(79, ' ') + "@" && grid_row(s, 0) == "top 5red");
    s.write("\x1b[2;1Hsecond\x1b[1;1H\x1b[J");
    CHECK(grid_row(s, 0) == "" && grid_row(s, 1) == "" && grid_row(s, 24) == "");
    // A sequence cut in two by the wire.
    s.write("\x1b[3;");
    s.write("4Hx");
    CHECK(grid_row(s, 2) == "   x");

    // Scrolling: the top line goes into the history, which holds 175.
    s.clear();
    for (int i = 0; i < 30; i++) {
        char line[24];
        snprintf(line, sizeof line, "line %d\r\n", i);
        s.write(line);
    }
    CHECK(grid_row(s, 0) == "line 6" && grid_row(s, 23) == "line 29" && grid_row(s, 24) == "");
    CHECK(s.history() == 6 && s.scrolled() == 0 && s.cursor_row() == 24);
    s.take_changes(first, last);
    // Looking back, as far as there is, and forward again.
    s.scroll_view(4);
    CHECK(s.scrolled() == 4 && grid_row(s, 0) == "line 2" && s.cursor_row() == -1);
    CHECK(s.take_changes(first, last) && first == 0 && last == 24);
    s.scroll_view(100);
    CHECK(s.scrolled() == 6 && grid_row(s, 0) == "line 0" && grid_row(s, 24) == "line 24");
    s.scroll_view(-1);
    CHECK(s.scrolled() == 5 && s.cursor_row() == -1);
    s.scroll_view(-100);
    CHECK(s.scrolled() == 0 && grid_row(s, 0) == "line 6" && s.cursor_row() == 24);
    CHECK(s.take_changes(first, last));
    s.scroll_view(-1); // already at the end: nothing to paint
    CHECK(!s.take_changes(first, last));
    // Anything written brings the view back.
    s.scroll_view(3);
    s.write("x");
    CHECK(s.scrolled() == 0 && grid_row(s, 24) == "x");
    for (int i = 30; i < 400; i++) {
        char line[24];
        snprintf(line, sizeof line, "\r\nline %d", i);
        s.write(line);
    }
    CHECK(s.history() == Screen::kHistory && grid_row(s, 24) == "line 399");
    s.scroll_view(1000);
    CHECK(s.scrolled() == 175 && grid_row(s, 0) == "line 200" && grid_row(s, 24) == "line 224");
    // Erasing the screen keeps the history.
    s.write("\x1b[2J\x1b[H");
    CHECK(grid_row(s, 0) == "" && s.history() == Screen::kHistory);
    s.clear();
    CHECK(s.history() == 0);
}

// A grid that changes size: taller brings lines back from the history,
// shorter sends lines into it so the cursor stays on the screen.
void test_grid_size()
{
    using tty::Screen;
    static Screen s;
    s.write("line 0");
    for (int i = 1; i < 30; i++) {
        char line[24];
        snprintf(line, sizeof line, "\r\nline %d", i);
        s.write(line);
    }
    CHECK(s.columns() == 80 && s.rows() == 25 && s.history() == 5 && grid_row(s, 0) == "line 5");
    s.set_size(100, 30);
    CHECK(s.columns() == 100 && s.rows() == 30 && s.history() == 0);
    CHECK(grid_row(s, 0) == "line 0" && grid_row(s, 29) == "line 29" && s.cursor_row() == 29);
    int first = -1, last = -1;
    CHECK(s.take_changes(first, last) && first == 0 && last == 29);
    // Taller than there are lines to bring back: empty rows at the bottom.
    s.set_size(100, 36);
    CHECK(s.rows() == 36 && grid_row(s, 0) == "line 0" && grid_row(s, 30) == "" && s.cursor_row() == 29);
    // 100 characters fit on one row now.
    s.write("\r\n");
    for (int i = 0; i < 100; i++) {
        s.write("w");
    }
    CHECK(s.cursor_row() == 30 && std::string(reinterpret_cast<const char *>(s.row(30)), 100) == std::string(100, 'w'));
    // Shorter: the top goes into the history.
    s.set_size(40, 10);
    CHECK(s.rows() == 10 && s.columns() == 40 && s.cursor_row() == 9 && s.history() == 21);
    CHECK(grid_row(s, 8) == "line 29" && s.cursor_column() <= 39);
    s.scroll_view(21);
    CHECK(grid_row(s, 0) == "line 0");
    // Out of range sizes are brought within the limits.
    s.set_size(1000, 1000);
    CHECK(s.columns() == Screen::kMaxColumns && s.rows() == Screen::kMaxRows);
    s.set_size(1, 1);
    CHECK(s.columns() == Screen::kMinColumns && s.rows() == Screen::kMinRows);
}

} // namespace

int main()
{
    test_grid_size();
    test_echo_and_lines();
    test_editing();
    test_utf8();
    test_interrupt_and_end();
    test_history();
    test_raw();
    test_full_queue();
    test_grid();
    test_session_on_a_thread();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("tty: all checks passed\n");
    return 0;
}
