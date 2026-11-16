#include "core/fs/fs.hpp"

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

} // namespace path

void Storage::mount(char letter, ports::Volume &volume)
{
    if (letter >= 'a' && letter <= 'z') {
        letter = static_cast<char>(letter - 'a' + 'A');
    }
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
    if (letter >= 'a' && letter <= 'z') {
        letter = static_cast<char>(letter - 'a' + 'A');
    }
    for (int i = 0; i < count_; i++) {
        if (letters_[i] == letter) {
            return volumes_[i];
        }
    }
    return nullptr;
}

int Storage::drives(char *letters, int capacity) const
{
    int n = 0;
    for (int i = 0; i < count_ && n < capacity; i++) {
        letters[n++] = letters_[i];
    }
    return n;
}

ports::Volume *Storage::resolve(const char *p, FsError &error) const
{
    ports::Volume *v = volume(path::drive(p));
    error = v != nullptr ? FsError::Ok : FsError::NotFound;
    return v;
}

FsError Storage::info(char letter, VolumeInfo &out) const
{
    ports::Volume *v = volume(letter);
    return v != nullptr ? v->info(out) : FsError::NotFound;
}

FsError Storage::entry(const char *folder, int index, DirEntry &out) const
{
    FsError e;
    ports::Volume *v = resolve(folder, e);
    return v != nullptr ? v->entry(path::relative(folder), index, out) : e;
}

FsError Storage::stat(const char *p, DirEntry &out) const
{
    FsError e;
    ports::Volume *v = resolve(p, e);
    return v != nullptr ? v->stat(path::relative(p), out) : e;
}

FsError Storage::make_folder(const char *p) const
{
    FsError e;
    ports::Volume *v = resolve(p, e);
    return v != nullptr ? v->make_folder(path::relative(p)) : e;
}

FsError Storage::remove(const char *p) const
{
    FsError e;
    ports::Volume *v = resolve(p, e);
    return v != nullptr ? v->remove(path::relative(p)) : e;
}

FsError Storage::rename(const char *from, const char *to) const
{
    FsError e;
    ports::Volume *v = resolve(from, e);
    if (v == nullptr) {
        return e;
    }
    if (path::drive(to) != 0 && path::drive(to) != path::drive(from)) {
        return FsError::BadName; // moving between drives is copying, later
    }
    return v->rename(path::relative(from), path::relative(to));
}

FsError Storage::open(const char *p, OpenMode mode, File &file) const
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

FsError Storage::read(File &file, void *data, size_t size, size_t &done) const
{
    done = 0;
    return file.open() ? file.volume->read(file.handle, data, size, done) : FsError::NotFound;
}

FsError Storage::write(File &file, const void *data, size_t size) const
{
    return file.open() ? file.volume->write(file.handle, data, size) : FsError::NotFound;
}

FsError Storage::seek(File &file, uint32_t offset) const
{
    return file.open() ? file.volume->seek(file.handle, offset) : FsError::NotFound;
}

FsError Storage::close(File &file) const
{
    if (!file.open()) {
        return FsError::NotFound;
    }
    FsError e = file.volume->close(file.handle);
    file.volume = nullptr;
    file.handle = -1;
    return e;
}

FsError Storage::read_file(const char *p, void *data, size_t capacity, size_t &size) const
{
    size = 0;
    File f;
    FsError e = open(p, OpenMode::Read, f);
    if (e != FsError::Ok) {
        return e;
    }
    e = read(f, data, capacity, size);
    close(f);
    return e;
}

FsError Storage::write_file(const char *p, const void *data, size_t size) const
{
    File f;
    FsError e = open(p, OpenMode::Write, f);
    if (e != FsError::Ok) {
        return e;
    }
    e = write(f, data, size);
    FsError c = close(f);
    return e != FsError::Ok ? e : c;
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
    folder("docs");
    folder("pictures");
    folder("system");
    file("readme.txt",
         "Chinchilla RAM disk\r\n"
         "\r\n"
         "This drive lives in the board's external RAM.\r\n"
         "Everything on it is lost when the board restarts.\r\n"
         "Use the computer (tools/dbg.py get) to keep files.\r\n");
    file("docs\\notes.txt",
         "Shopping list for the VGA board\r\n"
         "\r\n"
         "  9 colour resistors: 510, 1k, 2k\r\n"
         "  2 sync resistors: 100\r\n"
         "  1 cut VGA cable (mind the shield!)\r\n"
         "\r\n"
         "640 x 480, 256 colours, 60 Hz.\r\n"
         "Screenshots travel over the serial port.\r\n");
    file("docs\\todo.txt",
         "To do\r\n"
         "\r\n"
         "- file system on the flash\r\n"
         "- Wi-Fi and the clock from the network\r\n"
         "- USB keyboard and mouse\r\n"
         "- image viewer\r\n");
    // Text files are Latin-1, as 98 wrote them (names are kept in UTF-8).
    file("docs\\español.txt", "Texto en espa\xf1ol: a\xf1o, ni\xf1o, caf\xe9, \xa1hola!\r\n");
    file("system\\version.txt", "Chinchilla 0.3\r\n");
}

} // namespace fs
