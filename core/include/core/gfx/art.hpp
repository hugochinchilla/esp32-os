// Small pictures of the interface. All original, drawn after the sketches in
// tools/mockup/desktop.py.
#pragma once

#include "core/gfx/canvas.hpp"

namespace gfx {

const Bitmap &mascot_16();      // the chinchilla face, logo of the OS
const Bitmap &folder_icon_16();
const Bitmap &text_icon_16();
const Bitmap &network_icon_16();
const Bitmap &settings_icon_16();
const Bitmap &drive_icon_16();
const Bitmap &picture_icon_16();
// Radio signal: four bars, `bars` of them filled (0 to 4). With `locked`, a
// small padlock (the network wants a password). bars < 0: no link, shown
// as empty bars and a red cross.
const Bitmap &signal_icon_16(int bars, bool locked = false);

// Desktop icons, 32x32.
const Bitmap &board_icon_32(); // the ESP32 board: My Board
const Bitmap &folder_icon_32();
const Bitmap &network_icon_32();
const Bitmap &bin_icon_32();
const Bitmap &text_icon_32();
const Bitmap &picture_icon_32();
const Bitmap &drive_icon_32();
const Bitmap &file_icon_32(); // any other kind of file

} // namespace gfx
