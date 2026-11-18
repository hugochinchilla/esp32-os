#include "core/fs/fs.hpp"

#include "core/fs/samples.hpp"

#include <cstdio>
#include <cstring>

namespace fs {

const char *error_text(FsError e)
{
    switch (e) {
    case FsError::Ok:
        return "done";
    case FsError::NotFound:
        return "not found";
    case FsError::Exists:
        return "already exists";
    case FsError::NotEmpty:
        return "the folder is not empty";
    case FsError::NoSpace:
        return "the disk is full";
    case FsError::BadName:
        return "the name cannot be used";
    case FsError::IsFolder:
        return "it is a folder";
    case FsError::NotFolder:
        return "it is not a folder";
    case FsError::TooManyOpen:
        return "too many files are open";
    case FsError::InUse:
        return "it is in use";
    default:
        return "the disk could not be read or written";
    }
}

namespace path {

char drive(const char *p)
{
    if (p[0] != 0 && p[1] == ':') {
        char c = p[0];
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        return (c >= 'A' && c <= 'Z') ? c : 0;
    }
    return 0;
}

const char *relative(const char *p)
{
    if (drive(p) != 0) {
        p += 2;
    }
    while (*p == '\\' || *p == '/') {
        p++;
    }
    return p;
}

const char *name(const char *p)
{
    const char *r = relative(p);
    const char *last = r;
    for (const char *q = r; *q != 0; q++) {
        if ((*q == '\\' || *q == '/') && q[1] != 0) {
            last = q + 1;
        }
    }
    return last;
}

void extension(const char *p, char *out, int capacity)
{
    const char *n = name(p);
    const char *dot = nullptr;
    for (const char *q = n; *q != 0; q++) {
        if (*q == '.') {
            dot = q;
        }
    }
    int i = 0;
    if (dot != nullptr) {
        for (const char *q = dot + 1; *q != 0 && i < capacity - 1; q++) {
            out[i++] = (*q >= 'A' && *q <= 'Z') ? static_cast<char>(*q - 'A' + 'a') : *q;
        }
    }
    out[i] = 0;
}

void parent(const char *p, char *out, int capacity)
{
    snprintf(out, capacity, "%s", p);
    int n = static_cast<int>(strlen(out));
    while (n > 0 && (out[n - 1] == '\\' || out[n - 1] == '/')) {
        out[--n] = 0; // trailing separators
    }
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/' && out[n - 1] != ':') {
        out[--n] = 0;
    }
    // Keep "C:\" for a drive root; drop the separator otherwise.
    if (n >= 1 && (out[n - 1] == '\\' || out[n - 1] == '/') && !(n == 3 && out[1] == ':')) {
        out[--n] = 0;
    } else if (n == 2 && out[1] == ':' && n < capacity - 1) {
        out[2] = '\\';
        out[3] = 0;
    }
}

bool join(const char *folder, const char *n, char *out, int capacity)
{
    const size_t len = strlen(folder);
    const bool sep = len > 0 && folder[len - 1] != '\\' && folder[len - 1] != '/';
    const int written = snprintf(out, capacity, "%s%s%s", folder, sep ? "\\" : "", n);
    return written >= 0 && written < capacity;
}

bool valid_name(const char *n)
{
    if (n[0] == 0 || strlen(n) > 63 || strcmp(n, ".") == 0 || strcmp(n, "..") == 0) {
        return false;
    }
    for (const char *p = n; *p != 0; p++) {
        if (static_cast<uint8_t>(*p) < 0x20 || strchr("\\/:*?\"<>|", *p) != nullptr) {
            return false;
        }
    }
    // No spaces or dots at the end, as in 98.
    const char last = n[strlen(n) - 1];
    return last != ' ' && last != '.';
}

bool temporary(const char *p, char *out, int capacity)
{
    // ".~" hides it from folder windows; ".tmp" keeps the name valid when
    // the cut falls on a dot or a space. 57 of the name's 63 characters fit.
    char folder[kMaxPath], leaf[64];
    parent(p, folder, sizeof(folder));
    snprintf(leaf, sizeof(leaf), ".~%.57s.tmp", name(p));
    return name(p)[0] != 0 && join(folder, leaf, out, capacity);
}

uint32_t key(const char *folder)
{
    size_t n = strlen(folder);
    while (n > 0 && (folder[n - 1] == '\\' || folder[n - 1] == '/')) {
        n--;
    }
    uint32_t h = 2166136261u; // FNV-1a
    for (size_t i = 0; i < n; i++) {
        char c = folder[i];
        c = c == '/' ? '\\' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    return h;
}

} // namespace path

namespace {

// Holds the file system's lock for as long as it lives.
class Held {
public:
    explicit Held(ports::Lock *lock) : lock_(lock)
    {
        if (lock_ != nullptr) {
            lock_->lock();
        }
    }
    ~Held()
    {
        if (lock_ != nullptr) {
            lock_->unlock();
        }
    }
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;

private:
    ports::Lock *lock_;
};

char upper(char letter)
{
    return (letter >= 'a' && letter <= 'z') ? static_cast<char>(letter - 'a' + 'A') : letter;
}

} // namespace

// The public calls: each takes the lock once and does its work through the
// do_ functions below, which never take it.

void Storage::mount(char letter, ports::Volume &volume)
{
    const Held held(lock_);
    letter = upper(letter);
    for (int i = 0; i < count_; i++) {
        if (letters_[i] == letter) {
            volumes_[i] = &volume;
            return;
        }
    }
    if (count_ < kMaxDrives) {
        letters_[count_] = letter;
        volumes_[count_] = &volume;
        count_++;
    }
}

ports::Volume *Storage::volume(char letter) const
{
    const Held held(lock_);
    return find(letter);
}

int Storage::drives(char *letters, int capacity) const
{
    const Held held(lock_);
    int n = 0;
    for (int i = 0; i < count_ && n < capacity; i++) {
        letters[n++] = letters_[i];
    }
    return n;
}

FsError Storage::info(char letter, VolumeInfo &out) const
{
    const Held held(lock_);
    ports::Volume *v = find(letter);
    return v != nullptr ? v->info(out) : FsError::NotFound;
}

FsError Storage::entry(const char *folder, int index, DirEntry &out) const
{
    const Held held(lock_);
    FsError e;
    ports::Volume *v = resolve(folder, e);
    return v != nullptr ? v->entry(path::relative(folder), index, out) : e;
}

FsError Storage::entries(const char *folder, int first, DirEntry *out, int capacity, int &count) const
{
    const Held held(lock_);
    count = 0;
    FsError e;
    ports::Volume *v = resolve(folder, e);
    return v != nullptr ? v->entries(path::relative(folder), first, out, capacity, count) : e;
}

FsError Storage::stat(const char *p, DirEntry &out) const
{
    const Held held(lock_);
    return do_stat(p, out);
}

void Storage::tell(const char *p) const
{
    if (changed_ == nullptr) {
        return;
    }
    char folder[kMaxPath];
    path::parent(p, folder, sizeof(folder));
    changed_(folder, changed_context_);
}

FsError Storage::make_folder(const char *p) const
{
    FsError e;
    {
        const Held held(lock_);
        ports::Volume *v = resolve(p, e);
        e = v != nullptr ? v->make_folder(path::relative(p)) : e;
    }
    if (e == FsError::Ok) {
        tell(p);
    }
    return e;
}

FsError Storage::remove(const char *p) const
{
    FsError e;
    {
        const Held held(lock_);
        e = do_remove(p);
    }
    if (e == FsError::Ok) {
        tell(p);
    }
    return e;
}

FsError Storage::rename(const char *from, const char *to) const
{
    FsError e;
    {
        const Held held(lock_);
        ports::Volume *v = resolve(from, e);
        if (v == nullptr) {
            return e;
        }
        if (path::drive(to) != 0 && path::drive(to) != path::drive(from)) {
            return FsError::BadName; // moving between drives is copying, later
        }
        e = v->rename(path::relative(from), path::relative(to));
    }
    if (e == FsError::Ok) {
        tell_both(from, to);
    }
    return e;
}

FsError Storage::replace(const char *from, const char *to) const
{
    FsError e;
    {
        const Held held(lock_);
        e = do_replace(from, to);
    }
    if (e == FsError::Ok) {
        tell_both(from, to);
    }
    return e;
}

void Storage::tell_both(const char *from, const char *to) const
{
    if (changed_ == nullptr) {
        return;
    }
    char a[kMaxPath], b[kMaxPath];
    path::parent(from, a, sizeof(a));
    path::parent(to, b, sizeof(b));
    changed_(a, changed_context_);
    // `to` may come without its drive: it is on `from`'s then, and the
    // notice needs the whole path.
    if (path::drive(to) == 0) {
        char whole[kMaxPath];
        const char root[4] = {path::drive(from), ':', '\\', 0};
        if (!path::join(root, path::relative(to), whole, sizeof(whole))) {
            return;
        }
        path::parent(whole, b, sizeof(b));
    }
    if (path::key(a) != path::key(b)) {
        changed_(b, changed_context_);
    }
}

FsError Storage::open(const char *p, OpenMode mode, File &file) const
{
    FsError e;
    {
        const Held held(lock_);
        e = do_open(p, mode, file);
    }
    if (e == FsError::Ok && mode != OpenMode::Read) {
        // Its folder is told when it is closed; a new or emptied file is
        // told now too, since its name is there from this moment.
        path::parent(p, file.folder, sizeof(file.folder));
        if (mode == OpenMode::Write) {
            tell(p);
        }
    }
    return e;
}

FsError Storage::read(File &file, void *data, size_t size, size_t &done) const
{
    const Held held(lock_);
    return do_read(file, data, size, done);
}

FsError Storage::write(File &file, const void *data, size_t size) const
{
    const Held held(lock_);
    return do_write(file, data, size);
}

FsError Storage::seek(File &file, uint32_t offset) const
{
    const Held held(lock_);
    return file.open() ? file.volume->seek(file.handle, offset) : FsError::NotFound;
}

FsError Storage::close(File &file) const
{
    char folder[kMaxPath];
    memcpy(folder, file.folder, sizeof(folder));
    FsError e;
    {
        const Held held(lock_);
        e = do_close(file);
    }
    file.folder[0] = 0;
    if (folder[0] != 0 && e != FsError::NotFound && changed_ != nullptr) {
        changed_(folder, changed_context_); // written, or it may have been
    }
    return e;
}

FsError Storage::begin_save(const char *p, Save &save) const
{
    const Held held(lock_);
    return do_begin_save(p, save);
}

FsError Storage::finish_save(Save &save, bool keep) const
{
    char p[kMaxPath];
    memcpy(p, save.path, sizeof(p));
    FsError e;
    {
        const Held held(lock_);
        e = do_finish_save(save, keep);
    }
    if (e == FsError::Ok && keep) {
        tell(p);
    }
    return e;
}

FsError Storage::read_file(const char *p, void *data, size_t capacity, size_t &size) const
{
    // One step for the other tasks: a file swapped in by a save while this
    // runs is read whole, the old one or the new one.
    const Held held(lock_);
    size = 0;
    File f;
    FsError e = do_open(p, OpenMode::Read, f);
    if (e != FsError::Ok) {
        return e;
    }
    e = do_read(f, data, capacity, size);
    do_close(f);
    return e;
}

FsError Storage::write_file(const char *p, const void *data, size_t size) const
{
    // One step too, so two tasks writing the same file take turns instead of
    // meeting in its temporary file. The others wait for the whole write:
    // this is for small files (settings); a long save goes piece by piece
    // through begin_save().
    FsError e;
    {
        const Held held(lock_);
        Save save;
        e = do_begin_save(p, save);
        if (e != FsError::Ok) {
            return e;
        }
        e = do_write(save.file, data, size);
        const FsError c = do_finish_save(save, e == FsError::Ok);
        e = e != FsError::Ok ? e : c;
    }
    if (e == FsError::Ok) {
        tell(p);
    }
    return e;
}

// From here on the lock is already held (or there is none).

ports::Volume *Storage::find(char letter) const
{
    letter = upper(letter);
    for (int i = 0; i < count_; i++) {
        if (letters_[i] == letter) {
            return volumes_[i];
        }
    }
    return nullptr;
}

ports::Volume *Storage::resolve(const char *p, FsError &error) const
{
    ports::Volume *v = find(path::drive(p));
    error = v != nullptr ? FsError::Ok : FsError::NotFound;
    return v;
}

FsError Storage::do_stat(const char *p, DirEntry &out) const
{
    FsError e;
    ports::Volume *v = resolve(p, e);
    return v != nullptr ? v->stat(path::relative(p), out) : e;
}

FsError Storage::do_remove(const char *p) const
{
    FsError e;
    ports::Volume *v = resolve(p, e);
    return v != nullptr ? v->remove(path::relative(p)) : e;
}

FsError Storage::do_replace(const char *from, const char *to) const
{
    FsError e;
    ports::Volume *v = resolve(from, e);
    if (v == nullptr) {
        return e;
    }
    if (path::drive(to) != 0 && path::drive(to) != path::drive(from)) {
        return FsError::BadName;
    }
    return v->replace(path::relative(from), path::relative(to));
}

FsError Storage::do_open(const char *p, OpenMode mode, File &file) const
{
    FsError e;
    ports::Volume *v = resolve(p, e);
    if (v == nullptr) {
        return e;
    }
    int handle = -1;
    e = v->open(path::relative(p), mode, handle);
    if (e == FsError::Ok) {
        file.volume = v;
        file.handle = handle;
    }
    return e;
}

FsError Storage::do_read(File &file, void *data, size_t size, size_t &done) const
{
    done = 0;
    return file.open() ? file.volume->read(file.handle, data, size, done) : FsError::NotFound;
}

FsError Storage::do_write(File &file, const void *data, size_t size) const
{
    return file.open() ? file.volume->write(file.handle, data, size) : FsError::NotFound;
}

FsError Storage::do_close(File &file) const
{
    if (!file.open()) {
        return FsError::NotFound;
    }
    FsError e = file.volume->close(file.handle);
    file.volume = nullptr;
    file.handle = -1;
    return e;
}

FsError Storage::do_begin_save(const char *p, Save &save) const
{
    save.file = File();
    DirEntry there;
    if (do_stat(p, there) == FsError::Ok && there.folder) {
        return FsError::IsFolder;
    }
    if (snprintf(save.path, sizeof(save.path), "%s", p) >= static_cast<int>(sizeof(save.path)) ||
        !path::temporary(p, save.temporary, sizeof(save.temporary))) {
        return FsError::BadName;
    }
    return do_open(save.temporary, OpenMode::Write, save.file);
}

FsError Storage::do_finish_save(Save &save, bool keep) const
{
    if (!save.file.open()) {
        return FsError::NotFound; // never begun
    }
    FsError e = do_close(save.file);
    save.file = File();
    if (e == FsError::Ok && keep) {
        e = do_replace(save.temporary, save.path);
    }
    if (e != FsError::Ok || !keep) {
        do_remove(save.temporary);
    }
    return e;
}

void add_samples(Storage &storage, char drive)
{
    char root[4] = {drive, ':', '\\', 0};
    char p[kMaxPath];
    auto file = [&](const char *rel, const char *text) {
        path::join(root, rel, p, sizeof(p));
        storage.write_file(p, text, strlen(text));
    };
    auto folder = [&](const char *rel) {
        path::join(root, rel, p, sizeof(p));
        storage.make_folder(p);
    };
    folder("Documents");
    folder("pictures");
    folder("system");
    ports::VolumeInfo info = {};
    storage.info(drive, info);
    if (info.keeps_files) {
        file("readme.txt",
             "Chinchilla flash disk\r\n"
             "\r\n"
             "This drive lives in the board's flash memory.\r\n"
             "Files on it stay when the board restarts.\r\n"
             "These samples were put here when the drive was first formatted.\r\n");
    } else {
        file("readme.txt",
             "Chinchilla RAM disk\r\n"
             "\r\n"
             "This drive lives in the board's external RAM.\r\n"
             "Everything on it is lost when the board restarts.\r\n"
             "Use the computer (tools/dbg.py get) to keep files.\r\n");
    }
    file("Documents\\notes.txt",
         "Shopping list for the VGA board\r\n"
         "\r\n"
         "  9 colour resistors: 510, 1k, 2k\r\n"
         "  2 sync resistors: 100\r\n"
         "  1 cut VGA cable (mind the shield!)\r\n"
         "\r\n"
         "640 x 480, 256 colours, 60 Hz.\r\n"
         "Screenshots travel over the serial port.\r\n");
    file("Documents\\todo.txt",
         "To do\r\n"
         "\r\n"
                  "- Wi-Fi and the clock from the network\r\n"
         "- USB keyboard and mouse\r\n"
         "- image viewer\r\n");
    // Text files are Latin-1, as 98 wrote them (names are kept in UTF-8).
    file("Documents\\español.txt", "Texto en espa\xf1ol: a\xf1o, ni\xf1o, caf\xe9, \xa1hola!\r\n");
    file("system\\version.txt", "Chinchilla 0.3\r\n");
    // Pictures made by tools/gen_pictures.py, and two NASA photographs
    // (public domain, see third_party/samples/README.md).
    for (int i = 0; i < kSamplePictureCount; i++) {
        char rel[96];
        snprintf(rel, sizeof(rel), "pictures\\%s", kSamplePictures[i].name);
        path::join(root, rel, p, sizeof(p));
        storage.write_file(p, kSamplePictures[i].data, kSamplePictures[i].size);
    }
}

} // namespace fs
