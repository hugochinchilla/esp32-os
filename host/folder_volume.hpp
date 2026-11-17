// A folder of the PC shown as a drive in the simulator and the tests.
#pragma once

#include <sys/statvfs.h>

#include <cstdio>
#include <string>

#include "core/fs/posix_volume.hpp"

namespace host {

class FolderVolume : public fs::PosixVolume {
public:
    // `label` is what the drive is called (My Board, the start-up screen).
    explicit FolderVolume(const std::string &root, const char *label = "PC folder")
        : PosixVolume(root), label_(label)
    {
    }

    ports::FsError info(ports::VolumeInfo &out) override
    {
        snprintf(out.label, sizeof(out.label), "%s", label_);
        struct statvfs s;
        if (statvfs(root_.c_str(), &s) != 0) {
            return ports::FsError::Io;
        }
        out.total_bytes = static_cast<uint64_t>(s.f_blocks) * s.f_frsize;
        out.free_bytes = static_cast<uint64_t>(s.f_bavail) * s.f_frsize;
        out.keeps_files = true;
        return ports::FsError::Ok;
    }

private:
    const char *label_;
};

} // namespace host
