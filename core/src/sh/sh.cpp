#include "core/sh/sh.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "commands.hpp"

namespace sh {

/* ---- Work for the UI task --------------------------------------------------------- */

void QueueCall::deliver(const msg::Message &m)
{
    Job *job = static_cast<Job *>(m.context);
    job->fn(job->context);
    ports::Event *done = job->done; // the job is the waiter's, gone once it sees `finished`
    job->finished = true;
    done->set();
}

void QueueCall::run(void (*fn)(void *context), void *context)
{
    Job job{fn, context, &done_, {false}};
    // No time limit on either wait: the job lives on this stack, so leaving
    // before the UI task has run it would hand it a dead pointer.
    queue_.post({&deliver, &job, 0, 0, proc::kNone}, msg::kForever);
    while (!job.finished) {
        done_.wait(1000);
    }
}

/* ---- Small helpers for the commands ----------------------------------------------- */

bool has(const char *found, char letter) { return strchr(found, letter) != nullptr; }

bool options(Session &s, int &argc, char **argv, const char *allowed, char *found, int capacity)
{
    int count = 0, taken = 0;
    found[0] = 0;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-' || arg[1] == 0) {
            break;
        }
        taken++;
        if (strcmp(arg, "--") == 0) {
            break;
        }
        for (const char *p = arg + 1; *p != 0; p++) {
            if (strchr(allowed, *p) == nullptr) {
                s.printf("%s: -%c is not an option it knows\n", argv[0], *p);
                return false;
            }
            if (count < capacity - 1) {
                found[count++] = *p;
                found[count] = 0;
            }
        }
    }
    for (int i = 1; i + taken < argc; i++) {
        argv[i] = argv[i + taken];
    }
    argc -= taken;
    return true;
}

const char *grouped(uint64_t value, char *out, int capacity)
{
    // By hand: the board's small printf may not know 64-bit numbers.
    char digits[24];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    int at = 0;
    for (int i = count - 1; i >= 0 && at < capacity - 2; i--) {
        out[at++] = digits[i];
        if (i > 0 && i % 3 == 0) {
            out[at++] = ',';
        }
    }
    out[at] = 0;
    return out;
}

/* ---- Commands about the shell itself ---------------------------------------------- */

namespace {

int cmd_echo(Session &s, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        s.printf("%s%s", i > 1 ? " " : "", argv[i]);
    }
    s.print("\n");
    return 0;
}

int cmd_cls(Session &s, int, char **)
{
    s.clear_screen();
    return 0;
}

int cmd_exit(Session &s, int, char **)
{
    s.end();
    return 0;
}

const Command *find(const char *name)
{
    for (int i = 0; i < kCommandCount; i++) {
        if (strcmp(kCommands[i].name, name) == 0) {
            return &kCommands[i];
        }
    }
    return nullptr;
}

int cmd_help(Session &s, int argc, char **argv)
{
    if (argc > 1) {
        const Command *c = find(argv[1]);
        if (c == nullptr) {
            return s.fail("help", argv[1], "there is no such command");
        }
        while (c > kCommands && c->usage == nullptr) {
            c--; // another name: the command itself is before it
        }
        if (c->usage == nullptr || c->summary == nullptr) {
            return 1; // the table's first entry is a command, so never
        }
        s.printf("%s\n  %s\n", c->usage, c->summary);
        if (c + 1 < kCommands + kCommandCount && c[1].usage == nullptr) {
            s.print("  Also:");
            for (const Command *other = c + 1; other < kCommands + kCommandCount && other->usage == nullptr; other++) {
                s.printf(" %s", other->name);
            }
            s.print("\n");
        }
        return 0;
    }
    for (int i = 0; i < kCommandCount; i++) {
        if (kCommands[i].usage == nullptr) {
            continue;
        }
        char names[48];
        int at = snprintf(names, sizeof names, "%s", kCommands[i].name);
        for (int j = i + 1; j < kCommandCount && kCommands[j].usage == nullptr; j++) {
            at += snprintf(names + at, sizeof names - static_cast<size_t>(at), ", %s", kCommands[j].name);
        }
        s.printf("  %-22s %s\n", names, kCommands[i].summary);
    }
    s.print("help NAME shows how a command is written. A: to Z: changes drive.\n"
            "Paths take \\ and /. * and ? stand for any letters. Ctrl+C stops a command.\n"
            "A | B gives B what A wrote. > F writes the file F, >> F adds to it, < F reads.\n");
    return 0;
}

} // namespace

