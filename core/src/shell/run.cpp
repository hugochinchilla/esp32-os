// Run: starting a program or opening a file by typing its name. The Run
// dialog of the Start menu, and the desktop's side of the command shell's
// open, run and edit (backlog item 16; docs/journal/2026-11-17-run.md).
// The dialog is a program of its own: made when opened, gone when closed,
// with the last thing typed kept in C:\.config\run.ini.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/app/app.hpp"
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/sh/sh.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"

namespace shell {

// A folder in the File manager, which is a program file (nullptr: My
// Board). It opens a window for it, or brings forward the one that shows
// it.
void open_folder(const char *path)
{
    start_program("File manager", path);
}

// The Trash is the File manager on C:\.trash.
void open_trash() { open_folder(kTrashFolder); }

// What opens what, by extension: 1 the Editor, 2 the Picture viewer, 3 a
// program that is loaded and run, 4 the web browser, 0 nothing.
static int program_kind(const char *path)
{
    static const char *const kText[] = {"txt", "log", "ini", "md"};
    static const char *const kPictures[] = {"bmp", "png", "jpg", "jpeg"};
    if (is_program_file(path)) {
        return 3;
    }
    char ext[8];
    fs::path::extension(path, ext, sizeof(ext));
    for (const char *e : kText) {
        if (strcmp(ext, e) == 0) {
            return 1;
        }
    }
    for (const char *e : kPictures) {
        if (strcmp(ext, e) == 0) {
            return 2;
        }
    }
    if (strcmp(ext, "htm") == 0 || strcmp(ext, "html") == 0) {
        return 4;
    }
    return 0;
}

bool has_program_for(const char *path) { return program_kind(path) != 0; }

const char *file_kind(const char *path)
{
    static const char *const kKinds[] = {nullptr, "Text", "Picture", "Program", "Web page"};
    return kKinds[program_kind(path)];
}

bool open_file(const char *path)
{
    switch (program_kind(path)) {
    case 1:
        open_in_editor(path);
        return true;
    case 2:
        open_picture(path);
        return true;
    case 3: {
        // A program in a file: loaded and run.
        static char text[200];
        char why[120];
        if (!run_file(path, why, sizeof(why))) {
            snprintf(text, sizeof(text), "'%s' could not be run:\n%s.", fs::path::name(path), why);
            message_box("Run", text);
        }
        return true;
    }
    case 4:
        start_program("Vizcacha", path);
        return true;
    default:
        break;
    }
    char ext[8];
    fs::path::extension(path, ext, sizeof(ext));
    static char text[160];
    snprintf(text, sizeof(text), "There is no program for %s%s files yet.", ext[0] ? "." : "",
             ext[0] ? ext : "these");
    message_box(fs::path::name(path), text);
    return false;
}


namespace {

bool open_path(const char *full, char *why, size_t capacity)
{
    if (strlen(full) <= 3) {
        if (!os::file::has_drive(fs::path::drive(full))) {
            snprintf(why, capacity, "there is no such drive");
            return false;
        }
        open_folder(full);
        return true;
    }
    fs::DirEntry entry;
    const ports::FsError e = os::file::stat(full, entry);
    if (e != ports::FsError::Ok) {
        snprintf(why, capacity, "%s", fs::error_text(e));
        return false;
    }
    if (entry.folder) {
        open_folder(full);
        return true;
    }
    if (!has_program_for(full)) {
        char ext[8];
        fs::path::extension(full, ext, sizeof(ext));
        snprintf(why, capacity, "there is no program for %s%s files yet", ext[0] ? "." : "", ext[0] ? ext : "such");
        return false;
    }
    return open_file(full);
}

class SessionDesktop final : public sh::Desktop {
public:
    bool open(const char *full_path, char *why, size_t capacity) override
    {
        return open_path(full_path, why, capacity);
    }
    bool run(const char *program, char *why, size_t capacity) override
    {
        return run_text(program, "C:\\", why, capacity, true, false);
    }
    bool edit(const char *full_path, char *why, size_t capacity) override
    {
        if (full_path != nullptr) {
            // A name that is not there yet is a new file, as a console
            // editor would take it.
            fs::DirEntry entry;
            const ports::FsError found = os::file::stat(full_path, entry);
            if (found == ports::FsError::Ok && entry.folder) {
                snprintf(why, capacity, "%s", fs::error_text(ports::FsError::IsFolder));
                return false;
            }
            if (found != ports::FsError::Ok) {
                const ports::FsError made = os::file::write_file(full_path, "", 0);
                if (made != ports::FsError::Ok) {
                    snprintf(why, capacity, "%s", fs::error_text(made));
                    return false;
                }
            }
        }
        open_in_editor(full_path);
        return true;
    }
    int program_count() override { return shell::program_count(); }
    const char *program_name(int index) override { return program(index).name; }
};

/* ---- The Run dialog ---------------------------------------------------------------- */

class RunDialog : public app::Window {
public:
    RunDialog()
        : line1_(label({52, 14, 276, 14}, "Type the name of a program, folder or file,")),
          line2_(label({52, 28, 276, 14}, "and Chinchilla will open it.")),
          label_(label({10, 63, 40, 14}, "&Open:")),
          field_(text_field({52, 60, 272, 21})),
          ok_(button({332 - 3 * 75 - 2 * 6 - 8, 92, 75, 23}, "OK")),
          cancel_(button({332 - 2 * 75 - 6 - 8, 92, 75, 23}, "Cancel")),
          browse_(button({332 - 75 - 8, 92, 75, 23}, "&Browse..."))
    {
        set_default(ok_);
        // What was run last time is there to run again, or to type over.
        if (load_settings("run", settings_)) {
            char last[2 * app::TextField::kCapacity + 1];
            if (settings_.get("run", "last", last, sizeof(last))) {
                field_.set(last);
            }
        }
        set_focus(field_);
        field_.select_all();
    }

