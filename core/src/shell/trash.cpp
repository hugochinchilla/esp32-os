// The Trash: deleted files and folders on C: are moved to C:\.trash, and
// C:\.config\trash.ini remembers where each came from, so they can be put
// back. Deleting from the Trash, emptying it, or deleting on a drive with
// no Trash removes for good, after asking.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/fs/ini.hpp"
#include "core/os/os.hpp"
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
    return os::file::stat(path, e) == ports::FsError::Ok;
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

char g_message[fs::kMaxPath + 120];

void report(ports::FsError e, const char *what, const char *path)
{
    if (e == ports::FsError::Ok) {
        return;
    }
    snprintf(g_message, sizeof(g_message), "Cannot %s '%s':\n%s.", what, fs::path::name(path), fs::error_text(e));
    message_box("Delete", g_message);
}

// Into the Trash, with the index the caller has loaded and will save if
// `indexed` comes back true: one write of the index however many go.
ports::FsError trash_one(const char *path, fs::Ini &index, bool &indexed)
{
    if (!trashable(path)) {
        return ports::FsError::BadName;
    }
    ports::DirEntry e;
    ports::FsError err = os::file::stat(path, e);
    if (err != ports::FsError::Ok) {
        return err;
    }
    os::file::make_folder(kTrashFolder);
    char name[fs::kMaxPath], to[fs::kMaxPath];
    if (!free_name(kTrashFolder, fs::path::name(path), e.folder, &index, name, sizeof(name)) ||
        !fs::path::join(kTrashFolder, name, to, sizeof(to))) {
        return ports::FsError::Exists;
    }
    err = os::file::rename(path, to);
    if (err == ports::FsError::Ok && indexable(name) && index.set("from", name, path)) {
        indexed = true;
    }
    return err;
}

// What the Delete question is about, kept until it is answered: a block of
// the asker's, given back with the answer. A question that is dropped
// without one (another took its place) leaves its block here, and the next
// Delete gives it back.
struct Pending {
    int count;
    bool permanent;
    char folder[fs::kMaxPath];
    char names[1]; // `count` of them, each with its closing zero
};
Pending *g_pending = nullptr;

void forget_pending()
{
    os::release(g_pending);
    g_pending = nullptr;
}

void delete_answer(int choice, void *)
{
    Pending *p = g_pending;
    if (p == nullptr) {
        return;
    }
    if (choice != 0) {
        forget_pending();
        return;
    }
    ports::FsError e = ports::FsError::Ok;
    char path[fs::kMaxPath] = "";
    {
        const os::Busy hourglass;
        fs::Ini index;
        bool indexed = false;
        if (!p->permanent) {
            load_settings(kIndex, index);
        }
        const char *name = p->names;
        for (int i = 0; i < p->count && e == ports::FsError::Ok; i++, name += strlen(name) + 1) {
            if (!fs::path::join(p->folder, name, path, sizeof(path))) {
                e = ports::FsError::BadName;
            } else {
                e = p->permanent ? remove_tree(path) : trash_one(path, index, indexed);
            }
        }
        if (indexed) {
            save_settings(kIndex, index);
        }
    }
    const bool permanent = p->permanent;
    forget_pending();
    files_changed();
    report(e, permanent ? "delete" : "move to the Trash", path); // the one it stopped at
}

} // namespace

bool free_name_in(const char *folder, const char *name, bool is_folder, char *out, int capacity)
{
    return free_name(folder, name, is_folder, nullptr, out, capacity);
}

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
    ports::FsError err = os::file::stat(p, e);
    if (err != ports::FsError::Ok || !e.folder) {
        return err == ports::FsError::Ok ? os::file::remove(p) : err;
    }
    if (depth > 12) {
        return ports::FsError::NotEmpty;
    }
    // Entry 0 again and again: each removal moves the rest up.
    char child[fs::kMaxPath];
    while (os::file::entry(p, 0, e) == ports::FsError::Ok) {
        if (!fs::path::join(p, e.name, child, sizeof(child))) {
            return ports::FsError::BadName;
        }
        err = remove_tree(child, depth + 1);
        if (err != ports::FsError::Ok) {
            return err;
        }
    }
    return os::file::remove(p);
}

