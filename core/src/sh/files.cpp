// The shell's commands about files and folders.
#include <cstdio>
#include <cstring>

#include "commands.hpp"

namespace sh {

namespace {

using fs::DirEntry;
using fs::FsError;

constexpr const char *kTooLong = "the path is too long";

// What is at a path. A drive's root is a folder with no name.
FsError kind(Session &s, const char *full, DirEntry &out)
{
    fs::Storage &storage = *s.env().storage;
    if (strlen(full) <= 3) {
        out = {};
        out.folder = true;
        return storage.volume(fs::path::drive(full)) != nullptr ? FsError::Ok : FsError::NotFound;
    }
    return storage.stat(full, out);
}

// How many columns a name takes on the screen: its characters, not its
// bytes (names are UTF-8).
int columns(const char *text)
{
    int count = 0;
    for (; *text != 0; text++) {
        count += (static_cast<uint8_t>(*text) & 0xC0) != 0x80 ? 1 : 0;
    }
    return count;
}

bool no_drives(Session &s, const char *command)
{
    if (s.env().storage != nullptr) {
        return false;
    }
    s.fail(command, nullptr, "there are no drives");
    return true;
}

// `inside` is `folder` or somewhere under it.
bool within(const char *folder, const char *inside)
{
    const size_t length = strlen(folder);
    if (strlen(inside) < length) {
        return false;
    }
    char head[fs::kMaxPath];
    snprintf(head, sizeof head, "%.*s", static_cast<int>(length), inside);
    return path::same(head, folder) && (inside[length] == 0 || inside[length] == '\\' || folder[length - 1] == '\\');
}

// Calls `visit` for what was typed: once for a plain path, once for every
// name in the folder that matches when the last part has * or ?. `visit`
// returns 0 when it did its work. `removes`: a visit that worked took the
// entry out of the folder, so the next one has moved into its place.
using Visit = int (*)(Session &s, const char *full, void *context);

int each(Session &s, const char *command, const char *typed, Visit visit, void *context, bool removes = false)
{
    char full[fs::kMaxPath];
    if (!s.resolve(typed, full)) {
        return s.fail(command, typed, kTooLong);
    }
    if (!path::has_wildcard(full)) {
        return visit(s, full, context);
    }
    char folder[fs::kMaxPath], pattern[64];
    fs::path::parent(full, folder, sizeof folder);
    snprintf(pattern, sizeof pattern, "%s", fs::path::name(full));
    if (path::has_wildcard(folder)) {
        return s.fail(command, typed, "* and ? only work in the last name");
    }
    int result = 0, matches = 0;
    DirEntry entry;
    for (int i = 0; !s.stopped() && s.env().storage->entry(folder, i, entry) == FsError::Ok;) {
        if (!path::match(pattern, entry.name)) {
            i++;
            continue;
        }
        matches++;
        char found[fs::kMaxPath];
        int failed = 1;
        if (fs::path::join(folder, entry.name, found, sizeof found)) {
            failed = visit(s, found, context);
        } else {
            s.fail(command, entry.name, kTooLong);
        }
        result |= failed;
        if (failed != 0 || !removes) {
            i++;
        }
    }
    if (matches == 0) {
        return s.fail(command, typed, "nothing has that name");
    }
    return result;
}

/* ---- ls, dir ---------------------------------------------------------------------- */

struct Listing {
    bool details, hidden;
    bool several; // more than one thing was asked for: say which is which
    const char *command;
};

bool shown(const DirEntry &entry, const char *pattern, bool hidden)
{
    if (pattern != nullptr) {
        return path::match(pattern, entry.name);
    }
    return hidden || entry.name[0] != '.';
}

// "2026-11-17 14:05         1,234  notes.txt": when it was last written (in
// local time; blank if not known), its size or <DIR>, its name.
void detail_line(Session &s, const DirEntry &entry)
{
    char size[32], when[24];
    date_text(entry.modified, s.env().zone, when, sizeof when);
    if (entry.folder) {
        s.printf("%-16s  <DIR>           %s\n", when, entry.name);
    } else {
        s.printf("%-16s  %14s  %s\n", when, grouped(entry.size, size, sizeof size), entry.name);
    }
}

// Lists a folder, or the names in it that match `pattern`.
int list_folder(Session &s, const Listing &how, const char *folder, const char *pattern)
{
    fs::Storage &storage = *s.env().storage;
    DirEntry entry;
    int files = 0, folders = 0, widest = 0;
    uint64_t bytes = 0;
    for (int i = 0; storage.entry(folder, i, entry) == FsError::Ok; i++) {
        if (!shown(entry, pattern, how.hidden)) {
            continue;
        }
        (entry.folder ? folders : files)++;
        bytes += entry.folder ? 0 : entry.size;
        const int width = columns(entry.name) + (entry.folder ? 1 : 0);
        widest = width > widest ? width : widest;
    }
    if (pattern != nullptr && files + folders == 0) {
        return s.fail(how.command, pattern, "nothing has that name");
    }

    if (how.details) {
        s.printf(" Folder %s\n\n", folder);
        for (int i = 0; !s.stopped() && storage.entry(folder, i, entry) == FsError::Ok; i++) {
            if (shown(entry, pattern, how.hidden)) {
                detail_line(s, entry);
            }
        }
        char number[32];
        s.printf(" %6d file%s   %14s bytes\n", files, files == 1 ? " " : "s", grouped(bytes, number, sizeof number));
        fs::VolumeInfo info;
        if (storage.info(fs::path::drive(folder), info) == FsError::Ok) {
            s.printf(" %6d folder%s %14s bytes free\n", folders, folders == 1 ? " " : "s",
                     grouped(info.free_bytes, number, sizeof number));
        }
        return 0;
    }

    // Names only, in as many columns as the terminal has room for, read
    // across. A folder has a \ after its name.
    if (how.several && pattern == nullptr) {
        s.printf("%s:\n", folder);
    }
    const int column = widest + 2;
    int per_row = s.terminal().columns() / (column > 0 ? column : 1);
    // Into a file or a pipe: a name on each line, for whatever reads them.
    per_row = (per_row < 1 || s.output_redirected()) ? 1 : per_row;
    int in_row = 0, owed = 0; // spaces to the next column, written only if a name follows
    for (int i = 0; !s.stopped() && storage.entry(folder, i, entry) == FsError::Ok; i++) {
        if (!shown(entry, pattern, how.hidden)) {
            continue;
        }
        char name[72];
        snprintf(name, sizeof name, "%s%s", entry.name, entry.folder ? "\\" : "");
        if (in_row == per_row) {
            s.print("\n");
            in_row = 0;
        } else if (in_row > 0) {
            s.printf("%*s", owed, "");
        }
        s.print(name);
        owed = column - columns(name);
        in_row++;
    }
    if (in_row != 0) {
        s.print("\n");
    }
    return 0;
}

int list(Session &s, const Listing &how, const char *typed)
{
    char full[fs::kMaxPath];
    if (!s.resolve(typed, full)) {
        return s.fail(how.command, typed, kTooLong);
    }
    if (path::has_wildcard(full)) {
        char folder[fs::kMaxPath], pattern[64];
        fs::path::parent(full, folder, sizeof folder);
        snprintf(pattern, sizeof pattern, "%s", fs::path::name(full));
        if (path::has_wildcard(folder)) {
            return s.fail(how.command, typed, "* and ? only work in the last name");
        }
        return list_folder(s, how, folder, pattern);
    }
    DirEntry entry;
    const FsError e = kind(s, full, entry);
    if (e != FsError::Ok) {
        return s.fail(how.command, typed, e);
    }
    if (entry.folder) {
        return list_folder(s, how, full, nullptr);
    }
    if (how.details) {
        detail_line(s, entry);
    } else {
        s.printf("%s\n", entry.name);
    }
    return 0;
}

int list_all(Session &s, int argc, char **argv, bool details)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    char found[8];
    if (!options(s, argc, argv, "la1", found, sizeof found)) {
        return 1;
    }
    const Listing how = {details || has(found, 'l'), has(found, 'a'), argc > 2, argv[0]};
    if (argc == 1) {
        return list(s, how, ".");
    }
    int result = 0;
    for (int i = 1; i < argc && !s.stopped(); i++) {
        if (i > 1) {
            s.print("\n");
        }
        result |= list(s, how, argv[i]);
    }
    return result;
}

/* ---- cat -------------------------------------------------------------------------- */

int show_file(Session &s, const char *full, void *context)
{
    const char *command = static_cast<const char *>(context);
    fs::Storage &storage = *s.env().storage;
    fs::File file;
    const FsError e = storage.open(full, fs::OpenMode::Read, file);
    if (e != FsError::Ok) {
        return s.fail(command, full, e);
    }
    int result = 0;
    while (!s.stopped()) {
        size_t got = 0;
        const FsError read = storage.read(file, s.buffer(), Session::kBufferSize, got);
        if (read != FsError::Ok) {
            result = s.fail(command, full, read);
            break;
        }
        if (got == 0) {
            break;
        }
        s.write_latin1(s.buffer(), got);
    }
    storage.close(file);
    return result;
}

/* ---- cp --------------------------------------------------------------------------- */

int copy_file(Session &s, const char *command, const char *from, const char *to)
{
    fs::Storage &storage = *s.env().storage;
    if (path::same(from, to)) {
        return s.fail(command, from, "it would be copied onto itself");
    }
    DirEntry there;
    if (kind(s, to, there) == FsError::Ok && there.folder) {
        return s.fail(command, to, FsError::IsFolder);
    }
    fs::File in;
    FsError e = storage.open(from, fs::OpenMode::Read, in);
    if (e != FsError::Ok) {
        return s.fail(command, from, e);
    }
    // The safe way: the copy is written beside its place and only takes
    // the name when it is whole.
    fs::Save save;
    e = storage.begin_save(to, save);
    if (e != FsError::Ok) {
        storage.close(in);
        return s.fail(command, to, e);
    }
    const char *failed_on = to;
    for (;;) {
        if (s.stopped()) {
            storage.close(in);
            storage.finish_save(save, false);
            return 1;
        }
        size_t got = 0;
        e = storage.read(in, s.buffer(), Session::kBufferSize, got);
        if (e != FsError::Ok) {
            failed_on = from;
            break;
        }
        if (got == 0) {
            break;
        }
        e = storage.write(save.file, s.buffer(), got);
        if (e != FsError::Ok) {
            break;
        }
    }
    storage.close(in);
    if (e != FsError::Ok) {
        storage.finish_save(save, false);
        return s.fail(command, failed_on, e);
    }
    e = storage.finish_save(save);
    return e == FsError::Ok ? 0 : s.fail(command, to, e);
}

// Adds "\name" to a path in place; false if it does not fit.
bool grow(char *path, const char *name)
{
    const size_t length = strlen(path);
    const bool separator = length > 0 && path[length - 1] != '\\';
    return snprintf(path + length, fs::kMaxPath - length, "%s%s", separator ? "\\" : "", name) <
           static_cast<int>(fs::kMaxPath - length);
}

// Copies the folder s.walk_from() and all in it to s.walk_to(). The two
// paths grow and shrink as it goes down and comes back, so each level of
// folders costs one directory entry of stack and no more.
int copy_tree(Session &s, const char *command)
{
    fs::Storage &storage = *s.env().storage;
    char *from = s.walk_from(), *to = s.walk_to();
    DirEntry entry;
    if (kind(s, to, entry) != FsError::Ok) {
        const FsError e = storage.make_folder(to);
        if (e != FsError::Ok) {
            return s.fail(command, to, e);
        }
    } else if (!entry.folder) {
        return s.fail(command, to, FsError::NotFolder);
    }
    const size_t from_length = strlen(from), to_length = strlen(to);
    int result = 0;
    for (int i = 0; !s.stopped() && storage.entry(from, i, entry) == FsError::Ok; i++) {
        if (!grow(from, entry.name) || !grow(to, entry.name)) {
            result = s.fail(command, entry.name, kTooLong);
        } else if (entry.folder) {
            result |= copy_tree(s, command);
        } else {
            result |= copy_file(s, command, from, to);
        }
        from[from_length] = 0;
        to[to_length] = 0;
    }
    return result | (s.stopped() ? 1 : 0);
}

struct Transfer {
    const char *command;
    const char *to;      // the full path given last
    bool into;           // it is a folder: things go into it under their own names
    bool recursive;
};

bool target_for(Session &s, const Transfer &t, const char *from, char *out)
{
    if (!t.into) {
        snprintf(out, fs::kMaxPath, "%s", t.to);
        return true;
    }
    if (!fs::path::join(t.to, fs::path::name(from), out, fs::kMaxPath)) {
        s.fail(t.command, from, kTooLong);
        return false;
    }
    return true;
}

int copy_one(Session &s, const char *from, void *context)
{
    const Transfer &t = *static_cast<const Transfer *>(context);
    char to[fs::kMaxPath];
    DirEntry entry;
    const FsError e = kind(s, from, entry);
    if (e != FsError::Ok) {
        return s.fail(t.command, from, e);
    }
    if (strlen(from) <= 3) {
        return s.fail(t.command, from, "a whole drive is copied as X:\\*");
    }
    if (!target_for(s, t, from, to)) {
        return 1;
    }
    if (!entry.folder) {
        return copy_file(s, t.command, from, to);
    }
    if (!t.recursive) {
        return s.fail(t.command, from, "it is a folder (cp -r copies folders)");
    }
    if (within(from, to)) {
        return s.fail(t.command, from, "a folder cannot be copied into itself");
    }
    snprintf(s.walk_from(), fs::kMaxPath, "%s", from);
    snprintf(s.walk_to(), fs::kMaxPath, "%s", to);
    return copy_tree(s, t.command);
}

/* ---- mv --------------------------------------------------------------------------- */

int move_one(Session &s, const char *from, void *context)
{
    const Transfer &t = *static_cast<const Transfer *>(context);
    fs::Storage &storage = *s.env().storage;
    char to[fs::kMaxPath];
    DirEntry entry, there;
    FsError e = kind(s, from, entry);
    if (e != FsError::Ok) {
        return s.fail(t.command, from, e);
    }
    if (strlen(from) <= 3) {
        return s.fail(t.command, from, "a drive cannot be moved");
    }
    if (!target_for(s, t, from, to)) {
        return 1;
    }
    if (strcmp(from, to) == 0) {
        return s.fail(t.command, from, "it is already there");
    }
    if (entry.folder && within(from, to) && !path::same(from, to)) {
        return s.fail(t.command, from, "a folder cannot be moved into itself");
    }
    if (entry.folder && within(from, s.cwd())) {
        return s.fail(t.command, from, "this session is in it");
    }
    if (fs::path::drive(from) != fs::path::drive(to)) {
        // Between drives there is no renaming: copy, then delete.
        if (entry.folder) {
            return s.fail(t.command, from, "a folder goes to another drive with cp -r, then rm -r");
        }
        if (copy_file(s, t.command, from, to) != 0) {
            return 1;
        }
        e = storage.remove(from);
        return e == FsError::Ok ? 0 : s.fail(t.command, from, e);
    }
    // A file moved onto another file takes its place, in one step. (Not for
    // a change of capitals only: that is the same file.)
    const bool onto_file = !entry.folder && !path::same(from, to) && kind(s, to, there) == FsError::Ok && !there.folder;
    e = onto_file ? storage.replace(from, to) : storage.rename(from, to);
    return e == FsError::Ok ? 0 : s.fail(t.command, onto_file ? to : from, e);
}

int transfer(Session &s, int argc, char **argv, Visit visit, const char *allowed)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    char found[8], to[fs::kMaxPath];
    if (!options(s, argc, argv, allowed, found, sizeof found)) {
        return 1;
    }
    if (argc < 3) {
        return s.fail(argv[0], nullptr, "it needs what to take and where to put it");
    }
    if (!s.resolve(argv[argc - 1], to)) {
        return s.fail(argv[0], argv[argc - 1], kTooLong);
    }
    DirEntry entry;
    Transfer t = {argv[0], to, kind(s, to, entry) == FsError::Ok && entry.folder, has(found, 'r') || has(found, 'R')};
    const bool many = argc > 3 || path::has_wildcard(argv[1]);
    if (many && !t.into) {
        return s.fail(argv[0], argv[argc - 1], "several things can only go into a folder");
    }
    // "ren Notes.TXT notes.txt" names the file itself, not a place to put it.
    if (!many && t.into) {
        char from[fs::kMaxPath];
        if (s.resolve(argv[1], from) && path::same(from, to) && visit == &move_one) {
            t.into = false;
        }
    }
    int result = 0;
    for (int i = 1; i < argc - 1 && !s.stopped(); i++) {
        result |= each(s, argv[0], argv[i], visit, &t, visit == &move_one);
    }
    return result;
}

