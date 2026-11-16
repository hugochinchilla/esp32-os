#include "listing.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
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

bool Listing::ready()
{
    if (items_ == nullptr) {
        items_ = static_cast<Item *>(memory().allocate(sizeof(Item) * kMax));
    }
    return items_ != nullptr;
}

ports::FsError Listing::read(const fs::Storage &storage, const char *folder)
{
    count_ = 0;
    truncated_ = false;
    if (!ready()) {
        return ports::FsError::NoSpace;
    }
    ports::FsError e = ports::FsError::Ok;
    for (int i = 0;; i++) {
        ports::DirEntry d;
        e = storage.entry(folder, i, d);
        if (e != ports::FsError::Ok) {
            break;
        }
        if (count_ == kMax) {
            truncated_ = true;
            break;
        }
        Item &it = items_[count_];
        snprintf(it.name, sizeof(it.name), "%s", d.name);
        it.folder = d.folder;
        it.size = d.size;
        drives_[count_] = 0;
        count_++;
    }
    std::sort(items_, items_ + count_, before);
    for (int i = 0; i < count_; i++) {
        names_[i] = items_[i].name;
    }
    // Running off the end of the folder is how a listing finishes.
    return e == ports::FsError::NotFound ? ports::FsError::Ok : e;
}

void Listing::read_drives(const fs::Storage &storage)
{
    count_ = 0;
    truncated_ = false;
    if (!ready()) {
        return;
    }
    char letters[fs::Storage::kMaxDrives];
    const int n = storage.drives(letters, fs::Storage::kMaxDrives);
    for (int i = 0; i < n; i++) {
        ports::VolumeInfo info = {};
        storage.info(letters[i], info);
        Item &it = items_[count_];
        snprintf(it.name, sizeof(it.name), "%s (%c:)", info.label, letters[i]);
        it.folder = true;
        it.size = 0;
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
    if (strcmp(ext, "png") == 0 || strcmp(ext, "jpg") == 0 || strcmp(ext, "bmp") == 0 ||
        strcmp(ext, "gif") == 0) {
        return large ? &gfx::picture_icon_32() : &gfx::text_icon_16();
    }
    return large ? &gfx::file_icon_32() : &gfx::text_icon_16();
}

} // namespace shell
