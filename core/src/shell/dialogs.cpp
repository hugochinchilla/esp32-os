// The file dialogs (Open, Save As) and the input dialog.
#include <cstdio>
#include <cstring>

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"
#include "core/shell/listing.hpp"

namespace shell {

namespace {

/* ---- Open and Save As ---------------------------------------------------------- */

class FileDialog : public app::Window {
public:
    FileDialog(bool save, const char *folder, const char *name, PathFn chosen, void *context)
        : save_(save), chosen_(chosen), context_(context),
          look_label_(label({8, 12, 56, 14}, "Look &in:")),
          look_(drop_down(&gfx::folder_icon_16(), {64, 8, 270, 22})),
          up_(button({340, 8, 72, 22}, "&Up")),
          list_(list({8, 38, 404, 150}, nullptr, 0)),
          name_label_(label({8, 202, 70, 14}, "File &name:")),
          name_(text_field({80, 198, 244, 21}, name != nullptr ? name : "")),
          ok_(button({332, 196, 80, 23}, save ? "&Save" : "&Open")),
          cancel_(button({332, 224, 80, 23}, "Cancel"))
    {
        set_default(ok_);
        snprintf(folder_, sizeof(folder_), "%s", folder != nullptr && folder[0] ? folder : "C:\\");
        refresh();
        set_focus(name_);
        name_.select_all();
    }

    void command(app::Control which) override
    {
        if (which == ok_) {
            try_name();
        } else if (which == cancel_) {
            close();
        } else if (which == up_) {
            go_up();
        } else if (which == look_) {
            look_in();
        }
    }

    // A row was picked: a file's name goes to the field.
    void changed(app::Control) override
    {
        const int i = list_.selected();
        if (i >= 0 && !listing_.item(i).folder) {
            name_.set(listing_.item(i).name);
        }
    }

    // Enter or a double click on a row: into the folder, or that file.
    bool chosen(app::Control) override
    {
        const int i = list_.selected();
        if (i < 0) {
            return true;
        }
        char path[fs::kMaxPath];
        fs::path::join(folder_, listing_.item(i).name, path, sizeof(path));
        if (listing_.item(i).folder) {
            go_to(path);
        } else {
            name_.set(listing_.item(i).name);
            try_name();
        }
        return true;
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && k.usage == wm::key::Backspace && focus() == list_) {
            go_up();
            return true;
        }
        return false;
    }

private:
    void refresh()
    {
        ports::DirEntry e;
        if (fs::path::relative(folder_)[0] != 0 && os::file::stat(folder_, e) != ports::FsError::Ok) {
            snprintf(folder_, sizeof(folder_), "C:\\"); // gone: back to the start
        }
        listing_.read(folder_);
        for (int i = 0; i < listing_.count(); i++) {
            icons_[i] = listing_.icon(i, false);
        }
        list_.set_items(listing_.names(), listing_.count(), icons_);
        look_.set_text(folder_);
    }

    void go_to(const char *folder)
    {
        snprintf(folder_, sizeof(folder_), "%s", folder);
        refresh();
    }

    void go_up()
    {
        char parent[fs::kMaxPath];
        fs::path::parent(folder_, parent, sizeof(parent));
        go_to(parent);
    }

    void accept(const char *path)
    {
        snprintf(result_, sizeof(result_), "%s", path);
        PathFn chosen = chosen_;
        void *context = context_;
        close();
        if (chosen != nullptr) {
            chosen(result_, context);
        }
    }

    void try_name()
    {
        char name[96];
        name_.get(name, sizeof(name));
        if (name[0] == 0) {
            return;
        }
        // A full path, or a name in the folder shown.
        char path[fs::kMaxPath];
        if (fs::path::drive(name) != 0) {
            snprintf(path, sizeof(path), "%s", name);
        } else if (!fs::path::join(folder_, name, path, sizeof(path))) {
            return;
        }
        ports::DirEntry e;
        const ports::FsError found = os::file::stat(path, e);
        if (found == ports::FsError::Ok && e.folder) {
            go_to(path);
            name_.set("");
            return;
        }
        if (!save_) {
            if (found != ports::FsError::Ok) {
                message_box("Open", "That file was not found.\nCheck the name and try again.");
                return;
            }
            accept(path);
            return;
        }
        if (!fs::path::valid_name(fs::path::name(path))) {
            message_box("Save As", "That name cannot be used for a file.\nAvoid \\ / : * ? \" < > |");
            return;
        }
        if (found == ports::FsError::Ok) {
            snprintf(pending_, sizeof(pending_), "%s", path);
            static const char *const yes_no[] = {"&Yes", "&No"};
            char text[200];
            snprintf(text, sizeof(text), "%s already exists.\nDo you want to replace it?", fs::path::name(path));
            ask("Save As", text, yes_no, 2, &FileDialog::replace_answer, this);
            return;
        }
        accept(path);
    }

    static void replace_answer(int choice, void *context)
    {
        auto *d = static_cast<FileDialog *>(context);
        if (choice == 0) {
            d->accept(d->pending_);
        }
    }

