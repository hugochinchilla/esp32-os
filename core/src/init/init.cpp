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

Init *g_the = nullptr;

} // namespace

Init *the() { return g_the; }

const char *Init::default_text()
{
    return "# What Chinchilla starts, from the top down. Read once, when the\r\n"
           "# machine starts. Without this file the lines below are used and\r\n"
           "# the file is written again.\r\n"
           "#\r\n"
           "#   WHAT:ACTION, as in a Unix /etc/inittab.\r\n"
           "#\r\n"
           "#   WHAT    a part of the system (desktop, taskbar, inetd, console),\r\n"
           "#           or a program or a file, as Start > Run... takes it\r\n"
           "#   ACTION  once     started\r\n"
           "#           respawn  started, and started again whenever it ends\r\n"
           "#                    (the taskbar can be)\r\n"
           "#           off      not started\r\n"
           "#   # ...   a comment\r\n"
           "#\r\n"
           "# inetd is the network services: the remote screen, its web page,\r\n"
           "# and telnet and updates if network.ini switches them on. The\r\n"
           "# desktop and its taskbar are started even if no line names them.\r\n"
           "# With once, a taskbar that is ended stays away until run taskbar.\r\n"
           "\r\n"
           "desktop:once\r\n"
           "taskbar:respawn\r\n"
           "inetd:once\r\n"
           "\r\n"
           "# The command shell on the serial port, on the same wire as the\r\n"
           "# debug link (tools/dbg.py console, or any serial terminal):\r\n"
           "console:off\r\n"
           "\r\n"
           "# terminal:once\r\n";
}

Init::Init(proc::Table &procs) : procs_(procs)
{
    if (procs_.count() == 0) {
        pid_ = procs_.spawn("init", proc::kNone, proc::kSystem | proc::kProtected);
    } else {
        pid_ = proc::kRoot;
    }
    procs_.set_supervisor(this);
    g_the = this;
}

Init::~Init()
{
    if (procs_.supervisor() == this) {
        procs_.set_supervisor(nullptr);
    }
    if (g_the == this) {
        g_the = nullptr;
    }
}

Init::Unit *Init::unit(const char *name)
{
    for (int i = 0; i < count_; i++) {
        if (same(units_[i].name, name)) {
            return &units_[i];
        }
    }
    return nullptr;
}

bool Init::has(const char *name) const { return const_cast<Init *>(this)->unit(name) != nullptr; }

