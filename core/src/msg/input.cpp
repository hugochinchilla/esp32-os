#include "core/msg/input.hpp"

#include <cstring>

namespace msg {

namespace {

ports::InputSink &target_of(const Message &m) { return *static_cast<ports::InputSink *>(m.context); }

void run_key(const Message &m) { target_of(m).key(static_cast<uint16_t>(m.a), (m.a >> 16) != 0); }

void run_mouse(const Message &m)
{
    target_of(m).mouse({(m.b >> 16) != 0, static_cast<int16_t>(m.a), static_cast<int16_t>(m.a >> 16),
                        static_cast<uint8_t>(m.b), static_cast<int8_t>(m.b >> 8)});
}

// Up to seven bytes of text and the zero that ends it, in the two values.
void run_text(const Message &m)
{
    char text[8];
    memcpy(text, &m.a, 4);
    memcpy(text + 4, &m.b, 4);
    text[7] = '\0';
    target_of(m).text(text);
}

} // namespace

void PostedInput::send(Handler fn, uint32_t a, uint32_t b)
{
    if (!post(proc::kNone, fn, &target_, a, b, wait_ms_)) {
        lost_++;
    }
}

void PostedInput::key(uint16_t usage, bool pressed) { send(&run_key, usage | (pressed ? 0x10000u : 0u), 0); }

void PostedInput::mouse(const ports::MouseEvent &e)
{
    send(&run_mouse, static_cast<uint16_t>(e.x) | static_cast<uint32_t>(static_cast<uint16_t>(e.y)) << 16,
         e.buttons | static_cast<uint32_t>(static_cast<uint8_t>(e.wheel)) << 8 | (e.absolute ? 0x10000u : 0u));
}

void PostedInput::text(const char *utf8)
{
    size_t left = strlen(utf8);
    while (left > 0) {
        size_t take = left < 7 ? left : 7;
        // Not in the middle of a character: its later bytes are 10xxxxxx.
        while (take > 1 && take < left && (static_cast<uint8_t>(utf8[take]) & 0xC0) == 0x80) {
            take--;
        }
        char part[8] = {};
        memcpy(part, utf8, take);
        uint32_t a, b;
        memcpy(&a, part, 4);
        memcpy(&b, part + 4, 4);
        send(&run_text, a, b);
        utf8 += take;
        left -= take;
    }
}

} // namespace msg
