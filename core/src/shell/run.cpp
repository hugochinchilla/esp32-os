// Run: starting a program or opening a file by typing its name. The Run
// dialog of the Start menu, and the desktop's side of the command shell's
// open, run and edit (backlog item 16; docs/journal/2026-11-17-run.md).
// The dialog is a program of its own: made when opened, gone when closed,
// with the last thing typed kept in C:\.config\run.ini.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/sh/sh.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "internal.hpp"

namespace shell {

namespace {

// The program with this name, in any case; else the only one whose name
// starts with it ("edit" for the Editor, not "c" for two programs).
const App *program_named(const char *name)
{
    const size_t length = strlen(name);
    if (length == 0) {
        return nullptr;
    }
    const App *starts = nullptr;
    int count = 0;
    for (int i = 0; i < app_count(); i++) {
        const App &a = app(i);
        if (strcasecmp(a.name, name) == 0) {
            return &a;
        }
        if (strncasecmp(a.name, name, length) == 0) {
            starts = &a;
            count++;
        }
    }
    return count == 1 ? starts : nullptr;
}

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
    int program_count() override { return app_count(); }
    const char *program_name(int index) override { return app(index).name; }
};

/* ---- The Run dialog ---------------------------------------------------------------- */

class RunDialog : public wm::Window {
public:
    RunDialog()
        : Window("Run", {0, 0, 340, 150}, &gfx::run_icon_16()),
          line1_({52, 14, 276, 14}, "Type the name of a program, folder or file,"),
          line2_({52, 28, 276, 14}, "and Chinchilla will open it."),
          label_({10, 63, 40, 14}, "&Open:"),
          field_({52, 60, 272, 21}),
          ok_({332 - 3 * 75 - 2 * 6 - 8, 92, 75, 23}, "OK", &RunDialog::ok, this),
          cancel_({332 - 2 * 75 - 6 - 8, 92, 75, 23}, "Cancel", &RunDialog::cancel, this),
          browse_({332 - 75 - 8, 92, 75, 23}, "&Browse...", &RunDialog::browse, this)
    {
        set_dialog(true);
        set_frame(os::centred(frame().w, frame().h));
        add(line1_);
        add(line2_);
        add(label_);
        add(field_);
        add(ok_);
        add(cancel_);
        add(browse_);
        ok_.set_default(true);
        set_default(&ok_);
        // What was run last time is there to run again, or to type over.
        if (load_settings("run", settings_)) {
            char last[2 * wm::TextField::kCapacity + 1];
            if (settings_.get("run", "last", last, sizeof(last))) {
                field_.set(last);
            }
        }
        set_focus(&field_);
        field_.select_all();
    }

    bool layered() const override { return true; } // the picture over the face
    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit(gfx::run_icon_32(), 10, 12);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('o' - 'a')) {
            set_focus(&field_);
            field_.select_all();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('b' - 'a')) {
            browse_.click();
            return true;
        }
        return Window::key(k);
    }

private:
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<RunDialog *>(context);
        char typed[2 * wm::TextField::kCapacity + 1];
        d->field_.get(typed, sizeof(typed));
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
            snprintf(d->message_, sizeof(d->message_), "%s", why);
            message_box("Run", d->message_);
            return;
        }
        d->settings_.set("run", "last", typed);
        save_settings("run", d->settings_);
        d->close();
    }
    static void cancel(wm::Button &, void *context) { static_cast<RunDialog *>(context)->close(); }
    static void browse(wm::Button &, void *context)
    {
        open_file_dialog(static_cast<RunDialog *>(context), "C:\\", &RunDialog::chosen, context);
    }
    static void chosen(const char *path, void *context)
    {
        auto *d = static_cast<RunDialog *>(context);
        d->field_.set(path);
        d->set_focus(&d->field_);
    }

    char message_[200] = "";
    fs::Ini settings_;
    wm::Label line1_, line2_, label_;
    wm::TextField field_;
    wm::Button ok_, cancel_, browse_;
};

} // namespace

bool run_text(const char *typed, const char *folder, char *why, size_t capacity, bool programs, bool paths)
{
    // Without the spaces around it, and without quotes around all of it.
    char text[2 * wm::TextField::kCapacity + 1];
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
        if (const App *a = program_named(text)) {
            launch(*a);
            return true;
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
    open_one<RunDialog>("Run");
}

sh::Desktop &desktop_for_sessions()
{
    static SessionDesktop desktop;
    return desktop;
}

} // namespace shell
