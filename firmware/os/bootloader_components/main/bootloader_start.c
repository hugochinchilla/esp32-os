/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Espressif's components/bootloader/subproject/main/bootloader_start.c
 * (ESP-IDF v5.5), with one change: selected_boot_partition() starts the
 * BIOS (the app of subtype "test" in the partition table) unless the BIOS
 * has just run. The factory-reset and test-pin options, the deep-sleep short
 * cut and the optional hooks, which this project does not use, are removed.
 */
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_rom_caps.h"
#include "rom/rtc.h"
#include "esp_image_format.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"
#include "boot_flag.h"

static const char *TAG = "boot";

static int select_partition_number(bootloader_state_t *bs);
static int selected_boot_partition(const bootloader_state_t *bs);

/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void __attribute__((noreturn)) call_start_cpu0(void)
{
    // 1. Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

    // 2. Select the number of boot partition
    bootloader_state_t bs = {0};
    int boot_index = select_partition_number(&bs);
    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    // 3. Load the app image for booting
    bootloader_utility_load_boot_image(&bs, boot_index);
}

// Select the number of boot partition
static int select_partition_number(bootloader_state_t *bs)
{
    // 1. Load partition table
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }

    // 2. Select the number of boot partition
    return selected_boot_partition(bs);
}

/*
 * The BIOS or the OS.
 *
 * The BIOS runs first after every reset. When it is done it leaves
 * CHINCHILLA_START_OS in the reserved RTC memory and restarts the chip; this
 * code then finds the word and starts the OS instead, leaving
 * CHINCHILLA_FROM_BIOS for it. After a power-on the memory holds anything,
 * which is not the word, so the BIOS runs.
 *
 * The update record (otadata) is read, and changed, only on the way to the
 * OS: bootloader_utility_get_selected_boot_partition() marks a new version
 * "on trial" and a version that was on trial "failed", and a pass through
 * the BIOS must not count as a start of the OS.
 *
 * With no BIOS in the flash, or one that does not verify, the OS starts.
 */
static int selected_boot_partition(const bootloader_state_t *bs)
{
    // Loading the OS resets the whole reserved block if its checksum is
    // wrong (after a power-on), and our word with it. Make it right first.
    // To find why the word was lost on the board (the journal, "Second try"):
    // the block as it was found, before anything here changes it, and a
    // second copy the BIOS leaves in RTC slow memory, read but not used.
    const bool valid = bootloader_common_get_rtc_retain_mem_partition() != NULL;
    {
        const uint32_t *raw = (const uint32_t *)bootloader_common_get_rtc_retain_mem();
        esp_rom_printf("chinchilla: reset reason %d, block at %p %s: %08x %08x %08x %08x %08x %08x, slow copy %08x\n",
                       (int)esp_rom_get_reset_reason(0), raw, valid ? "valid" : "NOT valid",
                       raw[0], raw[1], raw[2], raw[3], raw[4], raw[5],
                       *(const volatile uint32_t *)CHINCHILLA_SLOW_COPY_ADDRESS);
    }
    if (!valid) {
        bootloader_common_reset_rtc_retain_mem();
        bootloader_common_update_rtc_retain_mem(NULL, false);
    }
    uint32_t word;
    uint8_t *custom = bootloader_common_get_rtc_retain_mem()->custom;
    memcpy(&word, custom, sizeof(word));

    // The image is only read when the BIOS is wanted.
    esp_image_metadata_t data;
    const bool wanted = word != CHINCHILLA_START_OS;
    const bool present = wanted && bs->test.offset != 0 &&
                         esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &bs->test, &data) == ESP_OK;
    const bool bios = chinchilla_start_bios(&word, present);
    memcpy(custom, &word, sizeof(word));
    if (bios) {
        ESP_LOGI(TAG, "starting the BIOS");
        return TEST_APP_INDEX;
    }
    if (wanted) {
        ESP_LOGW(TAG, "no BIOS in the flash: starting the OS");
    } else {
        ESP_LOGI(TAG, "BIOS done: starting the OS");
    }
    return bootloader_utility_get_selected_boot_partition(bs);
}

#if CONFIG_LIBC_NEWLIB
// Return global reent struct if any newlib functions are linked to bootloader
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif
