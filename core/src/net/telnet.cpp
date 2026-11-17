#include "core/net/telnet.hpp"

#include <cstring>
#include <new>

namespace net {

namespace {

// Telnet's command bytes (RFC 854) and the options used here.
constexpr uint8_t kIac = 255, kDont = 254, kDo = 253, kWont = 252, kWill = 251, kSb = 250, kSe = 240;
constexpr uint8_t kInterruptProcess = 244, kEraseCharacter = 247;
constexpr uint8_t kEcho = 1, kSuppressGoAhead = 3, kWindowSize = 31;

// Room a piece of a program's output may need: Tty::kPiece bytes, each of
// which may be a line end that goes out as two.
constexpr size_t kPieceRoom = 2 * tty::Tty::kPiece;
// A program only writes while this much would be left over after its piece.
// It is the echo's: however much a program writes and however slowly the
// client reads, what is typed (a Ctrl+C above all) still gets in.
constexpr size_t kEchoReserve = 512;
// Typed bytes are handed to the TTY, 32 at a time, while there is this much
// room for their echo.
constexpr size_t kEchoRoom = 256;

} // namespace

Telnet::Telnet(const sh::Env &env, const TelnetKit &kit) : env_(env), kit_(kit) {}

Telnet::~Telnet() { stop(); }

void Telnet::stop()
{
    if (stream_ != nullptr) {
        lost();
        // The session's task is still using what is in here: wait for it.
        while (!over_) {
            kit_.typed->set();
            kit_.room->wait(10);
        }
        end();
    }
}

/* ---- The session's side ----------------------------------------------------------- */

void Telnet::Out::write(const uint8_t *data, size_t size)
{
    Telnet &t = owner_;
    if (t.gone_) {
        return;
    }
    const size_t head = t.head_.load();
    const size_t free_now = kRing - (head - t.tail_.load());
    if (size > free_now) {
        // Only the echo of typing can find it full (programs wait for room
        // first), and only if the client stopped reading: it loses that.
        t.dropped_ += static_cast<uint32_t>(size - free_now);
        size = free_now;
    }
    for (size_t i = 0; i < size; i++) {
        t.ring_[(head + i) % kRing] = data[i];
    }
    t.head_.store(head + size);
}

// The session's task, before each piece it writes: until the ring has room
// for it, or there is nobody left to write to.
void Telnet::wait_for_room(void *self)
{
    Telnet &t = *static_cast<Telnet *>(self);
    while (!t.gone_ && kRing - t.waiting() < kPieceRoom + kEchoReserve) {
        t.kit_.room->wait(50);
    }
}

void Telnet::run(void *self)
{
    Telnet &t = *static_cast<Telnet *>(self);
    t.session_->run();
    t.over_ = true;
}

/* ---- The services task ------------------------------------------------------------ */

void Telnet::begin(Stream &stream)
{
    stream_ = &stream;
    sessions_++;
    head_ = 0;
    tail_ = 0;
    reply_count_ = 0;
    pending_count_ = 0;
    reading_ = Reading::Text;
    after_cr_ = false;
    over_ = false;
    gone_ = false;
    tty_ = new (tty_room_) tty::Tty(out_, kit_.lock, kit_.typed);
    tty_->set_size(80, 24);
    tty_->on_room(&Telnet::wait_for_room, this);
    session_ = new (session_room_) sh::Session(*tty_, env_);
    // Characters as they are typed, echoed by us; and the client's size.
    reply(kIac, kWill, kEcho);
    reply(kIac, kWill, kSuppressGoAhead);
    reply(kIac, kDo, kWindowSize);
    task_ = kit_.tasks != nullptr
                ? kit_.tasks->start("telnet", &Telnet::run, this, kSessionStack, ports::Priority::Normal)
                : nullptr;
    if (task_ == nullptr) {
        static const char sorry[] = "No memory for a session. Try again later.\r\n";
        stream_->write(reinterpret_cast<const uint8_t *>(sorry), sizeof sorry - 1);
        over_ = true;
        gone_ = true;
    }
}

// The session's task has finished: everything goes back.
void Telnet::end()
{
    if (task_ != nullptr) {
        kit_.tasks->end(task_);
        task_ = nullptr;
    }
    session_->~Session();
    tty_->~Tty();
    session_ = nullptr;
    tty_ = nullptr;
    stream_->close();
    stream_ = nullptr;
}

// The connection broke or the client left: the session is told its terminal
// has gone, as a Terminal window does when it is closed. A command that is
// running stops as for Ctrl+C.
void Telnet::lost()
{
    if (gone_) {
        return;
    }
    gone_ = true;
    if (!over_) {
        tty_->input("\x03");
        tty_->hang_up();
    }
    kit_.room->set();
}

void Telnet::reply(uint8_t a, uint8_t b, uint8_t c)
{
    if (reply_count_ + 3 <= sizeof replies_) {
        replies_[reply_count_++] = a;
        replies_[reply_count_++] = b;
        replies_[reply_count_++] = c;
    }
}

// What we do and do not: echo and no go-ahead from our side, the window
// size from theirs. Everything else is refused, once.
void Telnet::option(uint8_t verb, uint8_t which)
{
    if (verb == kDo && which != kEcho && which != kSuppressGoAhead) {
        reply(kIac, kWont, which);
    } else if (verb == kWill && which != kWindowSize && which != kSuppressGoAhead) {
        reply(kIac, kDont, which);
    }
}

void Telnet::subnegotiation()
{
    if (sub_count_ >= 5 && sub_[0] == kWindowSize) {
        const int columns = (sub_[1] << 8) | sub_[2], rows = (sub_[3] << 8) | sub_[4];
        if (columns >= 20 && columns <= 250 && rows >= 4 && rows <= 250) {
            tty_->set_size(columns, rows);
        }
    }
}

// One byte from the client: telnet's own are acted on, the rest is typing.
void Telnet::typed(uint8_t byte)
{
    switch (reading_) {
    case Reading::Text:
        if (byte == kIac) {
            reading_ = Reading::Command;
        } else if (after_cr_ && (byte == '\n' || byte == 0)) {
            after_cr_ = false; // Enter arrives as CR LF or CR NUL: one key
        } else {
            after_cr_ = byte == '\r';
            pending_[pending_count_++] = byte;
        }
        break;
    case Reading::Command:
        reading_ = Reading::Text;
        if (byte == kWill || byte == kWont || byte == kDo || byte == kDont) {
            verb_ = byte;
            reading_ = Reading::Option;
        } else if (byte == kSb) {
            sub_count_ = 0;
            reading_ = Reading::Sub;
        } else if (byte == kInterruptProcess) {
            pending_[pending_count_++] = 3; // Ctrl+C
        } else if (byte == kEraseCharacter) {
            pending_[pending_count_++] = 127;
        }
        // IAC IAC would be the character 255, which UTF-8 never has.
        break;
    case Reading::Option:
        reading_ = Reading::Text;
        option(verb_, byte);
        break;
    case Reading::Sub:
        if (byte == kIac) {
            reading_ = Reading::SubCommand;
        } else if (sub_count_ < sizeof sub_) {
            sub_[sub_count_++] = byte;
        }
        break;
    case Reading::SubCommand:
        if (byte == kSe) {
            reading_ = Reading::Text;
            subnegotiation();
        } else {
            reading_ = Reading::Sub;
            if (byte == kIac && sub_count_ < sizeof sub_) {
                sub_[sub_count_++] = kIac;
            }
        }
        break;
    }
}

// What has arrived, a little at a time and only while the echo has room:
// the rest waits in the connection.
void Telnet::take_input()
{
    for (int rounds = 0; rounds < 8 && !gone_ && !over_; rounds++) {
        if (kRing - waiting() < kEchoRoom) {
            return;
        }
        uint8_t bytes[32];
        const int got = stream_->read(bytes, sizeof bytes);
        if (got < 0) {
            lost();
            return;
        }
        if (got == 0) {
            return;
        }
        pending_count_ = 0;
        for (int i = 0; i < got; i++) {
            typed(bytes[i]);
        }
        if (pending_count_ > 0) {
            tty_->input(pending_, pending_count_);
        }
    }
}

// Sends what fits: telnet's answers, then what the session has shown.
// False if the connection is broken.
bool Telnet::send()
{
    if (reply_count_ > 0) {
        const int done = stream_->write(replies_, reply_count_);
        if (done < 0) {
            return false;
        }
        memmove(replies_, replies_ + done, reply_count_ - static_cast<size_t>(done));
        reply_count_ -= static_cast<size_t>(done);
        if (reply_count_ > 0) {
            return true;
        }
    }
    for (;;) {
        const size_t tail = tail_.load();
        const size_t count = head_.load() - tail;
        if (count == 0) {
            break;
        }
        const size_t at = tail % kRing;
        const size_t run = count < kRing - at ? count : kRing - at;
        const int done = stream_->write(ring_ + at, run);
        if (done < 0) {
            return false;
        }
        tail_.store(tail + static_cast<size_t>(done));
        if (static_cast<size_t>(done) < run) {
            break; // the connection has taken what it can for now
        }
    }
    kit_.room->set();
    return true;
}

void Telnet::poll()
{
    if (stream_ == nullptr) {
        Stream *arrived = listener_ != nullptr ? listener_->accept() : nullptr;
        if (arrived == nullptr) {
            return;
        }
        begin(*arrived);
    }
    if (!gone_ && !send()) {
        lost();
    }
    take_input();
    if (over_) {
        // `exit`, or the session noticed its terminal had gone. What it
        // said last still goes out if it can.
        if (!gone_) {
            send();
        }
        gone_ = true;
        end();
    }
}

uint32_t Telnet::watch(Watch &watch)
{
    if (stream_ == nullptr) {
        // Nobody: only someone knocking brings it back.
        if (listener_ == nullptr || !listener_->watch(watch)) {
            return listener_ != nullptr ? 100000 : kNever;
        }
        return kNever;
    }
    const int handle = stream_->handle();
    if (handle >= 0 && !gone_) {
        watch.add(handle, true, waiting() > 0 || reply_count_ > 0);
    }
    return kLookUs;
}

} // namespace net