/* ---- rm, rmdir -------------------------------------------------------------------- */

// Deletes all that is in the folder s.walk_from(), then the folder.
int remove_tree(Session &s, const char *command)
{
    fs::Storage &storage = *s.env().storage;
    char *folder = s.walk_from();
    const size_t length = strlen(folder);
    DirEntry entry;
    int result = 0;
    // Each entry deleted lets the next one into its place; one that could
    // not be deleted is stepped over.
    for (int i = 0; !s.stopped() && storage.entry(folder, i, entry) == FsError::Ok;) {
        int failed = 1;
        if (!grow(folder, entry.name)) {
            s.fail(command, entry.name, kTooLong);
        } else if (entry.folder) {
            failed = remove_tree(s, command);
        } else {
            const FsError e = storage.remove(folder);
            failed = e == FsError::Ok ? 0 : s.fail(command, folder, e);
        }
        folder[length] = 0;
        result |= failed;
        i += failed != 0 ? 1 : 0;
    }
    if (s.stopped()) {
        return 1;
    }
    const FsError e = storage.remove(folder);
    return e == FsError::Ok ? result : s.fail(command, folder, e);
}

struct Removal {
    const char *command;
    bool recursive, quiet;
};

int remove_one(Session &s, const char *full, void *context)
{
    const Removal &r = *static_cast<const Removal *>(context);
    DirEntry entry;
    const FsError e = kind(s, full, entry);
    if (e == FsError::NotFound && r.quiet) {
        return 0;
    }
    if (e != FsError::Ok) {
        return s.fail(r.command, full, e);
    }
    if (!entry.folder) {
        const FsError removed = s.env().storage->remove(full);
        return removed == FsError::Ok ? 0 : s.fail(r.command, full, removed);
    }
    if (!r.recursive) {
        return s.fail(r.command, full, "it is a folder (rm -r deletes a folder and all in it)");
    }
    if (strlen(full) <= 3) {
        return s.fail(r.command, full, "a drive cannot be deleted");
    }
    if (within(full, s.cwd())) {
        return s.fail(r.command, full, "this session is in it");
    }
    snprintf(s.walk_from(), fs::kMaxPath, "%s", full);
    return remove_tree(s, r.command);
}

