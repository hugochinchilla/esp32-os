// The file dialogs (Open, Save As) and the input dialog.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "listing.hpp"

namespace shell {

namespace {

const char *const kDownGlyph[] = {"XXXXXXX", " XXXXX ", "  XXX  ", "   X   "};

// A box showing a value with a drop-down arrow, as the 98 "Look in" box.
class DropDown : public wm::Widget {
public:
    using Callback = void (*)(void *context);
    DropDown(const gfx::Rect &r, Callback on_click, void *context)
        : Widget(r), on_click_(on_click), context_(context)
    {
    }
    void set_text(const char *text)
    {
        text_ = text;
        invalidate();
    }
    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        c.fill(r.inset(2), gfx::kWhite);
        gfx::Canvas inner = c.sub({2, 2, rect.w - 4 - 16, rect.h - 4});
        inner.blit(gfx::folder_icon_16(), 2, (rect.h - 4 - 16) / 2);
        inner.text(22, (rect.h - 4 - 12) / 2, text_, gfx::kBlack);
        const gfx::Rect b = {rect.w - 2 - 16, 2, 16, rect.h - 4};
        c.button(b, held_ ? gfx::ButtonLook::Pressed : gfx::ButtonLook::Raised);
        c.pattern(b.x + 4 + held_, b.y + (b.h - 4) / 2 + held_, kDownGlyph, 4, gfx::kBlack);
    }
    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind == wm::MouseKind::Down) {
            held_ = 1;
            invalidate();
            on_click_(context_);
        } else if (m.kind == wm::MouseKind::Up) {
            held_ = 0;
            invalidate();
        }
    }

private:
    const char *text_ = "";
    Callback on_click_;
    void *context_;
    int held_ = 0;
};

/* ---- Open and Save As ---------------------------------------------------------- */

class FileDialog : public wm::Window {
public:
    FileDialog()
        : Window("Open", {0, 0, 428, 284}, nullptr),
          look_label_({8, 12, 56, 14}, "Look &in:"),
          look_({64, 8, 270, 22}, &FileDialog::look_in, this),
          up_({340, 8, 72, 22}, "&Up", &FileDialog::up, this),
          list_({8, 38, 404, 150}, nullptr, 0, &FileDialog::selected, this),
          name_label_({8, 202, 70, 14}, "File &name:"),
          name_({80, 198, 244, 21}),
          ok_({332, 196, 80, 23}, "&Open", &FileDialog::ok, this),
          cancel_({332, 224, 80, 23}, "Cancel", &FileDialog::cancel, this)
    {
        set_dialog(true);
        add(look_label_);
        add(look_);
        add(up_);
        add(list_);
        add(name_label_);
        add(name_);
        add(ok_);
        add(cancel_);
        list_.set_icons(&FileDialog::icon);
        list_.set_on_activate(&FileDialog::activated);
        ok_.set_default(true);
        set_default(&ok_);
    }

    void show(bool save, wm::Window *owner, const char *folder, const char *name, PathFn chosen,
              void *context)
    {
        if (open()) {
            close();
        }
        save_ = save;
        chosen_ = chosen;
        context_ = context;
        set_title(save ? "Save As" : "Open");
        ok_.set_label(save ? "&Save" : "&Open");
        set_modal(owner);
        snprintf(folder_, sizeof(folder_), "%s", folder != nullptr && folder[0] ? folder : "C:\\");
        name_.set(name != nullptr ? name : "");
        refresh();
        set_frame(centred(shell::manager(), frame().w, frame().h));
        shell::manager().add(*this);
        set_focus(&name_);
        name_.select_all();
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && k.usage == wm::key::Backspace && focus() == &list_) {
            go_up();
            return true;
        }
        return Window::key(k);
    }

