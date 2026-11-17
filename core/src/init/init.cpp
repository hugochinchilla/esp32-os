#include "core/init/init.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace init {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t'; }

// The same word, whatever the case.
bool same(const char *a, const char *b)
{
    for (; *a != 0 && *b != 0; a++, b++) {
        const char x = *a >= 'A' && *a <= 'Z' ? static_cast<char>(*a + 32) : *a;
        const char y = *b >= 'A' && *b <= 'Z' ? static_cast<char>(*b + 32) : *b;
        if (x != y) {
            return false;
        }
    }
    return *a == *b;
}

} // namespace

const char *Init::default_text()
{
    return "# What Chinchilla starts, from the top down. Read once, when the\r\n"
           "# machine starts. Without this file the lines below are used and\r\n"
           "# the file is written again.\r\n"
           "#\r\n"
           "#   start NAME   a part of the system: desktop, inetd, console\r\n"
           "#   run WHAT     a program or a file, as Start > Run... takes it\r\n"
           "#   # ...        a comment\r\n"
           "#\r\n"
           "# inetd is the network services: the remote screen, its web page,\r\n"
           "# and telnet and updates if network.ini switches them on. The\r\n"
           "# desktop is started even if no line asks for it.\r\n"
           "\r\n"
           "start desktop\r\n"
           "start inetd\r\n"
           "\r\n"
           "# The command shell on the serial port, on the same wire as the\r\n"
           "# debug link (tools/dbg.py console, or any serial terminal):\r\n"
           "# start console\r\n"
           "\r\n"
           "# run terminal\r\n";
}

Init::Init(proc::Table &procs) : procs_(procs)
{
    if (procs_.count() == 0) {
        pid_ = procs_.spawn("init", proc::kNone, proc::kSystem | proc::kProtected);
    } else {
        pid_ = proc::kRoot;
    }
    procs_.set_gave_up(&Init::gave_up, this);
}

bool Init::add(const char *name, StartFn start, void *context, bool needed)
{
    if (count_ == kMaxUnits || name == nullptr || start == nullptr) {
        return false;
    }
    Unit &u = units_[count_++];
    u.name = name;
    u.start = start;
    u.context = context;
    u.needed = needed;
    u.started = false;
    return true;
}

bool Init::started(const char *name) const
{
    for (int i = 0; i < count_; i++) {
        if (same(units_[i].name, name)) {
            return units_[i].started;
        }
    }
    return false;
}

void Init::log(const char *format, ...)
{
    if (log_ == nullptr) {
        return;
    }
    char text[kMaxLine + 64];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log_(text, log_context_);
}

void Init::gave_up(const char *name, void *context)
{
    static_cast<Init *>(context)->log("init: %s keeps ending as soon as it starts; not started again", name);
}

bool Init::start(Unit &unit)
{
    if (unit.started) {
        log("init: %s is already started", unit.name);
        return false;
    }
    // What it makes is init's child.
    const proc::Scope as(&procs_, pid_);
    if (!unit.start(unit.context)) {
        log("init: %s did not start", unit.name);
        return false;
    }
    unit.started = true;
    log("init: started %s", unit.name);
    return true;
}

// What is needed goes before anything else, wherever the file has its line
// or if it has none: a service or a program may count on the desktop being
// there. False if something needed did not start.
bool Init::needed_first(const char *before)
{
    bool all = true;
    for (int i = 0; i < count_; i++) {
        if (units_[i].needed && !units_[i].started) {
            log("init: %s goes before %s", units_[i].name, before);
            if (start(units_[i])) {
                units_[i].early = true;
            } else {
                all = false;
            }
        }
    }
    return all;
}

