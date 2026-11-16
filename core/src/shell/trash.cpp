// The Trash: deleted files and folders on C: are moved to C:\.trash, and
// C:\.config\trash.ini remembers where each came from, so they can be put
// back. Deleting from the Trash, emptying it, or deleting on a drive with
// no Trash removes for good, after asking.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/fs/ini.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr const char *kIndex = "trash"; // C:\.config\trash.ini, section [from]

// A name the index can hold as a key (no '=', no leading comment marks or
// spaces); others are trashed all the same but cannot be put back.
bool indexable(const char *name)
{
    const size_t n = strlen(name);
    return n > 0 && strchr(name, '=') == nullptr && strchr(";#[", name[0]) == nullptr && name[0] != ' ' &&
           name[n - 1] != ' ';
}

bool exists(const char *path)
{
    ports::DirEntry e;
    return storage().stat(path, e) == ports::FsError::Ok;
}

// "name (2).ext", "name (3).ext"... for a folder or a file without an
// extension, "name (2)".
void numbered(const char *name, bool folder, int n, char *out, int capacity)
{
    const char *dot = folder ? nullptr : strrchr(name, '.');
    if (dot == name) {
        dot = nullptr; // ".profile" has no extension
    }
    const int stem = dot != nullptr ? static_cast<int>(dot - name) : static_cast<int>(strlen(name));
    snprintf(out, capacity, "%.*s (%d)%s", stem, name, n, dot != nullptr ? dot : "");
}

// A free name for `name` in `folder` (and, in the Trash, not in the index).
bool free_name(const char *folder, const char *name, bool is_folder, const fs::Ini *index, char *out, int capacity)
{
    char candidate[fs::kMaxPath], path[fs::kMaxPath], seen[fs::kMaxPath];
    snprintf(candidate, sizeof(candidate), "%s", name);
    for (int n = 2; n < 1000; n++) {
        const bool listed = index != nullptr && index->get("from", candidate, seen, sizeof(seen)) && seen[0] != 0;
        if (fs::path::join(folder, candidate, path, sizeof(path)) && !exists(path) && !listed) {
            snprintf(out, capacity, "%s", candidate);
            return true;
        }
        numbered(name, is_folder, n, candidate, sizeof(candidate));
    }
    return false;
}

char g_pending[fs::kMaxPath];
char g_message[fs::kMaxPath + 120];

void report(ports::FsError e, const char *what, const char *path)
{
    if (e == ports::FsError::Ok) {
        return;
    }
    snprintf(g_message, sizeof(g_message), "Cannot %s '%s':\n%s.", what, fs::path::name(path), fs::error_text(e));
    message_box("Delete", g_message);
}

void delete_answer(int choice, void *permanent)
{
    if (choice != 0) {
        return;
    }
    const ports::FsError e = permanent != nullptr ? remove_tree(g_pending) : to_trash(g_pending);
    files_changed();
    report(e, permanent != nullptr ? "delete" : "move to the Trash", g_pending);
}

// Asks before removing g_pending for good: in the Trash, or on a drive
// without one.
void ask_permanent(bool folder)
{
    const char *name = fs::path::name(g_pending);
    static char text[fs::kMaxPath + 120];
    if (in_trash(g_pending)) {
        snprintf(text, sizeof(text), "Are you sure you want to delete '%s'%s?\nIt cannot be put back.", name,
                 folder ? " and everything in it" : "");
    } else {
        snprintf(text, sizeof(text), "Drive %c: has no Trash. Are you sure you want to\ndelete '%s'%s for good?",
                 fs::path::drive(g_pending), name, folder ? " and everything in it" : "");
    }
    static const char *const yes_no[] = {"&Yes", "&No"};
    ask(folder ? "Confirm Folder Delete" : "Confirm File Delete", text, yes_no, 2, &delete_answer, g_pending);
}

} // namespace

bool in_trash(const char *path)
{
    const size_t n = strlen(kTrashFolder);
    return strncasecmp(path, kTrashFolder, n) == 0 && (path[n] == 0 || path[n] == '\\');
}

bool trashable(const char *path)
{
    return (fs::path::drive(path) == 'C' || fs::path::drive(path) == 'c') && fs::path::relative(path)[0] != 0 &&
           !in_trash(path);
}

ports::FsError remove_tree(const char *p, int depth)
{
    ports::DirEntry e;
    ports::FsError err = storage().stat(p, e);
    if (err != ports::FsError::Ok || !e.folder) {
        return err == ports::FsError::Ok ? storage().remove(p) : err;
    }
    if (depth > 12) {
        return ports::FsError::NotEmpty;
    }
    // Entry 0 again and again: each removal moves the rest up.
    char child[fs::kMaxPath];
    while (storage().entry(p, 0, e) == ports::FsError::Ok) {
        if (!fs::path::join(p, e.name, child, sizeof(child))) {
            return ports::FsError::BadName;
        }
        err = remove_tree(child, depth + 1);
        if (err != ports::FsError::Ok) {
            return err;
        }
    }
    return storage().remove(p);
}

