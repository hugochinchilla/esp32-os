// The BIOS's text screen and its self-test: the grid, what goes to serial,
// the picture against the reference drawing, failed checks, the waiting.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/bios/post.hpp"
#include "core/bios/text_screen.hpp"
#include "core/image/image.hpp"
#include "../folder_volume.hpp"
#include "../host_adapters.hpp"
#include "../../firmware/common/boot_flag.h"

namespace {

int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                  \
        }                                                                \
    } while (0)

void collect(const char *bytes, size_t n, void *context) { static_cast<std::string *>(context)->append(bytes, n); }

std::string row(const bios::TextScreen &s, int r)
{
    char t[bios::kColumns + 1];
    s.row_text(r, t, sizeof(t));
    return t;
}

// What a terminal would show after receiving `bytes`: "\r\n" starts a line,
// "\r" ESC [ K empties the current one.
std::vector<std::string> terminal(const std::string &bytes)
{
    std::vector<std::string> lines(1);
    for (size_t i = 0; i < bytes.size(); i++) {
        if (bytes.compare(i, 2, "\r\n") == 0) {
            lines.emplace_back();
            i++;
        } else if (bytes.compare(i, 4, "\r\x1b[K") == 0) {
            lines.back().clear();
            i += 3;
        } else {
            CHECK(bytes[i] >= 0x20 && bytes[i] <= 0x7E); // nothing else is ever sent
            lines.back() += bytes[i];
        }
    }
    return lines;
}

struct Screen {
    Screen() : display(640, 480) { clip.set({0, 0, 640, 480}); }
    gfx::Canvas canvas() { return gfx::Canvas(display.pixels(), 640, 480, clip, {0, 0, 640, 480}); }
    host::MemoryDisplay display;
    gfx::RectList clip;
};

// The grid, the cursor and serial.
void test_text_screen()
{
    static bios::TextScreen s;
    std::string sent;
    s.clear();
    s.set_out(collect, &sent);
    s.move_to(2);
    s.print("Hello ", bios::kText);
    s.print("there", bios::kBright);
    CHECK(row(s, 2) == "Hello there" && row(s, 0).empty());
    CHECK(s.at(6, 2) == 't' && s.colour_at(6, 2) == bios::kBright && s.colour_at(0, 2) == bios::kText);
    CHECK(sent == "\r\n\r\nHello there");
    // A line written again in place.
    s.restart_line();
    s.print("Bye", bios::kText);
    CHECK(row(s, 2) == "Bye");
    CHECK(terminal(sent) == (std::vector<std::string>{"", "", "Bye"}));
    // put() is for the screen only; the cursor does not move.
    s.put(70, 29, "corner", bios::kDim);
    CHECK(row(s, 29).substr(70) == "corner" && s.cursor_row() == 2 && s.cursor_column() == 3);
    CHECK(terminal(sent).size() == 3);
    // Text stops at the right edge, on the screen and on serial; going up
    // is not possible; other bytes become '?'.
    s.move_to(3);
    const std::string before = sent;
    s.print(std::string(100, 'x').c_str(), bios::kText);
    CHECK(row(s, 3).size() == 80 && sent.size() == before.size() + 80);
    s.move_to(1);
    CHECK(s.cursor_row() == 3);
    s.move_to(4);
    s.print("a\tb", bios::kText);
    CHECK(row(s, 4) == "a?b");
    s.move_to(99);
    CHECK(s.cursor_row() == bios::kRows - 1);

    // Painting: only cells that changed are written, each whole; reserved
    // cells never.
    Screen screen;
    gfx::Canvas c = screen.canvas();
    s.clear();
    s.set_out(nullptr, nullptr);
    s.reserve(10, 0, 2, 1);
    std::fill(screen.display.pixels(), screen.display.pixels() + 640 * 480, uint8_t{0x5A});
    s.put(0, 0, "A", bios::kBright);
    s.put(10, 0, "ZZ", bios::kBright);
    s.paint(c);
    const uint8_t *px = screen.display.pixels();
    auto cell_has = [&](int column, int r, uint8_t colour) {
        for (int y = r * 16; y < r * 16 + 16; y++) {
            for (int x = column * 8; x < column * 8 + 8; x++) {
                if (px[y * 640 + x] == colour) {
                    return true;
                }
            }
        }
        return false;
    };
    CHECK(cell_has(0, 0, bios::kBright) && cell_has(0, 0, gfx::kBlack) && !cell_has(0, 0, 0x5A)); // 'A' on black
    CHECK(!cell_has(1, 0, 0x5A) && !cell_has(1, 0, bios::kBright));                             // an empty cell: black
    CHECK(!cell_has(10, 0, bios::kBright) && !cell_has(11, 0, gfx::kBlack));                    // reserved: untouched
    // Nothing changed: nothing written.
    std::fill(screen.display.pixels(), screen.display.pixels() + 640 * 480, uint8_t{0x5A});
    s.paint(c);
    CHECK(!cell_has(0, 0, bios::kBright));
    s.put(0, 0, "A", bios::kBright); // the same again is no change
    s.paint(c);
    CHECK(!cell_has(0, 0, bios::kBright));
    s.put(0, 0, "A", bios::kRed);
    s.paint(c);
    CHECK(cell_has(0, 0, bios::kRed) && cell_has(1, 0, 0x5A));
}

