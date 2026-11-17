#include "core/tty/tty.hpp"

#include <cstdio>
#include <cstring>

namespace tty {

namespace {

constexpr uint8_t kCtrlA = 1, kCtrlC = 3, kCtrlD = 4, kCtrlE = 5, kBackspace = 8, kCtrlK = 11, kCtrlU = 21,
                  kCtrlW = 23, kEscape = 27, kDelete = 127;

} // namespace

// Holds the TTY's lock, if it has one, until the end of the block.
class Tty::Hold {
public:
    explicit Hold(ports::Lock *lock) : lock_(lock)
    {
        if (lock_ != nullptr) {
            lock_->lock();
        }
    }
    ~Hold()
    {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }
    Hold(const Hold &) = delete;
    Hold &operator=(const Hold &) = delete;

private:
    ports::Lock *lock_;
};

Tty::Tty(ports::ByteSink &out, ports::Lock *lock, ports::Event *event) : out_(out), lock_(lock), event_(event) {}

/* ---- To the device ---------------------------------------------------------------- */

void Tty::put(const char *data, size_t size)
{
    if (size > 0 && !hung_up_) {
        out_.write(reinterpret_cast<const uint8_t *>(data), size);
    }
}

void Tty::echo(const char *data, size_t size)
{
    if (echo_ && !raw_) {
        put(data, size);
    }
}

void Tty::write(const char *data, size_t size)
{
    const Hold hold(lock_);
    if (raw_) {
        put(data, size);
        return;
    }
    // Whole runs between line ends, so a line is a few writes and not one
    // per character.
    size_t start = 0;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') {
            put(data + start, i - start);
            put("\r\n", 2);
            start = i + 1;
        }
    }
    put(data + start, size - start);
}

void Tty::print(const char *text) { write(text, strlen(text)); }

void Tty::printf(const char *format, ...)
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

/* ---- The line being typed --------------------------------------------------------- */

// Everything here redraws with nothing but characters, spaces and backspaces,
// which every terminal understands the same way.

void Tty::insert(char c)
{
    if (length_ >= kLineSize - 1) { // one place is kept for the line's '\n'
        bell();
        return;
    }
    memmove(line_ + cursor_ + 1, line_ + cursor_, static_cast<size_t>(length_ - cursor_));
    line_[cursor_] = c;
    length_++;
    cursor_++;
    echo(line_ + cursor_ - 1, static_cast<size_t>(length_ - cursor_ + 1));
    for (int i = cursor_; i < length_; i++) {
        echo("\b", 1);
    }
}

void Tty::erase_after(int count)
{
    if (count > length_ - cursor_) {
        count = length_ - cursor_;
    }
    if (count <= 0) {
        return;
    }
    memmove(line_ + cursor_, line_ + cursor_ + count, static_cast<size_t>(length_ - cursor_ - count));
    length_ -= count;
    // The rest of the line moves left over what was erased, spaces cover
    // what it leaves behind, and the cursor walks back.
    const int tail = length_ - cursor_;
    echo(line_ + cursor_, static_cast<size_t>(tail));
    for (int i = 0; i < count; i++) {
        echo(" ", 1);
    }
    for (int i = 0; i < tail + count; i++) {
        echo("\b", 1);
    }
}

void Tty::erase_before(int count)
{
    if (count > cursor_) {
        count = cursor_;
    }
    if (count <= 0) {
        return;
    }
    move_to(cursor_ - count);
    erase_after(count);
}

void Tty::move_to(int position)
{
    position = position < 0 ? 0 : position > length_ ? length_ : position;
    if (position < cursor_) {
        for (int i = position; i < cursor_; i++) {
            echo("\b", 1);
        }
    } else {
        // Forward: writing the characters again moves over them.
        echo(line_ + cursor_, static_cast<size_t>(position - cursor_));
    }
    cursor_ = position;
}

void Tty::replace_line(const char *text, int length)
{
    move_to(0);
    erase_after(length_);
    if (length > kLineSize - 1) {
        length = kLineSize - 1;
    }
    memcpy(line_, text, static_cast<size_t>(length));
    length_ = cursor_ = length;
    echo(line_, static_cast<size_t>(length));
}

void Tty::enter()
{
    if (queued_free() < length_ + 1) {
        bell(); // the program is not reading; the line stays to be sent later
        return;
    }
    echo("\r\n", 2);
    if (length_ > 0 && echo_) { // what was typed unseen is not kept
        remember(line_, length_);
    }
    line_[length_] = '\n';
    queue(line_, length_ + 1);
    length_ = cursor_ = 0;
    browsing_ = 0;
}

