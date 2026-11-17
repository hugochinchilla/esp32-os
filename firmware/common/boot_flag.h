/* The word the BIOS, the bootloader and the OS pass to each other across a
 * restart. It lives in the bootloader's reserved RTC memory
 * (CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC, the first four bytes of
 * rtc_retain_mem_t.custom), which a restart keeps and a power cut does not.
 *
 *   anything else       the bootloader starts the BIOS
 *   CHINCHILLA_START_OS  written by the BIOS when it is done, or by the OS
 *                        to restart without the BIOS: the bootloader starts
 *                        the OS and changes the word to
 *   CHINCHILLA_FROM_BIOS read and cleared by the OS: the start-up screens
 *                        have been shown (or were not wanted), do not show
 *                        them again
 *   CHINCHILLA_SKIP_BIOS written by the OS as it stops on an error: the
 *                        bootloader starts the OS and clears the word, so
 *                        the OS starts as it does with no BIOS and shows
 *                        its error screen with the record still whole
 *
 * A reset by a watchdog or a dip in the supply is treated as that last
 * case: the OS learns the cause from the chip, which a pass through the
 * BIOS would replace with "restarted by software".
 *
 * docs/journal/2026-11-17-bios-second-stage.md */
#pragma once

#define CHINCHILLA_START_OS 0x534F4843u  /* "CHOS" */
#define CHINCHILLA_FROM_BIOS 0x42464843u /* "CHFB" */
#define CHINCHILLA_SKIP_BIOS 0x4B534843u /* "CHSK" */

#include <stdbool.h>
#include <stdint.h>

/* The bootloader's choice, as a function so the PC tests can run it:
 * whether to start the BIOS, and the word to leave behind. `bios_present`:
 * there is a BIOS in the flash and its image verifies. */
static inline bool chinchilla_start_bios(uint32_t *word, bool bios_present)
{
    if (*word == CHINCHILLA_START_OS) {
        *word = CHINCHILLA_FROM_BIOS;
        return false;
    }
    if (*word == CHINCHILLA_SKIP_BIOS) {
        *word = 0;
        return false;
    }
    *word = 0;
    return bios_present;
}

/* Whether the chip's own reset reason (the ROM's number, soc_reset_reason_t)
 * is a failure: a watchdog or a brownout. Not power-on (1), a restart by
 * software (3, 12) or the USB port (21, 22). */
static inline bool chinchilla_reset_is_failure(int rom_reason)
{
    switch (rom_reason) {
    case 7:  /* main watchdog 0, digital core */
    case 8:  /* main watchdog 1, digital core */
    case 9:  /* RTC watchdog, digital core */
    case 11: /* main watchdog 0, processor 0 */
    case 13: /* RTC watchdog, processor 0 */
    case 15: /* brownout */
    case 16: /* RTC watchdog, whole chip */
    case 17: /* main watchdog 1, processor 0 */
    case 18: /* super watchdog */
        return true;
    default:
        return false;
    }
}
