// The command shell: paths as typed, every command with what it prints and
// what it does to the drive, a session typed into through a TTY, and a
// session on a thread of its own whose `ps` and `kill` are done by the UI
// thread.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/fs/fs.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/msg/queue.hpp"
#include "core/proc/table.hpp"
#include "core/sh/sh.hpp"
#include "core/shell/boot.hpp"
#include "core/tty/tty.hpp"
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
// The same, showing the text that was not what was expected.
#define CHECK_TEXT(got, expected)                                                                     \
    do {                                                                                              \
        const std::string got_ = (got), expected_ = (expected);                                       \
        if (got_ != expected_) {                                                                      \
            printf("FAILED %s:%d:\n--- got\n%s\n--- expected\n%s\n---\n", __FILE__, __LINE__, got_.c_str(), \
                   expected_.c_str());                                                                \
            failures++;                                                                               \
        }                                                                                             \
    } while (0)

namespace {

using fs::FsError;

// Everything the terminal was sent, with line ends as '\n'.
class Capture : public ports::ByteSink {
public:
    void write(const uint8_t *data, size_t size) override
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        for (size_t i = 0; i < size; i++) {
            if (data[i] != '\r') {
                text_.push_back(static_cast<char>(data[i]));
            }
        }
        writes_++;
        if (slow) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::string take()
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        std::string out;
        out.swap(text_);
        return out;
    }
    size_t size()
    {
        const std::lock_guard<std::mutex> hold(mutex_);
        return text_.size();
    }
    std::atomic<bool> slow{false};

private:
    std::mutex mutex_;
    std::string text_;
    int writes_ = 0;
};

// A machine with two RAM disks, a process table and a terminal.
struct Machine {
    Machine()
        : pool_c(512 * 1024), pool_d(64 * 1024), c(pool_c.data(), pool_c.size(), "RAM disk"),
          d(pool_d.data(), pool_d.size(), "Second"), terminal(screen)
    {
        storage.mount('C', c);
        storage.mount('D', d);
        env.storage = &storage;
        env.procs = &procs;
        env.system = &system;
    }
    void file(const char *path, const std::string &text)
    {
        CHECK(storage.write_file(path, text.data(), text.size()) == FsError::Ok);
    }
    std::string contents(const char *path)
    {
        std::vector<char> data(256 * 1024);
        size_t size = 0;
        if (storage.read_file(path, data.data(), data.size(), size) != FsError::Ok) {
            return "<none>";
        }
        return std::string(data.data(), size);
    }
    bool exists(const char *path)
    {
        fs::DirEntry entry;
        return storage.stat(path, entry) == FsError::Ok;
    }

    std::vector<uint8_t> pool_c, pool_d;
    fs::MemoryVolume c, d;
    fs::Storage storage;
    proc::Table procs;
    host::HostSystem system;
    Capture screen;
    tty::Tty terminal;
    sh::Env env;
};

// Runs a line and gives what it printed.
std::string run(Machine &m, sh::Session &s, const char *line, int expected_result = 0)
{
    m.screen.take();
    const int result = s.execute(line);
    if (result != expected_result) {
        printf("FAILED: \"%s\" returned %d, not %d\n", line, result, expected_result);
        failures++;
    }
    return m.screen.take();
}

/* ---- Paths ------------------------------------------------------------------------ */

void test_paths()
{
    const auto resolved = [](const char *cwd, const char *typed) {
        char out[fs::kMaxPath];
        return sh::path::resolve(cwd, typed, out, sizeof out) ? std::string(out) : std::string("<too long>");
    };
    // From the folder the session is in, with either slash.
    CHECK(resolved("C:\\docs", "a.txt") == "C:\\docs\\a.txt");
    CHECK(resolved("C:\\", "a.txt") == "C:\\a.txt");
    CHECK(resolved("C:\\docs", "sub/deep\\a.txt") == "C:\\docs\\sub\\deep\\a.txt");
    CHECK(resolved("C:\\docs", ".") == "C:\\docs" && resolved("C:\\docs", "") == "C:\\docs");
    CHECK(resolved("C:\\docs", "./a.txt") == "C:\\docs\\a.txt");
    // Up, and no further than the root.
    CHECK(resolved("C:\\docs\\sub", "..") == "C:\\docs" && resolved("C:\\docs", "..") == "C:\\");
    CHECK(resolved("C:\\docs\\sub", "..\\..\\..\\..\\x") == "C:\\x");
    CHECK(resolved("C:\\docs\\sub", "../other/./b") == "C:\\docs\\other\\b");
    // From the root: a slash in front, or a drive letter in any case.
    CHECK(resolved("C:\\docs", "/x/y") == "C:\\x\\y" && resolved("C:\\docs", "\\x") == "C:\\x");
    CHECK(resolved("C:\\docs", "/") == "C:\\" && resolved("C:\\docs", "\\") == "C:\\");
    CHECK(resolved("C:\\docs", "d:") == "D:\\" && resolved("C:\\docs", "d:/pics//a.bmp") == "D:\\pics\\a.bmp");
    CHECK(resolved("C:\\docs", "D:\\pics\\") == "D:\\pics" && resolved("C:\\docs", "c:x") == "C:\\x");
    // Spaces and dots in names are the name's own.
    CHECK(resolved("C:\\", "my file.txt") == "C:\\my file.txt" && resolved("C:\\", ".config/x.ini") == "C:\\.config\\x.ini");
    CHECK(resolved("C:\\", "...") == "C:\\...");
    // What does not fit is refused, not cut.
    CHECK(resolved("C:\\", std::string(200, 'x').c_str()) == "<too long>");
    CHECK(resolved("C:\\", std::string(156, 'x').c_str()) == "C:\\" + std::string(156, 'x'));
    CHECK(resolved("C:\\", std::string(157, 'x').c_str()) == "<too long>");

    using sh::path::match;
    CHECK(match("*", "a.txt") && match("*.txt", "a.txt") && match("*.TXT", "notes.txt") && !match("*.txt", "a.bmp"));
    CHECK(match("a*", "a") && match("a*b*c", "aXXbYYc") && !match("a*b*c", "aXXbYY") && match("**", "x"));
    CHECK(match("?.txt", "a.txt") && !match("?.txt", "ab.txt") && !match("?", "") && match("", "") && !match("", "a"));
    CHECK(match("notes.txt", "NOTES.TXT") && !match("notes", "notes.txt"));
    // Hidden names are not swept up by a star.
    CHECK(!match("*", ".config") && match(".*", ".config") && !match("?config", ".config"));
    CHECK(sh::path::has_wildcard("a*") && sh::path::has_wildcard("?") && !sh::path::has_wildcard("a.txt"));
    CHECK(sh::path::same("C:\\Docs", "c:\\docs") && !sh::path::same("C:\\docs", "C:\\docs2"));
}

/* ---- Words, names of commands, help ------------------------------------------------ */

