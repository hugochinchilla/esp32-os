// Start-up screens: a quick, real self-test (each line printed when its
// check has finished, with the value it found), then the splash with the
// mascot and the 256-colour ribbon, then the desktop takes over.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/gfx/canvas.hpp"
#include "core/ports.hpp"

namespace shell {

constexpr uint32_t kMemoryTestBudgetUs = 250000; // the external memory test stops after this
constexpr uint32_t kInternalTestBudgetUs = 20000; // and the internal one
constexpr const char *kVersion = "v0.3";
constexpr uint32_t kSelfTestPace = 130; // percent: how slowly the lines appear (selftest_pace)
constexpr uint32_t kSelfTestHoldMs = 350;        // the finished self-test stays this long
constexpr uint32_t kSplashMs = 0; // the owner: hidden by default (splash_ms)
constexpr uint32_t kSelfTestHoldS = 5; // the finished self-test counts down this long (selftest_hold_s)
// How long after the video starts the self-test stays up, so a monitor that
// takes a while to show a picture still shows it ([boot] monitor_wait_ms in
// C:\.config\boot.ini; 0: no waiting). A monitor cannot tell the board it is
// ready (VGA is one-way), so this is a time.
constexpr uint32_t kMonitorWaitMs = 2500; // the bench monitor: a picture 2.4 s after the video starts

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
    uint32_t splash_ms = kSplashMs;
    uint32_t pace_percent = kSelfTestPace;
    uint32_t hold_s = kSelfTestHoldS;
    // True once a key or mouse button asks to skip the screens (may be nullptr).
    bool (*skip)(void *context) = nullptr;
    bool skipped = false; // set when that happened: the splash is skipped too
};

// C:\.config\boot.ini, [boot] section (docs/settings.md).
struct BootSettings {
    bool fastboot = false;                   // no start-up screens, no memory test
    uint32_t monitor_wait_ms = kMonitorWaitMs;
    uint32_t splash_ms = kSplashMs;
    uint32_t pace_percent = kSelfTestPace;
    uint32_t hold_s = kSelfTestHoldS;
};
BootSettings boot_settings(const fs::Storage &storage);

// Fast start: the cheap checks only (no memory test), nothing drawn; each
// result goes to io.log. Returns the number of failed checks.
int run_quiet_checks(BootIo &io);

// The self-test screen; returns the number of failed checks. Every check
// runs to the end. With hold, it then stays up at least kSelfTestHoldMs and
// until monitor_wait_ms after the video started, unless skipped.
int run_self_test(BootIo &io, bool hold = true);
// The splash screen; with hold, stays io.splash_ms unless skipped.
void show_splash(BootIo &io, bool hold = true);

// The memory test alone: writes and verifies borrowed external RAM until
// the budget is spent or there is none left to borrow. `progress` is
// called after each block with the KB done so far.
MemoryTestResult test_memory(ports::Memory &memory, ports::Clock &clock, uint32_t budget_us,
                             void (*progress)(uint32_t kb, void *context), void *context, bool internal = false);

// The finished self-test screen with given values (for the PC test against
// the approved drawing).
struct SelfTestValues {
    const char *version, *processor;
    uint32_t external_kb, internal_kb;
    const char *video, *drive, *mouse, *sound;
};
void draw_self_test(gfx::Canvas &c, int w, int h, const SelfTestValues &values);

} // namespace shell
