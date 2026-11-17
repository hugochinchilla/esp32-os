// The TTY: one text terminal, as the OS sees it (backlog item 12;
// docs/DECISIONS.md, "Chosen for the TTY"; docs/journal/2026-11-17-tty.md).
//
// It sits between a device and a program and has one end for each:
//
//     keys  --> input()  --[ line being typed ]--[ finished lines ]--> read()
//     screen <-- sink  <------- echo ----------+<--------------------- write()
//
// The device end is whatever carries the characters: the serial port, a
// telnet connection or a terminal window. It hands in the bytes that were
// typed (input()) and is given the bytes to show (a ports::ByteSink). The
// program end is a shell session: it reads whole lines and writes text.
//
// In between the TTY does what a Unix terminal does in its usual ("cooked")
// mode, so that no program has to: it shows what is typed (echo), lets the
// line be corrected before the program sees it, and turns Ctrl+C into an
// interrupt and Ctrl+D into "end of input". On top of that it keeps the last
// lines typed, for the up and down arrows.
//
// The two ends are usually different tasks. With a ports::Lock the TTY may be
// used from both at once, and with a ports::Event read() can sleep until
// there is something to read. With neither it is a plain object for one task
// (tests, or a program that polls).
//
// Text is UTF-8 at both ends, as everywhere in the OS's interfaces: what the
// device types, what it is given to show, what the program reads and writes.
// Only the line being typed is kept as Latin-1 inside (the characters the
// fonts have), so that one character is one byte and one column; a typed
// character outside Latin-1 becomes '?'. No heap: about 1.2 KB per terminal.
#pragma once

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace tty {

// What read() answers besides a count of bytes.
constexpr int kNothing = 0;      // nothing was typed in the time given
constexpr int kEnd = -1;         // Ctrl+D on an empty line, or the device went away
constexpr int kInterrupted = -2; // Ctrl+C

// Called on the device's task, outside the TTY's lock, when Ctrl+C is typed.
using InterruptFn = void (*)(void *context);

class Tty {
public:
    static constexpr int kLineSize = 160;    // the longest line that can be typed, in characters
    static constexpr int kReadSize = 2 * kLineSize; // room for any line read(), as UTF-8
    static constexpr int kInputSize = 512;   // finished lines waiting to be read
    static constexpr int kHistorySize = 384; // bytes of earlier lines kept

    // `out` is the device: what is written there appears on the terminal. It
    // is called with the TTY's lock held and must not call back into the TTY.
    explicit Tty(ports::ByteSink &out, ports::Lock *lock = nullptr, ports::Event *event = nullptr);

    /* ---- The device end ---- */

    // Bytes typed at the terminal: characters (UTF-8), control keys, and
    // the escape sequences that arrow keys send.
    void input(const uint8_t *data, size_t size);
    void input(const char *text); // the same, up to the 0 (tests)
    // The device has gone (a telnet connection closed): every read() from
    // now on answers kEnd, and what is written is thrown away.
    void hang_up();
    // The terminal's size in characters, for programs that draw (top).
    void set_size(int columns, int rows);

    /* ---- The program end ---- */

    // Cooked mode: one typed line as UTF-8, with its '\n', or the first
    // `capacity` bytes of it (the rest comes with the next call; kReadSize
    // holds any line). Raw mode: whatever
    // bytes have arrived. Sleeps up to wait_ms for it (msg-style: 0 does not
    // wait, 0xFFFFFFFF waits for ever) when the TTY has an Event; without
    // one it never waits. Returns the count, or kNothing, kEnd, kInterrupted.
    // Lines typed before a Ctrl+C are thrown away with it.
    int read(char *out, size_t capacity, uint32_t wait_ms = 0);
    // Text for the terminal. A '\n' goes out as CR LF in cooked mode. After
    // a Ctrl+C nothing is shown until the program has taken notice of it
    // (read() answering kInterrupted, or take_interrupt()): what a stopped
    // command still had in hand does not land under the ^C.
    void write(const char *data, size_t size);
    void print(const char *text);
    void printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

    // Raw mode, for a program that takes the keys itself (a console editor,
    // top): no echo, no line editing, bytes are handed over as they come and
    // written as they are. Ctrl+C still interrupts. Leaving raw mode throws
    // away what was not read.
    // Whether a read() would answer something other than kNothing now.
    bool readable();

    void set_raw(bool raw);
    bool raw() const { return raw_; }
    // Echo off, for typing that must not be shown. Cooked mode only.
    void set_echo(bool echo);

    // Ctrl+C. The flag is raised when it is typed; a program that runs for
    // long looks at interrupted() and stops. read() answering kInterrupted
    // lowers it, and so does take_interrupt().
    bool interrupted() const { return interrupted_ || interrupt_arriving_; }
    // True if there was one. It first lets the TTY finish with a Ctrl+C that
    // is just arriving (its ^C shown, typed-ahead lines dropped), so what the
    // program writes next comes after it.
    bool take_interrupt();
    void on_interrupt(InterruptFn fn, void *context);

    bool hung_up() const { return hung_up_; }
    int columns() const { return columns_; }
    int rows() const { return rows_; }

    // Forgets the earlier lines.
    void clear_history();

private:
    class Hold;

    void put(const char *data, size_t size); // to the device, as it is
    void put(char c) { put(&c, 1); }
    void echo(const char *latin1, size_t size); // as UTF-8, if the echo is on
    void bell() { put('\a'); }

    bool typed(uint8_t byte); // one byte from the device; true if Ctrl+C
    bool key(uint8_t c);      // one typed character (Latin-1) or control code
    void escape_key(uint8_t last, int number);
    void insert(char c);
    void erase_before(int count); // the `count` characters left of the cursor
    void erase_after(int count);
    void move_to(int position);
    void replace_line(const char *text, int length);
    void enter();
    bool interrupt();

    bool queue(const char *data, int size);
    int queued_free() const { return kInputSize - queued_; }

    void remember(const char *line, int length);
    int history_count() const;
    int history_get(int back, char *out) const; // back: 1 is the newest
    void history_move(int step);

    ports::ByteSink &out_;
    ports::Lock *lock_;
    ports::Event *event_;

    // The line being typed.
    char line_[kLineSize];
    int length_ = 0, cursor_ = 0;

    // Finished input: a ring of bytes, and how many "end of input" marks
    // (Ctrl+D) wait behind them.
    char input_[kInputSize];
    int first_ = 0, queued_ = 0;
    int ends_ = 0;

    // Earlier lines, each followed by a 0, oldest first.
    char history_[kHistorySize];
    int history_used_ = 0;
    int browsing_ = 0;       // 0: the line being typed; 1: the newest earlier line...
    char draft_[kLineSize]; // what was being typed when the browsing began
    int draft_length_ = 0;

    // Escape sequences as they arrive: ESC, then '[' or 'O', digits, a letter.
    enum class Esc : uint8_t { None, Started, Csi, Ss3 };
    Esc esc_ = Esc::None;
    int esc_number_ = 0;
    bool esc_more_ = false; // past the first ';': the rest of the numbers are not ours
    // A UTF-8 character as it arrives: bytes still to come, and its code so far.
    int utf8_more_ = 0;
    uint32_t utf8_code_ = 0;
    bool after_cr_ = false; // a LF or NUL straight after a CR is part of the same Enter

    bool raw_ = false, echo_ = true;
    std::atomic<bool> interrupted_{false};
    // A Ctrl+C has arrived and input() is still waiting for the lock to deal
    // with it. Raised without the lock, so that a program that writes
    // without pause (and so holds the lock nearly all the time) still sees
    // interrupted() at once and stops.
    std::atomic<bool> interrupt_arriving_{false};
    std::atomic<bool> hung_up_{false};
    int columns_ = 80, rows_ = 24;

    InterruptFn interrupt_fn_ = nullptr;
    void *interrupt_context_ = nullptr;
};

} // namespace tty