void test_lines()
{
    Machine m;
    sh::Session s(m.terminal, m.env);
    CHECK(std::string(s.cwd()) == "C:\\");

    // An empty line is nothing. Words split at spaces and tabs; quotes keep
    // spaces.
    CHECK_TEXT(run(m, s, ""), "");
    CHECK_TEXT(run(m, s, "   \t "), "");
    CHECK_TEXT(run(m, s, "echo"), "\n");
    CHECK_TEXT(run(m, s, "  echo   one\ttwo  \"three   four\" fi\"ve si\"x "), "one two three   four five six\n");
    // Commands in any case, by either name.
    CHECK_TEXT(run(m, s, "ECHO Hello"), "Hello\n");
    CHECK_TEXT(run(m, s, "Ver"), std::string("Chinchilla ") + shell::kVersion + "\n");
    CHECK_TEXT(run(m, s, "uname"), std::string("Chinchilla ") + shell::kVersion + "\n");
    CHECK_TEXT(run(m, s, "frobnicate now", 127), "frobnicate: there is no such command. help lists them.\n");
    CHECK_TEXT(run(m, s, "a b c d e f g h i j k l m n o p q", 1), "sh: too many words on the line\n");
    CHECK_TEXT(run(m, s, "cls"), "\x1b[2J\x1b[H");

    // help: every command with its other names; help NAME by any of them.
    const std::string help = run(m, s, "help");
    CHECK(help.find("  ls, dir                List a folder") != std::string::npos);
    CHECK(help.find("  mv, move, ren, rename  Move or rename") != std::string::npos);
    CHECK(help.find("  kill, taskkill ") != std::string::npos && help.find("  exit, logout") != std::string::npos);
    CHECK_TEXT(run(m, s, "?"), help);
    CHECK_TEXT(run(m, s, "help del"),
               "rm [-r] PATH...\n  Delete for good; -r deletes folders and all in them\n  Also: del erase\n");
    CHECK_TEXT(run(m, s, "help pwd"), "pwd\n  Show the folder you are in\n");
    CHECK_TEXT(run(m, s, "help nothing", 1), "help: nothing: there is no such command\n");
    // No line of the list is wider than a terminal.
    size_t start = 0;
    for (size_t end; (end = help.find('\n', start)) != std::string::npos; start = end + 1) {
        CHECK(end - start <= 80);
    }

    CHECK(!s.ended());
    run(m, s, "exit");
    CHECK(s.ended());
}

/* ---- Pipes, redirection and the filters -------------------------------------------- */

void test_pipes()
{
    Machine m;
    m.storage.make_folder("C:\\docs");
    // Latin-1 inside the file, as the Editor writes it; \r\n line ends.
    m.file("C:\\docs\\fruit.txt", "apple\r\nBanana\r\ncherry\r\nmel\xF3n\r\nap ple pie\r\n");
    m.file("C:\\docs\\notes.txt", "one\ntwo\nthree");
    sh::Session s(m.terminal, m.env, "C:\\docs");

    // > writes what the command says into a file, >> adds to one, and
    // nothing of it is shown. A file holds Latin-1.
    CHECK_TEXT(run(m, s, "echo hello > out.txt"), "");
    CHECK(m.contents("C:\\docs\\out.txt") == "hello\n");
    CHECK_TEXT(run(m, s, "echo se\xC3\xB1or >> out.txt"), "");
    CHECK_TEXT(run(m, s, "echo again>>out.txt"), ""); // no spaces needed
    CHECK(m.contents("C:\\docs\\out.txt") == "hello\nse\xF1or\nagain\n");
    CHECK_TEXT(run(m, s, "echo new > out.txt"), "");
    CHECK(m.contents("C:\\docs\\out.txt") == "new\n");
    CHECK_TEXT(run(m, s, "echo first >> fresh.txt"), ""); // >> makes the file if there is none
    CHECK(m.contents("C:\\docs\\fresh.txt") == "first\n");
    CHECK_TEXT(run(m, s, "echo \"a > b\" > \"my file.txt\""), ""); // quotes keep a > and a space
    CHECK(m.contents("C:\\docs\\my file.txt") == "a > b\n");
    CHECK_TEXT(run(m, s, "> first.txt echo words"), ""); // anywhere on the line
    CHECK(m.contents("C:\\docs\\first.txt") == "words\n");
    // A file's own bytes go through untouched.
    CHECK_TEXT(run(m, s, "cat fruit.txt > copy.txt"), "");
    CHECK(m.contents("C:\\docs\\copy.txt") == m.contents("C:\\docs\\fruit.txt"));
    // A list into a file has a name on each line.
    CHECK_TEXT(run(m, s, "ls *.txt > list.txt"), "");
    CHECK(m.contents("C:\\docs\\list.txt").find("fruit.txt\nnotes.txt\n") != std::string::npos);

    // < gives a command a file as its input.
    CHECK_TEXT(run(m, s, "cat < notes.txt"), "one\ntwo\nthree");
    CHECK_TEXT(run(m, s, "wc -l < notes.txt"), "2\n");
    CHECK_TEXT(run(m, s, "grep e < notes.txt > e.txt"), "");
    CHECK(m.contents("C:\\docs\\e.txt") == "one\nthree\n");

    // Errors stay on the terminal and out of the file.
    CHECK_TEXT(run(m, s, "cat nothing.txt > out.txt", 1), "cat: C:\\docs\\nothing.txt: not found\n");
    CHECK(m.contents("C:\\docs\\out.txt") == "");
    CHECK_TEXT(run(m, s, "nonsense > out.txt", 127), "nonsense: there is no such command. help lists them.\n");
    CHECK_TEXT(run(m, s, "echo x >", 1), "sh: a file name is missing after < or >\n");
    CHECK_TEXT(run(m, s, "cat < nothing.txt", 1), "sh: C:\\docs\\nothing.txt: not found\n");
    CHECK_TEXT(run(m, s, "echo x > Q:\\a.txt", 1), "sh: Q:\\a.txt: not found\n");

    // grep: plain text, not patterns. find and findstr are the DOS names.
    CHECK_TEXT(run(m, s, "grep ap fruit.txt"), "apple\nap ple pie\n");
    CHECK_TEXT(run(m, s, "grep -i BAN fruit.txt"), "Banana\n");
    CHECK_TEXT(run(m, s, "grep -n e fruit.txt"), "1:apple\n3:cherry\n4:mel\xC3\xB3n\n5:ap ple pie\n");
    CHECK_TEXT(run(m, s, "grep -v -i a fruit.txt"), "cherry\nmel\xC3\xB3n\n");
    CHECK_TEXT(run(m, s, "grep -c ap fruit.txt"), "2\n");
    CHECK_TEXT(run(m, s, "grep \"ple p\" fruit.txt"), "ap ple pie\n");
    CHECK_TEXT(run(m, s, "grep -i MEL\xC3\x93N fruit.txt"), "mel\xC3\xB3n\n"); // accents, in either case
    CHECK_TEXT(run(m, s, "find \"cherry\" fruit.txt"), "cherry\n");
    CHECK_TEXT(run(m, s, "findstr one notes.txt fruit.txt"), "notes.txt:one\n");
    CHECK_TEXT(run(m, s, "grep zebra fruit.txt", 1), "");
    CHECK_TEXT(run(m, s, "grep", 1), "grep: it needs the text to look for\n");
    CHECK_TEXT(run(m, s, "grep x nothing.txt", 2), "grep: nothing.txt: not found\n");

    // wc, head, tail.
    CHECK_TEXT(run(m, s, "wc fruit.txt"), "      5       7      42 fruit.txt\n");
    CHECK_TEXT(run(m, s, "wc -w notes.txt"), "3 notes.txt\n");
    CHECK_TEXT(run(m, s, "head -n 2 fruit.txt"), "apple\nBanana\n");
    CHECK_TEXT(run(m, s, "head -1 fruit.txt notes.txt"), "==> fruit.txt <==\napple\n==> notes.txt <==\none\n");
    CHECK_TEXT(run(m, s, "tail -n 2 fruit.txt"), "mel\xC3\xB3n\nap ple pie\n");
    CHECK_TEXT(run(m, s, "tail -1 notes.txt"), "three\n"); // a last line without an end gets one
    CHECK_TEXT(run(m, s, "tail -n 99 notes.txt"), "one\ntwo\nthree\n");
    CHECK_TEXT(run(m, s, "head -n x notes.txt", 1), "head: -n: it takes -n and a number of lines\n");
    {
        std::string many;
        for (int i = 1; i <= 500; i++) {
            many += "line " + std::to_string(i) + "\n";
        }
        m.file("C:\\docs\\many.txt", many);
    }
    CHECK_TEXT(run(m, s, "head many.txt | tail -n 1"), "line 10\n");
    CHECK_TEXT(run(m, s, "tail -n 3 many.txt"), "line 498\nline 499\nline 500\n");
    CHECK_TEXT(run(m, s, "wc -l many.txt"), "500 many.txt\n");

    // A pipe: each command gets what the one before it wrote.
    CHECK_TEXT(run(m, s, "cat fruit.txt | grep ap"), "apple\nap ple pie\n");
    CHECK_TEXT(run(m, s, "cat fruit.txt | grep -i a | grep -v pie | wc -l"), "2\n");
    CHECK_TEXT(run(m, s, "ls | grep many"), "many.txt\n");
    CHECK_TEXT(run(m, s, "echo \"a | b\" | cat"), "a | b\n"); // a | inside quotes is text
    CHECK_TEXT(run(m, s, "cat < notes.txt | grep t | head -n 1 > t.txt"), "");
    CHECK(m.contents("C:\\docs\\t.txt") == "two\n");
    CHECK_TEXT(run(m, s, "cat many.txt|wc -l"), "500\n");
    // The session's own commands work in a pipe too, and it stays where it is.
    CHECK_TEXT(run(m, s, "pwd | cat"), "C:\\docs\n");
    // Nothing is left behind, not even with -a.
    CHECK(run(m, s, "ls -a C:\\").find("pipe") == std::string::npos);

    // What cannot be done is said, and the rest of the line is not run.
    CHECK_TEXT(run(m, s, "cat fruit.txt | | wc", 1), "sh: there is no command on one side of a |\n");
    CHECK_TEXT(run(m, s, "| wc", 1), "sh: there is no command on one side of a |\n");
    CHECK_TEXT(run(m, s, "echo a > x.txt | cat", 1),
               "sh: only the first command can take < and only the last > when they are joined with |\n");
    CHECK(!m.exists("C:\\docs\\x.txt"));
    CHECK_TEXT(run(m, s, "a|b|c|d|e|f|g", 1), "sh: too many commands joined with |\n");
    // A command that fails in the middle: its error is shown, the next one
    // gets nothing, and the line's result is the last command's.
    CHECK_TEXT(run(m, s, "cat nothing.txt | wc -l"), "cat: C:\\docs\\nothing.txt: not found\n0\n");
    CHECK(run(m, s, "ls -a C:\\").find("pipe") == std::string::npos);

    // This terminal cannot be waited on (the session has no task of its
    // own), so there is no keyboard input to read: the filters say so, and
    // more shows everything at once.
    CHECK_TEXT(run(m, s, "wc", 1), "wc: it needs the name of a file, or input from a | or a <\n");
    CHECK_TEXT(run(m, s, "cat", 1), "cat: it needs the name of a file\n");
    CHECK_TEXT(run(m, s, "more notes.txt"), "one\ntwo\nthree\n");
    CHECK(run(m, s, "cat many.txt | more").find("line 500\n") != std::string::npos);
}

