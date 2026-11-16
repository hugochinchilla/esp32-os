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

// Opens a window if it is not open, otherwise brings it to the front.
void show(wm::Window &w);

// Centres a window of the given size in the work area.
gfx::Rect centred(const wm::Wm &wm, int w, int h);

} // namespace shell
