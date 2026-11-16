#include "core/shell/boot.hpp"

#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/gfx/canvas.hpp"
#include "core/gfx/font.hpp"

namespace shell {

namespace {

constexpr gfx::Colour kText = gfx::rgb(5, 5, 2);   // light grey
constexpr gfx::Colour kValue = gfx::rgb(7, 7, 3);  // white
constexpr gfx::Colour kOk = gfx::rgb(2, 7, 1);     // green
constexpr gfx::Colour kFail = gfx::rgb(7, 1, 0);   // red
constexpr gfx::Colour kHeading = gfx::rgb(7, 6, 1); // warm yellow
constexpr int kLabelX = 32, kValueX = 200, kStatusX = 560, kTop = 84, kLineH = 18;

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

void wait_ms(BootIo &io, uint32_t ms)
{
    const uint32_t start = io.clock.micros();
    while (io.clock.micros() - start < ms * 1000) {
        if (io.idle != nullptr) {
            io.idle(io.context);
        }
    }
}

// A bitmap enlarged k times, nearest neighbour.
void blit_big(gfx::Canvas &c, const gfx::Bitmap &b, int x, int y, int k)
{
    for (int row = 0; row < b.height; row++) {
        for (int col = 0; col < b.width; col++) {
            const int p = b.pixels[row * b.width + col];
            if (p != b.transparent) {
                c.fill({x + col * k, y + row * k, k, k}, static_cast<gfx::Colour>(p));
            }
        }
    }
}

struct Line {
    BootIo &io;
    Screen &screen;
    int y;
    char text[96];