struct Rows : image::RowSink {
    uint8_t *out;
    void row(int y, const uint8_t *rgb) override
    {
        for (int x = 0; x < 640; x++) {
            out[y * 640 + x] = gfx::nearest({rgb[x * 3], rgb[x * 3 + 1], rgb[x * 3 + 2]});
        }
    }
};

// The finished screen is the reference drawing (docs/look/bios-text/
// bios-post.png, drawn by tools/mockup/bios_text.py from the font's BDF
// file), pixel for pixel.
template <typename Draw> void matches(const char *look_folder, const char *file, Draw draw)
{
    static uint8_t reference[640 * 480];
    host::FolderVolume volume(look_folder);
    fs::Storage storage;
    storage.mount('L', volume);
    host::HostMemory memory;
    image::Decoder d;
    image::Info info;
    char path[64];
    snprintf(path, sizeof(path), "L:\\%s", file);
    CHECK(d.open(storage, path, memory, info) == image::Error::None);
    CHECK(info.width == 640 && info.height == 480);
    Rows rows;
    rows.out = reference;
    while (!d.done()) {
        if (d.step(rows, 16) != image::Error::None) {
            CHECK(false);
            break;
        }
    }
    d.release(memory);

    Screen screen;
    gfx::Canvas c = screen.canvas();
    static bios::TextScreen s;
    draw(c, s);
    int differ = 0, x0 = 640, y0 = 480, x1 = -1, y1 = -1;
    for (int i = 0; i < 640 * 480; i++) {
        if (screen.display.pixels()[i] != reference[i]) {
            differ++;
            x0 = std::min(x0, i % 640); x1 = std::max(x1, i % 640);
            y0 = std::min(y0, i / 640); y1 = std::max(y1, i / 640);
        }
    }
    if (differ != 0) {
        printf("%s differs from the reference in %d pixels (x %d-%d, y %d-%d)\n", file, differ, x0, x1, y0, y1);
    }
    CHECK(differ == 0);
}

void test_matches_reference(const char *look_folder)
{
    matches(look_folder, "bios-post.png", [](gfx::Canvas &c, bios::TextScreen &s) {
        bios::draw_post(c, s,
                        {"v0.3", "ESP32-S3 dual core, 240 MHz", 8192, 376, "VGA 640x480, 256 colours",
                         "11 MB flash disk", "serial debug link", "none", "ota_0, 1.4 MB, image OK", 5,
                         "17/11/2026-ESP32S3-N16R8-CHINCHILLA"});
        // No text under the mascot.
        for (int r = bios::kLogoRow; r < bios::kLogoRow + bios::kLogoRows; r++) {
            CHECK(row(s, r).size() <= static_cast<size_t>(bios::kLogoColumn));
        }
    });
    matches(look_folder, "bios-splash.png", [](gfx::Canvas &c, bios::TextScreen &s) {
        bios::draw_splash(c, s, "v0.3", "17/11/2026-ESP32S3-N16R8-CHINCHILLA");
    });
}

