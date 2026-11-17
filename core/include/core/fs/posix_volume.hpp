// A drive kept in a folder of a POSIX file system: a folder of the PC in the
// simulator, the LittleFS flash drive on the board (through ESP-IDF's POSIX
// layer). Only POSIX calls, no ESP-IDF headers. info() is left to each.
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/ports.hpp"

namespace fs {

class PosixVolume : public ports::Volume {
public:
    explicit PosixVolume(const std::string &root) : root_(root) {}

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
                out.modified = when(s);
                closedir(d);
                return ports::FsError::Ok;
            }
        }
        closedir(d);
        return ports::FsError::NotFound;
    }

    // One pass over the folder for a run of entries (a listing): opening
    // it and stepping to the index again for every entry made a folder of
    // 80 files cost some 3,000 steps on the board's drive.
    ports::FsError entries(const char *folder, int first, ports::DirEntry *out, int capacity, int &count) override
    {
        count = 0;
        const std::string dir = full(folder);
        DIR *d = opendir(dir.c_str());
        if (d == nullptr) {
            const ports::FsError e = error();
            return e == ports::FsError::NotFound ? ports::FsError::Ok : e;
        }
        int seen = 0;
        for (dirent *e; count < capacity && (e = readdir(d)) != nullptr;) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            if (seen++ < first) {
                continue;
            }
            ports::DirEntry &o = out[count++];
            const size_t n = strnlen(e->d_name, sizeof(o.name) - 1); // longer names are cut
            memcpy(o.name, e->d_name, n);
            o.name[n] = 0;
            struct stat s;
            const std::string p = dir + "/" + e->d_name;
            o.folder = ::stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
            o.size = o.folder ? 0 : static_cast<uint32_t>(s.st_size);
            o.modified = when(s);
        }
        closedir(d);
        return ports::FsError::Ok;
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
        out.modified = when(s);
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
        const std::string f = full(from), t = full(to);
        if (access(t.c_str(), F_OK) == 0 && t != f) {
            return ports::FsError::Exists;
        }
        // A change of case only: the same entry, renamed to the name as given.
        const std::string target = t == f ? full_parent_then(to) : t;
        return ::rename(f.c_str(), target.c_str()) == 0 ? ports::FsError::Ok : error();
    }

    ports::FsError replace(const char *from, const char *to) override
    {
        // POSIX rename() swaps the name over in one step. LittleFS does the
        // same on the board (lfs_rename), as one change to its log.
        const std::string f = full(from), t = full(to);
        struct stat s;
        if (::stat(f.c_str(), &s) != 0) {
            return error();
        }
        if (S_ISDIR(s.st_mode) || (::stat(t.c_str(), &s) == 0 && S_ISDIR(s.st_mode))) {
            return ports::FsError::IsFolder;
        }
        return ::rename(f.c_str(), t.c_str()) == 0 ? ports::FsError::Ok : error();
    }

    ports::FsError open(const char *path, ports::OpenMode mode, int &file) override
    {
        const int flags = mode == ports::OpenMode::Read    ? O_RDONLY
                          : mode == ports::OpenMode::Write ? (O_WRONLY | O_CREAT | O_TRUNC)
                                                           : O_RDWR;
        const std::string p = full(path);
        struct stat s;
        if (::stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode)) {
            return ports::FsError::IsFolder;
        }
        const int fd = ::open(p.c_str(), flags, 0644);
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
        struct stat s;
        if (fstat(file, &s) != 0 || offset > static_cast<uint64_t>(s.st_size)) {
            return ports::FsError::Io; // not past the end, as on the RAM disk
        }
        return lseek(file, offset, SEEK_SET) == static_cast<off_t>(offset) ? ports::FsError::Ok : error();
    }

    ports::FsError close(int file) override
    {
        return ::close(file) == 0 ? ports::FsError::Ok : error();
    }

protected:
    // The path in the file system. Names ignore case, as everywhere in the
    // OS: each part that does not exist as written is looked for in its
    // folder without regard to case (ASCII letters); parts not found stay
    // as written (a name being created).
    std::string full(const char *path) const { return resolve(path, false); }
    // The same, but the last part exactly as written.
    std::string full_parent_then(const char *path) const { return resolve(path, true); }

    std::string resolve(const char *path, bool last_as_written) const
    {
        std::string p = root_;
        bool found = true;
        while (*path != 0) {
            while (*path == '\\' || *path == '/') {
                path++;
            }
            const char *end = path;
            while (*end != 0 && *end != '\\' && *end != '/') {
                end++;
            }
            if (end == path) {
                break;
            }
            const std::string name(path, static_cast<size_t>(end - path));
            const bool last = *end == 0 || end[1] == 0;
            std::string next = p + "/" + name;
            if (found && !(last && last_as_written) && access(next.c_str(), F_OK) != 0) {
                found = false;
                if (DIR *d = opendir(p.c_str())) {
                    for (dirent *e; (e = readdir(d)) != nullptr;) {
                        if (strcasecmp(e->d_name, name.c_str()) == 0) {
                            next = p + "/" + e->d_name;
                            found = true;
                            break;
                        }
                    }
                    closedir(d);
                }
            }
            p = next;
            path = end;
        }
        return p;
    }

    // The file system's own date. One from before 2000 was written while
    // the machine did not know the time (the board counts from 1970 until
    // the network tells it): not known.
    static int64_t when(const struct stat &s)
    {
        return s.st_mtime >= 946684800 ? static_cast<int64_t>(s.st_mtime) : 0;
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

} // namespace fs
