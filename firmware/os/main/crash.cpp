#include "crash.hpp"

#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bootloader_common.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_cpu_utils.h"
#include "esp_debug_helpers.h"
#include "esp_log.h"
#include "esp_private/panic_internal.h"
#include "esp_private/system_internal.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "xtensa_context.h"

#include "boot_flag.h"
#include "core/proc/table.hpp"
#include "core/sys/watchdog.hpp"

// The panic handler's own flag and text for abort() (esp_system/panic.c).
extern "C" bool g_panic_abort;
extern "C" char *g_panic_abort_details;
extern "C" void __real_esp_panic_handler(void *info);

namespace {

const char *TAG = "crash";

// The record waits here across the restart. This memory belongs to the
// chip's low-power clock domain: a restart by software leaves it alone, and
// no start-up code clears it. A power cut does not keep it (nor did the
// reset after flashing, when this memory was tried for the one-shot fast
// start), so what is found here is checked before it is believed
// (sys::crash_valid).
RTC_NOINIT_ATTR sys::CrashRecord s_record;

sys::CrashRecord s_last; // the last session's, copied out at start-up
bool s_have_last = false;

// This session's record has been written: whatever goes wrong after that
// (the restart itself failing, say) must not replace it.
volatile bool s_written = false;

sys::Watchdog s_dog;
int s_slot = sys::Watchdog::kNobody;
TaskHandle_t s_task = nullptr; // the watched task
esp_timer_handle_t s_timer = nullptr;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// The restart that follows an error goes straight back to this firmware,
// not through the BIOS (firmware/common/boot_flag.h): the record above was
// found spoiled after a pass through the BIOS on the board, and until the
// BIOS shows the error screen itself (step 3) this is where it is shown.
// A few bytes copied: safe in the panic handler.
void skip_bios_once()
{
    const uint32_t word = CHINCHILLA_SKIP_BIOS;
    memcpy(bootloader_common_get_rtc_retain_mem()->custom, &word, sizeof(word));
}

// The process the UI task is working for, if `task` is the UI task. Read
// from another task or from the panic handler without a lock: the UI task is
// stuck or stopped, and the worst a race could do is a wrong name.
void fill_process(sys::CrashRecord &r, TaskHandle_t task)
{
    if (task == nullptr || task != s_task) {
        return;
    }
    const proc::Pid pid = proc::table().current();
    const proc::Process *p = proc::table().find(pid);
    if (p != nullptr) {
        r.pid = pid;
        sys::crash_text(r.program, sizeof(r.program), p->name);
    }
}

// Once a second, on the timer task (priority 22, above everything of ours
// and below Wi-Fi): a few loads and a compare while all is well.
void supervise(void *)
{
    uint32_t waited = 0;
    const uint32_t now = now_ms();
    const int slot = s_dog.overdue(now, waited);
    if (slot == sys::Watchdog::kNobody || s_written) {
        return;
    }
    sys::CrashRecord &r = s_record;
    sys::crash_begin(r, sys::CrashKind::Hang);
    r.uptime_ms = now;
    r.waited_ms = waited;
    sys::crash_text(r.task, sizeof(r.task), s_dog.name(slot));
    // Spinning on a processor, or waiting for something that never comes?
    r.core = sys::CrashRecord::kNoCore;
    for (int core = 0; core < portNUM_PROCESSORS; core++) {
        if (xTaskGetCurrentTaskHandleForCore(core) == s_task) {
            r.core = static_cast<uint8_t>(core);
        }
    }
    fill_process(r, s_task);
    sys::crash_seal(r);
    s_written = true;
    skip_bios_once();

    char line[160];
    sys::crash_headline(r, line, sizeof(line));
    ESP_LOGE(TAG, "watchdog: %s Silent for %u ms. Restarting.", line, static_cast<unsigned>(waited));
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(200));
    // As esp_restart(), without the shutdown handlers: they stop Wi-Fi and
    // the like politely, and may wait for ever on something the stuck task
    // holds.
    vTaskSuspendAll();
    esp_restart_noos();
}

// The chip's own word for an abnormal restart that left no record; nullptr
// for the ordinary ones (power on, the reset button, Restart, flashing).
const char *abnormal(esp_reset_reason_t why)
{
    switch (why) {
    case ESP_RST_PANIC: return "a panic that left no record";
    case ESP_RST_INT_WDT: return "the interrupt watchdog (interrupts were off too long)";
    case ESP_RST_TASK_WDT: return "the task watchdog";
    case ESP_RST_WDT: return "a hardware watchdog";
    case ESP_RST_BROWNOUT: return "the supply voltage dipped (brownout)";
    case ESP_RST_PWR_GLITCH: return "a glitch on the power supply";
    case ESP_RST_CPU_LOCKUP: return "the processor locked up";
    default: return nullptr;
    }
}

} // namespace

