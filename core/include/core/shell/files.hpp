// What the File manager asks of the system beyond the OS calls: the
// Trash, the file commands (Delete, Copy, Cut, Paste, a drop on a folder)
// and the listing of a folder. They stay in the system because the desktop
// uses them too (its Trash icon, its own icons, the file dialogs); the
// File manager is a program file (programs/file_manager) and finds them
// by name (tools/gen_exports.py).
#pragma once

#include "core/fs/fs.hpp"
#include "core/ports.hpp"
#include "core/shell/listing.hpp"

namespace shell {

// The Trash (trash.cpp): C:\.trash, with C:\.config\trash.ini remembering
// where each entry came from.
constexpr const char *kTrashFolder = "C:\\.trash";
bool in_trash(const char *path);  // C:\.trash or inside it
bool trashable(const char *path); // on C:, not a drive, not in the Trash
ports::FsError to_trash(const char *path);
ports::FsError remove_tree(const char *path, int depth = 0); // for good
int trash_count();
ports::FsError empty_trash();
bool trash_origin(const char *name, char *out, int capacity);
ports::FsError restore_from_trash(const char *name);
// Drag and drop between folders (same drive); into the Trash trashes.
ports::FsError move_to_folder(const char *path, const char *folder);
// A name for `name` not taken in `folder`: itself, else "name (2).ext"...
bool free_name_in(const char *folder, const char *name, bool is_folder, char *out, int capacity);
// What a file command acts on (backlog items 54 and 55): entries of one
// folder, by name. The names are the caller's and need only last the call.
struct Selection {
    const char *folder;
    const char *const *names;
    int count;
};
// The Delete command: asks once, for all of them, then moves them to the
// Trash (on C:) or deletes them for good (in the Trash, on other drives).
void delete_asking(const char *path);
void delete_asking(const Selection &s);
// Copy and Cut (file_ops.cpp): onto the clipboard, as files.
void copy_files(const Selection &s, bool cut);
// Whether Paste would do anything in `folder`: there are files on the
// clipboard, and it is a folder that takes them (not My Board, not the
// Trash).
bool can_paste(const char *folder);
// Paste: what is on the clipboard is copied into `folder`, or moved there
// if it was cut, by a job of the asker's; a name taken there is numbered,
// nothing is replaced. `window` shows the hourglass meanwhile
// (os::kNoWindow: none does). It says what went wrong, if anything.
void paste_files(const char *folder, int window);
// What open_file() takes a file for, by its name: "Text", "Picture",
// "Program", "Web page"; nullptr when there is no program for it.
const char *file_kind(const char *path);

} // namespace shell
