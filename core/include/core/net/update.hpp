// Updates over the network: a new version of the system sent to the board
// and written to the app slot that is not running (backlog item 25;
// docs/journal/2026-11-17-updates.md). `tools/ota.py` is the sender.
//
// One service of the services loop (core/net/services.hpp), one update at a
// time. What the sender says is text, so it can be watched and tried by
// hand:
//
//     sender   UPDATE <bytes> <crc32, 8 hex digits> [restart]\n
//     board    OK\n                      or   ERROR <why>\n   and it hangs up
//     sender   <the image, that many bytes>
//     board    DONE\n                    or   ERROR <why>\n
//
// The check is over the whole image; a wrong one, a sender that goes quiet
// or a connection that breaks leaves the running system as it was. With
// `restart` the machine is told to restart once DONE has been sent. No
// password, as everywhere in this system: the service is off unless a
// settings file switches it on.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/net/services.hpp"
#include "core/ports.hpp"

namespace net {

class Update : public Service {
public:
    static constexpr int kPort = 3232;
    static constexpr size_t kPiece = 1024;          // written at a time
    static constexpr uint32_t kQuietUs = 15000000;  // a sender silent this long is given up
    static constexpr uint32_t kLargest = 16u << 20; // no image is bigger; the updater knows the real limit

    using WrittenFn = void (*)(bool restart, void *context);
    using LogFn = void (*)(const char *line, void *context);

    Update(ports::Updater &updater, ports::Clock &clock) : updater_(updater), clock_(clock) {}

    void set_listener(Listener *listener) { listener_ = listener; }
    // Called on the services task once a new version is in place and the
    // sender has been told, with whether it asked for a restart.
    void on_written(WrittenFn fn, void *context)
    {
        written_ = fn;
        written_context_ = context;
    }
    void set_log(LogFn fn, void *context)
    {
        log_ = fn;
        log_context_ = context;
    }

    void poll() override;
    uint32_t watch(Watch &watch) override;

    bool busy() const { return stream_ != nullptr; }
    uint32_t received() const { return received_; } // of the update under way
    uint32_t size() const { return size_; }
    uint32_t done() const { return done_; }     // updates written since the start
    uint32_t failed() const { return failed_; } // and given up

private:
    enum class State : uint8_t { Idle, Header, Ok, Body, Reply };

    void header();
    void body();
    void answer(const char *text, bool good);
    void fail(const char *why);
    void hang_up();
    void log(const char *format, ...) __attribute__((format(printf, 2, 3)));

    ports::Updater &updater_;
    ports::Clock &clock_;
    Listener *listener_ = nullptr;
    Stream *stream_ = nullptr;
    WrittenFn written_ = nullptr;
    void *written_context_ = nullptr;
    LogFn log_ = nullptr;
    void *log_context_ = nullptr;

    State state_ = State::Idle;
    char line_[64] = "";
    size_t line_size_ = 0;
    uint32_t size_ = 0;
    uint32_t check_ = 0;
    bool restart_ = false;
    uint32_t received_ = 0;
    uint32_t so_far_ = 0;    // the check of what has arrived
    bool begun_ = false;     // the updater has an update open
    bool more_ = false;      // the last read filled its piece: come straight back
    uint32_t last_us_ = 0;   // when bytes last arrived
    char reply_[96] = "";
    size_t reply_size_ = 0, reply_sent_ = 0;
    bool good_ = false;      // the reply being sent is DONE
    uint32_t done_ = 0, failed_ = 0;
};

} // namespace net