const Command kCommands[] = {
    {"ls", &cmd_ls, "ls [-l] [-a] [PATH...]", "List a folder. -l with sizes, -a with hidden names"},
    {"dir", &cmd_dir, nullptr, ""},
    {"cd", &cmd_cd, "cd [FOLDER]", "Go to a folder; alone, show where you are"},
    {"chdir", &cmd_cd, nullptr, ""},
    {"pwd", &cmd_pwd, "pwd", "Show the folder you are in"},
    {"cat", &cmd_cat, "cat [FILE...]", "Show what is in a file"},
    {"type", &cmd_cat, nullptr, ""},
    {"cp", &cmd_cp, "cp [-r] FROM... TO", "Copy files; -r copies folders too"},
    {"copy", &cmd_cp, nullptr, ""},
    {"mv", &cmd_mv, "mv FROM... TO", "Move or rename"},
    {"move", &cmd_mv, nullptr, ""},
    {"ren", &cmd_mv, nullptr, ""},
    {"rename", &cmd_mv, nullptr, ""},
    {"rm", &cmd_rm, "rm [-r] PATH...", "Delete for good; -r deletes folders and all in them"},
    {"del", &cmd_rm, nullptr, ""},
    {"erase", &cmd_rm, nullptr, ""},
    {"mkdir", &cmd_mkdir, "mkdir FOLDER...", "Make a folder"},
    {"md", &cmd_mkdir, nullptr, ""},
    {"rmdir", &cmd_rmdir, "rmdir FOLDER...", "Delete an empty folder"},
    {"rd", &cmd_rmdir, nullptr, ""},
    {"touch", &cmd_touch, "touch FILE...", "Make an empty file"},
    {"more", &cmd_more, "more [FILE...]", "Show text a screen at a time: Space, Enter, q"},
    {"grep", &cmd_grep, "grep [-i] [-v] [-n] [-c] TEXT [FILE...]", "Show the lines that have the text"},
    {"find", &cmd_grep, nullptr, ""},
    {"findstr", &cmd_grep, nullptr, ""},
    {"head", &cmd_head, "head [-n N] [FILE...]", "Show the first lines (10)"},
    {"tail", &cmd_tail, "tail [-n N] [FILE...]", "Show the last lines (10)"},
    {"wc", &cmd_wc, "wc [-l] [-w] [-c] [FILE...]", "Count lines, words and bytes"},
    {"df", &cmd_df, "df", "Show the drives and their free space"},
    {"echo", &cmd_echo, "echo [TEXT...]", "Write the text"},
    {"ps", &cmd_ps, "ps", "List the processes"},
    {"tasklist", &cmd_ps, nullptr, ""},
    {"top", &cmd_top, "top [-n N]", "Show the busiest processes, every two seconds; q leaves"},
    {"kill", &cmd_kill, "kill [-TERM | -KILL | -9] PID...", "Ask a process to end; -9 ends it without asking"},
    {"taskkill", &cmd_kill, nullptr, ""},
    {"free", &cmd_free, "free", "Show the free memory"},
    {"mem", &cmd_free, nullptr, ""},
    {"uptime", &cmd_uptime, "uptime", "Show how long the machine has been on"},
    {"date", &cmd_date, "date", "Show the date and the time"},
    {"time", &cmd_date, nullptr, ""},
    {"dmesg", &cmd_dmesg, "dmesg", "Show what the system has logged"},
    {"ver", &cmd_ver, "ver", "Show the version of Chinchilla"},
    {"uname", &cmd_ver, nullptr, ""},
    {"open", &cmd_open, "open PATH...", "Open a file with its program, or a folder"},
    {"start", &cmd_open, nullptr, ""},
    {"run", &cmd_run, "run [PROGRAM]", "Start a program; alone, list them"},
    {"edit", &cmd_edit, "edit [FILE...]", "Open the Editor, with a file if one is named"},
    {"ted", &cmd_ted, "ted [FILE]", "Edit a text file here, in the terminal"},
    {"cls", &cmd_cls, "cls", "Clear the screen"},
    {"clear", &cmd_cls, nullptr, ""},
    {"help", &cmd_help, "help [NAME]", "Show this list, or how one command is written"},
    {"?", &cmd_help, nullptr, ""},
    {"exit", &cmd_exit, "exit", "End the session"},
    {"logout", &cmd_exit, nullptr, ""},
};
const int kCommandCount = static_cast<int>(sizeof kCommands / sizeof kCommands[0]);