bool Init::add(const char *name, StartFn start, void *context, bool needed, Again again)
{
    if (count_ == kMaxUnits || name == nullptr || start == nullptr || has(name)) {
        return false;
    }
    Unit &u = units_[count_++];
    u.name = name;
    u.start = start;
    u.context = context;
    u.needed = needed;
    u.started = false;
    u.again = again;
    u.respawn = again == Again::Does;
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

/* ---- What comes back ------------------------------------------------------- */

// The brake: false when this thing has just ended quickly once too often.
bool Init::may_restart(Unit &unit, const proc::Process &process)
{
    uint32_t now = 0;
    if (!procs_.now(now)) {
        return true;
    }
    const uint32_t lived = now - process.born; // the clock wraps; the difference does not
    if (lived >= kQuickUs) {
        unit.quick = 0;
        return true;
    }
    if (++unit.quick < kQuickLimit) {
        return true;
    }
    unit.quick = 0;
    given_up_++;
    log("init: %s keeps ending as soon as it starts; not started again", unit.name);
    return false;
}

// From inside the table's exit(): nothing is started here, only noted.
void Init::ended(const proc::Process &process)
{
    for (int i = 0; i < count_; i++) {
        Unit &u = units_[i];
        if (u.pid != process.pid) {
            continue;
        }
        u.pid = proc::kNone;
        u.started = false;
        if (u.respawn && may_restart(u, process) && due_count_ < kMaxUnits) {
            due_[due_count_++] = &u;
        }
        return;
    }
}

int Init::restart_due()
{
    // The list is emptied first: a start function may end a process too.
    Unit *now[kMaxUnits];
    const int n = due_count_;
    for (int i = 0; i < n; i++) {
        now[i] = due_[i];
    }
    due_count_ = 0;
    int started = 0;
    for (int i = 0; i < n; i++) {
        if (!now[i]->started) {
            log("init: %s ended; starting it again", now[i]->name);
            started += start(*now[i]) ? 1 : 0;
        }
    }
    return started;
}

bool Init::start_again(const char *name)
{
    Unit *u = unit(name);
    if (u == nullptr || u->started) {
        return false;
    }
    u->quick = 0;
    if (!start(*u)) {
        return false;
    }
    // Before the file has been done at all: its line, when it comes, finds
    // it started and is not a line gone wrong.
    u->early = !done_;
    return true;
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
    // What can come back is watched by its process, which has its name:
    // init is told when it ends, to start it again or to know it has gone.
    unit.pid = unit.again != Again::No ? procs_.find_by_name(unit.name) : proc::kNone;
    if (unit.respawn) {
        if (const proc::Process *p = procs_.find(unit.pid)) {
            procs_.set_flags(unit.pid, p->flags | proc::kRespawn);
        } else {
            log("init: %s has no process to watch; it will not come back", unit.name);
        }
    }
    return true;
}

// What is needed goes before anything else, wherever the file has its line
// or if it has none: a service or a program may count on the desktop being
// there. False if something needed did not start. `off`: a thing the line
// being done says is not to be started, which may only now come to exist
// (the desktop adds its taskbar).
bool Init::needed_first(const char *before, const char *off)
{
    bool all = true;
    for (int i = 0; i < count_; i++) {
        Unit &u = units_[i];
        if (off != nullptr && same(u.name, off) && !u.started) {
            u.off = true;
        }
        if (u.needed && !u.started && !u.off) {
            if (!same(u.name, before)) { // its own line: nothing to say
                log("init: %s goes before %s", u.name, before);
            }
            if (start(u)) {
                u.early = true;
            } else {
                all = false;
            }
        }
    }
    return all;
}

// A part of the system, by its name: started once, started and started
// again, or left alone.
bool Init::part(const char *name, Action action)
{
    Unit *u = unit(name);
    if (u == nullptr) {
        // What is needed may bring it: the desktop adds its taskbar.
        if (!needed_first(name, action == Action::Off ? name : nullptr)) {
            return false;
        }
        u = unit(name);
    }
    if (u == nullptr) {
        log("init: nothing here is called %s", name);
        return false;
    }
    if (action == Action::Off) {
        if (u->needed && u->again == Again::No) {
            log("init: %s cannot be off", name);
            return false;
        }
        if (u->started) {
            log("init: %s is already started; its off line goes higher up", name);
            return false;
        }
        u->off = true;
        log("init: %s is off", name);
        return true;
    }
    const bool respawn = action == Action::Respawn;
    if (respawn && u->again == Again::No) {
        log("init: %s cannot be started again when it ends; started once", name);
    }
    const bool comes_back = respawn && u->again != Again::No;
    if (u->early) {
        u->early = false; // its own line, a little late
        if (u->respawn != comes_back) {
            // Started already, the other way: its process is told.
            u->respawn = comes_back;
            if (const proc::Process *p = procs_.find(u->pid)) {
                procs_.set_flags(p->pid, comes_back ? p->flags | proc::kRespawn
                                                    : p->flags & static_cast<uint8_t>(~proc::kRespawn));
            }
        }
        return true;
    }
    u->respawn = comes_back;
    u->off = false;
    if (!u->needed && !needed_first(name)) {
        return false;
    }
    return start(*u);
}

// A program or a file, as Start > Run... takes it.
bool Init::program(const char *what)
{
    if (run_ == nullptr) {
        log("init: nothing to run %s with", what);
        return false;
    }
    if (!needed_first(what)) {
        return false;
    }
    char why[80] = "";
    bool opened;
    {
        const proc::Scope as(&procs_, pid_);
        opened = run_(what, why, sizeof(why), run_context_);
    }
    if (!opened) {
        log("init: run %s: %s", what, why);
        return false;
    }
    log("init: ran %s", what);
    return true;
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
    // WHAT:ACTION. The action is after the last colon: a path has its own.
    const char *colon = strrchr(text, ':');
    if (colon == nullptr) {
        log("init: not understood: %s", text);
        return false;
    }
    const char *word = colon + 1;
    while (is_space(*word)) {
        word++;
    }
    Action action;
    if (same(word, "once")) {
        action = Action::Once;
    } else if (same(word, "respawn")) {
        action = Action::Respawn;
    } else if (same(word, "off")) {
        action = Action::Off;
    } else {
        log("init: not understood: %s (once, respawn or off after the colon)", text);
        return false;
    }
    char what[kMaxLine];
    size_t length = static_cast<size_t>(colon - text);
    while (length > 0 && is_space(text[length - 1])) {
        length--;
    }
    memcpy(what, text, length);
    what[length] = 0;
    if (length == 0) {
        log("init: %s what? (%s)", word, text);
        return false;
    }
    // A part of the system if there is one of that name (or the desktop
    // brings one); anything else is a program or a file.
    if (unit(what) == nullptr && !needed_first(what, action == Action::Off ? what : nullptr)) {
        return false; // the desktop may bring it (its taskbar); off is marked before it starts
    }
    if (unit(what) != nullptr) {
        return part(what, action);
    }
    if (action == Action::Off) {
        return true; // a program that is off: nothing to do
    }
    if (action == Action::Respawn) {
        log("init: only a part of the system can be started again: %s", what);
        return false;
    }
    return program(what);
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
        if (units_[i].needed && !units_[i].started && !units_[i].off) {
            log("init: no line starts %s; started anyway", units_[i].name);
            if (!start(units_[i])) {
                failed_++;
            }
        }
    }
    done_ = true;
    const int failed = failed_;
    failed_ = 0;
    return failed;
}

int Init::run_text(const char *text, size_t length)
{
    booting_ = true;
    take(text, length);
    const int failed = finish();
    booting_ = false;
    return failed;
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
        booting_ = true;
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
        const int failed = finish();
        booting_ = false;
        return failed;
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
