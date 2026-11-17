// Keys and mouse from another task. The window manager takes input on the
// UI task only, so a task that has some (the services task, for a VNC
// viewer) hands it to this instead: each event becomes a message, and the UI
// task gives it to the real sink in its own time, in the order it was sent.
#pragma once

#include <cstdint>

#include "core/msg/queue.hpp"
#include "core/ports.hpp"

namespace msg {

class PostedInput : public ports::InputSink {
public:
    // When the queue is full the sender waits this long for room, then the
    // event is lost and counted. A desktop that is busy for a moment (a long
    // save) holds the viewer back; one that never comes back does not hold
    // the services task for ever.
    static constexpr uint32_t kWaitMs = 1000;

    // `target` is only ever called on the UI task.
    explicit PostedInput(ports::InputSink &target, uint32_t wait_ms = kWaitMs) : target_(target), wait_ms_(wait_ms) {}

    void key(uint16_t usage, bool pressed) override;
    void mouse(const ports::MouseEvent &event) override;
    // Long text goes as several messages, cut between characters.
    void text(const char *utf8) override;

    uint32_t lost() const { return lost_; }

private:
    void send(Handler fn, uint32_t a, uint32_t b);

    ports::InputSink &target_;
    uint32_t wait_ms_;
    uint32_t lost_ = 0;
};

} // namespace msg