/* ---- open, run, edit: what is asked of the desktop ---------------------------------- */

struct FakeDesktop final : sh::Desktop {
    std::string log;
    bool open(const char *full_path, char *why, size_t capacity) override
    {
        log += std::string("open ") + full_path + "\n";
        if (strstr(full_path, ".zip") != nullptr) {
            snprintf(why, capacity, "there is no program for .zip files yet");
            return false;
        }
        return true;
    }
    bool run(const char *program, char *why, size_t capacity) override
    {
        log += std::string("run ") + program + "\n";
        if (strcmp(program, "nothing") == 0) {
            snprintf(why, capacity, "there is no such program; run alone lists them");
            return false;
        }
        return true;
    }
    bool edit(const char *full_path, char *, size_t) override
    {
        log += std::string("edit ") + (full_path != nullptr ? full_path : "<new>") + "\n";
        return true;
    }
    int program_count() override { return 2; }
    const char *program_name(int index) override { return index == 0 ? "Editor" : "File manager"; }
};

void test_desktop_commands()
{
    Machine m;
    m.storage.make_folder("C:\\docs");
    sh::Session s(m.terminal, m.env, "C:\\docs");

    // Where there is no desktop (a console on a PC) the commands say so.
    CHECK_TEXT(run(m, s, "open a.txt", 1), "open: there is no desktop here to open it on\n");
    CHECK_TEXT(run(m, s, "run editor", 1), "run: there is no desktop here to open it on\n");
    CHECK_TEXT(run(m, s, "edit", 1), "edit: there is no desktop here to open it on\n");

    FakeDesktop desktop;
    m.env.desktop = &desktop;
    sh::Session d(m.terminal, m.env, "C:\\docs");
    // Paths are made whole from the folder the session is in.
    CHECK_TEXT(run(m, d, "open a.txt ..\\b.bmp"), "");
    CHECK_TEXT(run(m, d, "start ."), ""); // the DOS name
    CHECK(desktop.log == "open C:\\docs\\a.txt\nopen C:\\b.bmp\nopen C:\\docs\n");
    desktop.log.clear();
    CHECK_TEXT(run(m, d, "open x.zip", 1), "open: x.zip: there is no program for .zip files yet\n");
    CHECK_TEXT(run(m, d, "open", 1), "open: it needs the name of a file or a folder\n");
    desktop.log.clear();

    // run: a program's name, spaces and all; alone, the list.
    CHECK_TEXT(run(m, d, "run file manager"), "");
    CHECK_TEXT(run(m, d, "run \"File manager\""), "");
    CHECK_TEXT(run(m, d, "run nothing", 1), "run: nothing: there is no such program; run alone lists them\n");
    CHECK_TEXT(run(m, d, "run"), "The programs:\n  Editor\n  File manager\n");
    CHECK(desktop.log == "run file manager\nrun File manager\nrun nothing\n");
    desktop.log.clear();

    CHECK_TEXT(run(m, d, "edit"), "");
    CHECK_TEXT(run(m, d, "edit notes.txt D:\\x.txt"), "");
    CHECK(desktop.log == "edit <new>\nedit C:\\docs\\notes.txt\nedit D:\\x.txt\n");
}

