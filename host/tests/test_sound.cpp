// Sound (backlog items 58 and 59): the mixer that stands behind a sample
// output, the beeper's codes at the end of the self-test, and the system's
// sounds where the desktop plays them.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/bios/post.hpp"
#include "core/fs/memory_volume.hpp"
#include "core/fs/samples.hpp"
#include "core/os/os.hpp"
#include "core/shell/boot.hpp"
#include "core/shell/shell.hpp"
#include "core/sound/beeps.hpp"
#include "core/sound/mixer.hpp"
#include "core/sound/sound.hpp"
#include "core/wm/wm.hpp"
#include "../host_adapters.hpp"
#include "../../core/src/shell/internal.hpp"

static int failures = 0;
#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);  \
            failures++;                                                    \
        }                                                                  \
    } while (0)

namespace {

constexpr size_t kRate = ports::kSoundRate;

std::vector<int16_t> render(sound::Mixer &m, size_t count)
{
    std::vector<int16_t> out(count, 1234);
    // In uneven pieces, as a sound card asks.
    for (size_t at = 0; at < count;) {
        const size_t n = count - at < 331 ? count - at : 331;
        m.render(out.data() + at, n);
        at += n;
    }
    return out;
}

int peak(const std::vector<int16_t> &s, size_t from, size_t to)
{
    int most = 0;
    for (size_t i = from; i < to && i < s.size(); i++) {
        most = abs(s[i]) > most ? abs(s[i]) : most;
    }
    return most;
}

// The pitch of a stretch, from how often it crosses zero going up.
double pitch(const std::vector<int16_t> &s, size_t from, size_t to)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = from + 1; i < to; i++) {
        if (s[i - 1] < 0 && s[i] >= 0) {
            first = crossings == 0 ? i : first;
            last = i;
            crossings++;
        }
    }
    return crossings < 2 ? 0 : static_cast<double>(crossings - 1) * kRate / static_cast<double>(last - first);
}

// How much of a stretch is at one pitch, of the loudest it could be.
double level_at(const std::vector<int16_t> &s, size_t from, size_t to, double hz)
{
    double re = 0, im = 0;
    for (size_t i = from; i < to; i++) {
        const double a = 2 * M_PI * hz * static_cast<double>(i) / kRate;
        re += s[i] * cos(a);
        im += s[i] * sin(a);
    }
    return 2 * sqrt(re * re + im * im) / static_cast<double>(to - from);
}

void test_mixer_silence()
{
    sound::Mixer m;
    CHECK(!m.playing());
    const std::vector<int16_t> out = render(m, 2000);
    CHECK(peak(out, 0, out.size()) == 0);
}

void test_mixer_beep()
{
    sound::Mixer m;
    const ports::Beep one[] = {{1000, 200}};
    m.beep(one, 1);
    CHECK(m.playing());
    const std::vector<int16_t> out = render(m, kRate / 2);
    // 200 ms of 1,000 Hz, then nothing.
    const size_t end = kRate / 5;
    CHECK(fabs(pitch(out, 200, end - 200) - 1000) < 3);
    CHECK(peak(out, 200, end - 200) > sound::Mixer::kBeepLevel / 2);
    CHECK(peak(out, 0, out.size()) < 2 * sound::Mixer::kBeepLevel);
    CHECK(peak(out, end + kRate / 20, out.size()) == 0); // 50 ms later: quite gone
    CHECK(!m.playing());
    // It starts and ends without a click: no jump at either end.
    CHECK(abs(out[0]) < 600 && abs(out[1] - out[0]) < 900);
    // A square wave: the third harmonic is there, a third as strong; no
    // pitch that was not asked for (the edges are rounded).
    const double h1 = level_at(out, 441, end - 441, 1000), h3 = level_at(out, 441, end - 441, 3000);
    CHECK(h3 > h1 * 0.2 && h3 < h1 * 0.45);
    CHECK(level_at(out, 441, end - 441, 1500) < h1 * 0.02);
    CHECK(level_at(out, 441, end - 441, 2050) < h1 * 0.02); // where 20 kHz would fold to
}

