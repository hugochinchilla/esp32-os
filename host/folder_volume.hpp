// A folder of the PC shown as a drive in the simulator (POSIX only).
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "core/ports.hpp"

namespace host {

class FolderVolume : public ports::Volume {
public:
    explicit FolderVolume(const std::string &root) : root_(root) {}

    ports::FsError info(ports::VolumeInfo &out) override
    {
        snprintf(out.label, sizeof(out.label), "PC folder");
        struct statvfs s;
        if (statvfs(root_.c_str(), &s) != 0) {
            return ports::FsError::Io;
        }
        out.total_bytes = static_cast<uint64_t>(s.f_blocks) * s.f_frsize;
        out.free_bytes = static_cast<uint64_t>(s.f_bavail) * s.f_frsize;
        out.keeps_files = true;
        return ports::FsError::Ok;
    }

    ports::FsError entry(const char *folder, int index, ports::DirEntry &out) override
    {
        const std::string dir = full(folder);
        DIR *d = opendir(dir.c_str());
        if (d == nullptr) {
            return error();
        }
        int seen = 0;
        for (dirent *e; (e = readdir(d)) != nullptr;) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if (seen++ == index) {
                const size_t n = strnlen(e->d_name, sizeof(out.name) - 1); // longer names are cut
                memcpy(out.name, e->d_name, n);
                out.name[n] = 0;
                struct stat s;
                const std::string p = dir + "/" + e->d_name;
                out.folder = ::stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
                out.size = out.folder ? 0 : static_cast<uint32_t>(s.st_size);
                closedir(d);
                return ports::FsError::Ok;
            }
        }
        closedir(d);
        return ports::FsError::NotFound;
    }

    ports::FsError stat(const char *path, ports::DirEntry &out) override
    {
        struct stat s;
        if (::stat(full(path).c_str(), &s) != 0) {
            return error();
        }
        const char *slash = strrchr(path, '\\');
        snprintf(out.name, sizeof(out.name), "%s", slash ? slash + 1 : path);
        out.folder = S_ISDIR(s.st_mode);
        out.size = out.folder ? 0 : static_cast<uint32_t>(s.st_size);
        return ports::FsError::Ok;
    }

    ports::FsError make_folder(const char *path) override
    {
        return mkdir(full(path).c_str(), 0755) == 0 ? ports::FsError::Ok : error();
    }

    ports::FsError remove(const char *path) override
    {
        const std::string p = full(path);
        struct stat s;
        if (::stat(p.c_str(), &s) != 0) {
            return error();
        }
        return (S_ISDIR(s.st_mode) ? rmdir(p.c_str()) : unlink(p.c_str())) == 0 ? ports::FsError::Ok
                                                                                : error();
    }

    ports::FsError rename(const char *from, const char *to) override
    {
        const std::string t = full(to);
        if (access(t.c_str(), F_OK) == 0) {
            return ports::FsError::Exists;
        }
        return ::rename(full(from).c_str(), t.c_str()) == 0 ? ports::FsError::Ok : error();
    }

    ports::FsError open(const char *path, ports::OpenMode mode, int &file) override
    {
        const int flags = mode == ports::OpenMode::Read    ? O_RDONLY
                          : mode == ports::OpenMode::Write ? (O_WRONLY | O_CREAT | O_TRUNC)
                                                           : O_RDWR;
        const int fd = ::open(full(path).c_str(), flags, 0644);
        if (fd < 0) {
            return error();
        }
        file = fd;
        return ports::FsError::Ok;
    }

    ports::FsError read(int file, void *data, size_t size, size_t &done) override
    {
        const ssize_t n = ::read(file, data, size);
        done = n > 0 ? static_cast<size_t>(n) : 0;
        return n < 0 ? error() : ports::FsError::Ok;
    }

    ports::FsError write(int file, const void *data, size_t size) override
    {
        return ::write(file, data, size) == static_cast<ssize_t>(size) ? ports::FsError::Ok : error();
    }

    ports::FsError seek(int file, uint32_t offset) override
    {
        return lseek(file, offset, SEEK_SET) == static_cast<off_t>(offset) ? ports::FsError::Ok : error();
    }

    ports::FsError close(int file) override
    {
        return ::close(file) == 0 ? ports::FsError::Ok : error();
    }

private:
    std::string full(const char *path) const
    {
        std::string p = root_ + "/" + path;
        for (char &c : p) {
            if (c == '\\') {
                c = '/';
            }
        }
        return p;
    }

    static ports::FsError error()
    {
        switch (errno) {
        case ENOENT:
            return ports::FsError::NotFound;
        case EEXIST:
            return ports::FsError::Exists;
        case ENOTEMPTY:
            return ports::FsError::NotEmpty;
        case ENOSPC:
            return ports::FsError::NoSpace;
        case EISDIR:
            return ports::FsError::IsFolder;
        case ENOTDIR:
            return ports::FsError::NotFolder;
        default:
            return ports::FsError::Io;
        }
    }

    std::string root_;
};

} // namespace host