/* ---- Folders: cd, pwd, ls, dir, mkdir, rmdir ---------------------------------------- */

void test_folders()
{
    Machine m;
    m.storage.make_folder("C:\\Documents");
    m.storage.make_folder("C:\\Documents\\Letters");
    m.storage.make_folder("C:\\.config");
    m.file("C:\\readme.txt", std::string(1234, 'r'));
    m.file("C:\\Documents\\notes.txt", "notes\n");
    m.file("C:\\Documents\\todo.txt", "to do\n");
    m.file("C:\\Documents\\picture.bmp", std::string(70000, 'p'));
    m.file("C:\\Documents\\.hidden", "h");
    m.file("C:\\Documents\\a\xC3\xB1o.txt", "x");
    sh::Session s(m.terminal, m.env);

    CHECK_TEXT(run(m, s, "pwd"), "C:\\\n");
    CHECK_TEXT(run(m, s, "cd"), "C:\\\n");
    CHECK_TEXT(run(m, s, "cd documents"), "");
    CHECK_TEXT(run(m, s, "pwd"), "C:\\documents\n");
    CHECK_TEXT(run(m, s, "cd Letters"), "");
    CHECK_TEXT(run(m, s, "chdir ../.."), "");
    CHECK(std::string(s.cwd()) == "C:\\");
    CHECK_TEXT(run(m, s, "cd /Documents/Letters"), "");
    CHECK_TEXT(run(m, s, "cd \\"), "");
    CHECK_TEXT(run(m, s, "cd nowhere", 1), "cd: nowhere: there is no such folder\n");
    CHECK_TEXT(run(m, s, "cd readme.txt", 1), "cd: readme.txt: there is no such folder\n");
    CHECK(std::string(s.cwd()) == "C:\\");
    // Drives.
    CHECK_TEXT(run(m, s, "d:"), "");
    CHECK(std::string(s.cwd()) == "D:\\");
    CHECK_TEXT(run(m, s, "e:", 1), "E:\\: there is no such drive\n");
    CHECK_TEXT(run(m, s, "cd c:\\Documents"), "");
    CHECK(std::string(s.cwd()) == "C:\\Documents");

    // ls: names across the terminal's width, a \ after folders, hidden
    // names only with -a. The column is as wide as the longest name, in
    // characters ("año.txt" is 7).
    CHECK_TEXT(run(m, s, "ls"), "Letters\\     notes.txt    todo.txt     picture.bmp  a\xC3\xB1o.txt\n");
    m.terminal.set_size(40, 24);
    CHECK_TEXT(run(m, s, "ls"), "Letters\\     notes.txt    todo.txt\npicture.bmp  a\xC3\xB1o.txt\n");
    CHECK_TEXT(run(m, s, "ls -a"), "Letters\\     notes.txt    todo.txt\npicture.bmp  .hidden      a\xC3\xB1o.txt\n");
    m.terminal.set_size(80, 24);
    CHECK_TEXT(run(m, s, "ls Letters"), "");
    CHECK_TEXT(run(m, s, "ls *.txt"), "notes.txt  todo.txt   a\xC3\xB1o.txt\n");
    CHECK_TEXT(run(m, s, "ls .*"), ".hidden\n");
    CHECK_TEXT(run(m, s, "ls n*.TXT"), "notes.txt\n");
    CHECK_TEXT(run(m, s, "ls notes.txt"), "notes.txt\n");
    CHECK_TEXT(run(m, s, "ls *.jpg", 1), "ls: *.jpg: nothing has that name\n");
    CHECK_TEXT(run(m, s, "ls nothing", 1), "ls: nothing: not found\n");
    CHECK_TEXT(run(m, s, "ls -x", 1), "ls: -x is not an option it knows\n");
    CHECK_TEXT(run(m, s, "ls *\\*.txt", 1), "ls: *\\*.txt: * and ? only work in the last name\n");
    // Several: each under its name.
    CHECK_TEXT(run(m, s, "ls / Letters"), "C:\\:\nDocuments\\  readme.txt\n\nC:\\Documents\\Letters:\n");

    // dir, and ls -l: sizes, totals, free space.
    fs::VolumeInfo info;
    m.storage.info('C', info);
    char free_text[32];
    snprintf(free_text, sizeof free_text, "%llu", static_cast<unsigned long long>(info.free_bytes));
    std::string free_grouped = free_text; // 6 digits: one comma
    free_grouped.insert(free_grouped.size() - 3, ",");
    const std::string listing = " Folder C:\\Documents\n\n"
                                " <DIR>           Letters\n"
                                "              6  notes.txt\n"
                                "              6  todo.txt\n"
                                "         70,000  picture.bmp\n"
                                "              1  a\xC3\xB1o.txt\n"
                                "      4 files           70,013 bytes\n"
                                "      1 folder         " + free_grouped + " bytes free\n";
    CHECK(free_grouped.size() == 7);
    CHECK_TEXT(run(m, s, "dir"), listing);
    CHECK_TEXT(run(m, s, "ls -l"), listing);
    CHECK_TEXT(run(m, s, "DIR c:/documents/"), " Folder C:\\documents" + listing.substr(20));
    CHECK_TEXT(run(m, s, "dir *.bmp"), " Folder C:\\Documents\n\n         70,000  picture.bmp\n"
                                       "      1 file            70,000 bytes\n      0 folders        " + free_grouped + " bytes free\n");
    CHECK_TEXT(run(m, s, "dir \\readme.txt"), "          1,234  readme.txt\n");
    CHECK(run(m, s, "dir -a").find("              1  .hidden\n") != std::string::npos);
    CHECK(run(m, s, "ls -la /").find(" <DIR>           .config\n") != std::string::npos);

    // mkdir and rmdir.
    CHECK_TEXT(run(m, s, "mkdir New \"Two words\""), "");
    CHECK(m.exists("C:\\Documents\\New") && m.exists("C:\\Documents\\Two words"));
    CHECK_TEXT(run(m, s, "md New", 1), "md: New: already exists\n");
    CHECK_TEXT(run(m, s, "mkdir a/b", 1), "mkdir: a/b: not found\n");
    CHECK_TEXT(run(m, s, "mkdir", 1), "mkdir: it needs a name for the folder\n");
    CHECK_TEXT(run(m, s, "rmdir New"), "");
    CHECK_TEXT(run(m, s, "rd \"Two words\""), "");
    CHECK(!m.exists("C:\\Documents\\New") && !m.exists("C:\\Documents\\Two words"));
    CHECK_TEXT(run(m, s, "rmdir notes.txt", 1), "rmdir: C:\\Documents\\notes.txt: it is not a folder\n");
    CHECK_TEXT(run(m, s, "rmdir nothing", 1), "rmdir: C:\\Documents\\nothing: not found\n");
    m.file("C:\\Documents\\Letters\\one.txt", "1");
    CHECK_TEXT(run(m, s, "rmdir Letters", 1), "rmdir: C:\\Documents\\Letters: the folder is not empty\n");
    // Not the folder the session is in, nor one above it, nor a drive.
    CHECK_TEXT(run(m, s, "rmdir .", 1), "rmdir: C:\\Documents: this session is in it\n");
    CHECK_TEXT(run(m, s, "rmdir /", 1), "rmdir: C:\\: a drive cannot be deleted\n");

    // df.
    const std::string df = run(m, s, "df");
    CHECK(df.find("Drive           Size           Used           Free  Name\n") == 0);
    CHECK(df.find("  RAM disk (emptied at restart)\n") != std::string::npos && df.find("\nD:    ") != std::string::npos);
}

/* ---- Files: cat, touch, cp, mv, rm -------------------------------------------------- */

void test_files()
{
    Machine m;
    m.storage.make_folder("C:\\docs");
    m.storage.make_folder("C:\\docs\\sub");
    m.storage.make_folder("C:\\docs\\sub\\deep");
    m.file("C:\\docs\\a.txt", "alpha\r\n");
    m.file("C:\\docs\\b.txt", "beta, with no line end");
    m.file("C:\\docs\\c.bmp", std::string(5000, 'c'));
    m.file("C:\\docs\\sub\\s.txt", "sub");
    m.file("C:\\docs\\sub\\deep\\d.txt", "deep");
    m.file("C:\\docs\\latin.txt", "a\xF1o\n"); // files are Latin-1, as the Editor writes them
    sh::Session s(m.terminal, m.env, "C:\\docs");
    CHECK(std::string(s.cwd()) == "C:\\docs");

    // cat and type.
    CHECK_TEXT(run(m, s, "cat a.txt"), "alpha\n");
    CHECK_TEXT(run(m, s, "type a.txt b.txt"), "alpha\nbeta, with no line end");
    CHECK_TEXT(run(m, s, "cat latin.txt"), "a\xC3\xB1o\n"); // shown as UTF-8
    CHECK_TEXT(run(m, s, "cat sub/deep/d.txt"), "deep");
    CHECK(run(m, s, "cat c.bmp") == std::string(5000, 'c'));
    CHECK_TEXT(run(m, s, "cat ?.txt"), "alpha\nbeta, with no line end");
    CHECK_TEXT(run(m, s, "cat nothing.txt", 1), "cat: C:\\docs\\nothing.txt: not found\n");
    CHECK_TEXT(run(m, s, "cat sub", 1), "cat: C:\\docs\\sub: it is a folder\n");
    CHECK_TEXT(run(m, s, "cat", 1), "cat: it needs the name of a file\n");
    CHECK_TEXT(run(m, s, "cat nothing.txt a.txt", 1), "cat: C:\\docs\\nothing.txt: not found\nalpha\n");

    // touch makes an empty file and leaves one that is there alone.
    CHECK_TEXT(run(m, s, "touch new.txt a.txt"), "");
    CHECK(m.contents("C:\\docs\\new.txt") == "" && m.contents("C:\\docs\\a.txt") == "alpha\r\n");
    CHECK_TEXT(run(m, s, "touch no/such/place.txt", 1), "touch: no/such/place.txt: not found\n");

    // cp: to a new name, over a file, into a folder, several, to another
    // drive.
    CHECK_TEXT(run(m, s, "cp a.txt copy.txt"), "");
    CHECK(m.contents("C:\\docs\\copy.txt") == "alpha\r\n" && m.contents("C:\\docs\\a.txt") == "alpha\r\n");
    CHECK_TEXT(run(m, s, "copy b.txt copy.txt"), "");
    CHECK(m.contents("C:\\docs\\copy.txt") == "beta, with no line end");
    CHECK_TEXT(run(m, s, "cp c.bmp sub"), "");
    CHECK(m.contents("C:\\docs\\sub\\c.bmp") == std::string(5000, 'c'));
    CHECK_TEXT(run(m, s, "cp a.txt b.txt d:\\"), "");
    CHECK(m.contents("D:\\a.txt") == "alpha\r\n" && m.contents("D:\\b.txt") == "beta, with no line end");
    CHECK_TEXT(run(m, s, "cp *.txt sub/deep"), "");
    CHECK(m.contents("C:\\docs\\sub\\deep\\new.txt") == "" && m.contents("C:\\docs\\sub\\deep\\latin.txt") == "a\xF1o\n");
    CHECK_TEXT(run(m, s, "cp a.txt a.txt", 1), "cp: C:\\docs\\a.txt: it would be copied onto itself\n");
    CHECK_TEXT(run(m, s, "cp a.txt A.TXT", 1), "cp: C:\\docs\\a.txt: it would be copied onto itself\n");
    CHECK_TEXT(run(m, s, "cp a.txt .", 1), "cp: C:\\docs\\a.txt: it would be copied onto itself\n");
    CHECK_TEXT(run(m, s, "cp nothing.txt x.txt", 1), "cp: C:\\docs\\nothing.txt: not found\n");
    CHECK_TEXT(run(m, s, "cp a.txt b.txt x.txt", 1), "cp: x.txt: several things can only go into a folder\n");
    CHECK_TEXT(run(m, s, "cp *.txt x.txt", 1), "cp: x.txt: several things can only go into a folder\n");
    CHECK_TEXT(run(m, s, "cp a.txt", 1), "cp: it needs what to take and where to put it\n");
    CHECK_TEXT(run(m, s, "cp a.txt no\\such\\x.txt", 1), "cp: C:\\docs\\no\\such\\x.txt: not found\n");
    CHECK_TEXT(run(m, s, "cp sub d:\\", 1), "cp: C:\\docs\\sub: it is a folder (cp -r copies folders)\n");
    // A copy that does not fit leaves nothing half-written behind.
    m.file("C:\\big.bin", std::string(60000, 'b'));
    CHECK_TEXT(run(m, s, "cp \\big.bin d:\\big.bin", 1), "cp: D:\\big.bin: the disk is full\n");
    CHECK(!m.exists("D:\\big.bin"));
    CHECK_TEXT(run(m, s, "ls -a d:\\"), "a.txt  b.txt\n");

    // cp -r: a folder and all in it, to a new name and into a folder.
    CHECK_TEXT(run(m, s, "cp -r sub d:\\tree"), "");
    CHECK(m.contents("D:\\tree\\s.txt") == "sub" && m.contents("D:\\tree\\deep\\d.txt") == "deep");
    CHECK(m.contents("D:\\tree\\deep\\a.txt") == "alpha\r\n" && m.contents("D:\\tree\\c.bmp") == std::string(5000, 'c'));
    CHECK_TEXT(run(m, s, "mkdir \\backup"), "");
    CHECK_TEXT(run(m, s, "copy -r sub \\backup"), "");
    CHECK(m.contents("C:\\backup\\sub\\deep\\d.txt") == "deep");
    CHECK_TEXT(run(m, s, "cp -r sub sub\\deep", 1), "cp: C:\\docs\\sub: a folder cannot be copied into itself\n");
    CHECK_TEXT(run(m, s, "cp -r sub sub", 1), "cp: C:\\docs\\sub: a folder cannot be copied into itself\n");
    CHECK_TEXT(run(m, s, "cp -r \\ d:\\all", 1), "cp: C:\\: a whole drive is copied as X:\\*\n");

    // mv: a new name, into a folder, onto a file, a change of capitals, a
    // folder, another drive.
    CHECK_TEXT(run(m, s, "mv copy.txt moved.txt"), "");
    CHECK(!m.exists("C:\\docs\\copy.txt") && m.contents("C:\\docs\\moved.txt") == "beta, with no line end");
    CHECK_TEXT(run(m, s, "ren moved.txt Moved.TXT"), "");
    CHECK(run(m, s, "ls M*") == "Moved.TXT\n");
    CHECK_TEXT(run(m, s, "move Moved.TXT sub"), "");
    CHECK(m.contents("C:\\docs\\sub\\Moved.TXT") == "beta, with no line end" && !m.exists("C:\\docs\\Moved.TXT"));
    CHECK_TEXT(run(m, s, "mv new.txt a.txt"), ""); // onto a file: it takes its place
    CHECK(m.contents("C:\\docs\\a.txt") == "" && !m.exists("C:\\docs\\new.txt"));
    CHECK_TEXT(run(m, s, "mv sub\\deep Deep2"), "");
    CHECK(m.contents("C:\\docs\\Deep2\\d.txt") == "deep" && !m.exists("C:\\docs\\sub\\deep"));
    CHECK_TEXT(run(m, s, "rename Deep2 deep2"), "");
    CHECK(run(m, s, "ls d*") == "deep2\\\n");
    CHECK_TEXT(run(m, s, "mv b.txt d:\\across.txt"), "");
    CHECK(m.contents("D:\\across.txt") == "beta, with no line end" && !m.exists("C:\\docs\\b.txt"));
    CHECK_TEXT(run(m, s, "mv *.txt sub"), "");
    CHECK_TEXT(run(m, s, "ls"), "sub\\    deep2\\  c.bmp\n");
    CHECK(m.contents("C:\\docs\\sub\\latin.txt") == "a\xF1o\n");
    CHECK_TEXT(run(m, s, "mv nothing x", 1), "mv: C:\\docs\\nothing: not found\n");
    CHECK_TEXT(run(m, s, "mv c.bmp c.bmp", 1), "mv: C:\\docs\\c.bmp: it is already there\n");
    CHECK_TEXT(run(m, s, "mv sub sub\\inside", 1), "mv: C:\\docs\\sub: a folder cannot be moved into itself\n");
    CHECK_TEXT(run(m, s, "mv sub d:\\", 1), "mv: C:\\docs\\sub: a folder goes to another drive with cp -r, then rm -r\n");
    CHECK_TEXT(run(m, s, "mv \\docs \\elsewhere", 1), "mv: C:\\docs: this session is in it\n");
    CHECK_TEXT(run(m, s, "mv c.bmp deep2 nowhere", 1), "mv: nowhere: several things can only go into a folder\n");

    // rm: files, by pattern, folders only with -r.
    CHECK_TEXT(run(m, s, "rm c.bmp"), "");
    CHECK(!m.exists("C:\\docs\\c.bmp"));
    CHECK_TEXT(run(m, s, "rm c.bmp", 1), "rm: C:\\docs\\c.bmp: not found\n");
    CHECK_TEXT(run(m, s, "rm -f c.bmp"), "");
    CHECK_TEXT(run(m, s, "rm *.bmp", 1), "rm: *.bmp: nothing has that name\n");
    CHECK_TEXT(run(m, s, "rm sub", 1), "rm: C:\\docs\\sub: it is a folder (rm -r deletes a folder and all in it)\n");
    CHECK_TEXT(run(m, s, "del sub\\*.txt"), "");
    CHECK_TEXT(run(m, s, "ls sub"), "c.bmp\n");
    CHECK_TEXT(run(m, s, "rm", 1), "rm: it needs the name of what to delete\n");
    CHECK_TEXT(run(m, s, "rm -r d:\\tree \\backup"), "");
    CHECK(!m.exists("D:\\tree") && !m.exists("C:\\backup"));
    CHECK_TEXT(run(m, s, "rm -r .", 1), "rm: C:\\docs: this session is in it\n");
    CHECK_TEXT(run(m, s, "rm -r \\", 1), "rm: C:\\: a drive cannot be deleted\n");
    // A star does not take hidden names, so the settings folder survives
    // "del *".
    m.storage.make_folder("C:\\.config");
    m.file("C:\\.config\\desktop.ini", "x");
    CHECK_TEXT(run(m, s, "cd \\"), "");
    CHECK_TEXT(run(m, s, "rm -r *"), "");
    CHECK_TEXT(run(m, s, "ls -a"), ".config\\\n");
    CHECK(m.contents("C:\\.config\\desktop.ini") == "x");
}

/* ---- Processes and the machine ------------------------------------------------------ */

void test_processes()
{
    Machine m;
    sh::Session s(m.terminal, m.env);
    const proc::Pid desktop = m.procs.spawn("desktop", proc::kNone, proc::kSystem | proc::kProtected);
    const proc::Pid editor = m.procs.spawn("editor", desktop, proc::kWindowed);
    const proc::Pid game = m.procs.spawn("culebra", desktop, proc::kWindowed | proc::kRespawn);
    CHECK(desktop == 1 && editor == 2 && game == 3);

    CHECK_TEXT(run(m, s, "ps"), "  PID  PPID  FLAGS     TIME    MEM  NAME\n"
                                "    1     0  SP--     0.0s      -  desktop\n"
                                "    2     1  ---W     0.0s      -  editor\n"
                                "    3     1  --RW     0.0s      -  culebra\n"
                                "S system  P protected  R restarts  W has windows\n");
    CHECK_TEXT(run(m, s, "tasklist"), run(m, s, "ps"));

    // The figures: processor time and memory, in ps and in top. Where top
    // cannot wait for a key (here; or sent to a file) it shows one table of
    // the totals and returns.
    m.procs.add_cpu(editor, 4200000);
    m.procs.add_memory(editor, 640 * 1024);
    m.procs.add_cpu(game, 187000000);
    m.procs.add_memory(game, 1200 * 1024);
    CHECK(run(m, s, "ps").find("    2     1  ---W     4.2s   640K  editor\n"
                               "    3     1  --RW     3:07   1.2M  culebra\n") != std::string::npos);
    const std::string table = run(m, s, "top");
    CHECK(table.rfind("3 processes, on for 0:00:0", 0) == 0);
    CHECK(table.find("\n\n  PID     TIME    MEM  NAME\n"
                     "    1     0.0s      -  desktop\n"
                     "    2     4.2s   640K  editor\n"
                     "    3     3:07   1.2M  culebra\n") != std::string::npos);
    CHECK(table.find("q to leave") == std::string::npos);
    CHECK_TEXT(run(m, s, "top -n 2"), table);
    CHECK_TEXT(run(m, s, "top > t.txt"), "");
    CHECK(m.contents("C:\\t.txt").find("    3     3:07   1.2M  culebra") != std::string::npos);
    CHECK_TEXT(run(m, s, "top -x", 1), "top: -x: it takes -n and a number of screens\n");
    CHECK_TEXT(run(m, s, "top -n 0", 1), "top: -n: it takes -n and a number of screens\n");
    m.procs.add_cpu(editor, 0);

    // kill leaves the signal on the row, for whoever runs the process.
    CHECK_TEXT(run(m, s, "kill 2"), "");
    CHECK(m.procs.take_signals(editor) == static_cast<uint8_t>(proc::Signal::Term));
    CHECK_TEXT(run(m, s, "kill -9 2"), "");
    CHECK(m.procs.take_signals(editor) == static_cast<uint8_t>(proc::Signal::Kill));
    CHECK_TEXT(run(m, s, "kill -kill 2 3"), "");
    CHECK(m.procs.take_signals(editor) == static_cast<uint8_t>(proc::Signal::Kill) &&
          m.procs.take_signals(game) == static_cast<uint8_t>(proc::Signal::Kill));
    CHECK_TEXT(run(m, s, "taskkill -f 3"), "");
    CHECK(m.procs.take_signals(game) == static_cast<uint8_t>(proc::Signal::Kill));
    CHECK_TEXT(run(m, s, "kill -TERM 3"), "");
    CHECK_TEXT(run(m, s, "kill -15 3"), "");
    CHECK(m.procs.take_signals(game) == static_cast<uint8_t>(proc::Signal::Term));
    CHECK_TEXT(run(m, s, "kill 1", 1), "kill: 1: it is protected\n");
    CHECK_TEXT(run(m, s, "kill 99 2", 1), "kill: 99: there is no such process\n");
    CHECK(m.procs.take_signals(editor) != 0); // the rest of the list was still done
    CHECK_TEXT(run(m, s, "kill editor", 1), "kill: editor: that is not the number of a process\n");
    CHECK_TEXT(run(m, s, "kill 0", 1), "kill: 0: that is not the number of a process\n");
    CHECK_TEXT(run(m, s, "kill -HUP 2", 1), "kill: -HUP: the signals are -TERM (15) and -KILL (9)\n");
    CHECK_TEXT(run(m, s, "kill", 1), "kill: it needs the number of a process (ps lists them)\n");
    CHECK_TEXT(run(m, s, "kill -9", 1), "kill: it needs the number of a process (ps lists them)\n");

    // A full table fits in the session's buffer.
    while (m.procs.spawn("a process with a long name", desktop, proc::kWindowed) != proc::kNone) {
    }
    CHECK(m.procs.count() == proc::Table::kMaxProcesses);
    const std::string full = run(m, s, "ps");
    CHECK(full.find("   32     1  ---W     0.0s      -  a process with a long n\n") != std::string::npos);

    m.system.free_internal_ram = 123456;
    m.system.free_external_ram = 7654321;
    CHECK_TEXT(run(m, s, "free"), "Internal RAM       123,456 bytes free\nExternal RAM     7,654,321 bytes free\n");
    CHECK_TEXT(run(m, s, "mem"), run(m, s, "free"));
    CHECK(run(m, s, "uptime").rfind("On for 0:00:0", 0) == 0);

    // A session that was given less says so.
    sh::Env bare;
    sh::Session poor(m.terminal, bare);
    CHECK_TEXT(run(m, poor, "ps", 1), "ps: there is no process table here\n");
    CHECK_TEXT(run(m, poor, "kill 1", 1), "kill: there is no process table here\n");
    CHECK_TEXT(run(m, poor, "free", 1), "free: the machine does not say\n");
    CHECK_TEXT(run(m, poor, "ls", 1), "ls: there are no drives\n");
    CHECK_TEXT(run(m, poor, "cd x", 1), "cd: x: there is no such folder\n");
    CHECK_TEXT(run(m, poor, "echo still here"), "still here\n");
}

