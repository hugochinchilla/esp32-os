// ted: the text editor of the console (backlog item 22;
// docs/journal/2026-11-17-console-editor.md). A full screen of text and a
// status line under it, drawn with the few escape sequences every terminal
// knows (place the cursor, erase to the end of the line), so it works in the
// Terminal window, over a serial line and over telnet alike.
//
//     arrows, Home, End, Page Up, Page Down     move
//     Ctrl+A, Ctrl+E, Ctrl+Y, Ctrl+V            the same four, for terminals
//                                               that keep those keys
//     typing, Enter, Tab, Backspace, Delete     change the text
//     Ctrl+K                                    cut out the line
//     Ctrl+F, Ctrl+G                            find, find the next
//     Ctrl+S (or Ctrl+O)                        save
//     Ctrl+Q (or Ctrl+X)                        leave; asks if not saved
//     Ctrl+L                                    draw the screen again
//
// The text is Latin-1 in one block of memory, as in a file; the terminal is
// given UTF-8. A line longer than the screen is wide is not folded: the
// whole view moves sideways with the cursor. Only the rows that changed are
// sent again, so a key costs a line of text over a slow wire, not a screen.
#include <cstdio>
#include <cstring>

#include "commands.hpp"

namespace sh {

namespace {

constexpr size_t kCapacity = 128 * 1024; // the largest file it opens
constexpr int kTab = 4;                  // Tab puts spaces up to the next multiple
constexpr int kMaxColumns = 160, kMaxRows = 64;
// What it remembers of the terminal's screen, kept after the text.
constexpr size_t kScreen = static_cast<size_t>(kMaxColumns) * kMaxRows;
constexpr uint32_t kForever = 0xFFFFFFFF;
// How long the rest of a key's escape sequence may take to arrive, ms.
constexpr uint32_t kSequenceMs = 50;

// Keys that are not characters.
enum : int {
    kUp = 256,
    kDown,
    kLeft,
    kRight,
    kHome,
    kEndKey,
    kDelete,
    kPageUp,
    kPageDown,
    kEscape,
    kOther, // a key ted has no use for
    kBreak, // Ctrl+C
    kGone,  // the terminal went away
};
constexpr int kPromptGone = -1; // prompt(): the terminal went away

constexpr int kCtrlA = 1, kCtrlE = 5, kCtrlF = 6, kCtrlG = 7, kBackspace = 8, kTabKey = 9, kCtrlK = 11, kCtrlL = 12,
              kEnter = 13, kCtrlO = 15, kCtrlQ = 17, kCtrlS = 19, kCtrlV = 22, kCtrlX = 24, kCtrlY = 25, kRubout = 127;

// The text's memory comes from the Memory port, which is the UI task's.
struct Room {
    ports::Memory *memory;
    void *block;
};
void take_room(void *context)
{
    Room &r = *static_cast<Room *>(context);
    r.block = r.memory->allocate(kCapacity + kScreen);
}
void give_room(void *context)
{
    Room &r = *static_cast<Room *>(context);
    r.memory->release(r.block);
    r.block = nullptr;
}

uint8_t lower(uint8_t c) { return c >= 'A' && c <= 'Z' ? static_cast<uint8_t>(c + 32) : c; }

class Ted {
public:
    Ted(Session &s, uint8_t *text) : s_(s), tty_(s.terminal()), text_(text) {}

    // False, having said why, if the file cannot be edited.
    bool load(const char *command, const char *typed);
    void run();

private:
    // Keys.
    int byte(uint32_t wait_ms);
    int key();
    // The text.
    size_t line_start(size_t i) const;
    size_t line_end(size_t i) const;
    size_t column() const { return at_ - line_start(at_); }
    void insert(uint8_t c);
    void remove(size_t from, size_t count);
    void vertical(int lines);
    void find(bool again);
    bool save();
    bool leave();
    // The screen.
    void layout();
    void draw();
    void row_text(size_t from, bool exists, char *out) const;
    void status_text(char *out) const;
    void show_row(int row, const char *line, bool inverse);
    void say(const char *text);
    int prompt(const char *label, char *out, int capacity);
    void forget_screen() { memset(shown_, 0, static_cast<size_t>(rows_ * columns_)); }

    Session &s_;
    tty::Tty &tty_;
    uint8_t *text_;
    size_t length_ = 0;
    size_t at_ = 0;   // the cursor: before this character
    size_t top_ = 0;  // start of the first line shown
    size_t want_ = 0; // the column Up and Down try to keep
    int left_ = 0;    // columns moved sideways
    bool changed_ = false, crlf_ = false, fresh_ = false;
    char path_[fs::kMaxPath] = {};
    char message_[80] = {};
    char sought_[40] = {};

