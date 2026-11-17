// The commands that read their input and write something about it: what a
// pipe is for (docs/journal/2026-11-17-pipes.md). Each takes its input from
// the files named on the line, or else from the pipe, a < file, or the
// keyboard. Inside, text is Latin-1, as it is in files.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "commands.hpp"

namespace sh {

namespace {

using fs::FsError;

// Input for a filter: a named file, or the session's input.
class Source {
public:
    explicit Source(Session &s) : s_(s) {}
    ~Source() { close(); }
    Source(const Source &) = delete;
    Source &operator=(const Source &) = delete;

    // False, having said why, if the file cannot be opened.
    bool open(const char *command, const char *typed)
    {
        close();
        char full[fs::kMaxPath];
        if (s_.env().storage == nullptr || !s_.resolve(typed, full)) {
            s_.fail(command, typed, "the name is too long");
            return false;
        }
        const FsError e = s_.env().storage->open(full, fs::OpenMode::Read, file_);
        if (e != FsError::Ok) {
            s_.fail(command, typed, e);
            return false;
        }
        return true;
    }
    void close()
    {
        if (file_.open()) {
            s_.env().storage->close(file_);
        }
        used_ = at_ = 0;
        ended_ = false;
    }

    // The next line, without its end; a line longer than the room is given
    // in pieces. False at the end of the input, or once Ctrl+C was typed.
    bool line(char *out, int capacity, int &length)
    {
        length = 0;
        bool any = false;
        for (;;) {
            if (at_ == used_) {
                if (ended_ || s_.stopped() || !fill()) {
                    return any;
                }
            }
            const char c = block_[at_++];
            any = true;
            if (c == '\n') {
                break;
            }
            if (c == '\r') {
                continue;
            }
            out[length++] = c;
            if (length == capacity - 1) {
                break;
            }
        }
        out[length] = 0;
        return true;
    }

    // The next piece as it is, 0 at the end.
    size_t block(const char *&data)
    {
        if (at_ == used_ && (ended_ || s_.stopped() || !fill())) {
            return 0;
        }
        data = block_ + at_;
        const size_t size = used_ - at_;
        at_ = used_;
        return size;
    }

private:
    bool fill()
    {
        size_t got = 0;
        if (file_.open()) {
            if (s_.env().storage->read(file_, block_, sizeof block_, got) != FsError::Ok) {
                got = 0;
            }
        } else {
            got = s_.read_input(block_, sizeof block_);
        }
        used_ = got;
        at_ = 0;
        ended_ = got == 0;
        return got > 0;
    }