int remove_folder(Session &s, const char *full, void *context)
{
    const char *command = static_cast<const char *>(context);
    DirEntry entry;
    FsError e = kind(s, full, entry);
    if (e == FsError::Ok && !entry.folder) {
        e = FsError::NotFolder;
    }
    if (e == FsError::Ok && strlen(full) <= 3) {
        return s.fail(command, full, "a drive cannot be deleted");
    }
    if (e == FsError::Ok && within(full, s.cwd())) {
        return s.fail(command, full, "this session is in it");
    }
    if (e == FsError::Ok) {
        e = s.env().storage->remove(full);
    }
    return e == FsError::Ok ? 0 : s.fail(command, full, e);
}

} // namespace

/* ---- The commands ----------------------------------------------------------------- */

int cmd_ls(Session &s, int argc, char **argv) { return list_all(s, argc, argv, false); }
int cmd_dir(Session &s, int argc, char **argv) { return list_all(s, argc, argv, true); }

int cmd_pwd(Session &s, int, char **)
{
    s.printf("%s\n", s.cwd());
    return 0;
}

int cmd_cd(Session &s, int argc, char **argv)
{
    if (argc == 1) {
        return cmd_pwd(s, argc, argv); // as on DOS
    }
    char full[fs::kMaxPath];
    if (!s.resolve(argv[1], full)) {
        return s.fail(argv[0], argv[1], kTooLong);
    }
    return s.set_cwd(full) ? 0 : s.fail(argv[0], argv[1], "there is no such folder");
}

