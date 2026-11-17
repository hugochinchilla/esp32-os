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
 *
 * docs/journal/2026-11-17-bios-second-stage.md */
#pragma once

#define CHINCHILLA_START_OS 0x534F4843u  /* "CHOS" */
#define CHINCHILLA_FROM_BIOS 0x42464843u /* "CHFB" */

#include <stdbool.h>
#include <stdint.h>

/* Diagnosis only (2026-11-17): the last word of RTC slow memory, where the
 * BIOS leaves a second copy of the word and the bootloader prints it. Nothing
 * else of ours uses RTC slow memory. To be removed once the handover works. */
#define CHINCHILLA_SLOW_COPY_ADDRESS 0x50001ffcu

/* The bootloader's choice, as a function so the PC tests can run it:
 * whether to start the BIOS, and the word to leave behind. `bios_present`:
 * there is a BIOS in the flash and its image verifies. */
static inline bool chinchilla_start_bios(uint32_t *word, bool bios_present)
{
    if (*word == CHINCHILLA_START_OS) {
        *word = CHINCHILLA_FROM_BIOS;
        return false;
    }
    *word = 0;
    return bios_present;
}
