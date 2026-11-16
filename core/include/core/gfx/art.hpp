// Small pictures of the interface. All original, drawn after the sketches in
// tools/mockup/desktop.py.
#pragma once

#include "core/gfx/canvas.hpp"

namespace gfx {

const Bitmap &mascot_16();      // the chinchilla face, logo of the OS
const Bitmap &folder_icon_16();
const Bitmap &snake_icon_16();  // the snake game
const Bitmap &text_icon_16();
const Bitmap &network_icon_16();
const Bitmap &settings_icon_16();
const Bitmap &drive_icon_16();
const Bitmap &picture_icon_16();

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