    Session &s_;
    fs::File file_;
    char block_[512];
    size_t used_ = 0, at_ = 0;
    bool ended_ = false;
};

// For a filter given no file: is there any input to read?
bool nothing_to_read(Session &s, const char *command)
{
    if (s.has_input()) {
        return false;
    }
    s.fail(command, nullptr, "it needs the name of a file, or input from a | or a <");
    return true;
}

// Runs `one` over each file named from argv[first] on, or once over the
// session's input when none is.
template <typename One> int over_inputs(Session &s, int argc, char **argv, int first, One one)
{
    Source in(s);
    if (first >= argc) {
        if (nothing_to_read(s, argv[0])) {
            return 1;
        }
        one(in, nullptr);
        return 0;
    }
    int result = 0;
    for (int i = first; i < argc && !s.stopped(); i++) {
        if (!in.open(argv[0], argv[i])) {
            result = 1;
            continue;
        }
        one(in, argv[i]);
    }
    return result;
}

char lower(char c)
{
    const uint8_t u = static_cast<uint8_t>(c);
    if (u >= 'A' && u <= 'Z') {
        return static_cast<char>(u + 32);
    }
    // Latin-1 capitals with accents, but for the multiplication sign.
    return (u >= 0xC0 && u <= 0xDE && u != 0xD7) ? static_cast<char>(u + 32) : c;
}

bool contains(const char *line, const char *text, bool any_case)
{
    const size_t n = strlen(text);
    for (const char *p = line; *p != 0 || n == 0; p++) {
        size_t i = 0;
        while (i < n && p[i] != 0 && (any_case ? lower(p[i]) == lower(text[i]) : p[i] == text[i])) {
            i++;
        }
        if (i == n) {
            return true;
        }
        if (*p == 0) {
            break;
        }
    }
    return false;
}

// What was typed (UTF-8) as the Latin-1 the files hold.
void to_latin1(const char *utf8, char *out, int capacity)
{
    int used = 0;
    for (const uint8_t *p = reinterpret_cast<const uint8_t *>(utf8); *p != 0 && used < capacity - 1; p++) {
        if ((*p == 0xC2 || *p == 0xC3) && (p[1] & 0xC0) == 0x80) {
            out[used++] = static_cast<char>(((*p & 0x03) << 6) | (p[1] & 0x3F));
            p++;
        } else {
            out[used++] = *p < 0x80 ? static_cast<char>(*p) : '?';
        }
    }
    out[used] = 0;
}

void put_line(Session &s, const char *line, int length)
{
    s.write_latin1(line, static_cast<size_t>(length));
    s.print("\n");
}

// "-n 5", "-n5" or "-5" at argv[1]: the number, and argv moved past it.
// False, having said why, for something else that starts with '-'.
bool count_option(Session &s, int argc, char **argv, int &first, int &count)
{
    first = 1;
    if (argc < 2 || argv[1][0] != '-' || argv[1][1] == 0) {
        return true;
    }
    const char *digits = argv[1] + 1;
    if (*digits == 'n') {
        digits++;
        if (*digits == 0 && argc > 2) {
            digits = argv[2];
            first = 2;
        }
    }
    char *end = nullptr;
    const long value = strtol(digits, &end, 10);
    if (*digits == 0 || *end != 0 || value < 0 || value > 100000) {
        s.fail(argv[0], argv[1], "it takes -n and a number of lines");
        return false;
    }
    count = static_cast<int>(value);
    first++;
    return true;
}

} // namespace

/* ---- cat without a file ------------------------------------------------------------ */

int copy_input(Session &s)
{
    Source in(s);
    const char *data = nullptr;
    for (size_t size = in.block(data); size > 0; size = in.block(data)) {
        s.write_latin1(data, size);
    }
    return 0;
}

/* ---- grep -------------------------------------------------------------------------- */

int cmd_grep(Session &s, int argc, char **argv)
{
    char found[8];
    if (!options(s, argc, argv, "ivnc", found, sizeof found)) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs the text to look for");
    }
    char text[128];
    to_latin1(argv[1], text, sizeof text);
    const bool any_case = has(found, 'i'), invert = has(found, 'v'), numbers = has(found, 'n'),
               only_count = has(found, 'c');
    const bool several = argc > 3;
    bool any = false;
    const int result = over_inputs(s, argc, argv, 2, [&](Source &in, const char *name) {
        char line[256];
        int length = 0;
        long number = 0, matches = 0;
        while (in.line(line, sizeof line, length)) {
            number++;
            if (contains(line, text, any_case) == invert) {
                continue;
            }
            matches++;
            if (only_count) {
                continue;
            }
            if (several && name != nullptr) {
                s.printf("%s:", name);
            }
            if (numbers) {
                s.printf("%ld:", number);
            }
            put_line(s, line, length);
        }
        if (only_count) {
            if (several && name != nullptr) {
                s.printf("%s:", name);
            }
            s.printf("%ld\n", matches);
        }
        any = any || matches > 0;
    });
    return result != 0 ? 2 : (any ? 0 : 1); // as grep answers: 1 for "not found"
}

/* ---- wc ---------------------------------------------------------------------------- */

int cmd_wc(Session &s, int argc, char **argv)
{
    char found[8];
    if (!options(s, argc, argv, "lwc", found, sizeof found)) {
        return 1;
    }
    const bool all = found[0] == 0;
    return over_inputs(s, argc, argv, 1, [&](Source &in, const char *name) {
        unsigned long lines = 0, words = 0, bytes = 0;
        bool in_word = false;
        const char *data = nullptr;
        for (size_t size = in.block(data); size > 0; size = in.block(data)) {
            bytes += size;
            for (size_t i = 0; i < size; i++) {
                const char c = data[i];
                lines += c == '\n';
                const bool space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
                words += !space && !in_word;
                in_word = !space;
            }
        }
        // One number alone is written bare, for the next command to read.
        const unsigned long values[3] = {lines, words, bytes};
        const char letters[4] = "lwc";
        int asked = 0;
        for (int i = 0; i < 3; i++) {
            asked += all || has(found, letters[i]);
        }
        bool any = false;
        for (int i = 0; i < 3; i++) {
            if (all || has(found, letters[i])) {
                s.printf(asked == 1 ? "%s%lu" : "%s%7lu", any ? " " : "", values[i]);
                any = true;
            }
        }
        s.printf("%s%s\n", name != nullptr ? " " : "", name != nullptr ? name : "");
    });
}

