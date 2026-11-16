// Start-up screens: a quick, real self-test (each line printed when its
// check has finished, with the value it found), then the splash with the
// mascot and the 256-colour ribbon, then the desktop takes over.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/fs/fs.hpp"
#include "core/ports.hpp"

namespace shell {

constexpr uint32_t kMemoryTestBudgetUs = 250000; // the memory test stops after this
constexpr uint32_t kSelfTestHoldMs = 350;        // the finished self-test stays this long
constexpr uint32_t kSplashMs = 500;

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
};

// The self-test screen; returns the number of failed checks. With hold,
// waits kSelfTestHoldMs after the last line.
int run_self_test(BootIo &io, bool hold = true);
// The splash screen; with hold, waits kSplashMs.
void show_splash(BootIo &io, bool hold = true);

// The memory test alone: writes and verifies borrowed external RAM until
// the budget is spent or there is none left to borrow. `progress` is
// called after each block with the KB done so far.
MemoryTestResult test_memory(ports::Memory &memory, ports::Clock &clock, uint32_t budget_us,
                             void (*progress)(uint32_t kb, void *context), void *context);

} // namespace shell
