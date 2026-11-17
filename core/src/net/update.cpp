#include "core/net/update.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/dbg/crc.hpp"

namespace net {

void Update::log(const char *format, ...)
{
    if (log_ == nullptr) {
        return;
    }
    char text[128];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log_(text, log_context_);
}

void Update::hang_up()
{
    if (begun_) {
        updater_.abandon();
        begun_ = false;
    }
    if (stream_ != nullptr) {
        stream_->close();
        stream_ = nullptr;
    }
    state_ = State::Idle;
    more_ = false;
}

// The last thing said to the sender; the connection ends once it is out.
void Update::answer(const char *text, bool good)
{
    reply_size_ = static_cast<size_t>(snprintf(reply_, sizeof(reply_), "%s\n", text));
    if (reply_size_ >= sizeof(reply_)) {
        reply_size_ = sizeof(reply_) - 1;
    }
    reply_sent_ = 0;
    good_ = good;
    state_ = State::Reply;
    more_ = false;
    last_us_ = clock_.micros();
}

void Update::fail(const char *why)
{
    if (begun_) {
        updater_.abandon();
        begun_ = false;
    }
    failed_++;
    log("update: given up: %s", why);
    char text[80];
    snprintf(text, sizeof(text), "ERROR %s", why);
    answer(text, false);
}

// The first line: UPDATE <bytes> <check> [restart]
void Update::header()
{
    char *words[5] = {};
    int count = 0;
    for (char *at = line_; *at != 0 && count < 5;) {
        while (*at == ' ') {
            *at++ = 0;
        }
        if (*at == 0) {
            break;
        }
        words[count++] = at;
        while (*at != 0 && *at != ' ') {
            at++;
        }
    }
    char *end = nullptr;
    if (count < 3 || count > 4 || strcmp(words[0], "UPDATE") != 0) {
        fail("say UPDATE <bytes> <crc32> [restart]");
        return;
    }
    const unsigned long bytes = strtoul(words[1], &end, 10);
    if (*end != 0 || bytes == 0 || bytes > kLargest) {
        fail("that is not a size an image has");
        return;
    }
    const unsigned long check = strtoul(words[2], &end, 16);
    if (*end != 0 || strlen(words[2]) != 8) {
        fail("the check is eight hex digits");
        return;
    }
    if (count == 4 && strcmp(words[3], "restart") != 0) {
        fail("the last word can only be restart");
        return;
    }
    size_ = static_cast<uint32_t>(bytes);
    check_ = static_cast<uint32_t>(check);
    restart_ = count == 4;
    received_ = 0;
    so_far_ = 0;
    if (!updater_.begin(size_)) {
        fail("no room for it here");
        return;
    }
    begun_ = true;
    log("update: receiving %lu bytes", bytes);
    // Go ahead. The image is read once this has left.
    reply_size_ = static_cast<size_t>(snprintf(reply_, sizeof(reply_), "OK\n"));
    reply_sent_ = 0;
    state_ = State::Ok;
}

void Update::body()
{
    uint8_t piece[kPiece];
    const uint32_t left = size_ - received_;
    const size_t want = left < sizeof(piece) ? left : sizeof(piece);
    const int got = stream_->read(piece, want);
    more_ = false;
    if (got < 0) {
        failed_++;
        log("update: given up: the sender went away at %lu of %lu bytes", static_cast<unsigned long>(received_),
            static_cast<unsigned long>(size_));
        hang_up();
        return;
    }
    if (got == 0) {
        return;
    }
    last_us_ = clock_.micros();
    if (!updater_.write(piece, static_cast<size_t>(got))) {
        fail("it could not be written");
        return;
    }
    so_far_ = dbg::crc32(piece, static_cast<size_t>(got), so_far_);
    received_ += static_cast<uint32_t>(got);
    more_ = static_cast<size_t>(got) == want;
    if (received_ < size_) {
        return;
    }
    if (so_far_ != check_) {
        fail("what arrived is not what was sent (the check differs)");
        return;
    }
    const bool fine = updater_.finish();
    begun_ = false;
    if (!fine) {
        fail("it is not an image this machine can start");
        return;
    }
    done_++;
    log("update: %lu bytes written; %s", static_cast<unsigned long>(size_),
        restart_ ? "restarting into it" : "it runs from the next start");
    answer("DONE", true);
}

void Update::poll()
{
    if (stream_ == nullptr) {
        stream_ = listener_ != nullptr ? listener_->accept() : nullptr;
        if (stream_ == nullptr) {
            return;
        }
        state_ = State::Header;
        line_size_ = 0;
        last_us_ = clock_.micros();
    }
    if (state_ == State::Header) {
        // A byte at a time: what follows the line's end is the image.
        uint8_t one;
        int got;
        while (state_ == State::Header && (got = stream_->read(&one, 1)) == 1) {
            last_us_ = clock_.micros();
            if (one == '\n') {
                line_[line_size_] = 0;
                if (line_size_ > 0 && line_[line_size_ - 1] == '\r') {
                    line_[line_size_ - 1] = 0;
                }
                header();
            } else if (line_size_ < sizeof(line_) - 1) {
                line_[line_size_++] = static_cast<char>(one);
            } else {
                fail("the first line is too long");
            }
        }
        if (state_ == State::Header && got < 0) {
            hang_up();
            return;
        }
    }
    if (state_ == State::Ok) {
        const int sent = stream_->write(reinterpret_cast<const uint8_t *>(reply_) + reply_sent_, reply_size_ - reply_sent_);
        if (sent < 0) {
            failed_++;
            log("update: given up: the sender went away");
            hang_up();
            return;
        }
        reply_sent_ += static_cast<size_t>(sent);
        if (reply_sent_ == reply_size_) {
            state_ = State::Body;
        }
    }
    if (state_ == State::Body) {
        body();
    }
    if (state_ == State::Reply) {
        const int sent = stream_->write(reinterpret_cast<const uint8_t *>(reply_) + reply_sent_, reply_size_ - reply_sent_);
        if (sent < 0) {
            hang_up();
            return;
        }
        reply_sent_ += static_cast<size_t>(sent);
        if (reply_sent_ == reply_size_) {
            const bool good = good_;
            const bool restart = restart_;
            hang_up();
            if (good && written_ != nullptr) {
                written_(restart, written_context_);
            }
            return;
        }
    }
    if (stream_ != nullptr && static_cast<uint32_t>(clock_.micros() - last_us_) > kQuietUs) {
        if (state_ != State::Reply) {
            failed_++;
            log("update: given up: the sender went quiet");
        }
        hang_up();
    }
}

uint32_t Update::watch(Watch &watch)
{
    if (stream_ == nullptr) {
        if (listener_ == nullptr || !listener_->watch(watch)) {
            return listener_ != nullptr ? 100000 : kNever;
        }
        return kNever;
    }
    if (more_) {
        return 0; // more of the image is waiting
    }
    const int handle = stream_->handle();
    if (handle >= 0) {
        const bool sending = state_ == State::Reply || state_ == State::Ok;
        watch.add(handle, !sending, sending);
        return 1000000; // and a look at the clock, for a sender gone quiet
    }
    return 20000;
}

} // namespace net
