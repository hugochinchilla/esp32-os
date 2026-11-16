// Each program's settings: C:\.config\<program>.ini (docs/DECISIONS.md).
#include <cstdio>

#include "core/fs/ini.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

void settings_path(const char *program, char *out, size_t n)
{
    snprintf(out, n, "C:\\.config\\%s.ini", program);
}

} // namespace

bool load_settings(const char *program, fs::Ini &ini)
{
    char path[fs::kMaxPath];
    settings_path(program, path, sizeof(path));
    return ini.load(storage(), path);
}

bool save_settings(const char *program, const fs::Ini &ini)
{
    storage().make_folder("C:\\.config"); // already there is fine
    char path[fs::kMaxPath];
    settings_path(program, path, sizeof(path));
    return ini.save(storage(), path) == ports::FsError::Ok;
}

} // namespace shell