void test_mixer_code()
{
    // One long, two short: three bursts with silence between.
    sound::Mixer m;
    const sound::Tune code = sound::post_code(sound::PostResult::Video);
    CHECK(code.count == 5 && sound::length_ms(code) == 600 + 150 + 150 + 150 + 150);
    m.beep(code.steps, code.count);
    const std::vector<int16_t> out = render(m, kRate * 3 / 2);
    auto ms = [](int t) { return static_cast<size_t>(t) * kRate / 1000; };
    CHECK(peak(out, ms(100), ms(500)) > 4000);
    CHECK(peak(out, ms(650), ms(740)) == 0);
    CHECK(peak(out, ms(770), ms(880)) > 4000);
    CHECK(peak(out, ms(950), ms(1040)) == 0);
    CHECK(peak(out, ms(1070), ms(1180)) > 4000);
    CHECK(peak(out, ms(1250), out.size()) == 0);
    CHECK(!m.playing());

    // A new call replaces what was playing; none silences.
    m.beep(code.steps, code.count);
    render(m, ms(100));
    const ports::Beep high[] = {{2000, 100}};
    m.beep(high, 1);
    const std::vector<int16_t> next = render(m, ms(300));
    CHECK(fabs(pitch(next, ms(20), ms(90)) - 2000) < 10);
    CHECK(peak(next, ms(150), next.size()) == 0);
    m.beep(code.steps, code.count);
    render(m, ms(100));
    m.beep(nullptr, 0);
    const std::vector<int16_t> hushed = render(m, ms(200));
    CHECK(peak(hushed, ms(50), hushed.size()) == 0 && !m.playing());

    // More steps than it takes: the first kBeepSteps.
    std::vector<ports::Beep> many(ports::kBeepSteps + 8, ports::Beep{800, 10});
    m.beep(many.data(), static_cast<int>(many.size()));
    const std::vector<int16_t> run = render(m, ms(600));
    CHECK(peak(run, ms(200), ms(300)) > 4000);
    CHECK(peak(run, ms(10 * ports::kBeepSteps + 50), run.size()) == 0);
}

void test_mixer_samples()
{
    // Recorded samples go out as they are, and with the beeper on top.
    std::vector<int16_t> clip(3000);
    for (size_t i = 0; i < clip.size(); i++) {
        clip[i] = static_cast<int16_t>((i * 37) % 2001 - 1000);
    }
    sound::Mixer m;
    m.play(clip.data(), clip.size());
    CHECK(m.playing());
    std::vector<int16_t> out = render(m, 4000);
    CHECK(memcmp(out.data(), clip.data(), clip.size() * 2) == 0);
    CHECK(peak(out, clip.size(), out.size()) == 0 && !m.playing());

    // A new one takes the place of the one playing.
    m.play(clip.data(), clip.size());
    render(m, 1000);
    m.play(clip.data() + 500, 100);
    out = render(m, 500);
    CHECK(memcmp(out.data(), clip.data() + 500, 200) == 0 && peak(out, 100, 500) == 0);

    // Both voices, and stop() ends both.
    const ports::Beep one[] = {{1000, 500}};
    m.play(clip.data(), clip.size());
    m.beep(one, 1);
    out = render(m, 2000);
    bool differs = false;
    for (size_t i = 500; i < 2000; i++) {
        differs = differs || out[i] != clip[i];
    }
    CHECK(differs);
    m.stop();
    out = render(m, 2000);
    CHECK(peak(out, 200, 2000) == 0 && !m.playing());

    // Too loud together: held at the limit, not wrapped round.
    std::vector<int16_t> loud(2000, 32000);
    m.play(loud.data(), loud.size());
    m.beep(one, 1);
    out = render(m, 2000);
    int lowest = 32767;
    for (size_t i = 0; i < 2000; i++) {
        lowest = out[i] < lowest ? out[i] : lowest;
    }
    CHECK(lowest > 20000 && peak(out, 0, 2000) == 32767);
}

void test_codes()
{
    using sound::PostResult;
    CHECK(sound::post_code(PostResult::Ok).count == 1);
    CHECK(sound::post_code(PostResult::Ok).steps[0].hz == 1000 && sound::post_code(PostResult::Ok).steps[0].ms == 200);
    CHECK(sound::post_code(PostResult::NotFatal).count == 3);
    CHECK(sound::post_code(PostResult::Memory).count == 5);
    CHECK(sound::post_code(PostResult::Drive).count == 7);
    CHECK(sound::worse(PostResult::Ok, PostResult::Drive) == PostResult::Drive);
    CHECK(sound::worse(PostResult::Memory, PostResult::Drive) == PostResult::Memory);
    for (PostResult r : {PostResult::NoSystem, PostResult::Memory, PostResult::Video, PostResult::Drive,
                         PostResult::NotFatal, PostResult::Ok}) {
        CHECK(sound::post_code(r).count <= ports::kBeepSteps);
    }
}

// A clock that moves on 1 ms each time it is read.
class SteppingClock : public ports::Clock {
public:
    uint32_t micros() override { return now += 1000; }
    uint32_t now = 0;
};