bool Tty::interrupt()
{
    // As on Unix, what was typed ahead goes with it.
    length_ = cursor_ = 0;
    browsing_ = 0;
    first_ = queued_ = 0;
    ends_ = 0;
    echo("^C\r\n", 4);
    interrupted_ = true;
    return true;
}

/* ---- Keys ------------------------------------------------------------------------- */

void Tty::escape_key(uint8_t last, int number)
{
    switch (last) {
    case 'A': history_move(1); break;
    case 'B': history_move(-1); break;
    case 'C':
        if (cursor_ < length_) {
            move_to(cursor_ + 1);
        }
        break;
    case 'D':
        if (cursor_ > 0) {
            move_to(cursor_ - 1);
        }
        break;
    case 'H': move_to(0); break;
    case 'F': move_to(length_); break;
    case '~':
        if (number == 1 || number == 7) {
            move_to(0); // Home
        } else if (number == 4 || number == 8) {
            move_to(length_); // End
        } else if (number == 3) {
            erase_after(1); // Delete
        }
        break;
    default: break; // a key the line has no use for
    }
}

bool Tty::key(uint8_t c)
{
    if (hung_up_) {
        return false;
    }
    if (raw_) {
        if (c == kCtrlC) {
            return interrupt();
        }
        const char byte = static_cast<char>(c);
        queue(&byte, 1); // dropped if the program has let 512 pile up
        return false;
    }

    // Arrow keys and their like arrive as ESC [ numbers letter, or ESC O
    // letter.
    switch (esc_) {
    case Esc::None:
        if (c == kEscape) {
            esc_ = Esc::Started;
            return false;
        }
        break;
    case Esc::Started:
        esc_ = c == '[' ? Esc::Csi : c == 'O' ? Esc::Ss3 : Esc::None;
        esc_number_ = 0;
        esc_more_ = false;
        return false; // ESC and a plain key (Alt+key) is nothing here
    case Esc::Csi:
        if (c >= '0' && c <= '9') {
            if (!esc_more_ && esc_number_ < 1000) {
                esc_number_ = esc_number_ * 10 + (c - '0');
            }
        } else if (c == ';') {
            esc_more_ = true;
        } else {
            esc_ = Esc::None;
            if (c >= 0x40 && c <= 0x7E) {
                escape_key(c, esc_number_);
            }
        }
        return false;
    case Esc::Ss3:
        esc_ = Esc::None;
        escape_key(c, 0);
        return false;
    }

    // Enter is CR on a serial terminal, CR LF or CR NUL on telnet, LF from a
    // file sent down the wire: each is one Enter.
    const bool after_cr = after_cr_;
    after_cr_ = c == '\r';
    if (after_cr && (c == '\n' || c == 0)) {
        return false;
    }

    switch (c) {
    case '\r':
    case '\n': enter(); break;
    case kBackspace:
    case kDelete:
        if (cursor_ > 0) {
            erase_before(1);
        } else {
            bell();
        }
        break;
    case kCtrlC: return interrupt();
    case kCtrlD:
        if (length_ == 0) {
            ends_++;
        } else {
            bell();
        }
        break;
    case kCtrlA: move_to(0); break;
    case kCtrlE: move_to(length_); break;
    case kCtrlK: erase_after(length_ - cursor_); break;
    case kCtrlU: erase_before(cursor_); break;
    case kCtrlW: {
        int start = cursor_;
        while (start > 0 && line_[start - 1] == ' ') {
            start--;
        }
        while (start > 0 && line_[start - 1] != ' ') {
            start--;
        }
        erase_before(cursor_ - start);
        break;
    }
    default:
        if ((c >= 0x20 && c < kDelete) || c >= 0xA0) {
            insert(static_cast<char>(c));
        }
        break; // other control codes mean nothing here
    }
    return false;
}

void Tty::input(const uint8_t *data, size_t size)
{
    bool interrupts = false;
    {
        const Hold hold(lock_);
        for (size_t i = 0; i < size; i++) {
            interrupts |= key(data[i]);
        }
    }
    if (event_ != nullptr) {
        event_->set();
    }
    if (interrupts && interrupt_fn_ != nullptr) {
        interrupt_fn_(interrupt_context_);
    }
}