// A clock that moves on 1 ms each time it is read.
class SteppingClock : public ports::Clock {
public:
    uint32_t micros() override { return now += 1000; }
    uint32_t now = 0;
};

// A system whose flash cannot be read.
class NoFlashSystem : public host::HostSystem {
public:
    bool fact(ports::Fact which, uint32_t &value, char *text, size_t capacity) override
    {
        if (which == ports::Fact::Flash) {
            value = 0;
            return false;
        }
        return HostSystem::fact(which, value, text, capacity);
    }
};

// The first screen: stays until its time after the video started, sends
// nothing to serial, a key ends it, and the self-test's wait counts from its
// end.
void test_splash()
{
    Screen screen;
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    shell::BootIo io{screen.display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    io.video_start_us = 500000;
    clock.now = 600000;
    static bios::TextScreen s;
    std::string sent;
    s.set_out(collect, &sent);
    bios::Post post{io, s, "TEST-ID"};
    bios::run_splash(post, 3000);
    CHECK(clock.now >= 3500000 && clock.now < 3600000);
    CHECK(io.video_start_us >= 3500000);
    CHECK(sent.empty());
    CHECK(row(s, 12) == std::string("    Rodent BIOS ") + shell::kVersion);
    CHECK(row(s, 7).find("Energy Nibbler") == 58 && s.colour_at(58, 7) == bios::kLeaf);
    CHECK(row(s, bios::kIdRow) == "  TEST-ID");
    // The self-test that follows starts from a clean screen.
    io.monitor_wait_ms = 0;
    io.hold_s = 0;
    bios::run_post(post, false);
    CHECK(row(s, 12).empty() && row(s, 7).find("Energy Nibbler") == std::string::npos);

    // A key during it: over at once.
    SteppingClock clock2;
    static bool key = false;
    key = true;
    shell::BootIo io2{screen.display, system, memory, clock2, nullptr, nullptr, nullptr, nullptr};
    io2.skip = [](void *) { return key; };
    bios::Post post2{io2, s, nullptr};
    bios::run_splash(post2, 3000);
    CHECK(clock2.now < 100000);
}

// A whole run: the screen, the serial copy, the log.
void test_run()
{
    Screen screen;
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    static int logged = 0;
    logged = 0;
    shell::BootIo io{screen.display, system, memory, clock, nullptr, nullptr, nullptr,
                     [](const char *, void *) { logged++; }};
    io.monitor_wait_ms = 0;
    io.hold_s = 3;
    static bios::TextScreen s;
    std::string sent;
    s.set_out(collect, &sent);
    bios::Post post{io, s, "TEST-ID"};
    // No drive is given, so that one check fails.
    CHECK(bios::run_post(post, true) == 1);
    CHECK(logged == 10); // processor, two memory tests, three sizes, video, drive, keyboard, sound
    CHECK(row(s, bios::kTitleRow) == std::string("  Rodent BIOS ") + shell::kVersion);
    CHECK(row(s, bios::kProcessorRow) == "  Main Processor : ESP32-S3 dual core, 240 MHz (simulated)");
    CHECK(row(s, bios::kMemoryRow) == "  Memory Test    :   4096K OK   (external)");
    CHECK(row(s, bios::kMemoryRow + 1).find("K OK   (internal)") != std::string::npos);
    CHECK(row(s, bios::kVideoRow) == "  Detecting video ....... VGA 640x480, 256 colours");
    CHECK(row(s, bios::kDriveRow) == "  Detecting drive C: .... none" && s.colour_at(2, bios::kDriveRow) == bios::kRed);
    CHECK(row(s, bios::kInputRow) == "  Detecting keyboard .... serial debug link");
    CHECK(row(s, bios::kSoundRow) == "  Detecting sound ....... none" && s.colour_at(2, bios::kSoundRow) == bios::kText);
    CHECK(row(s, bios::kSummaryRow) == "  1 check failed. Starting Chinchilla anyway...");
    CHECK(row(s, bios::kKeyRow) == "  Press any key to continue" && row(s, bios::kCountdownRow) == "  Booting in 1");
    CHECK(row(s, bios::kSetupRow) == "  Press DEL to enter SETUP (there is no setup)" && row(s, bios::kIdRow) == "  TEST-ID");
    CHECK(s.colour_at(20, bios::kMemoryRow) == bios::kBright && s.colour_at(2, bios::kMemoryRow) == bios::kText);
    CHECK(clock.now >= 3000000); // the countdown

    // Serial shows the same lines: every row of the screen down to the
    // countdown is a line of the terminal, the bottom lines after the header.
    const std::vector<std::string> lines = terminal(sent);
    CHECK(lines.size() == static_cast<size_t>(bios::kCountdownRow + 2) && lines.back().empty());
    for (int r = 0; r <= bios::kCountdownRow && r < static_cast<int>(lines.size()); r++) {
        if (r == bios::kTitleRow + 2) {
            CHECK(lines[r] == "  Press DEL to enter SETUP (there is no setup)  TEST-ID");
        } else {
            if (lines[r] != row(s, r)) {
                printf("row %d: serial \"%s\", screen \"%s\"\n", r, lines[r].c_str(), row(s, r).c_str());
            }
            CHECK(lines[r] == row(s, r));
        }
    }
    // The count climbed in place: more was sent than is left on the lines.
    CHECK(std::count(sent.begin(), sent.end(), '\x1b') >= 2 + 2); // two memory lines at least, the countdown twice

    // The picture is what the grid says (nothing painted late or missed).
    std::vector<uint8_t> shown(screen.display.pixels(), screen.display.pixels() + 640 * 480);
    gfx::Canvas c = screen.canvas();
    bios::paint_frame(c, s);
    CHECK(memcmp(shown.data(), screen.display.pixels(), shown.size()) == 0);

    // Without hold: no waiting, no countdown, all checks pass with a drive
    // missing only.
    SteppingClock fast_clock;
    NoFlashSystem broken;
    host::HostMemory fresh; // a HostMemory lends its block once
    shell::BootIo quick{screen.display, broken, fresh, fast_clock, nullptr, nullptr, nullptr, nullptr};
    static bios::TextScreen q;
    bios::Post quick_post{quick, q};
    CHECK(bios::run_post(quick_post, false) == 2);
    CHECK(row(q, bios::kSummaryRow) == "  2 checks failed. Starting Chinchilla anyway..." && row(q, bios::kKeyRow).empty());
    CHECK(row(q, bios::kIdRow).empty());
    CHECK(fast_clock.now < 1000000);
}

// The monitor wait, and a key that skips the rest of the waiting.
void test_waiting_and_skipping()
{
    Screen screen;
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    shell::BootIo io{screen.display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    io.video_start_us = 0;
    io.monitor_wait_ms = 4000;
    io.hold_s = 0;
    static bios::TextScreen s;
    bios::Post post{io, s};
    bios::run_post(post, true);
    CHECK(clock.now >= 4000000 + 6 * 120 * 1300 && clock.now < 9000000 && !io.skipped);

    static int polls = 0;
    polls = 0;
    host::HostMemory fresh; // a HostMemory lends its block once
    shell::BootIo skip_io{screen.display, system, fresh, clock, nullptr, nullptr, nullptr, nullptr};
    skip_io.video_start_us = clock.now;
    skip_io.monitor_wait_ms = 4000;
    skip_io.hold_s = 5;
    skip_io.skip = [](void *) { return ++polls > 50; };
    const uint32_t start = clock.now;
    bios::Post skip_post{skip_io, s};
    bios::run_post(skip_post, true);
    CHECK(skip_io.skipped && clock.now - start < 2000000);
    CHECK(row(s, bios::kSummaryRow) == "  1 check failed. Starting Chinchilla anyway..."); // every check still ran
    CHECK(row(s, bios::kKeyRow).empty());                                                 // no countdown after a key
}

// What only the BIOS knows: the drive from the partition table, and the
// system it is about to start. With no system it stops and says so.
void test_boot_device()
{
    Screen screen;
    host::HostSystem system;
    host::HostMemory memory;
    SteppingClock clock;
    shell::BootIo io{screen.display, system, memory, clock, nullptr, nullptr, nullptr, nullptr};
    io.monitor_wait_ms = 0;
    io.hold_s = 2;
    static bios::TextScreen s;
    std::string sent;
    s.set_out(collect, &sent);
    bios::Post post{io, s};
    post.drive = "11 MB flash disk";
    static const char *found = "ota_0, 1.4 MB, image OK";
    post.boot_context = &found;
    post.boot_device = [](char *text, size_t capacity, void *context) {
        snprintf(text, capacity, "%s", *static_cast<const char **>(context));
        return true;
    };
    CHECK(bios::run_post(post, true) == 0 && post.bootable);
    CHECK(row(s, bios::kDriveRow) == "  Detecting drive C: .... 11 MB flash disk");
    CHECK(row(s, bios::kBootRow) == "  Boot device ........... ota_0, 1.4 MB, image OK");
    CHECK(row(s, bios::kSummaryRow) == "  Starting Chinchilla..." && row(s, bios::kCountdownRow) == "  Booting in 1");
    const std::vector<std::string> lines = terminal(sent);
    CHECK(lines.size() > static_cast<size_t>(bios::kCountdownRow) && lines[bios::kBootRow] == row(s, bios::kBootRow));

    host::HostMemory fresh;
    SteppingClock clock2;
    shell::BootIo io2{screen.display, system, fresh, clock2, nullptr, nullptr, nullptr, nullptr};
    io2.monitor_wait_ms = 0;
    io2.hold_s = 5;
    bios::Post none{io2, s};
    none.drive = "11 MB flash disk";
    none.boot_device = [](char *text, size_t capacity, void *) {
        snprintf(text, capacity, "ota_0, not a system");
        return false;
    };
    const uint32_t before = clock2.now;
    CHECK(bios::run_post(none, true) == 1 && !none.bootable);
    CHECK(s.colour_at(2, bios::kBootRow) == bios::kRed);
    CHECK(row(s, bios::kSummaryRow) == "  No operating system found." && s.colour_at(2, bios::kSummaryRow) == bios::kRed);
    CHECK(row(s, bios::kKeyRow) == "  Flash one with the cable, then restart." && row(s, bios::kCountdownRow).empty());
    CHECK(clock2.now - before < 5000000); // no countdown
}

// The bootloader's choice between the BIOS and the OS, over the restarts
// of a normal start, a fast restart and a flash with no BIOS in it.
void test_boot_word()
{
    // Power-on: the memory holds anything. The BIOS starts.
    uint32_t word = 0xDEADBEEF;
    CHECK(chinchilla_start_bios(&word, true) && word == 0);
    // The BIOS is done and restarts: the OS starts and is told so, once.
    word = CHINCHILLA_START_OS;
    CHECK(!chinchilla_start_bios(&word, true) && word == CHINCHILLA_FROM_BIOS);
    // The OS did not clear the word (it crashed at once): the next restart
    // still goes through the BIOS.
    CHECK(chinchilla_start_bios(&word, true) && word == 0);
    // No BIOS in the flash: the OS starts, and is not told a BIOS ran.
    word = 0;
    CHECK(!chinchilla_start_bios(&word, false) && word == 0);
    word = CHINCHILLA_FROM_BIOS;
    CHECK(!chinchilla_start_bios(&word, false) && word == 0);
    // The OS stopped on an error: straight back to the OS, which starts as
    // with no BIOS; the restart after that goes through the BIOS again.
    word = CHINCHILLA_SKIP_BIOS;
    CHECK(!chinchilla_start_bios(&word, true) && word == 0);
    CHECK(chinchilla_start_bios(&word, true) && word == 0);
    // Watchdogs and a brownout are failures; power-on, a restart by
    // software and the USB port are not.
    for (int reason : {7, 8, 9, 11, 13, 15, 16, 17, 18}) {
        CHECK(chinchilla_reset_is_failure(reason));
    }
    for (int reason : {1, 3, 5, 12, 21, 22}) {
        CHECK(!chinchilla_reset_is_failure(reason));
    }
}

} // namespace

int main(int argc, char **argv)
{
    test_boot_word();
    test_text_screen();
    test_splash();
    test_run();
    test_waiting_and_skipping();
    test_boot_device();
    if (argc > 1) {
        test_matches_reference(argv[1]);
    } else {
        printf("no docs/look/bios-text folder given: the screen not compared\n");
        failures++;
    }
    if (failures == 0) {
        printf("all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
