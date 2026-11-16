// The first desktop: an "About Chinchilla" window over a larger "Welcome"
// window, enough to see overlap, focus, dragging and repainting.
#pragma once

#include "core/wm/wm.hpp"

namespace shell {

// Opens the demo windows (once; later calls do nothing).
void open_demo_windows(wm::Wm &wm);

} // namespace shell