// The beep at the end of the self-test, on both of its screens.
void test_self_test_beep()
{
    host::MemoryDisplay screen(640, 480);
    host::HostSystem system;
    static host::HostMemory for_disk;
    static fs::Storage drives;
    static fs::MemoryVolume disk(for_disk.allocate(1 << 16), 1 << 16, "RAM disk");
    drives.mount('C', disk);

    // All well: one short beep, started when the checks are done.
    {
        SteppingClock clock;
        host::HostMemory memory; // its memory test lends once
        host::RecordingSound out;
        shell::BootIo io{screen, system, memory, clock, &drives, nullptr, nullptr, nullptr};
        io.sound = &out;
        io.hold_s = 0;
        CHECK(shell::run_self_test(io) == 0);
        CHECK(out.beep_calls == 1 && out.beeps.size() == 1);
        CHECK(out.beeps[0].hz == sound::kBeepHz && out.beeps[0].ms == sound::kOkMs);
    }
    // No drive: one long, three short. A key does not cut the beep short:
    // the self-test waits for it, but not for ever.
    {
        SteppingClock clock;
        host::HostMemory memory; // its memory test lends once
        host::RecordingSound out;
        out.busy = true; // an output that never says it has finished
        shell::BootIo io{screen, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
        io.sound = &out;
        io.skip = [](void *) { return true; };
        io.idle = [](void *) {};
        const uint32_t before = clock.now;
        CHECK(shell::run_self_test(io) == 1);
        CHECK(out.beep_calls == 1 && out.beeps.size() == 7 && out.beeps[0].ms == sound::kLongMs);
        const uint32_t code_us = sound::length_ms(sound::post_code(sound::PostResult::Drive)) * 1000;
        CHECK(clock.now - before >= code_us && clock.now - before < code_us + 2000000);
    }
    // Asked not to hold (the debug link's screenshot of it): no beep. And
    // none without an output, or on a fast start.
    {
        SteppingClock clock;
        host::HostMemory memory; // its memory test lends once
        host::RecordingSound out;
        shell::BootIo io{screen, system, memory, clock, &drives, nullptr, nullptr, nullptr};
        io.sound = &out;
        shell::run_self_test(io, false);
        shell::run_quiet_checks(io);
        CHECK(out.beep_calls == 0);
        host::HostMemory memory2;
        shell::BootIo quiet{screen, system, memory2, clock, &drives, nullptr, nullptr, nullptr};
        quiet.hold_s = 0;
        CHECK(shell::run_self_test(quiet) == 0);
    }
    // The BIOS's screen: the same, and it says what it found.
    {
        SteppingClock clock;
        host::HostMemory memory; // its memory test lends once
        host::RecordingSound out;
        shell::BootIo io{screen, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
        io.sound = &out;
        io.hold_s = 0;
        static bios::TextScreen text;
        bios::Post post{io, text, "TEST"};
        post.drive = "11 MB flash disk";
        CHECK(bios::run_post(post) == 0);
        CHECK(post.result == sound::PostResult::Ok && out.beep_calls == 1 && out.beeps.size() == 1);

        // Nothing to start: long beeps.
        host::RecordingSound out2;
        host::HostMemory memory2;
        shell::BootIo io2{screen, system, memory2, clock, nullptr, nullptr, nullptr, nullptr};
        io2.sound = &out2;
        io2.hold_s = 0;
        bios::Post none{io2, text, "TEST"};
        none.drive = "11 MB flash disk";
        none.boot_device = [](char *t, size_t n, void *) {
            snprintf(t, n, "none");
            return false;
        };
        CHECK(bios::run_post(none) == 1 && !none.bootable);
        CHECK(none.result == sound::PostResult::NoSystem && out2.beep_calls == 1);
        CHECK(out2.beeps.size() == 2 && out2.beeps[0].ms == sound::kLongMs && out2.beeps[1].hz == 0);
    }
}

// The system's sounds, where the desktop plays them.
host::MemoryDisplay display(640, 480);
host::MemoryPointer pointer;
host::FixedClock clock_;
host::HostSystem system_;
host::HostMemory memory;
fs::Storage storage;
wm::Wm desktop(display, pointer, clock_);

const int16_t kBoot[4] = {1, 2, 3, 4}, kClose[3] = {5, 6, 7}, kAlert[2] = {8, 9}, kError[5] = {1, 1, 1, 1, 1},
              kBell[1] = {3}, kGames[6] = {};
const sound::Clip kClips[sound::kIds] = {{kBoot, 4},      {kClose, 3},     {kAlert, 2},     {kError, 5},
                                         {kBell, 1},      {&kGames[0], 1}, {&kGames[1], 1}, {&kGames[2], 1},
                                         {&kGames[3], 1}, {&kGames[4], 1}, {&kGames[5], 1}};

void press(uint16_t usage)
{
    desktop.key(usage, true);
    desktop.key(usage, false);
}

// Culebra (backlog item 70): a berry eaten and the game's end are heard;
// steering, pausing and a new game are not.
void test_culebra_sounds(host::RecordingSound &out)
{
    using games::Snake;
    const size_t before = out.played.size();
    shell::open_culebra();
    const Snake &g = shell::culebra_game();
    const uint16_t keys[4] = {wm::key::Up, wm::key::Right, wm::key::Down, wm::key::Left};
    for (int steps = 0; steps < 600 && g.berries() < 1 && g.state() != Snake::State::Over; steps++) {
        const int dx = g.berry_col() - g.head_col(), dy = g.berry_row() - g.head_row();
        const int want = dx != 0 ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
        // Not back onto its own neck: around, when the berry is behind.
        const int col = g.head_col() + (want == 1) - (want == 3), row = g.head_row() + (want == 2) - (want == 0);
        press(g.at(col, row).kind == Snake::Kind::Body ? keys[(want + 1) & 3] : keys[want]);
        CHECK(g.berries() > 0 || out.played.size() == before);
        clock_.now += 130000;
        desktop.tick();
    }
    CHECK(g.berries() == 1 && out.played.size() == before + 1 && out.played.back() == &kGames[3]);
    // Into the wall.
    for (int steps = 0; steps < 100 && g.state() == Snake::State::Playing; steps++) {
        clock_.now += 130000;
        desktop.tick();
    }
    CHECK(g.state() == Snake::State::Over && out.played.back() == &kGames[4]);
    const size_t after = out.played.size();
    press(wm::key::Space); // a new game: nothing
    CHECK(out.played.size() == after);
    desktop.key(wm::key::LeftAlt, true);
    press(wm::key::F4);
    desktop.key(wm::key::LeftAlt, false);
    CHECK(desktop.window_count() == 0);
}

void test_system_sounds()
{
    CHECK(strcmp(sound::name(sound::Id::Boot), "boot") == 0 && strcmp(sound::name(sound::Id::Bell), "bell") == 0);
    // No output and no recordings: the calls do nothing, and nothing waits.
    CHECK(sound::clip(sound::Id::Boot).count == 0);
    os::play_sound(sound::Id::Boot);
    const ports::Beep one[] = {{1000, 200}};
    os::beep(one, 1);
    CHECK(!os::sound_playing());

    // An output without recordings (they are not part of the core): the
    // beeper only.
    host::RecordingSound out;
    os::install_sound(&out);
    os::play_sound(sound::Id::Alert);
    CHECK(out.played.empty());
    os::beep(one, 1);
    CHECK(out.beep_calls == 1 && out.beeps[0].hz == 1000);
    out.busy = true;
    CHECK(os::sound_playing());
    out.busy = false;

    sound::set_clips(kClips);
    os::play_sound(sound::Id::Error);
    CHECK(out.played.size() == 1 && out.played[0] == kError && out.played_count == 5);
    out.played.clear();

    // The desktop comes up: the welcome chime.
    static fs::MemoryVolume ram_disk(memory.allocate(1 << 20), 1 << 20, "RAM disk");
    storage.mount('C', ram_disk);
    fs::add_samples(storage, 'C');
    shell::start(desktop, system_, storage, memory);
    desktop.start();
    CHECK(out.played.size() == 1 && out.played[0] == kBoot && out.played_count == 4);

    // A message box knocks; one about memory running out sounds the error.
    shell::message_box("Note", "Something to read.");
    CHECK(out.played.size() == 2 && out.played[1] == kAlert);
    desktop.key(wm::key::Enter, true);
    desktop.key(wm::key::Enter, false);
    shell::no_memory_for("Chess");
    CHECK(out.played.size() == 4 && out.played[3] == kError);
    desktop.key(wm::key::Enter, true);
    desktop.key(wm::key::Enter, false);
    CHECK(desktop.window_count() == 0);

    CHECK(strcmp(sound::name(sound::Id::Crumple), "crumple") == 0 && sound::clip(sound::Id::Crumple).samples == &kGames[5]);
    test_culebra_sounds(out);
    out.played.resize(4);

    // Shut Down: the closing melody, once, with the last screen.
    CHECK(shell::power_down() && shell::powered_down());
    CHECK(out.played.size() == 5 && out.played[4] == kClose);
    CHECK(shell::power_down());
    CHECK(out.played.size() == 5);

    // The output taken away again: silence.
    os::install_sound(nullptr);
    os::play_sound(sound::Id::Boot);
    CHECK(out.played.size() == 5);
}

} // namespace

int main()
{
    test_mixer_silence();
    test_mixer_beep();
    test_mixer_code();
    test_mixer_samples();
    test_codes();
    test_self_test_beep();
    test_system_sounds(); // last: it ends on the power-down screen
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