ports::FsError to_trash(const char *path)
{
    fs::Ini index;
    load_settings(kIndex, index);
    bool indexed = false;
    const ports::FsError err = trash_one(path, index, indexed);
    if (indexed) {
        save_settings(kIndex, index);
    }
    return err;
}

int trash_count()
{
    int n = 0;
    ports::DirEntry e;
    while (os::file::entry(kTrashFolder, n, e) == ports::FsError::Ok) {
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
    ports::FsError err = os::file::stat(from, e);
    if (err != ports::FsError::Ok) {
        return err;
    }
    fs::path::parent(origin, folder, sizeof(folder));
    if (fs::path::relative(folder)[0] != 0 && !exists(folder)) {
        os::file::make_folder(folder); // gone since: made again (one level)
    }
    // Back under its old name, or numbered if that is taken now.
    if (!free_name(folder, fs::path::name(origin), e.folder, nullptr, free, sizeof(free)) ||
        !fs::path::join(folder, free, to, sizeof(to))) {
        return ports::FsError::Exists;
    }
    err = os::file::rename(from, to);
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
    const ports::FsError err = os::file::rename(path, to);
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
    if (fs::path::relative(path)[0] == 0) {
        return;
    }
    char folder[fs::kMaxPath];
    fs::path::parent(path, folder, sizeof(folder));
    const char *name = fs::path::name(path);
    delete_asking(Selection{folder, &name, 1});
}

// One question for all of them. On C: they go to the Trash; in the Trash,
// and on a drive that has none, they are deleted for good, and the question
// says so.
void delete_asking(const Selection &s)
{
    forget_pending();
    if (s.count < 1 || s.folder == nullptr || s.folder[0] == 0) {
        return;
    }
    size_t bytes = 0;
    for (int i = 0; i < s.count; i++) {
        bytes += strlen(s.names[i]) + 1;
    }
    char first[fs::kMaxPath];
    ports::DirEntry e;
    if (!fs::path::join(s.folder, s.names[0], first, sizeof(first)) || os::file::stat(first, e) != ports::FsError::Ok) {
        return;
    }
    Pending *p = static_cast<Pending *>(os::allocate(sizeof(Pending) + bytes));
    if (p == nullptr) {
        message_box("Delete", "There is not enough memory to delete these.");
        return;
    }
    p->count = s.count;
    p->permanent = !trashable(first);
    snprintf(p->folder, sizeof(p->folder), "%s", s.folder);
    char *at = p->names;
    for (int i = 0; i < s.count; i++) {
        const size_t n = strlen(s.names[i]) + 1;
        memcpy(at, s.names[i], n);
        at += n;
    }
    g_pending = p;

    static char text[fs::kMaxPath + 120];
    const char *title = e.folder ? "Confirm Folder Delete" : "Confirm File Delete";
    const char *all = e.folder ? " and everything in it" : "";
    if (s.count > 1) {
        title = "Confirm Delete";
        if (!p->permanent) {
            snprintf(text, sizeof(text), "Are you sure you want to move these %d items\nto the Trash?", s.count);
        } else if (in_trash(first)) {
            snprintf(text, sizeof(text), "Are you sure you want to delete these %d items?\nThey cannot be put back.",
                     s.count);
        } else {
            snprintf(text, sizeof(text),
                     "Drive %c: has no Trash. Are you sure you want to\ndelete these %d items for good?",
                     fs::path::drive(first), s.count);
        }
    } else if (!p->permanent) {
        snprintf(text, sizeof(text), "Are you sure you want to move '%s'%s\nto the Trash?", s.names[0], all);
    } else if (in_trash(first)) {
        snprintf(text, sizeof(text), "Are you sure you want to delete '%s'%s?\nIt cannot be put back.", s.names[0],
                 all);
    } else {
        snprintf(text, sizeof(text), "Drive %c: has no Trash. Are you sure you want to\ndelete '%s'%s for good?",
                 fs::path::drive(first), s.names[0], all);
    }
    static const char *const yes_no[] = {"&Yes", "&No"};
    ask(title, text, yes_no, 2, &delete_answer, nullptr);
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
