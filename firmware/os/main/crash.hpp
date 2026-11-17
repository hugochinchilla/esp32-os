// The watchdog and the crash record on the ESP32: the supervisor that
// notices a stuck UI task, the hook into the processor's panic handler, and
// the memory the record waits in across the restart.
// docs/journal/2026-11-17-watchdog.md.
#pragma once

#include <cstdint>

#include "core/ports.hpp"
#include "core/sys/crash.hpp"

// Call once, first thing in app_main: takes what the last session left (its
// crash record, or failing that the chip's own reason for an abnormal
// restart) and clears it, so it is reported once.
void crash_init();
// How the last session ended; nullptr if it ended well.
const sys::CrashRecord *crash_last();

// Starts watching the calling task, under `name`: from now on it must call
// watchdog_beat() at least every limit_ms, or the machine writes a crash
// record and restarts. limit_ms 0: no watchdog.
void watchdog_start(const char *name, uint32_t limit_ms);
void watchdog_beat();

// A fault on purpose, in the calling task (tools/dbg.py fault). Does not
// return.
void crash_fault(ports::Fault kind);
