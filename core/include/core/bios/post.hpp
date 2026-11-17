// The BIOS's power-on self-test on its text screen: each line printed when
// its check has finished, with the value found, on the monitor and as plain
// text on serial. The same checks as shell::run_self_test, which it will
// replace when the BIOS is a firmware of its own (backlog item 46).
#pragma once

#include "core/bios/text_screen.hpp"
#include "core/shell/boot.hpp"

namespace bios {

// Where things are on the 80 by 30 screen.
constexpr int kTitleRow = 1, kProcessorRow = 4, kMemoryRow = 5, kVideoRow = 8, kDriveRow = 9, kInputRow = 10,
              kSoundRow = 11, kBootRow = 13, kSummaryRow = 15, kKeyRow = 17, kCountdownRow = 18, kSetupRow = 28,
              kIdRow = 29;
constexpr int kLeftColumn = 2;
// The mascot: 16 squares of 5 pixels, over ten columns and five rows that
// the text leaves empty.
constexpr int kLogoColumn = 68, kLogoRow = 1, kLogoColumns = 10, kLogoRows = 5, kLogoScale = 5;

constexpr gfx::Colour kBright = gfx::rgb(7, 7, 3);
constexpr gfx::Colour kText = gfx::rgb(5, 5, 2);
constexpr gfx::Colour kDim = gfx::rgb(2, 2, 1);
constexpr gfx::Colour kYellow = gfx::rgb(7, 7, 0);
constexpr gfx::Colour kRed = gfx::rgb(7, 1, 0);
constexpr gfx::Colour kLeaf = gfx::rgb(1, 6, 1);
constexpr gfx::Colour kLeafDark = gfx::rgb(1, 3, 0);

// The first screen: how long it stays, counted from the start of the video.
constexpr uint32_t kSplashMs = 3000;

struct Post {
    shell::BootIo &io;
    TextScreen &screen;
    // The bottom line, e.g. the build's date and the board (may be nullptr).
    const char *id = nullptr;
    // Drive C: when io.storage is not given: the BIOS does not mount the
    // drive, it reads its size from the partition table (may be nullptr:
    // "none", a failed check).
    const char *drive = nullptr;
    // The system the BIOS is about to start: fills `text` (e.g. "ota_0,
    // 1.4 MB, image OK") and returns whether it can be started. nullptr: no
    // "Boot device" line (a BIOS that starts nothing, as on the PC).
    bool (*boot_device)(char *text, size_t capacity, void *context) = nullptr;
    void *boot_context = nullptr;
    // Set by run_post: there is a system to start. When false the screen
    // says so and does not count down; the caller must not hand over.
    bool bootable = true;
};

// Black, the mascot, and every cell of the screen.
void paint_frame(gfx::Canvas &c, TextScreen &screen);

// The first screen, as old BIOSes opened with their maker's logo and an
// energy badge: the mascot, a badge of our own, the version. It stays until
// `ms` after the video started (a key ends it), which is when a monitor is
// still waking, so nothing that matters is on it. Nothing goes to serial.
// The self-test's own wait for the monitor then counts from its end.
void run_splash(Post &post, uint32_t ms = kSplashMs);
// The same picture with given words (for the PC test against the drawing).
void draw_splash(gfx::Canvas &c, TextScreen &screen, const char *version, const char *id);

// Runs the self-test; returns the number of failed checks. Every check runs
// to the end. With hold it paces the lines, waits for the monitor first and
// counts down at the end, as io says; a key or button skips the waiting.
int run_post(Post &post, bool hold = true);

// The finished screen with given values, nothing sent to serial (for the PC
// test against the reference drawing).
struct PostValues {
    const char *version, *processor;
    uint32_t external_kb, internal_kb;
    const char *video, *drive, *input, *sound;
    const char *boot; // nullptr: no "Boot device" line
    uint32_t countdown; // 0: no "Press any key" lines
    const char *id;
};
void draw_post(gfx::Canvas &c, TextScreen &screen, const PostValues &values);

} // namespace bios
