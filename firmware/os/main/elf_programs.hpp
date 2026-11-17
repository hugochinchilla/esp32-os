// The Loader port on the board (backlog item 27): programs kept in files
// are ELF files, made ready by Espressif's ELF loader (the elf_loader
// component). Their code and data go into external RAM, which the ESP32-S3
// can run code from, and what they ask of the system by name is found in
// the table that tools/gen_exports.py writes from the core library when
// the firmware is built: the OS calls and little else.
#pragma once

#include <cerrno>
#include <cstdint>

#include "esp_elf.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "core/ports.hpp"

extern "C" {
struct chinchilla_export {
    const char *name;
    const void *address;
};
extern const struct chinchilla_export chinchilla_exports[];
extern const unsigned chinchilla_export_count;
}

class ElfLoader : public ports::Loader {
public:
    // Once, at start-up: the table of what programs may ask for.
    bool begin()
    {
        static_assert(sizeof(chinchilla_export) == sizeof(esp_elfsym), "the loader's own table entry");
        ready_ = esp_elf_register_symbol(reinterpret_cast<esp_elf_symbol_table_t *>(chinchilla_exports)) == 0;
        ESP_LOGI("programs", "%u names exported to loaded programs%s", chinchilla_export_count,
                 ready_ ? "" : " (the table was refused)");
        return ready_;
    }

    void *load(const uint8_t *image, size_t size, const char *&why) override
    {
        if (!ready_) {
            why = "the loader did not start";
            return nullptr;
        }
        if ((reinterpret_cast<uintptr_t>(image) & 3) != 0) {
            why = "the loader was given the file at an odd address";
            return nullptr;
        }
        auto *elf = static_cast<esp_elf_t *>(heap_caps_calloc(1, sizeof(esp_elf_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (elf == nullptr || esp_elf_init(elf) < 0) {
            heap_caps_free(elf);
            why = "there is not enough memory";
            return nullptr;
        }
        const int result = esp_elf_relocate(elf, image, size);
        if (result < 0) {
            ESP_LOGW("programs", "not loaded: %d", result);
            esp_elf_deinit(elf);
            heap_caps_free(elf);
            // The loader prints the name it could not find on the serial port.
            why = result == -ENOMEM ? "there is not enough memory"
                                    : "it is not a program for this system, or needs something this system does not have";
            return nullptr;
        }
        return elf;
    }

    int enter(void *module) override
    {
        // Its static objects are made, then its way in is called. What the
        // way in returns is not passed on by the loader.
        return esp_elf_request(static_cast<esp_elf_t *>(module), 0, 0, nullptr);
    }

    void unload(void *module) override
    {
        auto *elf = static_cast<esp_elf_t *>(module);
        esp_elf_deinit(elf);
        heap_caps_free(elf);
    }

private:
    bool ready_ = false;
};
