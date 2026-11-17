// Each program's settings: C:\.config\<program>.ini (docs/DECISIONS.md).
// The work is the OS's (core/os/os.hpp); these are the names the programs
// already use.
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"

namespace shell {

bool load_settings(const char *program, fs::Ini &ini) { return os::load_settings(program, ini); }
bool save_settings(const char *program, const fs::Ini &ini) { return os::save_settings(program, ini); }

} // namespace shell
