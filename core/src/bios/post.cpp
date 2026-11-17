#include "core/bios/post.hpp"

#include <cstdio>
#include <initializer_list>

namespace bios {

namespace {

constexpr uint32_t kLineMs = 120; // between lines at 100 % pace

// The first screen: the mascot, 16 squares of 8 pixels, and the badge's
// leaf, 12 squares of 4.
constexpr int kBigMascotColumn = 4, kBigMascotRow = 2, kBigMascotScale = 8;
constexpr int kLeafColumn = 62, kLeafRow = 2, kLeafScale = 4;
const char *const kLeafRows[12] = {
    ".........GGG", ".......GGGGG", ".....GGGYGGG", "....GGGYYGGG", "...GGGYYGGG.", "..GGGYYYYGG.",
    "..GGGGGYYGG.", ".GGGGGYYGG..", ".GGGGYYGGG..", ".GGGYGGGG...", "D.GGGGGG....", "DD.........."};

struct Painter {
    explicit Painter(ports::Display &d) : display(d) { clip.set({0, 0, d.width(), d.height()}); }
    gfx::Canvas canvas()
    {
        return gfx::Canvas(display.pixels(), display.width(), display.height(), clip,
                           {0, 0, display.width(), display.height()});
    }
    ports::Display &display;
    gfx::RectList clip;
};

void header(TextScreen &s, const char *version, const char *id)
{
    s.clear();
    s.reserve(kLogoColumn, kLogoRow, kLogoColumns, kLogoRows);
    char t[64];
    snprintf(t, sizeof(t), "Rodent BIOS %s", version);
    s.move_to(kTitleRow);
    s.print("  ", kText);
    s.print(t, kBright);
    s.move_to(kTitleRow + 1);
    s.print("  Copyleft 2026, built on nine resistors", kText);
    // The bottom lines have a fixed place on the screen; on serial they
    // follow the header.
    const char *setup = "Press DEL to enter SETUP (there is no setup)";
    s.put(kLeftColumn, kSetupRow, setup, kDim);
    s.move_to(kTitleRow + 2);
    s.send_only("  ");
    s.send_only(setup);
    if (id != nullptr) {
        s.put(kLeftColumn, kIdRow, id, kDim);
        s.send_only("  ");
        s.send_only(id);
    }
}

// A line through the cursor: the label in `colour`, then the value, bright
// when `bright_value`.
void line(TextScreen &s, int row, const char *label, const char *value, gfx::Colour colour,
          bool bright_value = false)
{
    s.move_to(row);
    if (s.cursor_column() != 0) {
        s.restart_line();
    }
    s.print("  ", colour);
    s.print(label, colour);
    s.print(value, bright_value ? kBright : colour);
}

const char *memory_label(bool internal) { return internal ? "               : " : "Memory Test    : "; }

void memory_value(char *out, size_t n, uint32_t kb, bool done)
{
    snprintf(out, n, "%6uK%s", static_cast<unsigned>(kb), done ? " OK" : "");
}

void memory_done(TextScreen &s, bool internal, uint32_t kb)
{
    char t[32];
    memory_value(t, sizeof(t), kb, true);
    line(s, kMemoryRow + (internal ? 1 : 0), memory_label(internal), t, kText, true);
    s.print(internal ? "   (internal)" : "   (external)", kText);
}

struct Counting {
    Post *post;
    gfx::Canvas *c;
    bool internal;
};

void count_progress(uint32_t kb, void *context)
{
    auto *p = static_cast<Counting *>(context);
    char t[32];
    memory_value(t, sizeof(t), kb, false);
    line(p->post->screen, kMemoryRow + (p->internal ? 1 : 0), memory_label(p->internal), t, kText);
    // No waiting here: the count climbs as fast as the memory is tested.
    p->post->screen.paint(*p->c);
}

} // namespace

void paint_frame(gfx::Canvas &c, TextScreen &screen)
{
    c.fill({0, 0, kColumns * kCellWidth, kRows * kCellHeight}, gfx::kBlack);
    shell::draw_mascot(c, kLogoColumn * kCellWidth, kLogoRow * kCellHeight, kLogoScale, kText, kBright);
    screen.paint_all(c);
}

void draw_splash(gfx::Canvas &c, TextScreen &s, const char *version, const char *id)
{
    s.clear();
    s.reserve(kBigMascotColumn, kBigMascotRow, 16, 8);
    s.reserve(kLeafColumn, kLeafRow, 6, 3);
    s.put(58, 7, "Energy Nibbler", kLeaf);
    s.put(56, 8, "Runs on a USB port", kText);
    char t[64];
    snprintf(t, sizeof(t), "Rodent BIOS %s", version);
    s.put(4, 12, t, kBright);
    s.put(4, 13, "Copyleft 2026, built on nine resistors", kText);
    s.put(4, 16, "Waking the monitor...", kDim);
    s.put(kLeftColumn, kSetupRow, "Press DEL to enter SETUP (there is no setup)", kDim);
    if (id != nullptr) {
        s.put(kLeftColumn, kIdRow, id, kDim);
    }

    c.fill({0, 0, kColumns * kCellWidth, kRows * kCellHeight}, gfx::kBlack);
    shell::draw_mascot(c, kBigMascotColumn * kCellWidth, kBigMascotRow * kCellHeight, kBigMascotScale, kText, kBright);
    for (int r = 0; r < 12; r++) {
        for (int q = 0; q < 12; q++) {
            const char ch = kLeafRows[r][q];
            if (ch != '.') {
                c.fill({kLeafColumn * kCellWidth + q * kLeafScale, kLeafRow * kCellHeight + r * kLeafScale, kLeafScale,
                        kLeafScale},
                       ch == 'G' ? kLeaf : (ch == 'Y' ? kYellow : kLeafDark));
            }
        }
    }
    s.paint_all(c);
    // The ring round the leaf, two pixels: the midpoint circle about the
    // leaf's middle; drawn last, over the cells' black.
    const int cx = kLeafColumn * kCellWidth + 6 * kLeafScale, cy = kLeafRow * kCellHeight + 6 * kLeafScale;
    for (int radius : {37, 36}) {
        int x = radius, y = 0, err = 1 - radius;
        while (x >= y) {
            const int points[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
            for (const auto &p : points) {
                c.fill({cx + p[0], cy + p[1], 1, 1}, kLeaf);
            }
            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    }
}

void run_splash(Post &post, uint32_t ms)
{
    shell::BootIo &io = post.io;
    Painter painter(io.display);
    gfx::Canvas c = painter.canvas();
    draw_splash(c, post.screen, shell::kVersion, post.id);
    if (ms > 0 && !io.skipped) {
        shell::wait_until(io, io.video_start_us + ms * 1000);
    }
    io.video_start_us = io.clock.micros();
}

void draw_post(gfx::Canvas &c, TextScreen &s, const PostValues &v)
{
    s.set_out(nullptr, nullptr);
    header(s, v.version, v.id);
    line(s, kProcessorRow, "Main Processor : ", v.processor, kText);
    memory_done(s, false, v.external_kb);
    memory_done(s, true, v.internal_kb);
    line(s, kVideoRow, "Detecting video ....... ", v.video, kText);
    line(s, kDriveRow, "Detecting drive C: .... ", v.drive, kText);
    line(s, kInputRow, "Detecting keyboard .... ", v.input, kText);
    line(s, kSoundRow, "Detecting sound ....... ", v.sound, kText);
    if (v.boot != nullptr) {
        line(s, kBootRow, "Boot device ........... ", v.boot, kText);
    }
    line(s, kSummaryRow, "Starting Chinchilla...", "", kYellow);
    if (v.countdown > 0) {
        char t[32];
        snprintf(t, sizeof(t), "Booting in %u", static_cast<unsigned>(v.countdown));
        line(s, kKeyRow, "Press any key to continue", "", kText);
        line(s, kCountdownRow, t, "", kBright);
    }
    paint_frame(c, s);
}

int run_post(Post &post, bool hold)
{
    shell::BootIo &io = post.io;
    TextScreen &s = post.screen;
    Painter painter(io.display);
    gfx::Canvas c = painter.canvas();
    // The header, the mascot and the bottom lines at once (an all-black
    // screen made the bench monitor take twice as long to show a picture);
    // the lines start when the monitor shows one.
    header(s, shell::kVersion, post.id);
    paint_frame(c, s);
    if (hold && io.monitor_wait_ms > 0) {
        shell::wait_until(io, io.video_start_us + io.monitor_wait_ms * 1000);
    }
    const uint32_t pace_us = kLineMs * io.pace_percent * 10;
    auto shown = [&]() {
        s.paint(c);
        if (hold && !io.skipped) {
            shell::wait_until(io, io.clock.micros() + pace_us);
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
    char v[96], t[128];
    uint32_t value = 0;
    shown();

    // The processor.
    v[0] = 0;
    bool ok = io.system.fact(ports::Fact::Processor, value, v, sizeof(v));
    line(s, kProcessorRow, "Main Processor : ", ok ? v : "not recognised", ok ? kText : kRed);
    log("Processor", v, ok);
    failures += ok ? 0 : 1;
    shown();

    // The memory tests, the count climbing as they go.
    for (int internal = 0; internal < 2; internal++) {
        Counting counting{&post, &c, internal != 0};
        const shell::MemoryTestResult r =
            shell::test_memory(io.memory, io.clock, internal ? shell::kInternalTestBudgetUs : shell::kMemoryTestBudgetUs,
                               hold ? &count_progress : nullptr, &counting, internal != 0);
        ok = r.ok && r.tested_kb > 0;
        if (!r.ok) {
            snprintf(t, sizeof(t), "error at %08lX", static_cast<unsigned long>(r.bad_address));
            line(s, kMemoryRow + internal, memory_label(internal != 0), t, kRed);
        } else if (r.tested_kb == 0) {
            line(s, kMemoryRow + internal, memory_label(internal != 0), "none free to test", kRed);
        } else {
            memory_done(s, internal != 0, r.tested_kb);
        }
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
        failures += ok ? 0 : 1;
        shown();
    }

    // Not on the screen, but checked and logged: the sizes of RAM and flash.
    for (ports::Fact f : {ports::Fact::InternalRam, ports::Fact::ExternalRam, ports::Fact::Flash}) {
        v[0] = 0;
        ok = io.system.fact(f, value, v, sizeof(v)) && value > 0;
        shell::format_bytes(v, sizeof(v), value);
        log(f == ports::Fact::Flash ? "Flash" : (f == ports::Fact::InternalRam ? "Internal RAM" : "External RAM"), v,
            ok);
        failures += ok ? 0 : 1;
    }

    // Video.
    ok = io.system.fact(ports::Fact::Video, value, v, sizeof(v));
    snprintf(v, sizeof(v), "VGA %dx%d, 256 colours", io.display.width(), io.display.height());
    line(s, kVideoRow, "Detecting video ....... ", v, ok ? kText : kRed);
    log("Video", v, ok);
    failures += ok ? 0 : 1;
    shown();

    // Drive C:.
    ok = false;
    snprintf(v, sizeof(v), "none");
    ports::VolumeInfo info = {};
    if (io.storage != nullptr && io.storage->info('C', info) == ports::FsError::Ok) {
        char size[24];
        shell::format_bytes(size, sizeof(size), info.total_bytes);
        char label[24];
        snprintf(label, sizeof(label), "%s", info.label);
        // "Flash disk" reads "flash disk" in the line; "RAM disk" stays.
        if (label[0] >= 'A' && label[0] <= 'Z' && label[1] >= 'a' && label[1] <= 'z') {
            label[0] = static_cast<char>(label[0] - 'A' + 'a');
        }
        snprintf(v, sizeof(v), "%s %s", size, label);
        ok = true;
    } else if (io.storage == nullptr && post.drive != nullptr) {
        snprintf(v, sizeof(v), "%s", post.drive);
        ok = true;
    }
    line(s, kDriveRow, "Detecting drive C: .... ", v, ok ? kText : kRed);
    log("Drive C:", v, ok);
    failures += ok ? 0 : 1;
    shown();

    // Keyboard (and mouse), sound.
    const struct {
        int row;
        const char *label, *name;
        ports::Fact fact;
    } facts[] = {{kInputRow, "Detecting keyboard .... ", "keyboard", ports::Fact::Input},
                 {kSoundRow, "Detecting sound ....... ", "sound", ports::Fact::Sound}};
    for (const auto &f : facts) {
        v[0] = 0;
        ok = io.system.fact(f.fact, value, v, sizeof(v));
        line(s, f.row, f.label, ok ? v : "not found", ok ? kText : kRed);
        log(f.name, v, ok);
        failures += ok ? 0 : 1;
        shown();
    }

    // The system to start. Without one the BIOS stops here.
    post.bootable = true;
    if (post.boot_device != nullptr) {
        v[0] = 0;
        post.bootable = post.boot_device(v, sizeof(v), post.boot_context);
        line(s, kBootRow, "Boot device ........... ", v, post.bootable ? kText : kRed);
        log("Boot device", v, post.bootable);
        failures += post.bootable ? 0 : 1;
        shown();
    }

    if (!post.bootable) {
        line(s, kSummaryRow, "No operating system found.", "", kRed);
        line(s, kKeyRow, "Flash one with the cable, then restart.", "", kText);
        s.paint(c);
        s.move_to(s.cursor_row() + 1);
        return failures;
    }
    if (failures == 0) {
        line(s, kSummaryRow, "Starting Chinchilla...", "", kYellow);
    } else {
        snprintf(t, sizeof(t), "%d check%s failed. Starting Chinchilla anyway...", failures, failures == 1 ? "" : "s");
        line(s, kSummaryRow, t, "", kRed);
    }
    s.paint(c);
    if (hold && !io.skipped) {
        if (io.hold_s > 0) {
            // The finished screen stays, counting down; a key goes on at once.
            line(s, kKeyRow, "Press any key to continue", "", kText);
            for (uint32_t left = io.hold_s; left > 0 && !io.skipped; left--) {
                snprintf(t, sizeof(t), "Booting in %u", static_cast<unsigned>(left));
                line(s, kCountdownRow, t, "", kBright);
                s.paint(c);
                shell::wait_until(io, io.clock.micros() + 1000000);
            }
        } else {
            // Long enough to read the last line (longer when something failed).
            shell::wait_until(io, io.clock.micros() + (failures ? 3 : 1) * shell::kSelfTestHoldMs * 1000);
        }
    }
    s.move_to(s.cursor_row() + 1); // serial: the last line is finished
    return failures;
}

} // namespace bios
