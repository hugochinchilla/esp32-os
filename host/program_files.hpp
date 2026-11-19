// The Linux program's programs: files of the PC, made by its build
// (programs/*.app beside the program itself), copied to C:\Programs when
// it starts. It is what tools/flash.py does for the board: the system
// carries no programs, and whoever puts the system on a machine puts the
// programs on its C: with it. A file that is already there with the same
// bytes is left alone; nothing else in C:\Programs is touched.
#pragma once

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/fs/fs.hpp"

namespace host {

// The folder the program files are in: `programs` beside the running
// program. Empty if that cannot be told.
inline std::string program_files_folder()
{
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) {
        return "";
    }
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (slash == nullptr) {
        return "";
    }
    *slash = 0;
    return std::string(exe) + "/programs";
}

// Copies every *.app of `from` (a folder of the PC) to `to` (a folder of a
// drive, made if need be). Returns how many were written, -1 if one could
// not be.
inline int install_program_files(fs::Storage &storage, const char *from, const char *to)
{
    DIR *dir = opendir(from);
    if (dir == nullptr) {
        return 0; // no programs came with this build
    }
    storage.make_folder(to); // there already: fine
    int written = 0;
    bool failed = false;
    while (const dirent *entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".app") != 0) {
            continue;
        }
        std::vector<char> bytes;
        if (FILE *f = fopen((std::string(from) + "/" + name).c_str(), "rb")) {
            char block[65536];
            size_t got = 0;
            while ((got = fread(block, 1, sizeof(block), f)) > 0) {
                bytes.insert(bytes.end(), block, block + got);
            }
            fclose(f);
        }
        if (bytes.empty()) {
            failed = true;
            continue;
        }
        const std::string path = std::string(to) + "\\" + name;
        fs::DirEntry there;
        if (storage.stat(path.c_str(), there) == ports::FsError::Ok && !there.folder && there.size == bytes.size()) {
            std::vector<char> old(bytes.size());
            size_t size = 0;
            if (storage.read_file(path.c_str(), old.data(), old.size(), size) == ports::FsError::Ok &&
                size == bytes.size() && memcmp(old.data(), bytes.data(), size) == 0) {
                continue;
            }
        }
        if (storage.write_file(path.c_str(), bytes.data(), bytes.size()) == ports::FsError::Ok) {
            written++;
        } else {
            failed = true;
        }
    }
    closedir(dir);
    return failed ? -1 : written;
}

} // namespace host
