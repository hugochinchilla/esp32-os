// The entries of a folder, sorted folders first then by name, kept in large
// memory (external RAM) that goes back with the Listing. Used by the file
// dialogs and the File manager.
#pragma once

#include "core/fs/fs.hpp"
#include "core/gfx/canvas.hpp"

namespace shell {

class Listing {
public:
    static constexpr int kMax = 160; // ponytail: more entries are not shown

    struct Item {
        char name[64];
        bool folder;
        uint32_t size;
    };

    // Names starting with a dot are hidden unless this is set (View > Show
    // hidden files in the File manager; it applies to the file dialogs too).
    static bool show_hidden;

    Listing() = default;
    ~Listing();
    Listing(const Listing &) = delete;
    Listing &operator=(const Listing &) = delete;

    // Takes its memory on first use; false if there is none.
    bool ready();
    ports::FsError read(const fs::Storage &storage, const char *folder);
    // The drives, as "RAM disk (C:)", for My Board.
    void read_drives(const fs::Storage &storage);

    int count() const { return count_; }
    const Item &item(int i) const { return items_[i]; }
    const char *const *names() const { return names_; }
    bool truncated() const { return truncated_; }
    // For My Board: the drive letter of an entry, 0 otherwise.
    char drive(int i) const { return drives_[i]; }
    // An icon for an entry, 16x16 or 32x32.
    const gfx::Bitmap *icon(int i, bool large) const;

private:
    Item *items_ = nullptr;
    const char *names_[kMax] = {};
    char drives_[kMax] = {};
    int count_ = 0;
    bool truncated_ = false;
};

} // namespace shell
