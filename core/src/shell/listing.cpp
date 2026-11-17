#include "listing.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "internal.hpp"

namespace shell {

namespace {

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool before(const Listing::Item &a, const Listing::Item &b)
{
    if (a.folder != b.folder) {
        return a.folder;
    }
    for (int i = 0;; i++) {
        const char x = lower(a.name[i]), y = lower(b.name[i]);
        if (x != y || x == 0) {
            return x < y;
        }
    }
}

} // namespace

bool Listing::show_hidden = false;

Listing::~Listing() { free_program_memory(items_); }

bool Listing::ready()
{
    if (items_ == nullptr) {
        items_ = static_cast<Item *>(program_memory(sizeof(Item) * kMax));
    }
    return items_ != nullptr;
}

ports::FsError Listing::read(const char *folder)
{
    count_ = 0;
    truncated_ = false;
    if (!ready()) {
        return ports::FsError::NoSpace;
    }
    // A few entries a call, each call one pass over the folder: asked for
    // one by one a folder of 80 files took the board longer than a program
    // is waited for.
    constexpr int kRun = 8;
    ports::FsError e = ports::FsError::Ok;
    for (int first = 0; !truncated_;) {
        ports::DirEntry run[kRun];
        int got = 0;
        e = os::file::entries(folder, first, run, kRun, got);
        if (e != ports::FsError::Ok) {
            break;
        }
        for (int i = 0; i < got; i++) {
            const ports::DirEntry &d = run[i];
            if (d.name[0] == '.' && !show_hidden) {
                continue;
            }
            if (count_ == kMax) {
                truncated_ = true;
                break;
            }
            Item &it = items_[count_];
            static_assert(sizeof(it.name) == sizeof(d.name), "a name is copied whole");
            memcpy(it.name, d.name, sizeof(it.name));
            it.name[sizeof(it.name) - 1] = 0;
            it.folder = d.folder;
            it.size = d.size;
            it.modified = d.modified;
            drives_[count_] = 0;
            count_++;
        }
        if (got < kRun) {
            break;
        }
        first += got;
    }
    std::sort(items_, items_ + count_, before);
    for (int i = 0; i < count_; i++) {
        names_[i] = items_[i].name;
    }
    return e;
}

void Listing::read_drives()
{
    count_ = 0;
    truncated_ = false;
    if (!ready()) {
        return;
    }
    char letters[fs::Storage::kMaxDrives];
    const int n = os::file::drives(letters, fs::Storage::kMaxDrives);
    for (int i = 0; i < n; i++) {
        ports::VolumeInfo info = {};
        os::file::drive_info(letters[i], info);
        Item &it = items_[count_];
        snprintf(it.name, sizeof(it.name), "%s (%c:)", info.label, letters[i]);
        it.folder = true;
        it.size = 0;
        it.modified = 0;
        drives_[count_] = letters[i];
        names_[count_] = it.name;
        count_++;
    }
}

const gfx::Bitmap *Listing::icon(int i, bool large) const
{
    if (drives_[i] != 0) {
        return large ? &gfx::drive_icon_32() : &gfx::drive_icon_16();
    }
    if (items_[i].folder) {
        return large ? &gfx::folder_icon_32() : &gfx::folder_icon_16();
    }
    char ext[8];
    fs::path::extension(items_[i].name, ext, sizeof(ext));
    if (strcmp(ext, "txt") == 0 || strcmp(ext, "log") == 0 || strcmp(ext, "ini") == 0 ||
        strcmp(ext, "md") == 0) {
        return large ? &gfx::text_icon_32() : &gfx::text_icon_16();
    }
    if (strcmp(ext, "png") == 0 || strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0 ||
        strcmp(ext, "bmp") == 0 || strcmp(ext, "gif") == 0) {
        return large ? &gfx::picture_icon_32() : &gfx::picture_icon_16();
    }
    if (strcmp(ext, "app") == 0) {
        return large ? &gfx::program_icon_32() : &gfx::program_icon_16();
    }
    return large ? &gfx::file_icon_32() : &gfx::text_icon_16();
}

} // namespace shell
