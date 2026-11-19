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
// A number that stands for a folder, the same however its path is written:
// case, '/' or '\\' and a separator at the end do not count. Two folders
// may share one (it is a 32-bit hash), so it says "may be the same folder",
// never "is". What a change notice is matched by (Storage::set_changed).
uint32_t key(const char *folder);
} // namespace path

// A file opened through Storage.
struct File {
    ports::Volume *volume = nullptr;
    int handle = -1;
    // The folder it is in, for a file opened to be written: closing it is a
    // change to that folder (Storage::set_changed). "" when opened to read.
    char folder[kMaxPath] = {};
    bool open() const { return volume != nullptr; }
};

// A file being saved the safe way: see Storage::begin_save().
struct Save {
    File file;
    char path[kMaxPath] = {};
    char temporary[kMaxPath] = {};
};

// Safe to use from several tasks once it has a lock (set_lock()): every call
// below runs from start to end with the lock held, so the drive list and each
// drive's own tables (open files, free blocks) only ever see one task at a
// time. One lock covers all the drives. The calls that are several steps
// (begin_save, finish_save, read_file, write_file) hold it across all of
// them. Between calls nothing is held: a task that reads or writes a file
// piece by piece lets the others in between the pieces.
//
// What the lock does not do: it does not stop two tasks from working on the
// same file. A File belongs to the task that opened it, and two saves of the
// same path at once use the same temporary file.
class Storage {
public:
    static constexpr int kMaxDrives = 4;

    // The lock to take around every call; nullptr (the default) for none,
    // which is right while only one task uses the file system. Set it before
    // a second task starts, never while a call is running.
    void set_lock(ports::Lock *lock) { lock_ = lock; }

    // Change notices: `changed` is called with the folder ("C:\\Documents")
    // whose list of files has just changed, after every call here that made,
    // removed, renamed or finished writing something in it. A rename or a
    // replace across folders tells both. It is called on the task that made
    // the change, after the lock has been given back, so it may be any task
    // and must be short and must not wait: set a flag, post a message
    // (msg::post). `folder` is only good during the call. One listener; set
    // it before a second task starts. The shell's is in
    // core/src/shell/desktop.cpp (docs/journal/2026-11-18-change-notices.md).
    //
    // What is not told: files written behind Storage's back (straight to a
    // Volume), and the hidden temporary file of a save, which comes and goes
    // inside one call.
    using Changed = void (*)(const char *folder, void *context);
    void set_changed(Changed changed, void *context)
    {
        changed_ = changed;
        changed_context_ = context;
    }

    void mount(char letter, ports::Volume &volume);
    ports::Volume *volume(char letter) const;
    // The mounted drive letters, in order; returns how many.
    int drives(char *letters, int capacity) const;

    FsError info(char letter, VolumeInfo &out) const;
    FsError entry(const char *folder, int index, DirEntry &out) const;
    FsError entries(const char *folder, int first, DirEntry *out, int capacity, int &count) const;
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
    // The work of the calls above without taking the lock, for the calls
    // made of several steps: the lock is not recursive, so nothing in here
    // may call the public ones.
    ports::Volume *find(char letter) const;
    ports::Volume *resolve(const char *path, FsError &error) const;
    FsError do_stat(const char *path, DirEntry &out) const;
    FsError do_remove(const char *path) const;
    FsError do_replace(const char *from, const char *to) const;
    FsError do_open(const char *path, OpenMode mode, File &file) const;
    FsError do_read(File &file, void *data, size_t size, size_t &done) const;
    FsError do_write(File &file, const void *data, size_t size) const;
    FsError do_close(File &file) const;
    FsError do_begin_save(const char *path, Save &save) const;
    FsError do_finish_save(Save &save, bool keep) const;

    // Tells the listener that the folder holding `path` changed. Never with
    // the lock held.
    void tell(const char *path) const;
    void tell_both(const char *from, const char *to) const;

    ports::Lock *lock_ = nullptr;
    Changed changed_ = nullptr;
    void *changed_context_ = nullptr;
    char letters_[kMaxDrives] = {};
    ports::Volume *volumes_[kMaxDrives] = {};
    int count_ = 0;
};

// Folders and a few text files for a new drive, so there is something to
// see. All a new C: gets from the firmware on the board; the rest is copied
// by the computer when the board is flashed (tools/flash.py).
void add_first_files(Storage &storage, char drive);
// The same and the sample pictures (core/fs/samples.hpp), for where nothing
// is copied afterwards: the PC programs, the browser version, the tests.
void add_samples(Storage &storage, char drive);

} // namespace fs