private:
    void refresh()
    {
        ports::DirEntry e;
        if (fs::path::relative(folder_)[0] != 0 && storage().stat(folder_, e) != ports::FsError::Ok) {
            snprintf(folder_, sizeof(folder_), "C:\\"); // gone: back to the start
        }
        listing_.read(storage(), folder_);
        list_.set_items(listing_.names(), listing_.count());
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
        const ports::FsError found = storage().stat(path, e);
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

    static void ok(wm::Button &, void *context) { static_cast<FileDialog *>(context)->try_name(); }
    static void cancel(wm::Button &, void *context) { static_cast<FileDialog *>(context)->close(); }
    static void up(wm::Button &, void *context) { static_cast<FileDialog *>(context)->go_up(); }

    static void selected(wm::ListBox &list, void *context)
    {
        auto *d = static_cast<FileDialog *>(context);
        const int i = list.selected();
        if (i >= 0 && !d->listing_.item(i).folder) {
            d->name_.set(d->listing_.item(i).name);
        }
    }

    static void activated(wm::ListBox &list, void *context)
    {
        auto *d = static_cast<FileDialog *>(context);
        const int i = list.selected();
        if (i < 0) {
            return;
        }
        char path[fs::kMaxPath];
        fs::path::join(d->folder_, d->listing_.item(i).name, path, sizeof(path));
        if (d->listing_.item(i).folder) {
            d->go_to(path);
        } else {
            d->name_.set(d->listing_.item(i).name);
            d->try_name();
        }
    }

    static const gfx::Bitmap *icon(int index, void *context)
    {
        return static_cast<FileDialog *>(context)->listing_.icon(index, false);
    }

    // The Look in list: the drives, then each folder down to this one.
    static void look_in(void *context)
    {
        auto *d = static_cast<FileDialog *>(context);
        int n = 0;
        char letters[fs::Storage::kMaxDrives];
        const int drives = storage().drives(letters, fs::Storage::kMaxDrives);
        for (int i = 0; i < drives && n < kLookMax; i++) {
            snprintf(d->look_paths_[n], sizeof(d->look_paths_[n]), "%c:\\", letters[i]);
            snprintf(d->look_labels_[n], sizeof(d->look_labels_[n]), "%c:\\", letters[i]);
            n++;
            if (letters[i] != fs::path::drive(d->folder_)) {
                continue;
            }
            // The folders of the current path, indented.
            const char *rel = fs::path::relative(d->folder_);
            int depth = 1;
            for (const char *p = rel; *p != 0 && n < kLookMax;) {
                const char *end = p;
                while (*end != 0 && *end != '\\') {
                    end++;
                }
                const int upto = static_cast<int>(end - d->folder_);
                snprintf(d->look_paths_[n], sizeof(d->look_paths_[n]), "%.*s", upto, d->folder_);
                snprintf(d->look_labels_[n], sizeof(d->look_labels_[n]), "%*s%.*s", depth * 2, "",
                         static_cast<int>(end - p), p);
                n++;
                depth++;
                p = *end ? end + 1 : end;
            }
        }
        for (int i = 0; i < n; i++) {
            d->look_items_[i] = {d->look_labels_[i], &FileDialog::look_chosen, &d->look_context_[i], nullptr,
                                 nullptr, 0};
            d->look_context_[i] = {d, i};
        }
        d->look_menu_.count = n;
        const gfx::Rect c = d->client();
        shell::manager().open_menu(d->look_menu_, c.x + d->look_.rect.x, c.y + d->look_.rect.bottom());
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
    bool save_ = false;
    PathFn chosen_ = nullptr;
    void *context_ = nullptr;
    char folder_[fs::kMaxPath] = "C:\\";
    char pending_[fs::kMaxPath] = "";
    char result_[fs::kMaxPath] = "";
    Listing listing_;
    char look_paths_[kLookMax][fs::kMaxPath];
    char look_labels_[kLookMax][48];
    LookContext look_context_[kLookMax];
    wm::MenuItem look_items_[kLookMax];
    wm::Menu look_menu_ = {look_items_, 0};

    wm::Label look_label_;
    DropDown look_;
    wm::Button up_;
    wm::ListBox list_;
    wm::Label name_label_;
    wm::TextField name_;
    wm::Button ok_, cancel_;
};

/* ---- Input dialog ---------------------------------------------------------------- */

class InputDialog : public wm::Window {
public:
    InputDialog()
        : Window("", {0, 0, 320, 120}, nullptr),
          prompt_({8, 10, 296, 14}, ""),
          field_({8, 30, 296, 21}),
          ok_({148, 62, 75, 23}, "OK", &InputDialog::ok, this),
          cancel_({229, 62, 75, 23}, "Cancel", &InputDialog::cancel, this)
    {
        set_dialog(true);
        add(prompt_);
        add(field_);
        add(ok_);
        add(cancel_);
        ok_.set_default(true);
        set_default(&ok_);
    }

    void show(wm::Window *owner, const char *title, const char *prompt, const char *initial, TextFn done,
              void *context)
    {
        if (open()) {
            close();
        }
        snprintf(title_, sizeof(title_), "%s", title);
        snprintf(prompt_text_, sizeof(prompt_text_), "%s", prompt);
        done_ = done;
        context_ = context;
        set_title(title_);
        prompt_.set_text(prompt_text_);
        field_.set(initial);
        set_modal(owner);
        set_frame(centred(shell::manager(), frame().w, frame().h));
        shell::manager().add(*this);
        set_focus(&field_);
        field_.select_all();
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

private:
    static void ok(wm::Button &, void *context)
    {
        auto *d = static_cast<InputDialog *>(context);
        d->field_.get(d->answer_, sizeof(d->answer_));
        TextFn done = d->done_;
        void *ctx = d->context_;
        d->close();
        if (done != nullptr) {
            done(d->answer_, ctx);
        }
    }
    static void cancel(wm::Button &, void *context) { static_cast<InputDialog *>(context)->close(); }

    char title_[48] = "";
    char prompt_text_[96] = "";
    char answer_[160] = "";
    TextFn done_ = nullptr;
    void *context_ = nullptr;
    wm::Label prompt_;
    wm::TextField field_;
    wm::Button ok_, cancel_;
};

// Each dialog is made when it is asked for and gone once it has closed, so
// two programs (or two Editors) can each have their own at the same time.
// A window that already has a dialog up gets that one brought forward.
bool busy(wm::Window *owner)
{
    if (owner != nullptr && owner->open() && manager().blocker(*owner) != nullptr) {
        manager().activate(*owner);
        return true;
    }
    return false;
}

void file_dialog(bool save, wm::Window *owner, const char *folder, const char *name, PathFn chosen, void *context)
{
    if (busy(owner)) {
        return;
    }
    if (FileDialog *d = make_window<FileDialog>()) {
        d->show(save, owner, folder, name, chosen, context);
        discard(d); // if the window manager had no room for it
    } else {
        no_memory_for(save ? "Save As" : "Open");
    }
}

} // namespace

void open_file_dialog(wm::Window *owner, const char *folder, PathFn chosen, void *context)
{
    file_dialog(false, owner, folder, "", chosen, context);
}

void save_file_dialog(wm::Window *owner, const char *folder, const char *name, PathFn chosen, void *context)
{
    file_dialog(true, owner, folder, name, chosen, context);
}

void input_dialog(wm::Window *owner, const char *title, const char *prompt, const char *initial, TextFn done,
                  void *context)
{
    if (busy(owner)) {
        return;
    }
    if (InputDialog *d = make_window<InputDialog>()) {
        d->show(owner, title, prompt, initial, done, context);
        discard(d); // if the window manager had no room for it
    } else {
        no_memory_for(title);
    }
}

} // namespace shell