/* ---- The session ------------------------------------------------------------------ */

Session::Session(tty::Tty &tty, const Env &env, const char *start_folder) : tty_(tty), env_(env)
{
    snprintf(cwd_, sizeof cwd_, "C:\\");
    char full[fs::kMaxPath];
    if (path::resolve(cwd_, start_folder, full, sizeof full)) {
        snprintf(cwd_, sizeof cwd_, "%s", full); // not checked: the drives may not be there yet
    }
}

void Session::say(const char *data, size_t size)
{
    if (size > 0) {
        tty_.write(data, size);
        at_line_start_ = data[size - 1] == '\n';
    }
}

void Session::write(const char *data, size_t size)
{
    if (!out_.open()) {
        say(data, size);
        return;
    }
    // To a file: the UTF-8 the commands write becomes the Latin-1 that text
    // files hold. A character outside Latin-1 becomes a question mark.
    char latin1[128];
    size_t used = 0;
    for (size_t i = 0; i < size; i++) {
        const uint8_t c = static_cast<uint8_t>(data[i]);
        if (used == sizeof latin1) {
            to_file(latin1, used);
            used = 0;
        }
        if (out_lead_ != 0) {
            latin1[used++] = (c & 0xC0) == 0x80 ? static_cast<char>(((out_lead_ & 0x03) << 6) | (c & 0x3F)) : '?';
            out_lead_ = 0;
        } else if (c < 0x80) {
            latin1[used++] = static_cast<char>(c);
        } else if (c == 0xC2 || c == 0xC3) {
            out_lead_ = c;
        } else if ((c & 0xC0) != 0x80) {
            latin1[used++] = '?'; // the start of a longer character; its other bytes are dropped
        }
    }
    to_file(latin1, used);
}

void Session::to_file(const char *latin1, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (out_used_ == static_cast<int>(sizeof out_buffer_)) {
            flush_file();
        }
        out_buffer_[out_used_++] = latin1[i];
    }
}

void Session::flush_file()
{
    if (out_used_ > 0 && out_.open() && out_error_ == fs::FsError::Ok) {
        out_error_ = env_.storage->write(out_, out_buffer_, static_cast<size_t>(out_used_));
    }
    out_used_ = 0;
}

bool Session::has_input() const { return in_.open() || tty_.can_wait(); }

size_t Session::read_input(char *out, size_t capacity)
{
    if (capacity == 0 || cut_) {
        return 0;
    }
    if (in_.open()) {
        size_t got = 0;
        if (tty_.interrupted() || env_.storage->read(in_, out, capacity, got) != fs::FsError::Ok) {
            return 0;
        }
        return got;
    }
    if (!tty_.can_wait()) {
        return 0;
    }
    // Typed: a line at a time, UTF-8 from the TTY.
    char line[tty::Tty::kReadSize];
    const size_t room = capacity < sizeof line ? capacity : sizeof line;
    const int count = tty_.read(line, room, msg::kForever);
    if (count == tty::kInterrupted) {
        cut_ = true; // the TTY has forgotten it by telling us: the line still stops
        return 0;
    }
    if (count <= 0) {
        return 0; // Ctrl+D, or the terminal went away
    }
    size_t used = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t c = static_cast<uint8_t>(line[i]);
        if ((c == 0xC2 || c == 0xC3) && i + 1 < count) {
            out[used++] = static_cast<char>(((c & 0x03) << 6) | (static_cast<uint8_t>(line[++i]) & 0x3F));
        } else {
            out[used++] = c < 0x80 ? static_cast<char>(c) : '?';
        }
    }
    return used;
}

