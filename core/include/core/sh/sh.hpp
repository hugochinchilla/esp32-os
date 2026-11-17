// The command shell: a prompt, a line, a command (backlog item 13;
// docs/DECISIONS.md, "Chosen for the command shell";
// docs/journal/2026-11-17-command-shell.md). Not to be confused with
// core/shell/, which is the desktop.
//
// A session reads lines from a TTY (core/tty/tty.hpp) and runs them. It does
// not know what the terminal is: the serial port, telnet or a window. The
// commands are built in, with Linux names and DOS aliases (ls and dir, cat
// and type, rm and del). Paths keep their drive letters and take both
// slashes: "C:\Documents\a.txt", "c:/documents/a.txt", "..\a.txt", "/docs".
//
// A session is meant to be a task of its own, sleeping in run() until a line
// is typed. Files are safe from there (fs::Storage has its lock). The process
// table is the UI task's, so `ps` and `kill` are handed to it through a
// UiCall and the session waits for the answer.
#pragma once

#include <atomic>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/msg/queue.hpp"
#include "core/ports.hpp"
#include "core/proc/table.hpp"
#include "core/tty/tty.hpp"

namespace sh {

// How a session gets something done on the UI task: run() returns when `fn`
// has run there.
class UiCall {
public:
    virtual void run(void (*fn)(void *context), void *context) = 0;

protected:
    ~UiCall() = default;
};

// The UiCall for a session on a task of its own: a message into the UI
// queue, and an Event the session sleeps on until the UI task has done it.
// One per session. Never for a session that runs on the UI task itself: it
// would wait for ever for itself, and needs no UiCall at all.
class QueueCall : public UiCall {
public:
    QueueCall(msg::Queue &queue, ports::Event &done) : queue_(queue), done_(done) {}
    void run(void (*fn)(void *context), void *context) override;

private:
    struct Job {
        void (*fn)(void *);
        void *context;
        ports::Event *done;
        std::atomic<bool> finished;
    };
    static void deliver(const msg::Message &m);

    msg::Queue &queue_;
    ports::Event &done_;
};

// What a session can reach. Anything left null makes the commands that need
// it say so.
struct Env {
    fs::Storage *storage = nullptr;
    proc::Table *procs = nullptr;   // ps, kill
    ports::System *system = nullptr; // free, uptime
    UiCall *ui = nullptr;            // null: the session is on the UI task and calls straight in
};

namespace path {
// The full path for what was typed, from the folder the session is in:
// ("C:\docs", "..\a.txt") -> "C:\a.txt"; ("C:\docs", "/x/y/") -> "C:\x\y";
// ("C:\docs", "d:") -> "D:\". Both slashes, "." and "..", any case of drive
// letter. False if it does not fit in `capacity`.
bool resolve(const char *cwd, const char *typed, char *out, int capacity);
// "*" for any run of characters, "?" for one; letters match in either case.
// A name that starts with a dot is only matched by a pattern that does too.
bool match(const char *pattern, const char *name);
bool has_wildcard(const char *text);
// The same file or folder (names compare without regard to case).
bool same(const char *a, const char *b);
} // namespace path

class Session {
public:
    static constexpr int kMaxArgs = 16;
    static constexpr int kBufferSize = 2048;

    Session(tty::Tty &tty, const Env &env, const char *start_folder = "C:\\");

    // The whole life of a session on its own task: greeting, then lines
    // until `exit`, Ctrl+D or the terminal going away.
    void run();
    // The same in pieces, for whoever cannot give it a task: greet() once,
    // then step() again and again. step() waits up to wait_ms for a line
    // (never, without an Event in the TTY), runs it if there is one, and
    // answers false once the session has ended.
    void greet();
    bool step(uint32_t wait_ms = 0);
    bool ended() const { return ended_; }
    // The prompt is on the screen and the session waits for a line.
    bool waiting() const { return prompted_ && !ended_; }

    // Runs one line as if typed: one command, or several joined with |,
    // with < > and >> (docs/journal/2026-11-17-pipes.md). Returns the last
    // command's result: 0 for done.
    int execute(const char *line);

    const char *cwd() const { return cwd_; }

    /* ---- For the commands ---- */

    const Env &env() const { return env_; }
    tty::Tty &terminal() { return tty_; }
    // What a command writes goes to the terminal, or to the file or pipe the
    // line sent it to. Text is UTF-8 here; a file gets Latin-1.
    void print(const char *text);
    void clear_screen();
    void printf(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void write(const char *data, size_t size);
    // What is in a text file: Latin-1, as the Editor keeps it. The terminal
    // is given UTF-8.
    void write_latin1(const char *data, size_t size);
    // A command's input, as Latin-1: the file or pipe the line gave it,
    // else what is typed, a line at a time, until Ctrl+D. Returns the number
    // of bytes, 0 at the end. On a terminal that cannot be waited on (a
    // session without a task of its own) there is nothing to read.
    size_t read_input(char *out, size_t capacity);
    bool input_redirected() const { return in_.open(); }
    bool output_redirected() const { return out_.open(); }
    // True if read_input() can give anything.
    bool has_input() const;
    // "name: what: why" on a line of its own, always on the terminal;
    // returns 1 for the command to return.
    int fail(const char *command, const char *what, const char *why);
    int fail(const char *command, const char *what, fs::FsError error);
    // True once Ctrl+C has been typed: a command that loops stops.
    bool stopped() const { return cut_ || tty_.interrupted(); }
    bool resolve(const char *typed, char *out) const { return path::resolve(cwd_, typed, out, fs::kMaxPath); }
    bool set_cwd(const char *full_path);
    void end() { ended_ = true; }
    void on_ui(void (*fn)(void *context), void *context);
    // Working room for one command at a time: pieces of a file, a list.
    char *buffer() { return buffer_; }
    // Two paths that the commands that walk folders grow and cut back.
    char *walk_from() { return walk_from_; }
    char *walk_to() { return walk_to_; }

private:
    static constexpr int kMaxStages = 6; // commands on one line, joined with |
    static constexpr int kNotRun = -1000; // run_stage(): the command was not started

    void prompt();
    int run_stage(char *text, bool first, bool last, const char *pipe_in, const char *pipe_out);
    int run_words(char *text);
    void say(const char *data, size_t size); // to the terminal, whatever is redirected
    void to_file(const char *latin1, size_t size);
    void flush_file();

    tty::Tty &tty_;
    Env env_;
    char cwd_[fs::kMaxPath];
    char line_[tty::Tty::kReadSize + 1];
    int have_ = 0;
    bool prompted_ = false, ended_ = false;
    bool at_line_start_ = true; // nothing written since the last '\n'
    char buffer_[kBufferSize];
    char walk_from_[fs::kMaxPath], walk_to_[fs::kMaxPath];

    // Redirection, for the command that is running.
    fs::File in_, out_;
    fs::FsError out_error_ = fs::FsError::Ok;
    char out_buffer_[256];
    int out_used_ = 0;
    uint8_t out_lead_ = 0; // the first byte of a two-byte UTF-8 character, its second not yet written
    bool cut_ = false;     // Ctrl+C was read as input: the line stops
    uint16_t id_ = 0;      // tells this session's pipe files from another's
};

} // namespace sh