/* ---- A session at a terminal -------------------------------------------------------- */

void test_typed_session()
{
    Machine m;
    m.storage.make_folder("C:\\docs");
    m.file("C:\\docs\\b.txt", "no line end");
    sh::Session s(m.terminal, m.env);

    s.greet();
    CHECK_TEXT(m.screen.take(), std::string("Chinchilla ") + shell::kVersion + "\nhelp lists the commands.\n\n");
    // The prompt comes with the first step and only once.
    CHECK(s.step() && s.step());
    CHECK_TEXT(m.screen.take(), "C:\\> ");
    // Typing is echoed by the TTY; nothing runs before Enter.
    m.terminal.input("cd do");
    CHECK(s.step());
    CHECK_TEXT(m.screen.take(), "cd do");
    m.terminal.input("cs\r");
    CHECK(s.step() && s.step());
    CHECK_TEXT(m.screen.take(), "cs\nC:\\docs> ");
    // A file that does not end its last line: the prompt still starts a line.
    m.terminal.input("type b.txt\r");
    CHECK(s.step() && s.step());
    CHECK_TEXT(m.screen.take(), "type b.txt\nno line end\nC:\\docs> ");
    // An empty line: a new prompt.
    m.terminal.input("\r");
    CHECK(s.step() && s.step());
    CHECK_TEXT(m.screen.take(), "\nC:\\docs> ");
    // Ctrl+C on a half-typed line: the line is gone, a new prompt.
    m.terminal.input("rm -r everyth\x03");
    CHECK(s.step() && s.step());
    CHECK_TEXT(m.screen.take(), "rm -r everyth^C\nC:\\docs> ");
    CHECK(m.exists("C:\\docs\\b.txt"));
    // An earlier line with the up arrow, changed and run.
    m.terminal.input("\x1b[A\x7f\x7f\x7f\x7f\x7f" "nothing\r");
    CHECK(s.step() && s.step());
    CHECK(m.screen.take().find("type: C:\\docs\\nothing: not found\nC:\\docs> ") != std::string::npos);
    // Names beyond ASCII, typed as UTF-8.
    m.terminal.input("touch a\xC3\xB1o.txt\rls\r");
    CHECK(s.step() && s.step() && s.step());
    CHECK(m.screen.take().find("b.txt    a\xC3\xB1o.txt\nC:\\docs> ") != std::string::npos);
    CHECK(m.exists("C:\\docs\\a\xC3\xB1o.txt"));
    // Two lines typed ahead run in order.
    m.terminal.input("cd \\\recho done\r");
    CHECK(s.step() && s.step() && s.step());
    CHECK_TEXT(m.screen.take(), "cd \\\necho done\nC:\\> done\nC:\\> ");
    // Ctrl+D ends the session; so does exit.
    m.terminal.input("\x04");
    CHECK(!s.step() && s.ended() && !s.step());

    sh::Session second(m.terminal, m.env);
    m.terminal.input("exit\recho not run\r");
    CHECK(!second.step() && second.ended());
    m.screen.take();
    CHECK(!second.step());
    CHECK_TEXT(m.screen.take(), "");
    char rest[64];
    CHECK(m.terminal.read(rest, sizeof rest) == 13); // the line typed after exit is still the terminal's

    // The terminal going away ends it too.
    sh::Session third(m.terminal, m.env);
    m.terminal.hang_up();
    CHECK(!third.step() && third.ended());
}