    void paint(app::Canvas &c) override { c.blit(gfx::run_icon_32(), 10, 12); } // the picture over the face

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('o' - 'a')) {
            set_focus(field_);
            field_.select_all();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('b' - 'a')) {
            browse_.press();
            return true;
        }
        return false;
    }

    void command(app::Control which) override
    {
        if (which == ok_) {
            ok();
        } else if (which == cancel_) {
            close();
        } else if (which == browse_) {
            open_file_dialog(number(), "C:\\", &RunDialog::browsed, this);
        }
    }

private:
    void ok()
    {
        char typed[2 * app::TextField::kCapacity + 1];
        field_.get(typed, sizeof(typed));
        char why[160];
        // What it opens is its own program, a child of the desktop, not of
        // this dialog: the dialog is gone a moment later.
        bool opened;
        {
            const os::As as_desktop(os::find("desktop"));
            opened = run_text(typed, "C:\\", why, sizeof(why));
        }
        if (!opened) {
            // The dialog stays, with what was typed, for another try.
            snprintf(message_, sizeof(message_), "%s", why);
            message_box("Run", message_);
            return;
        }
        settings_.set("run", "last", typed);
        save_settings("run", settings_);
        close();
    }
    static void browsed(const char *path, void *context)
    {
        auto *d = static_cast<RunDialog *>(context);
        d->field_.set(path);
        d->set_focus(d->field_);
    }

    char message_[200] = "";
    fs::Ini settings_;
    app::Label line1_, line2_, label_;
    app::TextField field_;
    app::Button ok_, cancel_, browse_;
};

app::Number g_run = app::kNone; // the one there may be: a number, dead once it has closed

} // namespace

bool run_text(const char *typed, const char *folder, char *why, size_t capacity, bool programs, bool paths)
{
    // Without the spaces around it, and without quotes around all of it.
    char text[2 * app::TextField::kCapacity + 1];
    while (*typed == ' ') {
        typed++;
    }
    snprintf(text, sizeof(text), "%s", typed);
    size_t length = strlen(text);
    while (length > 0 && text[length - 1] == ' ') {
        text[--length] = 0;
    }
    if (length >= 2 && text[0] == '"' && text[length - 1] == '"') {
        text[length - 1] = 0;
        memmove(text, text + 1, length - 1);
    }
    if (text[0] == 0) {
        snprintf(why, capacity, "Type the name of a program, a folder or a file first.");
        return false;
    }
    if (programs && strcasecmp(text, "taskbar") == 0) {
        // Not in the Start menu, for the day it is not there to have one:
        // from a console, after it ended too often to be started again.
        if (!start_taskbar_again()) {
            snprintf(why, capacity, "The taskbar is already running.");
            return false;
        }
        return true;
    }
    if (programs) {
        if (const Program *found = find_program(text, true)) {
            char reason[96] = "";
            if (start_program(*found, nullptr, reason, sizeof(reason))) {
                return true;
            }
            snprintf(why, capacity, "Cannot run %s:\n%s.", text, reason);
            return false;
        }
        // A program and what it is to open: "editor readme.txt",
        // "vizcacha http://example.com". A program's name may have spaces
        // of its own, so the longest beginning that names one is it. A
        // file of the drives goes as its whole path, anything else (an
        // address) as it was typed.
        for (size_t at = length; at-- > 1;) {
            if (text[at] != ' ') {
                continue;
            }
            text[at] = 0;
            const Program *found = find_program(text, true);
            if (found == nullptr) {
                text[at] = ' ';
                continue;
            }
            const char *argument = text + at + 1;
            while (*argument == ' ') {
                argument++;
            }
            char full[fs::kMaxPath];
            fs::DirEntry entry;
            if (sh::path::resolve(folder, argument, full, sizeof(full)) &&
                os::file::stat(full, entry) == ports::FsError::Ok) {
                argument = full;
            }
            char reason[96] = "";
            if (start_program(*found, argument, reason, sizeof(reason))) {
                return true;
            }
            snprintf(why, capacity, "Cannot run %s:\n%s.", text, reason);
            return false;
        }
    }
    if (paths) {
        char full[fs::kMaxPath];
        char reason[96] = "";
        if (sh::path::resolve(folder, text, full, sizeof(full)) && open_path(full, reason, sizeof(reason))) {
            return true;
        }
        if (reason[0] != 0 && strcmp(reason, fs::error_text(ports::FsError::NotFound)) != 0) {
            snprintf(why, capacity, "Cannot open %s:\n%s.", text, reason);
            return false;
        }
        snprintf(why, capacity, "Cannot find '%s'.\nCheck the name and try again.", text);
        return false;
    }
    snprintf(why, capacity, "there is no such program; run alone lists them");
    return false;
}

void run_dialog()
{
    const AsProgram as("Run");
    if (app::bring_forward(g_run)) {
        return;
    }
    app::Look look;
    look.title = "Run";
    look.width = 340;
    look.height = 150;
    look.icon = &gfx::run_icon_16();
    look.flags = app::kDialog | app::kPicture;
    g_run = app::open<RunDialog>(look);
    if (g_run == app::kNone) {
        no_memory_for("Run");
    }
}

// The Editor is a program file; what the desktop, the File manager and the
// command shell's `edit` want of it is this.
void open_in_editor(const char *path) { start_program("Editor", path); }
// The same for the Picture viewer: with no picture it asks for one.
void open_picture(const char *path) { start_program("Picture viewer", path); }

sh::Desktop &desktop_for_sessions()
{
    static SessionDesktop desktop;
    return desktop;
}

} // namespace shell
