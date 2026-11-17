// The table of built-in programs and the way into each (every program has
// a file of its own).
#include <cstdio>

#include "core/prog/prog.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/defrag.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

namespace {

void open_editor() { open_in_editor(nullptr); }
void open_viewer() { open_picture(nullptr); }
void open_file_manager() { open_folder(nullptr); }

// Each program with the function that gives its icon (icons are built on
// first use, so the table is filled once).
struct Entry {
    App app;
    const gfx::Bitmap &(*icon)();
};
// The stack of a program that reads folders and files on its own task: the
// file system's code runs on it too. A first figure, to be trimmed with what
// the board measures (the UI task, which ran them all until now, has 16 KB).
constexpr uint32_t kRoomy = 8 * 1024;
const Entry kApps[] = {
    {{"About Chinchilla", nullptr, &open_about, false, true}, &gfx::mascot_16},
#ifndef CHINCHILLA_GAMES_IN_FILES // on the board they are files in C:\Programs (backlog item 27)
    {{"Cactus Patch", nullptr, &open_cactus_patch, false, true}, &gfx::cactus_icon_16},
    {{"Culebra", nullptr, &open_culebra, false, true}, &gfx::snake_icon_16},
#endif
    {{"Defragmenter", nullptr, &open_defragmenter, false, true, kRoomy}, &gfx::defrag_icon_16},
    {{"Editor", nullptr, &open_editor, true, true, kRoomy}, &gfx::editor_icon_16},
    {{"File manager", nullptr, &open_file_manager, false, true, kRoomy}, &gfx::files_icon_16},
    {{"Picture viewer", nullptr, &open_viewer, false, true, kRoomy}, &gfx::viewer_icon_16},
    {{"Task Manager", nullptr, &open_task_manager, false, true, kRoomy}, &gfx::tasks_icon_16},
    {{"Terminal", nullptr, &open_terminal, true}, &gfx::terminal_icon_16},
    {{"Welcome", nullptr, &open_welcome, false, true}, &gfx::welcome_icon_16},
    {{"Widget gallery", nullptr, &open_gallery, false, true}, &gfx::gallery_icon_16},
};
constexpr int kAppCount = sizeof(kApps) / sizeof(kApps[0]);

App apps_with_icons[kAppCount];
bool icons_set = false;

const App *table()
{
    if (!icons_set) {
        for (int i = 0; i < kAppCount; i++) {
            apps_with_icons[i] = kApps[i].app;
            apps_with_icons[i].icon = &kApps[i].icon();
        }
        icons_set = true;
    }
    return apps_with_icons;
}

} // namespace

int app_count() { return kAppCount; }

const App &app(int index) { return table()[index]; }

const App *find_app(const char *name)
{
    for (int i = 0; i < kAppCount; i++) {
        const char *a = table()[i].name, *b = name;
        while (*a != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) {
            return &table()[i];
        }
    }
    return nullptr;
}

void launch(const App &app)
{
    const prog::InCall in_call;
    if (app.many) {
        app.open(); // makes its own process, one for each copy
        return;
    }
    enter_program(
        process_for(app.name), app.name, [](void *a) { static_cast<const App *>(a)->open(); },
        const_cast<App *>(&app), 0);
}

bool enter_program(proc::Pid pid, const char *program, proc::CallFn fn, void *context, size_t size,
                   proc::Keep keep)
{
    const prog::InCall in_call;
    const App *app = program != nullptr ? find_app(program) : nullptr;
    if (app != nullptr && app->own_task) {
        // Without a task (none here, none left) it runs on the caller's.
        prog::runner().start(pid, app->stack);
    }
    return proc::table().call(pid, fn, context, size, keep);
}

proc::Pid process_for(const char *program)
{
    const prog::InCall in_call;
    proc::Table &t = proc::table();
    proc::Pid pid = t.find_by_name(program);
    if (pid == proc::kNone) {
        pid = t.spawn(program, t.current(), proc::kWindowed);
    }
    return pid != proc::kNone ? pid : t.current();
}

} // namespace shell
