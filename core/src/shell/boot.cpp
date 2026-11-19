#include "core/shell/boot.hpp"

#include <cstdio>
#include <initializer_list>
#include <cstring>

#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/gfx/canvas.hpp"
#include "core/gfx/font.hpp"

namespace shell {

namespace {

struct Screen {
    explicit Screen(ports::Display &d) : display(d)
    {
        clip.set({0, 0, d.width(), d.height()});
    }
    gfx::Canvas canvas() { return gfx::Canvas(display.pixels(), display.width(), display.height(), clip,
                                              {0, 0, display.width(), display.height()}); }
    ports::Display &display;
    gfx::RectList clip;
};

// The address-dependent pattern: each word differs from its neighbours, so
// stuck bits and address faults both show.
inline uint32_t pattern(uintptr_t address) { return static_cast<uint32_t>(address) * 2654435761u ^ 0x5AC3E10Fu; }

} // namespace

bool wait_until(BootIo &io, uint32_t until)
{
    int32_t left;
    while ((left = static_cast<int32_t>(until - io.clock.micros())) > 0) {
        if (io.nap != nullptr && left > static_cast<int32_t>(BootIo::kNapMarginUs)) {
            io.nap(io.context);
        } else if (io.idle != nullptr) {
            io.idle(io.context);
        }
        if (io.skip != nullptr && io.skip(io.context)) {
            io.skipped = true;
            return false;
        }
    }
    return true;
}

void post_beep(BootIo &io, sound::PostResult result)
{
    if (io.sound != nullptr) {
        const sound::Tune code = sound::post_code(result);
        io.sound->beep(code.steps, code.count);
    }
}

void let_beep_end(BootIo &io, sound::PostResult result)
{
    if (io.sound == nullptr) {
        return;
    }
    // An output that plays nothing says so at once. One that never stops
    // saying it plays is not waited for longer than the code lasts.
    const uint32_t until = io.clock.micros() + (sound::length_ms(sound::post_code(result)) + 100) * 1000;
    while (io.sound->playing() && static_cast<int32_t>(until - io.clock.micros()) > 0) {
        if (io.idle != nullptr) {
            io.idle(io.context);
        }
    }
}

void format_bytes(char *out, size_t n, uint64_t bytes)
{
    if (bytes >= 1024u * 1024u) {
        const uint64_t tenths = bytes * 10 / (1024u * 1024u);
        if (tenths % 10 == 0) {
            snprintf(out, n, "%u MB", static_cast<unsigned>(tenths / 10));
        } else {
            snprintf(out, n, "%u.%u MB", static_cast<unsigned>(tenths / 10), static_cast<unsigned>(tenths % 10));
        }
    } else {
        snprintf(out, n, "%u KB", static_cast<unsigned>(bytes / 1024));
    }
}

BootSettings boot_settings(const fs::Storage &storage)
{
    static fs::Ini ini; // 2 KB, read once at start-up
    ini.load(storage, "C:\\.config\\boot.ini");
    auto ms = [](int v) { return v < 0 ? 0u : (v > 30000 ? 30000u : static_cast<uint32_t>(v)); };
    BootSettings s;
    s.fastboot = ini.get_bool("boot", "fastboot", false);
    s.monitor_wait_ms = ms(ini.get_int("boot", "monitor_wait_ms", static_cast<int>(kMonitorWaitMs)));
    const int hold = ini.get_int("boot", "selftest_hold_s", static_cast<int>(kSelfTestHoldS));
    s.hold_s = hold < 0 ? 0u : (hold > 60 ? 60u : static_cast<uint32_t>(hold));
    const int pace = ini.get_int("boot", "selftest_pace", static_cast<int>(kSelfTestPace));
    s.pace_percent = pace < 0 ? 0u : (pace > 400 ? 400u : static_cast<uint32_t>(pace));
    const int crash_hold = ini.get_int("boot", "crash_hold_s", static_cast<int>(kCrashHoldS));
    s.crash_hold_s = crash_hold < 0 ? 0u : (crash_hold > 600 ? 600u : static_cast<uint32_t>(crash_hold));
    s.watchdog_s = ini.get_int("watchdog", "timeout_s", sys::kWatchdogDefaultS);
    return s;
}

int run_quiet_checks(BootIo &io)
{
    int failures = 0;
    char v[96], line[160];
    uint32_t value = 0;
    const struct {
        const char *label;
        ports::Fact fact;
        bool bytes;
    } checks[] = {{"Processor", ports::Fact::Processor, false}, {"Internal RAM", ports::Fact::InternalRam, true},
                  {"External RAM", ports::Fact::ExternalRam, true}, {"Flash", ports::Fact::Flash, true},
                  {"Video", ports::Fact::Video, false}, {"Input", ports::Fact::Input, false},
                  {"Sound", ports::Fact::Sound, false}};
    for (const auto &c : checks) {
        v[0] = 0;
        const bool ok = io.system.fact(c.fact, value, v, sizeof(v)) && (!c.bytes || value > 0);
        if (c.bytes && ok) {
            format_bytes(v, sizeof(v), value);
        } else if (c.fact == ports::Fact::Video && ok) {
            snprintf(v, sizeof(v), "%u Hz pixel clock", static_cast<unsigned>(value));
        }
        failures += ok ? 0 : 1;
        if (io.log != nullptr) {
            snprintf(line, sizeof(line), "fast start check: %s: %s: %s", c.label, v, ok ? "OK" : "FAILED");
            io.log(line, io.context);
        }
    }
    if (io.log != nullptr) {
        io.log("fast start: memory test skipped", io.context);
    }
    return failures;
}

MemoryTestResult test_memory(ports::Memory &memory, ports::Clock &clock, uint32_t budget_us,
                             void (*progress)(uint32_t kb, void *context), void *context, bool internal)
{
    MemoryTestResult r;
    // External RAM: two chunks are larger than the data cache, so what is
    // checked is read back from the memory itself. Internal RAM has no cache.
    const size_t chunk = internal ? 4 * 1024 : 128 * 1024;
    constexpr int kMaxBlocks = 16;
    void *blocks[kMaxBlocks];
    int nblocks = 0;
    const uint32_t start = clock.micros();
    uint32_t outside = 0; // time spent in progress(): not part of the budget
    auto spent = [&]() { return clock.micros() - start - outside; };
    uint64_t done = 0;
    while (r.ok && nblocks < kMaxBlocks && spent() < budget_us) {
        size_t got = 0;
        void *block = internal ? memory.borrow_internal(64u * 1024 * 1024, got) : memory.borrow(64u * 1024 * 1024, got);
        if (block == nullptr || got < 2 * chunk) {
            if (block != nullptr) {
                memory.release(block);
            }
            break;
        }
        blocks[nblocks++] = block;
        // Write chunk i + 1 before checking chunk i.
        auto *words = static_cast<volatile uint32_t *>(block);
        const size_t per_chunk = chunk / 4, chunks = got / chunk;
        auto write = [&](size_t ch) {
            for (size_t i = ch * per_chunk; i < (ch + 1) * per_chunk; i++) {
                words[i] = pattern(reinterpret_cast<uintptr_t>(&words[i]));
            }
        };
        write(0);
        for (size_t ch = 0; ch < chunks && r.ok; ch++) {
            if (ch + 1 < chunks) {
                write(ch + 1);
            }
            for (size_t i = ch * per_chunk; i < (ch + 1) * per_chunk; i++) {
                const uint32_t want = pattern(reinterpret_cast<uintptr_t>(&words[i]));
                const uint32_t v = words[i];
                if (v != want) {
                    r.ok = false;
                    r.bad_address = reinterpret_cast<uintptr_t>(&words[i]);
                    r.wrote = want;
                    r.read = v;
                    break;
                }
            }
            done += chunk;
            if (progress != nullptr && (!internal || ch % 4 == 3 || ch + 1 == chunks)) {
                const uint32_t before = clock.micros();
                progress(static_cast<uint32_t>(done / 1024), context);
                outside += clock.micros() - before;
            }
            if (spent() >= budget_us) {
                break;
            }
        }
    }
    for (int i = 0; i < nblocks; i++) {
        memory.release(blocks[i]);
    }
    r.tested_kb = static_cast<uint32_t>(done / 1024);
    r.us = spent();
    return r;
}

// The mascot's face, as the start-up screens draw it.
namespace {

const char *const kMascot[16] = {
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................"};

// `on_its_back`: drawn upside down, for the error screen.
void mascot(gfx::Canvas &c, int x, int y, int s, gfx::Colour outline, gfx::Colour fur, bool on_its_back = false)
{
    for (int r = 0; r < 16; r++) {
        for (int q = 0; q < 16; q++) {
            gfx::Colour col;
            switch (kMascot[on_its_back ? 15 - r : r][q]) {
            case 'G':
            case 'D': col = outline; break;
            case 'L': col = fur; break;
            case 'P': col = gfx::rgb(7, 4, 2); break;
            case 'K': col = gfx::kBlack; break;
            case 'N': col = gfx::rgb(7, 3, 2); break;
            default: continue;
            }
            c.fill({x + q * s, y + r * s, s, s}, col);
        }
    }
}

} // namespace

void draw_mascot(gfx::Canvas &c, int x, int y, int scale, gfx::Colour outline, gfx::Colour fur)
{
    mascot(c, x, y, scale, outline, fur);
}

// ---- The self-test screen: the approved drawing (docs/look/boot/
// boot-b-self-test.png), as option_b() in tools/mockup/boot_drafts.py draws
// it, with real values. ----

namespace {

constexpr gfx::Colour kBlackC = gfx::kBlack;
constexpr gfx::Colour kWhiteC = gfx::rgb(7, 7, 3);
constexpr gfx::Colour kGreyC = gfx::rgb(5, 5, 2);
constexpr gfx::Colour kDarkC = gfx::rgb(2, 2, 1);
constexpr gfx::Colour kYellowC = gfx::rgb(7, 7, 0);
constexpr gfx::Colour kRedC = gfx::rgb(7, 1, 0);
constexpr int kLeft = 24, kFirstY = 24, kStepY = 18;
constexpr uint32_t kLineMs = 120;   // between lines at 100 % pace
constexpr uint32_t kCountStepMs = 22; // between memory count updates at 100 %

// Spleen 8x16 text, every pixel of each character cell written once
// (foreground or background), so a line can be written again without a
// flash.
void text16(gfx::Canvas &c, int x, int y, const char *s, gfx::Colour fg, int min_chars = 0)
{
    const gfx::WideFont &f = gfx::kSpleen8x16;
    int n = 0;
    for (; *s != 0 || n < min_chars; n++, x += f.width) {
        const uint8_t *g = *s != 0 ? f.glyph(*s) : nullptr;
        for (int row = 0; row < f.height; row++) {
            const uint8_t bits = g != nullptr ? g[row] : 0;
            for (int q = 0; q < f.width; q++) {
                c.fill({x + q, y + row, 1, 1}, (bits & (0x80 >> q)) ? fg : kBlackC);
            }
        }
        if (*s != 0) {
            s++;
        }
    }
}

void line(gfx::Canvas &c, int index, const char *s, gfx::Colour colour)
{
    text16(c, kLeft, kFirstY + index * kStepY, s, colour, 60); // the rest of the line cleared
}

void frame(gfx::Canvas &c, int w, int h, const char *version)
{
    c.fill({0, 0, w, h}, kBlackC);
    mascot(c, w - 16 * 5 - 24, 16, 5, kGreyC, kWhiteC);
    char t[48];
    snprintf(t, sizeof(t), "Chinchilla BIOS %s", version);
    line(c, 0, t, kWhiteC);
    line(c, 1, "Copyleft 2026, built on nine resistors", kGreyC);
    text16(c, kLeft, h - 40, "Press DEL to enter SETUP (there is no setup)", kDarkC);
}

void memory_line(char *out, size_t n, bool internal, uint32_t kb, bool done)
{
    snprintf(out, n, "%s: %6uK%s", internal ? "               " : "Memory Test   ", static_cast<unsigned>(kb),
             done ? (internal ? " OK   (internal)" : " OK   (external)") : "");
}

struct Counting {
    BootIo *io;
    gfx::Canvas *c;
    int index;
    bool internal;
};

void count_progress(uint32_t kb, void *context)
{
    auto *p = static_cast<Counting *>(context);
    char t[64];
    memory_line(t, sizeof(t), p->internal, kb, false);
    line(*p->c, p->index, t, kGreyC);
    if (p->io->skip == nullptr || !p->io->skipped) {
        wait_until(*p->io, p->io->clock.micros() + kCountStepMs * p->io->pace_percent * 10);
    }
}

} // namespace

void draw_self_test(gfx::Canvas &c, int w, int h, const SelfTestValues &v)
{
    frame(c, w, h, v.version);
    char t[96];
    snprintf(t, sizeof(t), "Main Processor : %s", v.processor);
    line(c, 3, t, kGreyC);
    memory_line(t, sizeof(t), false, v.external_kb, true);
    line(c, 4, t, kGreyC);
    memory_line(t, sizeof(t), true, v.internal_kb, true);
    line(c, 5, t, kGreyC);
    snprintf(t, sizeof(t), "Detecting video ....... %s", v.video);
    line(c, 7, t, kGreyC);
    snprintf(t, sizeof(t), "Detecting drive C: .... %s", v.drive);
    line(c, 8, t, kGreyC);
    snprintf(t, sizeof(t), "Detecting mouse ....... %s", v.mouse);
    line(c, 9, t, kGreyC);
    snprintf(t, sizeof(t), "Detecting sound ....... %s", v.sound);
    line(c, 10, t, kGreyC);
    line(c, 12, "Starting Chinchilla...", kYellowC);
}

int run_self_test(BootIo &io, bool hold)
{
    Screen s(io.display);
    gfx::Canvas c = s.canvas();
    const int w = io.display.width(), h = io.display.height();
    // The header, mascot and footer at once (an all-black screen made the
    // bench monitor take twice as long to show a picture); the lines start
    // when the monitor shows one, monitor_wait_ms after the video started.
    frame(c, w, h, kVersion);
    if (hold && io.monitor_wait_ms > 0) {
        wait_until(io, io.video_start_us + io.monitor_wait_ms * 1000);
    }
    const uint32_t pace_us = kLineMs * io.pace_percent * 10;
    auto pause = [&]() {
        if (hold && !io.skipped) {
            wait_until(io, io.clock.micros() + pace_us);
        }
    };
    auto log = [&](const char *label, const char *value, bool ok) {
        if (io.log != nullptr) {
            char l[160];
            snprintf(l, sizeof(l), "self-test: %s: %s: %s", label, value, ok ? "OK" : "FAILED");
            io.log(l, io.context);
        }
    };
    int failures = 0;
    // The beep code at the end is for the worst thing found.
    sound::PostResult result = sound::PostResult::Ok;
    auto found = [&](bool passed, sound::PostResult what) {
        failures += passed ? 0 : 1;
        result = passed ? result : sound::worse(result, what);
    };
    char v[96], t[128];
    uint32_t value = 0;
    pause();

    // The processor.
    v[0] = 0;
    bool ok = io.system.fact(ports::Fact::Processor, value, v, sizeof(v));
    snprintf(t, sizeof(t), "Main Processor : %s", ok ? v : "not recognised");
    line(c, 3, t, ok ? kGreyC : kRedC);
    log("Processor", v, ok);
    found(ok, sound::PostResult::NotFatal);
    pause();

    // The memory tests, the count climbing as they go.
    for (int internal = 0; internal < 2; internal++) {
        Counting counting{&io, &c, 4 + internal, internal != 0};
        const MemoryTestResult r =
            test_memory(io.memory, io.clock, internal ? kInternalTestBudgetUs : kMemoryTestBudgetUs,
                        hold ? &count_progress : nullptr, &counting, internal != 0);
        ok = r.ok && r.tested_kb > 0;
        if (!r.ok) {
            snprintf(t, sizeof(t), "%s: error at %08lX", internal ? "               " : "Memory Test   ",
                     static_cast<unsigned long>(r.bad_address));
        } else if (r.tested_kb == 0) {
            snprintf(t, sizeof(t), "%s: none free to test", internal ? "               " : "Memory Test   ");
        } else {
            memory_line(t, sizeof(t), internal != 0, r.tested_kb, true);
        }
        line(c, 4 + internal, t, ok ? kGreyC : kRedC);
        snprintf(v, sizeof(v), "%uK %s in %u ms%s", static_cast<unsigned>(r.tested_kb),
                 internal ? "internal" : "external", static_cast<unsigned>(r.us / 1000),
                 r.ok ? "" : ", a word read back wrong");
        log("Memory test", v, ok);
        if (!r.ok && io.log != nullptr) {
            snprintf(t, sizeof(t), "self-test: memory error at 0x%08lX: wrote %08lX, read %08lX",
                     static_cast<unsigned long>(r.bad_address), static_cast<unsigned long>(r.wrote),
                     static_cast<unsigned long>(r.read));
            io.log(t, io.context);
        }
        found(ok, sound::PostResult::Memory);
        pause();
    }

    // Not on the screen, but checked and logged: the sizes of RAM and flash.
    for (ports::Fact f : {ports::Fact::InternalRam, ports::Fact::ExternalRam, ports::Fact::Flash}) {
        v[0] = 0;
        ok = io.system.fact(f, value, v, sizeof(v)) && value > 0;
        format_bytes(v, sizeof(v), value);
        log(f == ports::Fact::Flash ? "Flash" : (f == ports::Fact::InternalRam ? "Internal RAM" : "External RAM"), v,
            ok);
        found(ok, f == ports::Fact::Flash ? sound::PostResult::NotFatal : sound::PostResult::Memory);
    }

    // Video.
    ok = io.system.fact(ports::Fact::Video, value, v, sizeof(v));
    snprintf(v, sizeof(v), "VGA %dx%d, 256 colours", w, h);
    snprintf(t, sizeof(t), "Detecting video ....... %s", v);
    line(c, 7, t, ok ? kGreyC : kRedC);
    log("Video", v, ok);
    found(ok, sound::PostResult::Video);
    pause();

    // Drive C:.
    ok = false;
    snprintf(v, sizeof(v), "none");
    ports::VolumeInfo info = {};
    if (io.storage != nullptr && io.storage->info('C', info) == ports::FsError::Ok) {
        char size[24];
        format_bytes(size, sizeof(size), info.total_bytes);
        char label[24];
        snprintf(label, sizeof(label), "%s", info.label);
        // "Flash disk" reads "flash disk" in the line; "RAM disk" stays.
        if (label[0] >= 'A' && label[0] <= 'Z' && label[1] >= 'a' && label[1] <= 'z') {
            label[0] = static_cast<char>(label[0] - 'A' + 'a');
        }
        snprintf(v, sizeof(v), "%s %s", size, label);
        ok = true;
    }
    snprintf(t, sizeof(t), "Detecting drive C: .... %s", v);
    line(c, 8, t, ok ? kGreyC : kRedC);
    log("Drive C:", v, ok);
    found(ok, sound::PostResult::Drive);
    pause();

    // Mouse (and keyboard), sound.
    const struct {
        int index;
        const char *label;
        ports::Fact fact;
    } facts[] = {{9, "mouse", ports::Fact::Input}, {10, "sound", ports::Fact::Sound}};
    for (const auto &f : facts) {
        v[0] = 0;
        ok = io.system.fact(f.fact, value, v, sizeof(v));
        snprintf(t, sizeof(t), "Detecting %s ....... %s", f.label, ok ? v : "not found");
        line(c, f.index, t, ok ? kGreyC : kRedC);
        log(f.label, v, ok);
        found(ok, sound::PostResult::NotFatal);
        pause();
    }

    if (failures == 0) {
        line(c, 12, "Starting Chinchilla...", kYellowC);
    } else {
        snprintf(t, sizeof(t), "%d check%s failed. Starting Chinchilla anyway...", failures, failures == 1 ? "" : "s");
        line(c, 12, t, kRedC);
    }
    if (hold) {
        // The beep, as a PC gives when its self-test is done: one short
        // when all is well, a code when not. It sounds over the countdown.
        post_beep(io, result);
    }
    if (hold && !io.skipped) {
        if (io.hold_s > 0) {
            // The finished screen stays, counting down; a key goes on at once.
            line(c, 14, "Press any key to continue", kGreyC);
            for (uint32_t left = io.hold_s; left > 0 && !io.skipped; left--) {
                snprintf(t, sizeof(t), "Booting in %u", static_cast<unsigned>(left));
                line(c, 15, t, kWhiteC);
                wait_until(io, io.clock.micros() + 1000000);
            }
        } else {
            // Long enough to read the last line (longer when something failed).
            wait_until(io, io.clock.micros() + (failures ? 3 : 1) * kSelfTestHoldMs * 1000);
        }
    }
    if (hold) {
        let_beep_end(io, result); // a key ended the wait: the beep still finishes
    }
    return failures;
}

// ---- The error screen after a crash. Own design: the self-test's black
// screen and type, the mascot on its back, a red rule. ----

void draw_crash_screen(gfx::Canvas &c, int w, int h, const sys::CrashRecord &record, bool logged,
                       uint32_t seconds_left)
{
    c.fill({0, 0, w, h}, kBlackC);
    mascot(c, w - 16 * 5 - 24, 16, 5, kGreyC, kWhiteC, true);
    line(c, 0, "Chinchilla had to restart", kWhiteC);
    line(c, 1, "The last session ended badly. This is what was written down:", kGreyC);
    c.fill({kLeft, kFirstY + 2 * kStepY + 6, 60 * 8, 2}, kRedC);

    // The headline, wrapped at spaces to the 60 columns left of the mascot.
    char text[400];
    sys::crash_headline(record, text, sizeof(text));
    int row = 4;
    constexpr int kColumns = 60;
    for (const char *p = text; *p != 0 && row < 8;) {
        int n = static_cast<int>(strlen(p));
        if (n > kColumns) {
            n = kColumns;
            while (n > 0 && p[n] != ' ') {
                n--;
            }
            n = n == 0 ? kColumns : n;
        }
        char piece[kColumns + 1];
        memcpy(piece, p, static_cast<size_t>(n));
        piece[n] = 0;
        line(c, row++, piece, kYellowC);
        p += n;
        while (*p == ' ') {
            p++;
        }
    }

    // The record, a line each.
    row++;
    sys::crash_details(record, text, sizeof(text), "\n");
    for (char *p = text; *p != 0 && row < 19;) {
        char *end = strchr(p, '\n');
        if (end != nullptr) {
            *end = 0;
        }
        text16(c, kLeft, kFirstY + row++ * kStepY, p, kGreyC);
        if (end == nullptr) {
            break;
        }
        p = end + 1;
    }

    row++;
    if (record.kind != sys::CrashKind::Reset) {
        line(c, row++, "Work that had not been saved is lost.", kGreyC);
    }
    snprintf(text, sizeof(text), logged ? "This report was added to %s." : "This report could not be added to %s.",
             sys::kCrashLog);
    line(c, row++, text, kGreyC);

    text16(c, kLeft, h - 58, "Press any key to continue", kGreyC);
    if (seconds_left > 0) {
        snprintf(text, sizeof(text), "Continuing in %u", static_cast<unsigned>(seconds_left));
        text16(c, kLeft, h - 40, text, kWhiteC, 24);
    }
}

void show_crash_screen(BootIo &io, const sys::CrashRecord &record, bool logged, bool hold)
{
    Screen s(io.display);
    gfx::Canvas c = s.canvas();
    const int w = io.display.width(), h = io.display.height();
    draw_crash_screen(c, w, h, record, logged, hold ? io.crash_hold_s : 0);
    if (io.log != nullptr) {
        char t[200];
        sys::crash_headline(record, t, sizeof(t));
        io.log(t, io.context);
    }
    if (!hold) {
        return;
    }
    io.skipped = false;
    // A monitor that is slow to show a picture still gets the whole
    // countdown: it starts when the self-test's lines would.
    if (io.monitor_wait_ms > 0) {
        wait_until(io, io.video_start_us + io.monitor_wait_ms * 1000);
    }
    if (io.crash_hold_s == 0) {
        while (!io.skipped) {
            wait_until(io, io.clock.micros() + 1000000);
        }
    }
    for (uint32_t left = io.crash_hold_s; left > 0 && !io.skipped; left--) {
        char t[32];
        snprintf(t, sizeof(t), "Continuing in %u", static_cast<unsigned>(left));
        text16(c, kLeft, h - 40, t, kWhiteC, 24);
        wait_until(io, io.clock.micros() + 1000000);
    }
    io.skipped = false;
}

} // namespace shell
