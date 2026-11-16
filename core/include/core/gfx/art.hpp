// Small pictures of the interface. All original, drawn after the sketches in
// tools/mockup/desktop.py.
#pragma once

#include "core/gfx/canvas.hpp"

namespace gfx {

const Bitmap &mascot_16(); // the chinchilla face, logo of the OS
const Bitmap &mascot_32(); // drawn for its size: About, Shut Down, messages

// Each program has its picture at 32x32 (a desktop shortcut, the icon view)
// and at 16x16 (Start menu, title bar, taskbar button, Task Manager). The
// small one is drawn for its size, not shrunk.
const Bitmap &editor_icon_16(); // notepad and pencil
const Bitmap &editor_icon_32();
const Bitmap &files_icon_16(); // the File manager: an open folder with papers
const Bitmap &files_icon_32();
const Bitmap &viewer_icon_16(); // the Picture viewer: a painting on an easel
const Bitmap &viewer_icon_32();
const Bitmap &tasks_icon_16(); // the Task Manager
const Bitmap &tasks_icon_32();
const Bitmap &defrag_icon_16();
const Bitmap &defrag_icon_32();
const Bitmap &snake_icon_16(); // Culebra
const Bitmap &snake_icon_32();
const Bitmap &cactus_icon_16(); // Cactus Patch
const Bitmap &cactus_icon_32();
const Bitmap &network_icon_16();
const Bitmap &network_icon_32();
const Bitmap &saver_icon_16(); // Screen Saver settings
const Bitmap &saver_icon_32();
const Bitmap &display_icon_16(); // Display settings
const Bitmap &display_icon_32();
const Bitmap &settings_icon_16();
const Bitmap &settings_icon_32();
const Bitmap &welcome_icon_16();
const Bitmap &welcome_icon_32();
const Bitmap &gallery_icon_16(); // Widget gallery
const Bitmap &gallery_icon_32();
const Bitmap &power_icon_16(); // Shut Down
const Bitmap &power_icon_32();
const Bitmap &run_icon_16();
const Bitmap &run_icon_32();
const Bitmap &service_icon_16(); // a process with no window
const Bitmap &service_icon_32();

// Folders, files and drives.
const Bitmap &folder_icon_16();
const Bitmap &folder_icon_32();
const Bitmap &text_icon_16(); // a text file
const Bitmap &text_icon_32();
const Bitmap &picture_icon_16(); // a picture file
const Bitmap &picture_icon_32();
const Bitmap &file_icon_32(); // any other kind of file
const Bitmap &drive_icon_16();
const Bitmap &drive_icon_32();
const Bitmap &board_icon_32(); // the ESP32 board: My Board
const Bitmap &bin_icon_16();
const Bitmap &bin_icon_32();
const Bitmap &bin_full_icon_16(); // the Trash with something in it
const Bitmap &bin_full_icon_32();
const Bitmap &back_icon_16(); // folder toolbar: the folder shown before
const Bitmap &up_icon_16();   // folder toolbar: the folder that holds this one
// Radio signal: four bars, `bars` of them filled (0 to 4). With `locked`, a
// small padlock (the network wants a password). bars < 0: no link, shown
// as empty bars and a red cross.
const Bitmap &signal_icon_16(int bars, bool locked = false);

} // namespace gfx