void Session::write_latin1(const char *data, size_t size)
{
    if (out_.open()) {
        to_file(data, size); // a file holds Latin-1: as it is
        return;
    }
    char out[128];
    size_t used = 0;
    for (size_t i = 0; i < size; i++) {
        const uint8_t c = static_cast<uint8_t>(data[i]);
        if (used + 2 > sizeof out) {
            write(out, used);
            used = 0;
        }
        if (c < 0x80) {
            out[used++] = static_cast<char>(c);
        } else {
            out[used++] = static_cast<char>(0xC0 | (c >> 6));
            out[used++] = static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    write(out, used);
}

void Session::print(const char *text) { write(text, strlen(text)); }

void Session::clear_screen()
{
    print("\x1b[2J\x1b[H"); // erase the screen, cursor to the top left
    at_line_start_ = true;
}

void Session::printf(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (length > 0) {
        write(text, static_cast<size_t>(length) < sizeof text ? static_cast<size_t>(length) : sizeof text - 1);
    }
}

int Session::fail(const char *command, const char *what, const char *why)
{
    // Always on the terminal: an error is not what the next command or the
    // file was meant to get.
    char text[256];
    const int length = what != nullptr ? snprintf(text, sizeof text, "%s: %s: %s\n", command, what, why)
                                       : snprintf(text, sizeof text, "%s: %s\n", command, why);
    if (length > 0) {
        say(text, static_cast<size_t>(length) < sizeof text ? static_cast<size_t>(length) : sizeof text - 1);
    }
    return 1;
}

int Session::fail(const char *command, const char *what, fs::FsError error)
{
    return fail(command, what, fs::error_text(error));
}

void Session::on_ui(void (*fn)(void *context), void *context)
{
    if (env_.ui != nullptr) {
        env_.ui->run(fn, context);
    } else {
        fn(context);
    }
}

bool Session::set_cwd(const char *full_path)
{
    if (env_.storage == nullptr) {
        return false;
    }
    if (strlen(full_path) <= 3) {
        if (env_.storage->volume(fs::path::drive(full_path)) == nullptr) {
            return false;
        }
    } else {
        fs::DirEntry entry;
        if (env_.storage->stat(full_path, entry) != fs::FsError::Ok || !entry.folder) {
            return false;
        }
    }
    snprintf(cwd_, sizeof cwd_, "%s", full_path);
    return true;
}

// Finds the next < > or >> outside quotes in `text`, takes the name after it
// out (the place is filled with spaces) and copies it to `name`. Returns the
// sign ('<', '>', or 'a' for >>), 0 if there is none, -1 for one without a
// name.
static int take_redirection(char *text, char *name, int capacity)
{
    bool quoted = false;
    for (char *p = text; *p != 0; p++) {
        if (*p == '"') {
            quoted = !quoted;
        }
        if (quoted || (*p != '<' && *p != '>')) {
            continue;
        }
        int sign = *p;
        *p++ = ' ';
        if (sign == '>' && *p == '>') {
            sign = 'a';
            *p++ = ' ';
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        int used = 0;
        bool inside = false;
        while (*p != 0 && (inside || (*p != ' ' && *p != '\t' && *p != '<' && *p != '>'))) {
            if (*p == '"') {
                inside = !inside;
            } else if (used < capacity - 1) {
                name[used++] = *p;
            }
            *p++ = ' ';
        }
        name[used] = 0;
        return used > 0 ? sign : -1;
    }
    return 0;
}

int Session::execute(const char *line)
{
    static std::atomic<uint16_t> sessions{0};
    if (id_ == 0) {
        id_ = ++sessions;
    }
    cut_ = false;
    char work[tty::Tty::kReadSize + 1];
    snprintf(work, sizeof work, "%s", line);

    // The line is cut at each | outside quotes.
    char *stages[kMaxStages];
    int count = 0;
    stages[count++] = work;
    bool quoted = false;
    for (char *p = work; *p != 0; p++) {
        if (*p == '"') {
            quoted = !quoted;
        } else if (*p == '|' && !quoted) {
            *p = 0;
            if (count == kMaxStages) {
                return fail("sh", nullptr, "too many commands joined with |");
            }
            stages[count++] = p + 1;
        }
    }
    if (count == 1) {
        const int result = run_stage(work, true, true, nullptr, nullptr);
        return result == kNotRun ? 1 : result;
    }
    for (int i = 0; i < count; i++) {
        const char *p = stages[i];
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == 0) {
            return fail("sh", nullptr, "there is no command on one side of a |");
        }
    }
    if (env_.storage == nullptr) {
        return fail("sh", nullptr, "a | needs a drive to work on");
    }
    // One command at a time, as on DOS: each writes all it has to say into a
    // hidden file, and the next reads that file. Two files, used in turn,
    // in the root of the drive the session is on.
    char pipes[2][32];
    for (int i = 0; i < 2; i++) {
        snprintf(pipes[i], sizeof pipes[i], "%c:\\.~pipe%u%c.tmp", cwd_[0], static_cast<unsigned>(id_), 'a' + i);
    }
    int result = 0;
    for (int i = 0; i < count && !stopped(); i++) {
        result = run_stage(stages[i], i == 0, i == count - 1, i > 0 ? pipes[(i - 1) % 2] : nullptr,
                           i < count - 1 ? pipes[i % 2] : nullptr);
        if (result == kNotRun) {
            break; // said why; what follows would work on nothing
        }
    }
    env_.storage->remove(pipes[0]);
    env_.storage->remove(pipes[1]);
    return result == kNotRun ? 1 : result;
}

// One command of the line, with its input and output set up and put back.
int Session::run_stage(char *text, bool first, bool last, const char *pipe_in, const char *pipe_out)
{
    char in_name[fs::kMaxPath] = "", out_name[fs::kMaxPath] = "";
    bool append = false;
    for (;;) {
        char name[fs::kMaxPath];
        const int sign = take_redirection(text, name, sizeof name);
        if (sign == 0) {
            break;
        }
        if (sign < 0) {
            fail("sh", nullptr, "a file name is missing after < or >");
            return kNotRun;
        }
        char *into = sign == '<' ? in_name : out_name;
        if (!resolve(name, into)) {
            fail("sh", name, "the name is too long");
            return kNotRun;
        }
        append = sign == 'a' ? true : (sign == '>' ? false : append);
    }
    if ((in_name[0] != 0 && !first) || (out_name[0] != 0 && !last)) {
        fail("sh", nullptr, "only the first command can take < and only the last > when they are joined with |");
        return kNotRun;
    }
    const char *from = in_name[0] != 0 ? in_name : pipe_in;
    const char *to = out_name[0] != 0 ? out_name : pipe_out;
    if ((from != nullptr || to != nullptr) && env_.storage == nullptr) {
        fail("sh", nullptr, "there are no drives");
        return kNotRun;
    }
    if (from != nullptr) {
        const fs::FsError e = env_.storage->open(from, fs::OpenMode::Read, in_);
        if (e != fs::FsError::Ok) {
            fail("sh", from, e);
            return kNotRun;
        }
    }
    if (to != nullptr) {
        fs::FsError e = fs::FsError::NotFound;
        fs::DirEntry there;
        if (append && env_.storage->stat(to, there) == fs::FsError::Ok && !there.folder) {
            // >>: the file as it is, written from its end on.
            e = env_.storage->open(to, fs::OpenMode::Update, out_);
            if (e == fs::FsError::Ok) {
                e = env_.storage->seek(out_, there.size);
            }
        } else {
            e = env_.storage->open(to, fs::OpenMode::Write, out_);
        }
        if (e != fs::FsError::Ok) {
            if (out_.open()) {
                env_.storage->close(out_);
            }
            if (in_.open()) {
                env_.storage->close(in_);
            }
            fail("sh", to, e);
            return kNotRun;
        }
        out_error_ = fs::FsError::Ok;
        out_used_ = 0;
        out_lead_ = 0;
    }
    int result = run_words(text);
    if (out_.open()) {
        flush_file();
        const fs::FsError closed = env_.storage->close(out_);
        const fs::FsError e = out_error_ != fs::FsError::Ok ? out_error_ : closed;
        if (e != fs::FsError::Ok) {
            result = fail("sh", to, e);
        }
    }
    if (in_.open()) {
        env_.storage->close(in_);
    }
    return result;
}

int Session::run_words(char *work)
{
    // Words are split at spaces; "double quotes" keep a name with spaces
    // together.
    char *argv[kMaxArgs];
    int argc = 0;
    char *read = work, *write = work;
    while (*read != 0) {
        while (*read == ' ' || *read == '\t') {
            read++;
        }
        if (*read == 0) {
            break;
        }
        if (argc == kMaxArgs) {
            return fail("sh", nullptr, "too many words on the line");
        }
        argv[argc++] = write;
        bool quoted = false;
        while (*read != 0 && (quoted || (*read != ' ' && *read != '\t'))) {
            if (*read == '"') {
                quoted = !quoted;
            } else {
                *write++ = *read;
            }
            read++;
        }
        if (*read != 0) {
            read++;
        }
        *write++ = 0;
    }
    if (argc == 0) {
        return 0;
    }

    // Commands are found in any case, as on DOS.
    char name[24];
    size_t length = 0;
    for (; argv[0][length] != 0 && length < sizeof name - 1; length++) {
        const char c = argv[0][length];
        name[length] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    name[length] = 0;

    // "D:" alone changes drive.
    if (length == 2 && name[1] == ':' && argc == 1 && fs::path::drive(name) != 0) {
        char root[4] = {fs::path::drive(name), ':', '\\', 0};
        return set_cwd(root) ? 0 : fail(root, nullptr, "there is no such drive");
    }

    const Command *command = find(name);
    if (command == nullptr) {
        char text[96];
        const int length = snprintf(text, sizeof text, "%s: there is no such command. help lists them.\n", argv[0]);
        say(text, static_cast<size_t>(length) < sizeof text ? static_cast<size_t>(length) : sizeof text - 1);
        return 127;
    }
    argv[0] = const_cast<char *>(command->name);
    return command->run(*this, argc, argv);
}

void Session::prompt()
{
    if (!at_line_start_) {
        print("\n"); // a file that did not end its last line
    }
    printf("%s> ", cwd_);
    at_line_start_ = true; // the Enter that ends the typing moves to a new line
}

void Session::greet()
{
    static const char *const kNoArgs[] = {"ver", nullptr};
    cmd_ver(*this, 1, const_cast<char **>(kNoArgs));
    print("help lists the commands.\n\n");
}

bool Session::step(uint32_t wait_ms)
{
    if (ended_) {
        return false;
    }
    if (!prompted_) {
        prompt();
        prompted_ = true;
    }
    const int count = tty_.read(line_ + have_, sizeof line_ - 1 - static_cast<size_t>(have_), wait_ms);
    if (count == tty::kNothing) {
        return true;
    }
    if (count == tty::kEnd) {
        ended_ = true;
        return false;
    }
    if (count == tty::kInterrupted) {
        have_ = 0;
        prompted_ = false; // the TTY has shown ^C; a new prompt under it
        return true;
    }
    have_ += count;
    if (line_[have_ - 1] != '\n' && have_ < static_cast<int>(sizeof line_) - 1) {
        return true; // a piece of a line; the rest is on its way
    }
    line_[line_[have_ - 1] == '\n' ? have_ - 1 : have_] = 0;
    have_ = 0;
    prompted_ = false;
    execute(line_);
    if (tty_.take_interrupt()) {
        // A Ctrl+C that stopped the command has done its work. The TTY has
        // shown ^C and gone to a new line.
        at_line_start_ = true;
    }
    return !ended_;
}

void Session::run()
{
    greet();
    while (step(msg::kForever)) {
    }
}

} // namespace sh