ports::FsError to_trash(const char *path)
{
    if (!trashable(path)) {
        return ports::FsError::BadName;
    }
    ports::DirEntry e;
    ports::FsError err = storage().stat(path, e);
    if (err != ports::FsError::Ok) {
        return err;
    }
    storage().make_folder(kTrashFolder);
    fs::Ini index;
    load_settings(kIndex, index);
    char name[fs::kMaxPath], to[fs::kMaxPath];
    if (!free_name(kTrashFolder, fs::path::name(path), e.folder, &index, name, sizeof(name)) ||
        !fs::path::join(kTrashFolder, name, to, sizeof(to))) {
        return ports::FsError::Exists;
    }
    err = storage().rename(path, to);
    if (err == ports::FsError::Ok && indexable(name) && index.set("from", name, path)) {
        save_settings(kIndex, index);
    }
    return err;
}

int trash_count()
{
    int n = 0;
    ports::DirEntry e;
    while (storage().entry(kTrashFolder, n, e) == ports::FsError::Ok) {
        n++;
    }
    return n;
}

ports::FsError empty_trash()
{
    ports::FsError e = remove_tree(kTrashFolder);
    if (e == ports::FsError::NotFound) {
        e = ports::FsError::Ok;
    }
    fs::Ini index;
    save_settings(kIndex, index);
    return e;
}

bool trash_origin(const char *name, char *out, int capacity)
{
    fs::Ini index;
    load_settings(kIndex, index);
    return indexable(name) && index.get("from", name, out, capacity) && out[0] != 0;
}

ports::FsError restore_from_trash(const char *name)
{
    char from[fs::kMaxPath], origin[fs::kMaxPath], folder[fs::kMaxPath], to[fs::kMaxPath], free[fs::kMaxPath];
    if (!trash_origin(name, origin, sizeof(origin)) || !fs::path::join(kTrashFolder, name, from, sizeof(from))) {
        return ports::FsError::NotFound;
    }
    ports::DirEntry e;
    ports::FsError err = storage().stat(from, e);
    if (err != ports::FsError::Ok) {
        return err;
    }
    fs::path::parent(origin, folder, sizeof(folder));
    if (fs::path::relative(folder)[0] != 0 && !exists(folder)) {
        storage().make_folder(folder); // gone since: made again (one level)
    }
    // Back under its old name, or numbered if that is taken now.
    if (!free_name(folder, fs::path::name(origin), e.folder, nullptr, free, sizeof(free)) ||
        !fs::path::join(folder, free, to, sizeof(to))) {
        return ports::FsError::Exists;
    }
    err = storage().rename(from, to);
    if (err == ports::FsError::Ok) {
        fs::Ini index;
        load_settings(kIndex, index);
        index.set("from", name, "");
        save_settings(kIndex, index);
    }
    return err;
}

ports::FsError move_to_folder(const char *path, const char *folder)
{
    if (in_trash(folder)) {
        return to_trash(path);
    }
    const size_t n = strlen(path);
    if (fs::path::drive(path) != fs::path::drive(folder) || fs::path::relative(path)[0] == 0 ||
        (strncasecmp(folder, path, n) == 0 && (folder[n] == 0 || folder[n] == '\\'))) {
        return ports::FsError::BadName; // other drive, a drive itself, or into itself
    }
    char to[fs::kMaxPath];
    if (!fs::path::join(folder, fs::path::name(path), to, sizeof(to))) {
        return ports::FsError::BadName;
    }
    if (exists(to)) {
        return ports::FsError::Exists;
    }
    const ports::FsError err = storage().rename(path, to);
    if (err == ports::FsError::Ok && in_trash(path) && indexable(fs::path::name(path))) {
        fs::Ini index; // taken out of the Trash by hand: forget where it came from
        load_settings(kIndex, index);
        index.set("from", fs::path::name(path), "");
        save_settings(kIndex, index);
    }
    return err;
}

void delete_asking(const char *path)
{
    ports::DirEntry e;
    if (fs::path::relative(path)[0] == 0 || storage().stat(path, e) != ports::FsError::Ok) {
        return;
    }
    snprintf(g_pending, sizeof(g_pending), "%s", path);
    if (!trashable(path)) {
        ask_permanent(e.folder);
        return;
    }
    static char text[fs::kMaxPath + 80];
    snprintf(text, sizeof(text), "Are you sure you want to move '%s'%s\nto the Trash?", fs::path::name(path),
             e.folder ? " and everything in it" : "");
    static const char *const yes_no[] = {"&Yes", "&No"};
    ask(e.folder ? "Confirm Folder Delete" : "Confirm File Delete", text, yes_no, 2, &delete_answer, nullptr);
}

void drop_in_trash(const char *path)
{
    if (in_trash(path)) {
        return;
    }
    if (!trashable(path)) {
        delete_asking(path); // no Trash there: asks before deleting for good
        return;
    }
    const ports::FsError e = to_trash(path);
    files_changed();
    report(e, "move to the Trash", path);
}

} // namespace shell