void crash_init()
{
    if (sys::crash_valid(s_record)) {
        s_last = s_record;
        s_have_last = true;
    } else if (const char *why = abnormal(esp_reset_reason())) {
        sys::crash_begin(s_last, sys::CrashKind::Reset);
        sys::crash_text(s_last.reason, sizeof(s_last.reason), why);
        sys::crash_seal(s_last);
        s_have_last = true;
    }
    sys::crash_clear(s_record);
}

const sys::CrashRecord *crash_last() { return s_have_last ? &s_last : nullptr; }

void watchdog_start(const char *name, uint32_t limit_ms)
{
    if (limit_ms == 0 || s_timer != nullptr) {
        return;
    }
    s_task = xTaskGetCurrentTaskHandle();
    s_slot = s_dog.watch(name, limit_ms, now_ms());
    const esp_timer_create_args_t args = {
        .callback = &supervise,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "watchdog",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &s_timer) != ESP_OK || esp_timer_start_periodic(s_timer, 1000000) != ESP_OK) {
        ESP_LOGE(TAG, "the watchdog could not be started");
        return;
    }
    ESP_LOGI(TAG, "watchdog: the %s task may be silent for %u s", name, static_cast<unsigned>(limit_ms / 1000));
}

void watchdog_beat() { s_dog.beat(s_slot, now_ms()); }

void crash_fault(ports::Fault kind)
{
    switch (kind) {
    case ports::Fault::Hang:
        // Spins without ever calling the OS, as a program stuck in a loop
        // would. The volatile keeps the compiler from removing the loop.
        for (volatile uint32_t n = 0;;) {
            const uint32_t next = n + 1;
            n = next;
        }
    case ports::Fault::BadPointer: {
        volatile uint32_t *const nowhere = nullptr;
        *nowhere = 0xC41C4111; // StoreProhibited
        break;
    }
    case ports::Fault::Abort:
        break;
    }
    abort();
}

// Every panic comes through here first (the linker puts this in front of
// ESP-IDF's handler: -Wl,--wrap=esp_panic_handler in CMakeLists.txt): a bad
// pointer, abort(), a failed assert, a stack overflow, the interrupt
// watchdog. ESP-IDF has already switched the cache back on and armed a
// hardware watchdog over the whole handler, so if anything here goes wrong
// in turn the machine still restarts, only without a record. Then ESP-IDF's
// handler prints its report on the serial port and restarts, as before.
extern "C" void __wrap_esp_panic_handler(void *arg)
{
    static volatile bool inside = false;
    const auto *info = static_cast<const panic_info_t *>(arg);
    if (!s_written && !inside && info != nullptr) {
        inside = true;
        sys::CrashRecord &r = s_record;
        sys::crash_begin(r, sys::CrashKind::Panic);
        r.uptime_ms = now_ms();
        r.core = static_cast<uint8_t>(info->core);
        r.pc = reinterpret_cast<uintptr_t>(info->addr);

        const char *reason = info->reason;
        if (g_panic_abort) {
            reason = g_panic_abort_details != nullptr ? g_panic_abort_details : "abort()";
            if (strncmp(reason, "***ERROR*** ", 12) == 0) {
                reason += 12; // "A stack overflow in task ui has been detected."
            }
        }
        sys::crash_text(r.reason, sizeof(r.reason), reason != nullptr ? reason : "unknown");

        const TaskHandle_t task = xTaskGetCurrentTaskHandleForCore(info->core);
        sys::crash_text(r.task, sizeof(r.task), task != nullptr ? pcTaskGetName(task) : "?");
        fill_process(r, task);

        // The chain of calls, as ESP-IDF's own report walks it.
        if (const auto *x = static_cast<const XtExcFrame *>(info->frame)) {
            if (!info->pseudo_excause && !g_panic_abort) {
                r.address = x->excvaddr;
            }
            esp_backtrace_frame_t frame = {};
            frame.pc = x->pc;
            frame.sp = x->a1;
            frame.next_pc = x->a0;
            frame.exc_frame = x;
            r.trace[r.trace_count++] = esp_cpu_process_stack_pc(frame.pc);
            while (r.trace_count < sys::CrashRecord::kTraceSize && frame.next_pc != 0 &&
                   esp_backtrace_get_next_frame(&frame)) {
                r.trace[r.trace_count++] = esp_cpu_process_stack_pc(frame.pc);
            }
        }
        sys::crash_seal(r);
        s_written = true;
    }
    skip_bios_once();
    __real_esp_panic_handler(arg);
}