/* ---- A session on a task of its own -------------------------------------------------- */

void test_session_on_a_thread()
{
    using Clock = std::chrono::steady_clock;
    Machine m;
    host::ThreadLock storage_lock, tty_lock;
    host::ThreadEvent typed, ui_done;
    m.storage.set_lock(&storage_lock);
    tty::Tty terminal(m.screen, &tty_lock, &typed);
    m.file("C:\\big.txt", std::string(200000, 'x'));
    const proc::Pid desktop = m.procs.spawn("desktop", proc::kNone, proc::kSystem | proc::kProtected);
    const proc::Pid editor = m.procs.spawn("editor", desktop, proc::kWindowed);

    // The UI thread: it owns the process table, and does what is posted to
    // its queue.
    host::ThreadQueue queue;
    std::atomic<bool> ui_stop{false};
    std::atomic<uint32_t> ui_ran{0};
    std::thread::id ui_id;
    std::thread ui([&] {
        ui_id = std::this_thread::get_id();
        msg::Inbox inbox(queue, &m.procs);
        while (!ui_stop) {
            queue.sleep(20000);
            inbox.deliver();
            ui_ran = inbox.delivered();
        }
    });

    sh::QueueCall call(queue, ui_done);
    m.env.ui = &call;
    sh::Session s(terminal, m.env);
    std::thread session([&] { s.run(); });

    const auto wait_for = [&](const char *text) {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        std::string seen;
        while (Clock::now() < deadline) {
            seen += m.screen.take();
            if (seen.find(text) != std::string::npos) {
                return seen;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        printf("FAILED: never saw \"%s\"; saw:\n%s\n", text, seen.c_str());
        failures++;
        return seen;
    };

    wait_for("C:\\> ");
    // ps and kill are done by the UI thread, and the session waits for them.
    terminal.input("ps\r");
    const std::string list = wait_for("has windows\nC:\\> ");
    CHECK(list.find("    2     1  ---W     0.0s      -  editor\n") != std::string::npos);
    CHECK(ui_ran == 1);
    terminal.input("kill 2\rkill 1\r");
    wait_for("kill: 1: it is protected\nC:\\> ");
    CHECK(ui_ran == 3);

    // Ctrl+C stops a command that is running: the file is 200,000 bytes and
    // the terminal is slow, so the whole of it would take seconds.
    m.screen.slow = true;
    terminal.input("type big.txt\r");
    wait_for("xxxxxxxx");
    terminal.input("\x03");
    // The ^C, then the prompt straight under it.
    const std::string cut = wait_for("^C\nC:\\> ");
    m.screen.slow = false;
    CHECK(cut.size() < 100000);
    // The session is as good as new afterwards.
    terminal.input("echo alive\r");
    wait_for("alive\nC:\\> ");

    // With a task of its own a command can read what is typed: lines until
    // Ctrl+D on an empty one.
    terminal.input("cat | wc -l\r");
    terminal.input("uno\rdos\rtres\r\x04");
    wait_for("\n3\nC:\\> ");
    terminal.input("cat > typed.txt\rse\xC3\xB1or\r\x04");
    wait_for("C:\\> ");
    CHECK(m.contents("C:\\typed.txt") == "se\xF1or\n");
    // Ctrl+C ends the reading and the whole line.
    terminal.input("cat | wc -l\rabc\r");
    wait_for("abc\n");
    terminal.input("\x03");
    const std::string after = wait_for("^C\nC:\\> ");
    CHECK(after.find("1\n") == std::string::npos); // wc did not run

    // more stops at each screen (24 rows: 23 lines and its own) and waits
    // for a key from the terminal, though its input is a pipe.
    {
        std::string many;
        for (int i = 1; i <= 60; i++) {
            many += "row " + std::to_string(i) + "\n";
        }
        m.file("C:\\many.txt", many);
    }
    terminal.input("cat many.txt | more\r");
    std::string page = wait_for("-- More --");
    CHECK(page.find("row 23\n") != std::string::npos && page.find("row 24\n") == std::string::npos);
    terminal.input(" ");
    page = wait_for("-- More --");
    CHECK(page.find("row 46\n") != std::string::npos && page.find("row 47\n") == std::string::npos);
    terminal.input("\r"); // Enter: one line more
    page = wait_for("-- More --");
    CHECK(page.find("row 47\n") != std::string::npos && page.find("row 48\n") == std::string::npos);
    terminal.input("q");
    page = wait_for("C:\\> ");
    CHECK(page.find("row 48") == std::string::npos);

    // top has a task to wait on here: it clears the screen, shows each
    // process's share of the processor since its last look, and comes back
    // every two seconds until q. -n 1: one screen and back to the prompt.
    terminal.input("top -n 1\r");
    page = wait_for("C:\\> ");
    CHECK(page.find("(q to leave)\n\n  PID   CPU     TIME    MEM  NAME\n") != std::string::npos);
    CHECK(page.find("    1   0.0%    0.0s      -  desktop\n") != std::string::npos);
    CHECK(page.find("\x1b[2J") != std::string::npos);
    terminal.input("top\r");
    wait_for("NAME\n");
    wait_for("desktop\n");
    terminal.input("q");
    wait_for("C:\\> ");
    terminal.input("top\r");
    wait_for("desktop\n");
    terminal.input("\x03");
    wait_for("C:\\> ");

    terminal.input("exit\r");
    session.join();
    CHECK(s.ended());
    ui_stop = true;
    queue.wake();
    ui.join();
    // The signal was left on the row by the UI thread.
    CHECK(m.procs.take_signals(editor) == static_cast<uint8_t>(proc::Signal::Term));
    m.storage.set_lock(nullptr);
}

} // namespace

int main()
{
    test_paths();
    test_lines();
    test_folders();
    test_files();
    test_pipes();
    test_desktop_commands();
    test_processes();
    test_typed_session();
    test_session_on_a_thread();
    if (failures != 0) {
        printf("%d checks failed\n", failures);
        return 1;
    }
    printf("sh: all checks passed\n");
    return 0;
}
