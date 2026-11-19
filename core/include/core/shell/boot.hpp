// Start-up screens: a quick, real self-test (each line printed when its
// check has finished, with the value it found), then the desktop, which
// fades in (Wm::start). There is no splash screen (the owner, 2026-11-19).
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/gfx/canvas.hpp"
#include "core/ports.hpp"
#include "core/sound/beeps.hpp"
#include "core/sys/crash.hpp"
#include "core/sys/watchdog.hpp"

namespace shell {

constexpr uint32_t kMemoryTestBudgetUs = 250000; // the external memory test stops after this
constexpr uint32_t kInternalTestBudgetUs = 20000; // and the internal one
constexpr const char *kVersion = "v0.3";
constexpr uint32_t kSelfTestPace = 130; // percent: how slowly the lines appear (selftest_pace)
constexpr uint32_t kSelfTestHoldMs = 350;        // the finished self-test stays this long
constexpr uint32_t kSelfTestHoldS = 5; // the finished self-test counts down this long (selftest_hold_s)
// How long after the video starts the self-test stays up, so a monitor that
// takes a while to show a picture still shows it ([boot] monitor_wait_ms in
// C:\.config\boot.ini; 0: no waiting). A monitor cannot tell the board it is
// ready (VGA is one-way), so this is a time.
constexpr uint32_t kMonitorWaitMs = 2500; // the bench monitor: a picture 2.4 s after the video starts
// The error screen after a crash counts down this long, then goes on by
// itself: a board with nothing to press a key on must still start
// ([boot] crash_hold_s; 0: stays until a key or button).
constexpr uint32_t kCrashHoldS = 15;

struct MemoryTestResult {
    uint32_t tested_kb = 0;
    uint32_t us = 0;
    bool ok = true;
    uintptr_t bad_address = 0; // the first word that read back wrong
    uint32_t wrote = 0, read = 0;
};

struct BootIo {
    ports::Display &display;
    ports::System &system;
    ports::Memory &memory;
    ports::Clock &clock;
    const fs::Storage *storage; // its drives are listed (may be nullptr)
    // Called between steps and while waiting, e.g. to answer the debug link.
    void (*idle)(void *context);
    void *context;
    // Each finished line as text, e.g. for the log (may be nullptr).
    void (*log)(const char *line, void *context);
    // When the video signal started (clock.micros()), and how long after
    // that the self-test stays up.
    uint32_t video_start_us = 0;
    uint32_t monitor_wait_ms = 0;
    uint32_t pace_percent = kSelfTestPace;
    uint32_t hold_s = kSelfTestHoldS;
    uint32_t crash_hold_s = kCrashHoldS;
    // True once a key or mouse button asks to skip the screens (may be nullptr).
    bool (*skip)(void *context) = nullptr;
    bool skipped = false; // set when that happened
    // Like idle, but may sleep a short while (on the board one 10 ms tick)
    // so other tasks run. Used while more than kNapMarginUs of a wait is
    // left; the rest is done with idle so the wait does not overshoot.
    void (*nap)(void *context) = nullptr;
    static constexpr uint32_t kNapMarginUs = 20000;
    // Where the self-test's beep goes (may be nullptr: no beep).
    ports::Sound *sound = nullptr;
};

// C:\.config\boot.ini, [boot] section (docs/settings.md).
struct BootSettings {
    bool fastboot = false;                   // no start-up screens, no memory test
    uint32_t monitor_wait_ms = kMonitorWaitMs;
    uint32_t pace_percent = kSelfTestPace;
    uint32_t hold_s = kSelfTestHoldS;
    uint32_t crash_hold_s = kCrashHoldS;
    int watchdog_s = sys::kWatchdogDefaultS; // [watchdog] timeout_s; 0: off
};
BootSettings boot_settings(const fs::Storage &storage);

// A one-shot fast boot for debug restarts (tools/dbg.py reboot --fast): the
// board keeps `word` where a restart and a flashing leave it (NVS). True
// once if it holds the magic value, which it then clears; boot.ini is not
// touched.
constexpr uint32_t kFastBootMagic = 0x46415354; // "FAST"
inline bool take_one_shot_fast_boot(uint32_t &word)
{
    const bool armed = word == kFastBootMagic;
    word = 0;
    return armed;
}

// Waits until the clock reaches `until` (micros, wrapping), answering the
// debug link meanwhile; false if a key or button skipped it.
bool wait_until(BootIo &io, uint32_t until);
// The self-test is done: the beep code for what it found, as a PC gives
// (core/sound/beeps.hpp). Starts it and returns.
void post_beep(BootIo &io, sound::PostResult result);
// Waits until the beeper has finished (at most as long as the code lasts).
// A key does not cut it short.
void let_beep_end(BootIo &io, sound::PostResult result);
// "12 MB", "1.5 MB", "376 KB".
void format_bytes(char *out, size_t n, uint64_t bytes);
// The mascot's face, 16 by 16 squares of `scale` pixels.
void draw_mascot(gfx::Canvas &c, int x, int y, int scale, gfx::Colour outline, gfx::Colour fur);

// Fast start: the cheap checks only (no memory test), nothing drawn; each
// result goes to io.log. Returns the number of failed checks.
int run_quiet_checks(BootIo &io);

// The self-test screen; returns the number of failed checks. Every check
// runs to the end. With hold, it then stays up at least kSelfTestHoldMs and
// until monitor_wait_ms after the video started, unless skipped.
int run_self_test(BootIo &io, bool hold = true);

// The memory test alone: writes and verifies borrowed external RAM until
// the budget is spent or there is none left to borrow. `progress` is
// called after each block with the KB done so far.
MemoryTestResult test_memory(ports::Memory &memory, ports::Clock &clock, uint32_t budget_us,
                             void (*progress)(uint32_t kb, void *context), void *context, bool internal = false);

// The error screen at the start after a crash: what happened, to which
// program, and the details from the record. `logged`: the report is also in
// the crash log (sys::kCrashLog). It stays until a key or mouse button, or
// io.crash_hold_s seconds (0: no limit), and is shown on a fast start too.
// It leaves io.skipped false: the key that ended it does not skip the
// screens that follow.
void show_crash_screen(BootIo &io, const sys::CrashRecord &record, bool logged, bool hold = true);
// The same without waiting; `seconds_left` is the countdown shown (0: none).
void draw_crash_screen(gfx::Canvas &c, int w, int h, const sys::CrashRecord &record, bool logged,
                       uint32_t seconds_left);

// The finished self-test screen with given values (for the PC test against
// the approved drawing).
struct SelfTestValues {
    const char *version, *processor;
    uint32_t external_kb, internal_kb;
    const char *video, *drive, *mouse, *sound;
};
void draw_self_test(gfx::Canvas &c, int w, int h, const SelfTestValues &values);

} // namespace shell
