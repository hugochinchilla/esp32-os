// Shared between the shell's source files.
#pragma once

#include "core/fs/fs.hpp"
#include "core/ports.hpp"
#include "core/wm/wm.hpp"

namespace shell {

wm::Wm &manager();
ports::System &system_port();
fs::Storage &storage();
ports::Memory &memory();

// Starts following the network, if there is one (network.cpp).
void start_network();

// Opens a window if it is not open, otherwise brings it to the front.
void show(wm::Window &w);

// Centres a window of the given size in the work area.
gfx::Rect centred(const wm::Wm &wm, int w, int h);

// Cactus Patch (cactus_patch.cpp): its Start menu entry.
const gfx::Bitmap &cactus_patch_icon();
void open_cactus_patch();

// Puts the screensaver in place with its settings (screensaver.cpp).
void install_screen_saver();

// Ctrl+Alt+Del (power.cpp): the Task Manager, or a restart from the
// power-down screen.
void attention_key(void *context);

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
// The Delete command: asks, then moves to the Trash (on C:) or deletes for
// good (in the Trash, on other drives).
void delete_asking(const char *path);
// Dragged to the Trash: moved without asking on C:, asks elsewhere.
void drop_in_trash(const char *path);
void open_trash();
// After files moved or went away: the desktop, the File manager windows
// and the Trash window read their folders again.
void files_changed();
void refresh_folder_windows(); // file_manager.cpp

} // namespace shell