/* ---- head and tail ----------------------------------------------------------------- */

int cmd_head(Session &s, int argc, char **argv)
{
    int first = 1, count = 10;
    if (!count_option(s, argc, argv, first, count)) {
        return 1;
    }
    const bool several = argc - first > 1;
    return over_inputs(s, argc, argv, first, [&](Source &in, const char *name) {
        if (several && name != nullptr) {
            s.printf("==> %s <==\n", name);
        }
        char line[256];
        int length = 0;
        for (int n = 0; n < count && in.line(line, sizeof line, length);) {
            // A long line comes in pieces; only its last piece ends it.
            const bool whole = length < static_cast<int>(sizeof line) - 1;
            s.write_latin1(line, static_cast<size_t>(length));
            if (whole) {
                s.print("\n");
                n++;
            }
        }
    });
}

int cmd_tail(Session &s, int argc, char **argv)
{
    int first = 1, count = 10;
    if (!count_option(s, argc, argv, first, count)) {
        return 1;
    }
    const bool several = argc - first > 1;
    return over_inputs(s, argc, argv, first, [&](Source &in, const char *name) {
        if (several && name != nullptr) {
            s.printf("==> %s <==\n", name);
        }
        // The end of the input is kept in the session's working room as it
        // goes by, so the last lines can be shown without reading twice:
        // 2 KB of them at most.
        char *keep = s.buffer();
        const size_t room = Session::kBufferSize;
        size_t used = 0;
        const char *data = nullptr;
        for (size_t size = in.block(data); size > 0; size = in.block(data)) {
            if (size >= room) {
                memcpy(keep, data + size - room, room);
                used = room;
            } else {
                if (used + size > room) {
                    const size_t drop = used + size - room;
                    memmove(keep, keep + drop, used - drop);
                    used -= drop;
                }
                memcpy(keep + used, data, size);
                used += size;
            }
        }
        // Back from the end over `count` lines.
        size_t start = used;
        int lines = 0;
        while (start > 0) {
            if (keep[start - 1] == '\n' && start != used && ++lines == count) {
                break;
            }
            start--;
        }
        if (count > 0 && used > start) {
            s.write_latin1(keep + start, used - start);
            if (keep[used - 1] != '\n') {
                s.print("\n");
            }
        }
    });
}

/* ---- more -------------------------------------------------------------------------- */

int cmd_more(Session &s, int argc, char **argv)
{
    tty::Tty &terminal = s.terminal();
    // A screen at a time only when there is someone to press a key: the
    // output goes to the terminal, and the terminal can be waited on.
    const bool pages = !s.output_redirected() && terminal.can_wait();
    const int page = terminal.rows() > 2 ? terminal.rows() - 1 : 23;
    const int columns = terminal.columns() > 8 ? terminal.columns() : 80;
    bool quit = false;
    return over_inputs(s, argc, argv, 1, [&](Source &in, const char *) {
        char line[256];
        int length = 0, shown = 0, left = page;
        while (!quit && in.line(line, sizeof line, length)) {
            put_line(s, line, length);
            shown += 1 + (length > 0 ? (length - 1) / columns : 0); // a long line takes several rows
            if (!pages || shown < left) {
                continue;
            }
            // The keys are read from the terminal itself, one at a time,
            // whatever the input is.
            s.print("-- More --");
            terminal.set_raw(true);
            char key = 0;
            const int got = terminal.read(&key, 1, 0xFFFFFFFF);
            terminal.set_raw(false);
            s.print("\r          \r");
            if (got != 1 || key == 'q' || key == 'Q') {
                quit = true;
            }
            shown = 0;
            left = (key == '\r' || key == '\n') ? 1 : page; // Enter: one more line
        }
    });
}

} // namespace sh