    // The Look in list: the drives, then each folder down to this one.
    void look_in()
    {
        int n = 0;
        char letters[fs::Storage::kMaxDrives];
        const int drives = os::file::drives(letters, fs::Storage::kMaxDrives);
        for (int i = 0; i < drives && n < kLookMax; i++) {
            snprintf(look_paths_[n], sizeof(look_paths_[n]), "%c:\\", letters[i]);
            snprintf(look_labels_[n], sizeof(look_labels_[n]), "%c:\\", letters[i]);
            n++;
            if (letters[i] != fs::path::drive(folder_)) {
                continue;
            }
            // The folders of the current path, indented.
            const char *rel = fs::path::relative(folder_);
            int depth = 1;
            for (const char *p = rel; *p != 0 && n < kLookMax;) {
                const char *end = p;
                while (*end != 0 && *end != '\\') {
                    end++;
                }
                const int upto = static_cast<int>(end - folder_);
                snprintf(look_paths_[n], sizeof(look_paths_[n]), "%.*s", upto, folder_);
                snprintf(look_labels_[n], sizeof(look_labels_[n]), "%*s%.*s", depth * 2, "",
                         static_cast<int>(end - p), p);
                n++;
                depth++;
                p = *end ? end + 1 : end;
            }
        }
        for (int i = 0; i < n; i++) {
            look_items_[i] = {look_labels_[i], &FileDialog::look_chosen, &look_context_[i], nullptr, nullptr, 0};
            look_context_[i] = {this, i};
        }
        look_menu_.count = n;
        look_.menu(look_menu_);
    }

    struct LookContext {
        FileDialog *dialog;
        int index;
    };

    static void look_chosen(void *context)
    {
        auto *lc = static_cast<LookContext *>(context);
        lc->dialog->go_to(lc->dialog->look_paths_[lc->index]);
    }

    static constexpr int kLookMax = 8;
    bool save_;
    PathFn chosen_;
    void *context_;
    char folder_[fs::kMaxPath] = "C:\\";
    char pending_[fs::kMaxPath] = "";
    char result_[fs::kMaxPath] = "";
    Listing listing_;
    const gfx::Bitmap *icons_[Listing::kMax] = {};
    char look_paths_[kLookMax][fs::kMaxPath];
    char look_labels_[kLookMax][48];
    LookContext look_context_[kLookMax];
    wm::MenuItem look_items_[kLookMax];
    wm::Menu look_menu_ = {look_items_, 0};

    app::Label look_label_;
    app::DropDown look_;
    app::Button up_;
    app::ListBox list_;
    app::Label name_label_;
    app::TextField name_;
    app::Button ok_, cancel_;
};

/* ---- Input dialog ---------------------------------------------------------------- */

class InputDialog : public app::Window {
public:
    InputDialog(const char *title, const char *prompt, const char *initial, TextFn done, void *context)
        : done_(done), context_(context),
          prompt_(label({8, 10, 296, 14}, prompt_text_)),
          field_(text_field({8, 30, 296, 21}, initial)),
          ok_(button({148, 62, 75, 23}, "OK")),
          cancel_(button({229, 62, 75, 23}, "Cancel"))
    {
        // The texts are the caller's: kept here for as long as the window is.
        snprintf(title_, sizeof(title_), "%s", title);
        snprintf(prompt_text_, sizeof(prompt_text_), "%s", prompt);
        set_title(title_);
        set_default(ok_);
        set_focus(field_);
        field_.select_all();
    }

    void command(app::Control which) override
    {
        if (which == ok_) {
            field_.get(answer_, sizeof(answer_));
            TextFn done = done_;
            void *ctx = context_;
            close();
            if (done != nullptr) {
                done(answer_, ctx);
            }
        } else if (which == cancel_) {
            close();
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return false;
    }

private:
    char title_[48] = "";
    char prompt_text_[96] = "";
    char answer_[160] = "";
    TextFn done_;
    void *context_;
    app::Label prompt_;
    app::TextField field_;
    app::Button ok_, cancel_;
};

// Each dialog is made when it is asked for and gone once it has closed, so
// two programs (or two Editors) can each have their own at the same time.
// A window that already has a dialog up gets that one brought forward.
void file_dialog(bool save, int owner, const char *folder, const char *name, PathFn chosen, void *context)
{
    if (has_dialog(owner)) {
        return;
    }
    app::Look look;
    look.title = save ? "Save As" : "Open";
    look.width = 428;
    look.height = 284;
    look.flags = app::kDialog;
    look.over = owner;
    if (app::open<FileDialog>(look, save, folder, name, chosen, context) == app::kNone) {
        no_memory_for(look.title);
    }
}

} // namespace

void open_file_dialog(int owner, const char *folder, PathFn chosen, void *context)
{
    file_dialog(false, owner, folder, "", chosen, context);
}

void save_file_dialog(int owner, const char *folder, const char *name, PathFn chosen, void *context)
{
    file_dialog(true, owner, folder, name, chosen, context);
}

void input_dialog(int owner, const char *title, const char *prompt, const char *initial, TextFn done,
                  void *context)
{
    if (has_dialog(owner)) {
        return;
    }
    app::Look look;
    look.width = 320;
    look.height = 120;
    look.flags = app::kDialog;
    look.over = owner;
    if (app::open<InputDialog>(look, title, prompt, initial, done, context) == app::kNone) {
        no_memory_for(title);
    }
}

} // namespace shell
