// C: on the board: the LittleFS partition "storage", mounted at /c through
// ESP-IDF's POSIX layer, so the shared POSIX drive code does the work.
#pragma once

#include <cstdio>

#include "esp_littlefs.h"

#include "core/fs/posix_volume.hpp"

class LittleFsVolume : public fs::PosixVolume {
public:
    LittleFsVolume() : PosixVolume("/c") {}

    // Mounts the partition, formatting it if it holds no file system yet.
    // Returns false if even that fails.
    bool mount(bool &formatted)
    {
        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path = "/c";
        conf.partition_label = "storage";
        conf.format_if_mount_failed = false;
        formatted = false;
        if (esp_vfs_littlefs_register(&conf) == ESP_OK) {
            return true;
        }
        // No file system yet (first start) or a damaged one: format.
        conf.format_if_mount_failed = true;
        formatted = true;
        return esp_vfs_littlefs_register(&conf) == ESP_OK;
    }

    ports::FsError info(ports::VolumeInfo &out) override
    {
        snprintf(out.label, sizeof(out.label), "Flash disk");
        size_t total = 0, used = 0;
        if (esp_littlefs_info("storage", &total, &used) != ESP_OK) {
            return ports::FsError::Io;
        }
        out.total_bytes = total;
        out.free_bytes = total - used;
        out.keeps_files = true;
        return ports::FsError::Ok;
    }
};
