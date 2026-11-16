// Drives and paths: "C:\docs\notes.txt". Volumes (ports::Volume) are
// mounted under drive letters; everything else goes through here.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ports.hpp"

namespace fs {

using ports::DirEntry;
using ports::FsError;
using ports::OpenMode;
using ports::VolumeInfo;

constexpr int kMaxPath = 160;

const char *error_text(FsError e);

namespace path {
// "C:\docs\a.txt" -> 'C' (upper case), 0 if there is no drive letter.
char drive(const char *path);
// The part after "C:\" ("docs\a.txt"), or the path itself without a drive.
const char *relative(const char *path);
// The last name in a path ("a.txt"), "" for a drive root.
const char *name(const char *path);
// The extension without the dot, lower case into `out` ("txt"), "" if none.
void extension(const char *path, char *out, int capacity);
// The folder holding a path ("C:\docs\a.txt" -> "C:\docs", "C:\docs" -> "C:\").
void parent(const char *path, char *out, int capacity);
// Folder plus name, with one '\' between them.
bool join(const char *folder, const char *name, char *out, int capacity);
// A name that can be used for a file or folder.
bool valid_name(const char *name);
// Where a file is written before it takes the place of `path`: a hidden name
// in the same folder ("C:\docs\a.txt" -> "C:\docs\.~a.txt.tmp"). False if it
// does not fit.
bool temporary(const char *path, char *out, int capacity);
} // namespace path

// A file opened through Storage.
struct File {
    ports::Volume *volume = nullptr;
    int handle = -1;
    bool open() const { return volume != nullptr; }
};

// A file being saved the safe way: see Storage::begin_save().
struct Save {
    File file;
    char path[kMaxPath] = {};
    char temporary[kMaxPath] = {};
};

class Storage {
public:
    static constexpr int kMaxDrives = 4;

    void mount(char letter, ports::Volume &volume);
    ports::Volume *volume(char letter) const;
    // The mounted drive letters, in order; returns how many.
    int drives(char *letters, int capacity) const;

    FsError info(char letter, VolumeInfo &out) const;
    FsError entry(const char *folder, int index, DirEntry &out) const;
    FsError stat(const char *path, DirEntry &out) const;
    FsError make_folder(const char *path) const;
    FsError remove(const char *path) const;
    FsError rename(const char *from, const char *to) const; // same drive only
    // `from` takes the name `to` in one step, over a file already there.
    FsError replace(const char *from, const char *to) const; // same drive only

    FsError open(const char *path, OpenMode mode, File &file) const;
    FsError read(File &file, void *data, size_t size, size_t &done) const;
    FsError write(File &file, const void *data, size_t size) const;
    FsError seek(File &file, uint32_t offset) const;
    FsError close(File &file) const;

    // Saving without putting the old file at risk. The new contents go to a
    // temporary file beside it (write() to save.file); finish_save() closes
    // it and gives it the real name in one step. Until then the old file is
    // untouched, so a power cut, a full disk or a failed write leaves it as
    // it was. finish_save(save, false) gives up: it removes the temporary
    // file. It also does that by itself when its own steps fail.
    FsError begin_save(const char *path, Save &save) const;
    FsError finish_save(Save &save, bool keep = true) const;

    // Whole files at once. write_file() saves the safe way too.
    FsError read_file(const char *path, void *data, size_t capacity, size_t &size) const;
    FsError write_file(const char *path, const void *data, size_t size) const;

private:
    ports::Volume *resolve(const char *path, FsError &error) const;

    char letters_[kMaxDrives] = {};
    ports::Volume *volumes_[kMaxDrives] = {};
    int count_ = 0;
};

// Folders and text files for a new RAM disk, so there is something to see.
void add_samples(Storage &storage, char drive);

} // namespace fs
