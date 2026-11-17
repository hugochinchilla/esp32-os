// The telnet service: the command shell for whoever connects to port 23
// (backlog item 17; docs/journal/2026-11-17-telnet.md). No password, by the
// owner's decision: it is switched on in a settings file, and it is for a
// network that is trusted.
//
// One session at a time; a second caller waits until the first has left.
// The service is one of the services loop's (core/net/services.hpp): all the
// socket work happens there, on the services task, and never waits. The
// session itself (core/sh/sh.hpp) runs on a task of its own, started when
// someone connects and given back when they leave. Between the two:
//
//     typed bytes   the services task takes telnet's own commands out
//                   (IAC ...) and gives the rest to the session's TTY
//     shown bytes   the TTY's sink puts them in a ring; the services task
//                   sends what the connection takes. A program that writes
//                   faster waits for room before each piece (Tty::on_room),
//                   outside every lock, so nothing is lost and a Ctrl+C
//                   still gets through.
//
// The client is asked for character-at-a-time mode with our echo (WILL
// ECHO, WILL SUPPRESS-GO-AHEAD), so the TTY's own line editing works, and
// for its window size (DO NAWS), which ted and top use.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/net/services.hpp"
#include "core/ports.hpp"
#include "core/sh/sh.hpp"
#include "core/tty/tty.hpp"

namespace net {

// What the machine lends a telnet session.
struct TelnetKit {
    ports::Lock *lock = nullptr;     // the TTY's
    ports::Event *typed = nullptr;   // the session sleeps on it until a line is typed
    ports::Event *room = nullptr;    // and on this one while its output has nowhere to go
    // Where the session's task comes from. Not the adapter the UI task
    // starts jobs with: this one is called from the services task.
    ports::Tasks *tasks = nullptr;
};

class Telnet : public Service {
public:
    static constexpr int kPort = 23;
    static constexpr size_t kRing = 2048;           // shown bytes waiting to be sent
    static constexpr uint32_t kSessionStack = 12288; // as a Terminal's session
    // While someone is connected the loop looks again this often: what the
    // session writes does not wake a sleep on sockets.
    static constexpr uint32_t kLookUs = 20000;

    // `env` is what the session's commands work with; its `ui` must be a
    // UiCall that reaches the UI task (the session is never on it).
    Telnet(const sh::Env &env, const TelnetKit &kit);
    ~Telnet();
    Telnet(const Telnet &) = delete;
    Telnet &operator=(const Telnet &) = delete;

    void set_listener(Listener *listener) { listener_ = listener; }

    void poll() override;
    // Whoever is connected is hung up on and their session ended.
    void stop() override;
    uint32_t watch(Watch &watch) override;

    bool connected() const { return stream_ != nullptr; }
    uint32_t sessions() const { return sessions_; } // since start
    uint32_t dropped() const { return dropped_; }   // shown bytes that found the ring full
    // Tests: the session's terminal while someone is connected.
    const tty::Tty *terminal() const { return tty_; }

private:
    // The TTY's sink: into the ring. Called with the TTY's lock held, by
    // the session's task or (the echo) the services task, so one at a time.
    class Out : public ports::ByteSink {
    public:
        explicit Out(Telnet &owner) : owner_(owner) {}
        void write(const uint8_t *data, size_t size) override;

    private:
        Telnet &owner_;
    };

    void begin(Stream &stream);
    void end();
    void take_input();
    void typed(uint8_t byte);
    void option(uint8_t verb, uint8_t which);
    void subnegotiation();
    void reply(uint8_t a, uint8_t b, uint8_t c);
    bool send();
    void lost();
    // The tail first: it only ever moves up to a head that was already there.
    size_t waiting() const
    {
        const size_t tail = tail_.load();
        return head_.load() - tail;
    }
    static void run(void *self);
    static void wait_for_room(void *self);

    sh::Env env_;
    TelnetKit kit_;
    Listener *listener_ = nullptr;
    Stream *stream_ = nullptr;
    Out out_{*this};

    // The session of whoever is connected: made when they arrive, in the
    // room kept here, and destroyed when they have gone.
    alignas(tty::Tty) unsigned char tty_room_[sizeof(tty::Tty)];
    alignas(sh::Session) unsigned char session_room_[sizeof(sh::Session)];
    tty::Tty *tty_ = nullptr;
    sh::Session *session_ = nullptr;
    ports::TaskId task_ = nullptr;
    std::atomic<bool> over_{false}; // the session's task has finished
    std::atomic<bool> gone_{false}; // the connection has: nothing more goes out
    uint32_t sessions_ = 0;
    std::atomic<uint32_t> dropped_{0};

    // Shown bytes: head_ is moved by whoever writes, tail_ by the services
    // task. Both only ever grow; the place in ring_ is the count modulo kRing.
    uint8_t ring_[kRing];
    std::atomic<size_t> head_{0}, tail_{0};

    // Telnet's own answers (the services task's alone), sent before the ring.
    uint8_t replies_[48];
    size_t reply_count_ = 0;

    // Reading what the client sends.
    enum class Reading : uint8_t { Text, Command, Option, Sub, SubCommand };
    Reading reading_ = Reading::Text;
    uint8_t verb_ = 0;
    uint8_t sub_[12];
    size_t sub_count_ = 0;
    bool after_cr_ = false;
    uint8_t pending_[64]; // typed bytes on their way to the TTY
    size_t pending_count_ = 0;
};

} // namespace net
