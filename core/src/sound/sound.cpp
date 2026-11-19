#include "core/sound/sound.hpp"

namespace sound {

namespace {

const Clip *g_clips = nullptr;

class Silence final : public ports::Sound {
public:
    void play(const int16_t *, size_t) override {}
    void beep(const ports::Beep *, int) override {}
    void stop() override {}
    bool playing() override { return false; }
};

} // namespace

const char *name(Id id)
{
    switch (id) {
    case Id::Boot: return "boot";
    case Id::Close: return "close";
    case Id::Alert: return "alert";
    case Id::Error: return "error";
    case Id::Bell: return "bell";
    }
    return "";
}

void set_clips(const Clip *clips) { g_clips = clips; }

Clip clip(Id id)
{
    const int index = static_cast<int>(id);
    return g_clips != nullptr && index >= 0 && index < kIds ? g_clips[index] : Clip{};
}

ports::Sound &silence()
{
    static Silence none;
    return none;
}

} // namespace sound