    void value(const char *v, gfx::Colour colour = kValue)
    {
        gfx::Canvas c = screen.canvas();
        // Padded to a fixed width so a shorter value covers a longer one.
        char padded[64];
        snprintf(padded, sizeof(padded), "%-58.58s", v);
        c.text_opaque(kValueX, y, padded, colour, gfx::kBlack);
        snprintf(text, sizeof(text), "%s", v);
    }
    void status(bool ok, const char *label)
    {
        gfx::Canvas c = screen.canvas();
        c.text_opaque(kStatusX, y, ok ? "[ OK ]" : "[FAIL]", ok ? kOk : kFail, gfx::kBlack);
        if (io.log != nullptr) {
            char line[160];
            snprintf(line, sizeof(line), "self-test: %s: %s: %s", label, text, ok ? "OK" : "FAILED");
            io.log(line, io.context);
        }
        if (io.idle != nullptr) {
            io.idle(io.context);
        }
    }
};

Line begin_line(BootIo &io, Screen &s, int index, const char *label)
{
    Line l{io, s, kTop + index * kLineH, ""};
    gfx::Canvas c = s.canvas();
    char dots[40];
    snprintf(dots, sizeof(dots), "%s ", label);
    const int len = static_cast<int>(strlen(dots));
    for (int i = len; i < 26; i++) {
        dots[i] = '.';
    }
    dots[len < 26 ? 26 : len] = 0;
    c.text(kLabelX, l.y, dots, kText);
    return l;
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

struct Progress {
    Line *line;
    BootIo *io;
};

void memory_progress(uint32_t kb, void *context)
{
    auto *p = static_cast<Progress *>(context);
    char v[48];
    snprintf(v, sizeof(v), "%u KB", static_cast<unsigned>(kb));
    p->line->value(v);
    if (p->io->idle != nullptr) {
        p->io->idle(p->io->context);
    }
}

// The address-dependent pattern: each word differs from its neighbours, so
// stuck bits and address faults both show.
inline uint32_t pattern(uintptr_t address) { return static_cast<uint32_t>(address) * 2654435761u ^ 0x5AC3E10Fu; }

} // namespace

MemoryTestResult test_memory(ports::Memory &memory, ports::Clock &clock, uint32_t budget_us,
                             void (*progress)(uint32_t kb, void *context), void *context)
{
    MemoryTestResult r;
    constexpr size_t kChunk = 128 * 1024; // two chunks are larger than the data cache
    constexpr int kMaxBlocks = 16;
    void *blocks[kMaxBlocks];
    int nblocks = 0;
    const uint32_t start = clock.micros();
    uint64_t done = 0;
    while (r.ok && nblocks < kMaxBlocks && clock.micros() - start < budget_us) {
        size_t got = 0;
        void *block = memory.borrow(64u * 1024 * 1024, got);
        if (block == nullptr || got < 2 * kChunk) {
            if (block != nullptr) {
                memory.release(block);
            }
            break;
        }
        blocks[nblocks++] = block;
        // Write chunk i + 1 before checking chunk i, so chunk i has left the
        // cache and is read back from the memory itself.
        auto *words = static_cast<volatile uint32_t *>(block);
        const size_t per_chunk = kChunk / 4, chunks = got / kChunk;
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
            done += kChunk;
            if (progress != nullptr && (ch % 8 == 7 || ch + 1 == chunks)) {
                progress(static_cast<uint32_t>(done / 1024), context);
            }
            if (clock.micros() - start >= budget_us) {
                break;
            }
        }
    }
    for (int i = 0; i < nblocks; i++) {
        memory.release(blocks[i]);
    }
    r.tested_kb = static_cast<uint32_t>(done / 1024);
    r.us = clock.micros() - start;
    return r;
}

int run_self_test(BootIo &io, bool hold)
{
    Screen s(io.display);
    {
        gfx::Canvas c = s.canvas();
        c.fill({0, 0, io.display.width(), io.display.height()}, gfx::kBlack);
        blit_big(c, gfx::mascot_16(), 32, 24, 2);
        c.text(72, 26, "Chinchilla", kHeading, true);
        c.text(72, 42, "Self-test", kText);
        c.text(kStatusX - 30, 26, "Version 0.3", kText);
    }
    int failures = 0, index = 0;
    char v[96];
    uint32_t value = 0;
    auto fact_line = [&](const char *label, ports::Fact f, bool bytes) {
        Line l = begin_line(io, s, index++, label);
        v[0] = 0;
        const bool ok = io.system.fact(f, value, v, sizeof(v)) && (!bytes || value > 0);
        if (bytes && ok) {
            format_bytes(v, sizeof(v), value);
        } else if (!ok && v[0] == 0) {
            snprintf(v, sizeof(v), "not found");
        }
        l.value(v, ok ? kValue : kFail);
        l.status(ok, label);
        failures += ok ? 0 : 1;
    };
    fact_line("Processor", ports::Fact::Processor, false);
    fact_line("Internal RAM", ports::Fact::InternalRam, true);
    fact_line("External RAM", ports::Fact::ExternalRam, true);
    {
        Line l = begin_line(io, s, index++, "Memory test");
        Progress p{&l, &io};
        l.value("0 KB");
        const MemoryTestResult r = test_memory(io.memory, io.clock, kMemoryTestBudgetUs, &memory_progress, &p);
        if (!r.ok) {
            snprintf(v, sizeof(v), "error at 0x%08lX: wrote %08lX, read %08lX",
                     static_cast<unsigned long>(r.bad_address), static_cast<unsigned long>(r.wrote),
                     static_cast<unsigned long>(r.read));
        } else if (r.tested_kb == 0) {
            snprintf(v, sizeof(v), "no free memory to test");
        } else {
            snprintf(v, sizeof(v), "%u KB in %u ms", static_cast<unsigned>(r.tested_kb),
                     static_cast<unsigned>(r.us / 1000));
        }
        const bool ok = r.ok && r.tested_kb > 0;
        l.value(v, ok ? kValue : kFail);
        l.status(ok, "Memory test");
        failures += ok ? 0 : 1;
    }
    fact_line("Flash", ports::Fact::Flash, true);
    {
        Line l = begin_line(io, s, index++, "Drives");
        bool ok = false;
        v[0] = 0;
        if (io.storage != nullptr) {
            char letters[fs::Storage::kMaxDrives];
            const int n = io.storage->drives(letters, fs::Storage::kMaxDrives);
            size_t used = 0;
            for (int i = 0; i < n; i++) {
                ports::VolumeInfo info = {};
                if (io.storage->info(letters[i], info) == ports::FsError::Ok) {
                    char size[24];
                    format_bytes(size, sizeof(size), info.total_bytes);
                    used += static_cast<size_t>(snprintf(v + used, sizeof(v) - used, "%s%c: %s, %s",
                                                         used ? "; " : "", letters[i], info.label, size));
                    ok = true;
                    if (used >= sizeof(v)) {
                        break;
                    }
                }
            }
        }
        if (!ok) {
            snprintf(v, sizeof(v), "none");
        }
        l.value(v, ok ? kValue : kFail);
        l.status(ok, "Drives");
        failures += ok ? 0 : 1;
    }
    {
        Line l = begin_line(io, s, index++, "Video");
        const bool ok = io.system.fact(ports::Fact::Video, value, v, sizeof(v));
        if (ok) {
            snprintf(v, sizeof(v), "%dx%d, %u.%03u MHz, 256 colours", io.display.width(), io.display.height(),
                     static_cast<unsigned>(value / 1000000), static_cast<unsigned>(value / 1000 % 1000));
        } else {
            snprintf(v, sizeof(v), "%dx%d, 256 colours", io.display.width(), io.display.height());
        }
        l.value(v, ok ? kValue : kFail);
        l.status(ok, "Video");
        failures += ok ? 0 : 1;
    }
    fact_line("Input", ports::Fact::Input, false);
    fact_line("Sound", ports::Fact::Sound, false);
    {
        gfx::Canvas c = s.canvas();
        const int y = kTop + (index + 1) * kLineH;
        if (failures == 0) {
            c.text(kLabelX, y, "All checks passed. Starting Chinchilla...", kText);
        } else {
            snprintf(v, sizeof(v), "%d check%s failed. Starting Chinchilla anyway...", failures,
                     failures == 1 ? "" : "s");
            c.text(kLabelX, y, v, kFail);
        }
    }
    if (hold) {
        wait_ms(io, kSelfTestHoldMs);
    }
    return failures;
}

// The approved splash (docs/look/boot/boot-e-colour-ribbon.png), drawn the way
// option_c() in tools/mockup/boot_drafts.py draws it.
namespace {

const char *const kSplashMascot[16] = {
    "................", ".GGG........GGG.", "GPPPG......GPPPG", "GPPPG.GGGG.GPPPG",
    "GPPPGGLLLLGGPPPG", ".GGGLLLLLLLLGGG.", "..GLLLLLLLLLLG..", "..GLKLLLLLLKLG..",
    "..GLKLLLLLLKLG..", "..GLLLLNNLLLLG..", "D.GLLLLNNLLLLG.D", ".DDGLLLLLLLLGDD.",
    "D..GGLLLLLLGG..D", "....GGGGGGGG....", "................", "................"};

void splash_mascot(gfx::Canvas &c, int x, int y, int s)
{
    for (int r = 0; r < 16; r++) {
        for (int q = 0; q < 16; q++) {
            gfx::Colour col;
            switch (kSplashMascot[r][q]) {
            case 'G':
            case 'D': col = gfx::kDarkGrey; break;
            case 'L': col = gfx::kWhite; break;
            case 'P': col = gfx::rgb(7, 4, 2); break;
            case 'K': col = gfx::kBlack; break;
            case 'N': col = gfx::rgb(7, 3, 2); break;
            default: continue;
            }
            c.fill({x + q * s, y + r * s, s, s}, col);
        }
    }
}

void wide_text(gfx::Canvas &c, const gfx::WideFont &f, int x, int y, const char *s, gfx::Colour col)
{
    for (; *s != 0; s++, x += f.width) {
        const uint8_t *g = f.glyph(*s);
        if (g == nullptr) {
            continue;
        }
        for (int row = 0; row < f.height; row++) {
            const uint8_t *bits = g + row * f.bytes_per_row;
            for (int q = 0; q < f.width; q++) {
                if (bits[q / 8] & (0x80 >> (q % 8))) {
                    c.fill({x + q, y + row, 1, 1}, col);
                }
            }
        }
    }
}

// The nine resistors as a progress row: three reds, three greens, three
// blues; the first `lit` in colour, the rest dark with a grey border.
void nine(gfx::Canvas &c, int x, int y, int size, int gap, int lit)
{
    const gfx::Colour cols[9] = {gfx::rgb(7, 0, 0), gfx::rgb(5, 0, 0), gfx::rgb(3, 0, 0),
                                 gfx::rgb(0, 7, 0), gfx::rgb(0, 5, 0), gfx::rgb(0, 3, 0),
                                 gfx::rgb(0, 0, 3), gfx::rgb(0, 0, 2), gfx::rgb(0, 0, 1)};
    for (int i = 0; i < 9; i++) {
        const gfx::Rect r = {x + i * (size + gap), y, size, size};
        c.fill(r, i < lit ? gfx::kWhite : gfx::kDarkGrey); // the border
        c.fill({r.x + 1, r.y + 1, size - 2, size - 2}, i < lit ? cols[i] : gfx::rgb(1, 1, 0));
    }
}

} // namespace

void show_splash(BootIo &io, bool hold)
{
    Screen s(io.display);
    gfx::Canvas c = s.canvas();
    c.fill({0, 0, io.display.width(), io.display.height()}, gfx::kWhite);
    splash_mascot(c, 70, 130, 11);
    wide_text(c, gfx::kSpleen32x64, 270, 150, "Chinchilla", gfx::kBlack);
    wide_text(c, gfx::kSpleen12x24, 274, 222, "ninety-six", gfx::rgb(0, 0, 2));
    c.text(274, 262, "A small desktop for a small computer", gfx::kDarkGrey);
    // The ribbon: all 256 colours in 16 rows of 16, 40 x 6 pixels each.
    for (int v = 0; v < 256; v++) {
        const int r = v >> 5, g = (v >> 2) & 7, b = v & 3;
        c.fill({(g * 2 + (b >> 1)) * 40, 380 + (r * 2 + (b & 1)) * 6, 40, 6}, static_cast<gfx::Colour>(v));
    }
    nine(c, 274, 300, 12, 6, 4);
    if (hold) {
        wait_ms(io, kSplashMs);
    }
}

} // namespace shell