// One line, already without its end. True if there was nothing to do or it
// was done.
bool Init::line(const char *text)
{
    while (is_space(*text)) {
        text++;
    }
    if (*text == 0 || *text == '#') {
        return true;
    }
    char verb[8];
    size_t n = 0;
    while (text[n] != 0 && !is_space(text[n]) && n < sizeof(verb) - 1) {
        verb[n] = text[n];
        n++;
    }
    verb[n] = 0;
    const char *rest = text + n;
    if (*rest != 0 && !is_space(*rest)) {
        log("init: not understood: %s", text);
        return false;
    }
    while (is_space(*rest)) {
        rest++;
    }
    if (*rest == 0) {
        log("init: %s what? (%s)", verb, text);
        return false;
    }
    if (same(verb, "start")) {
        for (int i = 0; i < count_; i++) {
            if (same(units_[i].name, rest)) {
                if (units_[i].early) {
                    units_[i].early = false; // its own line, a little late
                    return true;
                }
                if (!units_[i].needed && !needed_first(rest)) {
                    return false;
                }
                return start(units_[i]);
            }
        }
        log("init: nothing here is called %s", rest);
        return false;
    }
    if (same(verb, "run")) {
        if (run_ == nullptr) {
            log("init: nothing to run %s with", rest);
            return false;
        }
        if (!needed_first(rest)) {
            return false;
        }
        char why[80] = "";
        bool opened;
        {
            const proc::Scope as(&procs_, pid_);
            opened = run_(rest, why, sizeof(why), run_context_);
        }
        if (!opened) {
            log("init: run %s: %s", rest, why);
            return false;
        }
        log("init: ran %s", rest);
        return true;
    }
    log("init: not understood: %s", text);
    return false;
}

// Text as it comes, in pieces of any size: each whole line is done as soon
// as its end has arrived.
void Init::take(const char *text, size_t length)
{
    for (size_t at = 0; at < length; at++) {
        if (text[at] == '\n') {
            end_line();
        } else if (pending_ < sizeof(line_) - 1) {
            line_[pending_++] = text[at];
        } else {
            cut_ = true;
        }
    }
}

void Init::end_line()
{
    size_t n = pending_;
    while (n > 0 && (line_[n - 1] == '\r' || is_space(line_[n - 1]))) {
        n--;
    }
    line_[n] = 0;
    if (cut_) {
        log("init: a line is too long: %.40s...", line_);
        failed_++;
    } else if (!line(line_)) {
        failed_++;
    }
    pending_ = 0;
    cut_ = false;
}

// The end of the text: a last line without an end, then what must be
// started whatever the text says.
int Init::finish()
{
    if (pending_ > 0 || cut_) {
        end_line();
    }
    for (int i = 0; i < count_; i++) {
        if (units_[i].needed && !units_[i].started) {
            log("init: no line starts %s; started anyway", units_[i].name);
            if (!start(units_[i])) {
                failed_++;
            }
        }
    }
    const int failed = failed_;
    failed_ = 0;
    return failed;
}

int Init::run_text(const char *text, size_t length)
{
    take(text, length);
    return finish();
}

int Init::boot(const fs::Storage &storage, const char *path)
{
    had_file_ = false;
    wrote_file_ = false;
    // The file is read a piece at a time and is never whole in memory. It
    // stays open, for reading, while its lines are done.
    fs::File file;
    const ports::FsError opened = storage.open(path, ports::OpenMode::Read, file);
    if (opened == ports::FsError::Ok) {
        had_file_ = true;
        size_t total = 0;
        while (total < kMaxFile) {
            char piece[64];
            size_t got = 0;
            if (storage.read(file, piece, sizeof(piece), got) != ports::FsError::Ok || got == 0) {
                break;
            }
            take(piece, got);
            total += got;
        }
        storage.close(file);
        return finish();
    }
    const char *usual = default_text();
    const int failed = run_text(usual, strlen(usual));
    if (opened == ports::FsError::NotFound) {
        // So there is a file to edit. After everything has started: writing
        // to the flash is not what the screen should wait for.
        char folder[fs::kMaxPath];
        snprintf(folder, sizeof(folder), "%s", path);
        if (char *slash = strrchr(folder, '\\')) {
            *slash = 0;
            storage.make_folder(folder); // already there is fine
        }
        wrote_file_ = storage.write_file(path, usual, strlen(usual)) == ports::FsError::Ok;
        if (wrote_file_) {
            log("init: %s written, with what is started without it", path);
        }
    } else {
        log("init: %s cannot be read; the usual things started", path);
    }
    return failed;
}

} // namespace init
