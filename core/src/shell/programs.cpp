// The programs there are to start: the few that are part of the system
// (apps.cpp) and the program files of C:\Programs, as one list in the
// order of their names. The Start menu shows it, and Run, the command
// shell and os::launch start a program by its name through it, so nobody
// needs to know which kind a program is.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/app/file.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/prog/prog.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

// What is kept of a program file: read from its header once, and again
// only if the file changes.
struct FileProgram {
    char path[fs::kMaxPath];
    char title[app::FileInfo::kTitleSize];
    uint8_t pixels[16 * 16];
    gfx::Bitmap icon;
    bool own_icon;
    uint32_t size;
    int64_t modified;
};
struct Files {
    FileProgram rows[kMaxFilePrograms];
    int count;
};
// The desktop's, asked for the first time the list is wanted.
Files *g_files = nullptr;
Program g_list[kMaxPrograms];
int g_count = 0;

// Brings the rows up to date with the folder. The folder is listed each
// time; a file's header is read only if the file is new or has changed.
void read_folder()
{
    if (g_files == nullptr) {
        const os::As as_desktop(os::find("desktop"));
        g_files = static_cast<Files *>(os::allocate(sizeof(Files)));
        if (g_files == nullptr) {
            return;
        }
        g_files->count = 0;
    }
    Files &f = *g_files;
    int n = 0;
    fs::DirEntry entry;
    for (int i = 0; n < kMaxFilePrograms && os::file::entry(kProgramsFolder, i, entry) == ports::FsError::Ok; i++) {
        char path[fs::kMaxPath];
        snprintf(path, sizeof(path), "%s\\%s", kProgramsFolder, entry.name);
        if (entry.folder || !is_program_file(path)) {
            continue;
        }
        int known = -1;
        for (int j = n; j < f.count && known < 0; j++) {
            if (strcmp(f.rows[j].path, path) == 0 && f.rows[j].size == entry.size &&
                f.rows[j].modified == entry.modified) {
                known = j;
            }
        }
        FileProgram &row = f.rows[n];
        if (known > n) {
            const FileProgram moved = row;
            row = f.rows[known];
            f.rows[known] = moved;
        } else if (known < 0) {
            if (n < f.count) {
                // Its place goes to the new one; what was here is kept
                // further on if there is room, or read again another time.
                f.rows[f.count < kMaxFilePrograms ? f.count++ : kMaxFilePrograms - 1] = row;
            }
            // Its header, in memory asked for only while it is read.
            auto *header = static_cast<app::FileInfo *>(os::allocate(sizeof(app::FileInfo)));
            if (header == nullptr || !program_info(path, *header)) {
                os::release(header);
                continue; // not a program of this system: no entry
            }
            snprintf(row.path, sizeof(row.path), "%s", path);
            snprintf(row.title, sizeof(row.title), "%s", header->title);
            if (row.title[0] == 0) {
                snprintf(row.title, sizeof(row.title), "%.*s", static_cast<int>(sizeof(row.title)) - 1, entry.name);
                if (char *dot = strrchr(row.title, '.')) {
                    *dot = 0;
                }
            }
            memcpy(row.pixels, header->icon16, sizeof(row.pixels));
            row.icon = {16, 16, nullptr, header->clear};
            row.own_icon = header->has16 != 0;
            row.size = entry.size;
            row.modified = entry.modified;
            os::release(header);
        }
        row.icon.pixels = row.pixels; // the row may have moved
        n++;
    }
    f.count = n;
}

void add(const Program &program)
{
    // In the order of the names, whatever their case.
    int at = g_count;
    while (at > 0 && strcasecmp(g_list[at - 1].name, program.name) > 0) {
        g_list[at] = g_list[at - 1];
        at--;
    }
    g_list[at] = program;
    g_count++;
}

} // namespace

int program_count()
{
    const prog::InCall in_call;
    read_folder();
    g_count = 0;
    for (int i = 0; i < app_count() && g_count < kMaxPrograms; i++) {
        const App &a = app(i);
        add({a.name, a.icon, &a, nullptr});
    }
    for (int i = 0; g_files != nullptr && i < g_files->count && g_count < kMaxPrograms; i++) {
        const FileProgram &row = g_files->rows[i];
        if (find_app(row.title) != nullptr) {
            continue; // the system's own program of that name stands
        }
        add({row.title, row.own_icon ? &row.icon : &gfx::program_icon_16(), nullptr, row.path});
    }
    return g_count;
}

const Program &program(int index) { return g_list[index]; }

void forget_programs()
{
    g_count = 0;
    os::release(g_files);
    g_files = nullptr;
}

const Program *find_program(const char *name, bool or_start)
{
    const size_t length = strlen(name);
    if (length == 0) {
        return nullptr;
    }
    const int count = program_count();
    const Program *starts = nullptr;
    int found = 0;
    for (int i = 0; i < count; i++) {
        if (strcasecmp(g_list[i].name, name) == 0) {
            return &g_list[i];
        }
        if (or_start && strncasecmp(g_list[i].name, name, length) == 0) {
            starts = &g_list[i];
            found++;
        }
    }
    return found == 1 ? starts : nullptr;
}

bool start_program(const Program &program, const char *argument, char *why, size_t capacity)
{
    if (program.built_in != nullptr) {
        launch(*program.built_in);
        return true;
    }
    // The path is the list's, and the list may be made again while the
    // program starts.
    char path[fs::kMaxPath];
    snprintf(path, sizeof(path), "%s", program.path);
    return run_file(path, why, capacity, argument);
}

bool start_program(const char *name, const char *argument)
{
    static char text[200];
    char why[120] = "";
    const Program *found = find_program(name);
    if (found == nullptr) {
        snprintf(text, sizeof(text), "%s is not on this machine.\nIts file belongs in %s.", name, kProgramsFolder);
        message_box(name, text);
        return false;
    }
    if (!start_program(*found, argument, why, sizeof(why))) {
        snprintf(text, sizeof(text), "%s could not be run:\n%s.", name, why);
        message_box(name, text);
        return false;
    }
    return true;
}

} // namespace shell