int cmd_cat(Session &s, int argc, char **argv)
{
    if (argc < 2 && s.has_input()) {
        return copy_input(s); // from a pipe, a < file, or the keyboard until Ctrl+D
    }
    if (no_drives(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs the name of a file");
    }
    int result = 0;
    for (int i = 1; i < argc && !s.stopped(); i++) {
        result |= each(s, argv[0], argv[i], &show_file, argv[0]);
    }
    return result;
}

int cmd_cp(Session &s, int argc, char **argv) { return transfer(s, argc, argv, &copy_one, "rR"); }
int cmd_mv(Session &s, int argc, char **argv) { return transfer(s, argc, argv, &move_one, ""); }

int cmd_rm(Session &s, int argc, char **argv)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    char found[8];
    if (!options(s, argc, argv, "rRf", found, sizeof found)) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs the name of what to delete");
    }
    Removal r = {argv[0], has(found, 'r') || has(found, 'R'), has(found, 'f')};
    int result = 0;
    for (int i = 1; i < argc && !s.stopped(); i++) {
        result |= each(s, argv[0], argv[i], &remove_one, &r, true);
    }
    return result;
}

int cmd_mkdir(Session &s, int argc, char **argv)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs a name for the folder");
    }
    int result = 0;
    for (int i = 1; i < argc; i++) {
        char full[fs::kMaxPath];
        if (!s.resolve(argv[i], full)) {
            result = s.fail(argv[0], argv[i], kTooLong);
            continue;
        }
        const FsError e = s.env().storage->make_folder(full);
        if (e != FsError::Ok) {
            result = s.fail(argv[0], argv[i], e);
        }
    }
    return result;
}