    int columns_ = 80, rows_ = 24; // rows_ counts the status line
    char *shown_ = nullptr;        // what each row of the terminal shows now
    char keys_[32];
    int have_ = 0, used_ = 0;
};

/* ---- Keys -------------------------------------------------------------------------- */

// The next byte typed: 0 to 255, or kGone, kBreak, -1 for none in time.
int Ted::byte(uint32_t wait_ms)
{
    if (used_ < have_) {
        return static_cast<uint8_t>(keys_[used_++]);
    }
    const int got = tty_.read(keys_, sizeof keys_, wait_ms);
    if (got > 0) {
        have_ = got;
        used_ = 1;
        return static_cast<uint8_t>(keys_[0]);
    }
    return got == tty::kEnd ? kGone : (got == tty::kInterrupted ? kBreak : -1);
}

int Ted::key()
{
    int b;
    do {
        b = byte(kForever);
    } while (b == -1);
    if (b == kGone || b == kBreak) {
        return b;
    }
    if (b == 0x1b) {
        // An arrow or its like: ESC [ numbers letter, or ESC O letter. Esc
        // by itself is followed by nothing.
        int c = byte(kSequenceMs);
        if (c == kGone || c == kBreak) {
            return c;
        }
        if (c != '[' && c != 'O') {
            return c == -1 ? kEscape : kOther;
        }
        int number = 0;
        bool first = true;
        for (;;) {
            c = byte(kSequenceMs);
            if (c == kGone || c == kBreak) {
                return c;
            }
            if (c == -1) {
                return kOther;
            }
            if (c >= '0' && c <= '9') {
                if (first) {
                    number = number * 10 + (c - '0');
                }
            } else if (c == ';') {
                first = false; // what follows says Shift or Ctrl was held: not used
            } else {
                break;
            }
        }
        switch (c) {
        case 'A': return kUp;
        case 'B': return kDown;
        case 'C': return kRight;
        case 'D': return kLeft;
        case 'H': return kHome;
        case 'F': return kEndKey;
        case '~':
            switch (number) {
            case 1: case 7: return kHome;
            case 4: case 8: return kEndKey;
            case 3: return kDelete;
            case 5: return kPageUp;
            case 6: return kPageDown;
            default: return kOther;
            }
        default: return kOther;
        }
    }
    if (b < 0x80) {
        return b;
    }
    // UTF-8 from the terminal. What Latin-1 has is two bytes; anything
    // longer becomes a question mark.
    const int more = b >= 0xF0 ? 3 : (b >= 0xE0 ? 2 : (b >= 0xC0 ? 1 : 0));
    int value = b & (0x3F >> more);
    for (int i = 0; i < more; i++) {
        const int c = byte(kSequenceMs);
        if (c == kGone || c == kBreak) {
            return c;
        }
        if (c == -1 || (c & 0xC0) != 0x80) {
            return '?';
        }
        value = (value << 6) | (c & 0x3F);
    }
    return more == 1 && value >= 0xA0 && value <= 0xFF ? value : '?';
}

/* ---- The text ---------------------------------------------------------------------- */

size_t Ted::line_start(size_t i) const
{
    while (i > 0 && text_[i - 1] != '\n') {
        i--;
    }
    return i;
}

size_t Ted::line_end(size_t i) const
{
    const void *found = memchr(text_ + i, '\n', length_ - i);
    return found != nullptr ? static_cast<size_t>(static_cast<const uint8_t *>(found) - text_) : length_;
}

void Ted::insert(uint8_t c)
{
    if (length_ >= kCapacity) {
        say("No room for more: 128 KB is the most ted holds");
        return;
    }
    memmove(text_ + at_ + 1, text_ + at_, length_ - at_);
    text_[at_++] = c;
    length_++;
    changed_ = true;
}

void Ted::remove(size_t from, size_t count)
{
    memmove(text_ + from, text_ + from + count, length_ - from - count);
    length_ -= count;
    at_ = from;
    if (top_ > at_) {
        top_ = line_start(at_);
    }
    changed_ = true;
}

// Up or down by lines, to the column the cursor last chose.
void Ted::vertical(int lines)
{
    size_t start = line_start(at_);
    for (; lines < 0 && start > 0; lines++) {
        start = line_start(start - 1);
    }
    for (; lines > 0; lines--) {
        const size_t end = line_end(start);
        if (end >= length_) {
            break;
        }
        start = end + 1;
    }
    const size_t width = line_end(start) - start;
    at_ = start + (want_ < width ? want_ : width);
}

void Ted::find(bool again)
{
    if (!again || sought_[0] == 0) {
        char typed[sizeof sought_];
        const int got = prompt("Find: ", typed, sizeof typed);
        if (got <= 0) {
            return;
        }
        snprintf(sought_, sizeof sought_, "%s", typed);
    }
    // From just after the cursor, round to where it is. Letters match in
    // either case.
    const size_t width = strlen(sought_);
    for (size_t step = 1; step <= length_ && width <= length_; step++) {
        const size_t i = (at_ + step) % length_;
        if (i + width > length_) {
            continue;
        }
        size_t same = 0;
        while (same < width && lower(text_[i + same]) == lower(static_cast<uint8_t>(sought_[same]))) {
            same++;
        }
        if (same == width) {
            at_ = i;
            return;
        }
    }
    snprintf(message_, sizeof message_, "\"%.40s\" is not in the text", sought_);
}

bool Ted::save()
{
    if (path_[0] == 0) {
        char typed[fs::kMaxPath];
        const int got = prompt("Save as: ", typed, 60);
        if (got <= 0) {
            return false;
        }
        if (!s_.resolve(typed, path_)) {
            path_[0] = 0;
            say("That name is too long");
            return false;
        }
    }
    const fs::Storage &storage = *s_.env().storage;
    fs::Save out;
    fs::FsError e = storage.begin_save(path_, out);
    // A line at a time; with the line ends the file came with.
    for (size_t from = 0; e == fs::FsError::Ok && from < length_;) {
        const size_t end = line_end(from);
        e = storage.write(out.file, text_ + from, end - from);
        if (e == fs::FsError::Ok && end < length_) {
            e = storage.write(out.file, crlf_ ? "\r\n" : "\n", crlf_ ? 2 : 1);
        }
        from = end + 1;
    }
    if (e == fs::FsError::Ok) {
        e = storage.finish_save(out);
    } else if (out.file.open()) {
        storage.finish_save(out, false);
    }
    if (e != fs::FsError::Ok) {
        snprintf(message_, sizeof message_, "Not saved: %s", fs::error_text(e));
        if (fresh_ && path_[0] != 0 && e == fs::FsError::BadName) {
            path_[0] = 0; // ask again next time
        }
        return false;
    }
    changed_ = false;
    fresh_ = false;
    say("Saved");
    return true;
}

// True if ted may close.
bool Ted::leave()
{
    while (changed_) {
        char answer[4];
        const int got = prompt("Save the changes? (y: yes, n: no, Esc: go on editing) ", answer, 2);
        if (got == kPromptGone) {
            return true;
        }
        if (got <= 0) {
            return false;
        }
        if (lower(static_cast<uint8_t>(answer[0])) == 'n') {
            return true;
        }
        if (lower(static_cast<uint8_t>(answer[0])) == 'y') {
            return save();
        }
    }
    return true;
}

bool Ted::load(const char *command, const char *typed)
{
    if (typed == nullptr) {
        fresh_ = true;
        return true;
    }
    if (!s_.resolve(typed, path_)) {
        s_.fail(command, typed, "the name is too long");
        return false;
    }
    const fs::Storage &storage = *s_.env().storage;
    fs::DirEntry entry;
    const fs::FsError found = storage.stat(path_, entry);
    if (found == fs::FsError::NotFound) {
        fresh_ = true; // a new file, made when it is first saved
        say("A new file");
        return true;
    }
    if (found != fs::FsError::Ok) {
        s_.fail(command, typed, found);
        return false;
    }
    if (entry.folder) {
        s_.fail(command, typed, "it is a folder");
        return false;
    }
    if (entry.size > kCapacity) {
        s_.fail(command, typed, "it is too big for ted (128 KB at most)");
        return false;
    }
    size_t size = 0;
    const fs::FsError e = storage.read_file(path_, text_, kCapacity, size);
    if (e != fs::FsError::Ok) {
        s_.fail(command, typed, e);
        return false;
    }
    // Lines that end CR LF are kept as plain lines and written back as
    // they came.
    size_t kept = 0;
    for (size_t i = 0; i < size; i++) {
        if (text_[i] == '\r' && i + 1 < size && text_[i + 1] == '\n') {
            crlf_ = true;
            continue;
        }
        text_[kept++] = text_[i];
    }
    length_ = kept;
    return true;
}

/* ---- The screen -------------------------------------------------------------------- */

void Ted::say(const char *text) { snprintf(message_, sizeof message_, "%s", text); }

// The terminal's size, within what ted has room to remember of it.
void Ted::layout()
{
    columns_ = tty_.columns() < 20 ? 20 : (tty_.columns() > kMaxColumns ? kMaxColumns : tty_.columns());
    rows_ = tty_.rows() < 3 ? 3 : (tty_.rows() > kMaxRows ? kMaxRows : tty_.rows());
    shown_ = reinterpret_cast<char *>(text_) + kCapacity;
}

// One row of text as the screen shows it: `columns_` characters, spaces
// after the end of the line. What cannot be shown is a dot.
void Ted::row_text(size_t from, bool exists, char *out) const
{
    memset(out, ' ', static_cast<size_t>(columns_));
    if (!exists) {
        return;
    }
    const size_t end = line_end(from);
    for (int c = 0; c < columns_; c++) {
        const size_t i = from + static_cast<size_t>(left_ + c);
        if (i >= end) {
            break;
        }
        const uint8_t ch = text_[i];
        out[c] = ch == '\t' ? ' ' : ((ch < 0x20 || (ch >= 0x7F && ch < 0xA0)) ? '.' : static_cast<char>(ch));
    }
}

void Ted::status_text(char *out) const
{
    size_t line = 1;
    for (size_t i = 0; i < at_; i++) {
        line += text_[i] == '\n';
    }
    char where[32], text[kMaxColumns + 1];
    snprintf(where, sizeof where, "Ln %u, Col %u", static_cast<unsigned>(line), static_cast<unsigned>(column() + 1));
    const char *name = path_[0] != 0 ? fs::path::name(path_) : "(no name)";
    const char *note = message_[0] != 0 ? message_ : "^S save  ^Q leave  ^F find  ^K cut line";
    snprintf(text, sizeof text, " %s%s  %s", name, changed_ ? "*" : "", note);
    // The name and the note from the left, as much as fits; where the
    // cursor is at the right.
    memset(out, ' ', static_cast<size_t>(columns_));
    const int width = static_cast<int>(strlen(where));
    const int room = columns_ - width - 2;
    const int used = static_cast<int>(strlen(text));
    memcpy(out, text, static_cast<size_t>(used < room ? used : (room > 0 ? room : 0)));
    if (width + 1 <= columns_) {
        memcpy(out + columns_ - width - 1, where, static_cast<size_t>(width));
    }
}

// Sends a row if it is not what the terminal already shows.
void Ted::show_row(int row, const char *line, bool inverse)
{
    char *was = shown_ + row * columns_;
    if (memcmp(was, line, static_cast<size_t>(columns_)) == 0) {
        return;
    }
    memcpy(was, line, static_cast<size_t>(columns_));
    int width = columns_;
    while (!inverse && width > 0 && line[width - 1] == ' ') {
        width--;
    }
    // The last cell of the last row is left alone: writing there makes
    // some terminals move everything up a line.
    if (row == rows_ - 1 && width == columns_) {
        width--;
    }
    s_.printf("\x1b[%d;1H%s", row + 1, inverse ? "\x1b[7m" : "");
    s_.write_latin1(line, static_cast<size_t>(width));
    s_.print(inverse ? "\x1b[0m" : "\x1b[K");
}

void Ted::draw()
{
    // Keep the cursor in view: up and down by lines, sideways by columns.
    const int text_rows = rows_ - 1;
    if (at_ < top_) {
        top_ = line_start(at_);
    }
    int row = 0;
    for (size_t i = top_; i < at_; i++) {
        row += text_[i] == '\n';
    }
    for (; row >= text_rows; row--) {
        top_ = line_end(top_) + 1;
    }
    const int col = static_cast<int>(column());
    if (col < left_) {
        left_ = col;
    } else if (col >= left_ + columns_) {
        left_ = col - columns_ + 1;
    }

    char line[kMaxColumns + 1];
    size_t from = top_;
    bool exists = true;
    for (int r = 0; r < text_rows; r++) {
        row_text(from, exists, line);
        show_row(r, line, false);
        if (exists) {
            const size_t end = line_end(from);
            exists = end < length_;
            from = end + 1;
        }
    }
    status_text(line);
    show_row(rows_ - 1, line, true);
    s_.printf("\x1b[%d;%dH", row + 1, col - left_ + 1);
}

// A line typed on the status row. Returns its length, 0 for none or Esc,
// kPromptGone if the terminal went away.
int Ted::prompt(const char *label, char *out, int capacity)
{
    int length = 0;
    out[0] = 0;
    for (;;) {
        char line[kMaxColumns + 1];
        snprintf(line, static_cast<size_t>(columns_) + 1, " %s%-*s", label, columns_, out);
        show_row(rows_ - 1, line, true);
        const int at = 1 + static_cast<int>(strlen(label)) + length;
        s_.printf("\x1b[%d;%dH", rows_, (at < columns_ ? at : columns_ - 1) + 1);
        const int k = key();
        if (k == kGone) {
            return kPromptGone;
        }
        if (k == kEnter || k == '\n') {
            return length;
        }
        if (k == kEscape || k == kBreak) {
            if (k == kBreak) {
                forget_screen();
                s_.clear_screen();
            }
            return 0;
        }
        if ((k == kBackspace || k == kRubout) && length > 0) {
            out[--length] = 0;
        } else if (k >= 0x20 && k < 256 && k != kRubout && length < capacity - 1) {
            out[length++] = static_cast<char>(k);
            out[length] = 0;
            if (capacity == 2) {
                return length; // one letter is the whole answer
            }
        }
    }
}

void Ted::run()
{
    layout();
    tty_.set_raw(true);
    forget_screen();
    s_.clear_screen();
    for (;;) {
        draw();
        const int k = key();
        message_[0] = 0;
        bool sideways = true; // the cursor chose a new column
        switch (k) {
        case kGone:
            tty_.set_raw(false);
            return;
        case kBreak:
            // Ctrl+C stops commands; here it would throw the text away.
            forget_screen();
            s_.clear_screen();
            say("Ctrl+Q leaves");
            break;
        case kCtrlQ:
        case kCtrlX:
            if (leave()) {
                tty_.set_raw(false);
                s_.clear_screen();
                return;
            }
            break;
        case kCtrlS:
        case kCtrlO:
            save();
            break;
        case kCtrlL:
            layout();
            forget_screen();
            s_.clear_screen();
            break;
        case kCtrlF:
            find(false);
            break;
        case kCtrlG:
            find(true);
            break;
        case kUp:
        case kDown:
            vertical(k == kUp ? -1 : 1);
            sideways = false;
            break;
        case kPageUp:
        case kCtrlY:
        case kPageDown:
        case kCtrlV:
            vertical((k == kPageUp || k == kCtrlY ? -1 : 1) * (rows_ - 2));
            sideways = false;
            break;
        case kLeft:
            at_ -= at_ > 0;
            break;
        case kRight:
            at_ += at_ < length_;
            break;
        case kHome:
        case kCtrlA:
            at_ = line_start(at_);
            break;
        case kEndKey:
        case kCtrlE:
            at_ = line_end(at_);
            break;
        case kBackspace:
        case kRubout:
            if (at_ > 0) {
                remove(at_ - 1, 1);
            }
            break;
        case kDelete:
            if (at_ < length_) {
                remove(at_, 1);
            }
            break;
        case kCtrlK: {
            const size_t start = line_start(at_), end = line_end(at_);
            if (end < length_) {
                remove(start, end + 1 - start);
            } else if (end > start) {
                remove(start, end - start);
            }
            break;
        }
        case kEnter:
        case '\n':
            insert('\n');
            break;
        case kTabKey:
            do {
                insert(' ');
            } while (column() % kTab != 0 && length_ < kCapacity);
            break;
        default:
            if (k >= 0x20 && k < 256) {
                insert(static_cast<uint8_t>(k));
            }
            break;
        }
        if (sideways) {
            want_ = column();
        }
    }
}

} // namespace

int cmd_ted(Session &s, int argc, char **argv)
{
    if (argc > 2) {
        return s.fail(argv[0], nullptr, "it edits one file at a time");
    }
    if (s.env().storage == nullptr) {
        return s.fail(argv[0], nullptr, "there are no drives");
    }
    if (!s.terminal().can_wait() || s.input_redirected() || s.output_redirected()) {
        return s.fail(argv[0], nullptr, "it needs a terminal it can wait on for keys");
    }
    Room room = {s.env().memory, nullptr};
    if (room.memory != nullptr) {
        s.on_ui(&take_room, &room);
    }
    if (room.block == nullptr) {
        return s.fail(argv[0], nullptr, "there is no memory for the text");
    }
    int result = 1;
    {
        Ted ted(s, static_cast<uint8_t *>(room.block));
        if (ted.load(argv[0], argc > 1 ? argv[1] : nullptr)) {
            ted.run();
            result = 0;
        }
    }
    s.on_ui(&give_room, &room);
    return result;
}

} // namespace sh
