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
              kSoundRow = 11, kSummaryRow = 13, kKeyRow = 15, kCountdownRow = 16, kSetupRow = 28, kIdRow = 29;
constexpr int kLeftColumn = 2;
// The mascot: 16 squares of 5 pixels, over ten columns and five rows that
// the text leaves empty.
constexpr int kLogoColumn = 68, kLogoRow = 1, kLogoColumns = 10, kLogoRows = 5, kLogoScale = 5;

constexpr gfx::Colour kBright = gfx::rgb(7, 7, 3);
constexpr gfx::Colour kText = gfx::rgb(5, 5, 2);
constexpr gfx::Colour kDim = gfx::rgb(2, 2, 1);
constexpr gfx::Colour kYellow = gfx::rgb(7, 7, 0);
constexpr gfx::Colour kRed = gfx::rgb(7, 1, 0);

struct Post {
    shell::BootIo &io;
    TextScreen &screen;
    // The bottom line, e.g. the build's date and the board (may be nullptr).
    const char *id = nullptr;
};

// Black, the mascot, and every cell of the screen.
void paint_frame(gfx::Canvas &c, TextScreen &screen);

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
    uint32_t countdown; // 0: no "Press any key" lines
    const char *id;
};
void draw_post(gfx::Canvas &c, TextScreen &screen, const PostValues &values);

} // namespace bios