int cmd_rmdir(Session &s, int argc, char **argv)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs the name of a folder");
    }
    int result = 0;
    for (int i = 1; i < argc; i++) {
        result |= each(s, argv[0], argv[i], &remove_folder, argv[0], true);
    }
    return result;
}

int cmd_touch(Session &s, int argc, char **argv)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    if (argc < 2) {
        return s.fail(argv[0], nullptr, "it needs a name for the file");
    }
    fs::Storage &storage = *s.env().storage;
    int result = 0;
    for (int i = 1; i < argc; i++) {
        char full[fs::kMaxPath];
        DirEntry entry;
        if (!s.resolve(argv[i], full)) {
            result = s.fail(argv[0], argv[i], kTooLong);
            continue;
        }
        if (kind(s, full, entry) == FsError::Ok) {
            continue; // it is there; files have no dates to bring up to now
        }
        fs::File file;
        const FsError e = storage.open(full, fs::OpenMode::Write, file);
        if (e != FsError::Ok) {
            result = s.fail(argv[0], argv[i], e);
            continue;
        }
        storage.close(file);
    }
    return result;
}

int cmd_df(Session &s, int, char **argv)
{
    if (no_drives(s, argv[0])) {
        return 1;
    }
    char letters[fs::Storage::kMaxDrives];
    const int count = s.env().storage->drives(letters, sizeof letters);
    s.print("Drive           Size           Used           Free  Name\n");
    for (int i = 0; i < count; i++) {
        fs::VolumeInfo info;
        if (s.env().storage->info(letters[i], info) != FsError::Ok) {
            continue;
        }
        char total[32], used[32], free_bytes[32];
        s.printf("%c:    %14s %14s %14s  %s%s\n", letters[i], grouped(info.total_bytes, total, sizeof total),
                 grouped(info.total_bytes - info.free_bytes, used, sizeof used),
                 grouped(info.free_bytes, free_bytes, sizeof free_bytes), info.label,
                 info.keeps_files ? "" : " (emptied at restart)");
    }
    return 0;
}

} // namespace sh