void Tty::input(const char *text) { input(reinterpret_cast<const uint8_t *>(text), strlen(text)); }

void Tty::hang_up()
{
    {
        const Hold hold(lock_);
        hung_up_ = true;
    }
    if (event_ != nullptr) {
        event_->set();
    }
}

void Tty::set_size(int columns, int rows)
{
    const Hold hold(lock_);
    columns_ = columns < 1 ? 1 : columns;
    rows_ = rows < 1 ? 1 : rows;
}

/* ---- The program end -------------------------------------------------------------- */

bool Tty::queue(const char *data, int size)
{
    if (size > queued_free()) {
        return false;
    }
    for (int i = 0; i < size; i++) {
        input_[(first_ + queued_) % kInputSize] = data[i];
        queued_++;
    }
    return true;
}

int Tty::read(char *out, size_t capacity, uint32_t wait_ms)
{
    for (;;) {
        {
            const Hold hold(lock_);
            if (interrupted_) {
                interrupted_ = false;
                return kInterrupted;
            }
            if (queued_ > 0 && capacity > 0) {
                size_t count = 0;
                while (count < capacity && queued_ > 0) {
                    const char c = input_[first_];
                    first_ = (first_ + 1) % kInputSize;
                    queued_--;
                    out[count++] = c;
                    if (!raw_ && c == '\n') {
                        break;
                    }
                }
                return static_cast<int>(count);
            }
            if (ends_ > 0) {
                ends_--;
                return kEnd;
            }
            if (hung_up_) {
                return kEnd;
            }
        }
        if (event_ == nullptr || wait_ms == 0) {
            return kNothing;
        }
        if (!event_->wait(wait_ms)) {
            wait_ms = 0; // time is up: one last look
        }
    }
}

void Tty::set_raw(bool raw)
{
    const Hold hold(lock_);
    if (raw == raw_) {
        return;
    }
    if (raw_) {
        first_ = queued_ = 0;
    }
    raw_ = raw;
    length_ = cursor_ = 0;
    browsing_ = 0;
    esc_ = Esc::None;
}

void Tty::set_echo(bool echo)
{
    const Hold hold(lock_);
    echo_ = echo;
}

bool Tty::take_interrupt()
{
    const Hold hold(lock_);
    const bool was = interrupted_;
    interrupted_ = false;
    return was;
}

void Tty::on_interrupt(InterruptFn fn, void *context)
{
    const Hold hold(lock_);
    interrupt_fn_ = fn;
    interrupt_context_ = context;
}

/* ---- Earlier lines ---------------------------------------------------------------- */

int Tty::history_count() const
{
    int count = 0;
    for (int i = 0; i < history_used_; i++) {
        count += history_[i] == 0;
    }
    return count;
}

int Tty::history_get(int back, char *out) const
{
    int skip = history_count() - back, at = 0;
    while (skip > 0) {
        at += static_cast<int>(strlen(history_ + at)) + 1;
        skip--;
    }
    const int length = static_cast<int>(strlen(history_ + at));
    memcpy(out, history_ + at, static_cast<size_t>(length));
    return length;
}

void Tty::remember(const char *line, int length)
{
    char newest[kLineSize];
    if (history_count() > 0 && history_get(1, newest) == length && memcmp(newest, line, static_cast<size_t>(length)) == 0) {
        return; // the same command again takes no second place
    }
    // The oldest lines make room.
    while (history_used_ + length + 1 > kHistorySize) {
        const int oldest = static_cast<int>(strlen(history_)) + 1;
        memmove(history_, history_ + oldest, static_cast<size_t>(history_used_ - oldest));
        history_used_ -= oldest;
    }
    memcpy(history_ + history_used_, line, static_cast<size_t>(length));
    history_[history_used_ + length] = 0;
    history_used_ += length + 1;
}

void Tty::history_move(int step)
{
    const int target = browsing_ + step;
    if (target < 0 || target > history_count()) {
        bell();
        return;
    }
    if (browsing_ == 0) {
        memcpy(draft_, line_, static_cast<size_t>(length_));
        draft_length_ = length_;
    }
    browsing_ = target;
    if (target == 0) {
        replace_line(draft_, draft_length_);
    } else {
        char earlier[kLineSize];
        const int length = history_get(target, earlier);
        replace_line(earlier, length);
    }
}

void Tty::clear_history()
{
    const Hold hold(lock_);
    history_used_ = 0;
    browsing_ = 0;
}

} // namespace tty
