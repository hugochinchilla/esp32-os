#include "core/sh/sh.hpp"

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
    s.print("\x1b[2J\x1b[H"); // erase the screen, cursor to the top left
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
        while (c->usage == nullptr) {
            c--; // another name: the command itself is before it
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
            "Paths take \\ and /. * and ? stand for any letters. Ctrl+C stops a command.\n");
    return 0;
}

} // namespace

const Command kCommands[] = {
    {"ls", &cmd_ls, "ls [-l] [-a] [PATH...]", "List a folder. -l with sizes, -a with hidden names"},
    {"dir", &cmd_dir, nullptr, nullptr},
    {"cd", &cmd_cd, "cd [FOLDER]", "Go to a folder; alone, show where you are"},
    {"chdir", &cmd_cd, nullptr, nullptr},
    {"pwd", &cmd_pwd, "pwd", "Show the folder you are in"},
    {"cat", &cmd_cat, "cat FILE...", "Show what is in a file"},
    {"type", &cmd_cat, nullptr, nullptr},
    {"cp", &cmd_cp, "cp [-r] FROM... TO", "Copy files; -r copies folders too"},
    {"copy", &cmd_cp, nullptr, nullptr},
    {"mv", &cmd_mv, "mv FROM... TO", "Move or rename"},
    {"move", &cmd_mv, nullptr, nullptr},
    {"ren", &cmd_mv, nullptr, nullptr},
    {"rename", &cmd_mv, nullptr, nullptr},
    {"rm", &cmd_rm, "rm [-r] PATH...", "Delete for good; -r deletes folders and all in them"},
    {"del", &cmd_rm, nullptr, nullptr},
    {"erase", &cmd_rm, nullptr, nullptr},
    {"mkdir", &cmd_mkdir, "mkdir FOLDER...", "Make a folder"},
    {"md", &cmd_mkdir, nullptr, nullptr},
    {"rmdir", &cmd_rmdir, "rmdir FOLDER...", "Delete an empty folder"},
    {"rd", &cmd_rmdir, nullptr, nullptr},
    {"touch", &cmd_touch, "touch FILE...", "Make an empty file"},
    {"df", &cmd_df, "df", "Show the drives and their free space"},
    {"echo", &cmd_echo, "echo [TEXT...]", "Write the text"},
    {"ps", &cmd_ps, "ps", "List the processes"},
    {"tasklist", &cmd_ps, nullptr, nullptr},
    {"kill", &cmd_kill, "kill [-TERM | -KILL | -9] PID...", "Ask a process to end; -9 ends it without asking"},
    {"taskkill", &cmd_kill, nullptr, nullptr},
    {"free", &cmd_free, "free", "Show the free memory"},
    {"mem", &cmd_free, nullptr, nullptr},
    {"uptime", &cmd_uptime, "uptime", "Show how long the machine has been on"},
    {"ver", &cmd_ver, "ver", "Show the version of Chinchilla"},
    {"uname", &cmd_ver, nullptr, nullptr},
    {"cls", &cmd_cls, "cls", "Clear the screen"},
    {"clear", &cmd_cls, nullptr, nullptr},
    {"help", &cmd_help, "help [NAME]", "Show this list, or how one command is written"},
    {"?", &cmd_help, nullptr, nullptr},
    {"exit", &cmd_exit, "exit", "End the session"},
    {"logout", &cmd_exit, nullptr, nullptr},
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

void Session::write(const char *data, size_t size)
{
    if (size > 0) {
        tty_.write(data, size);
        at_line_start_ = data[size - 1] == '\n';
    }
}

void Session::write_latin1(const char *data, size_t size)
{
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
    if (what != nullptr) {
        printf("%s: %s: %s\n", command, what, why);
    } else {
        printf("%s: %s\n", command, why);
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

int Session::execute(const char *line)
{
    // Words are split at spaces; "double quotes" keep a name with spaces
    // together.
    char work[tty::Tty::kReadSize + 1];
    snprintf(work, sizeof work, "%s", line);
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
        printf("%s: there is no such command. help lists them.\n", argv[0]);
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
