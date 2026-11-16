// The Editor: a text file in a TextEdit, with File and Edit menus.
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/text_edit.hpp"
#include "core/wm/wm.hpp"
#include "internal.hpp"

namespace shell {

namespace {

constexpr size_t kTextCapacity = 512 * 1024; // the largest file it opens
constexpr size_t kUndoCapacity = 64 * 1024;

enum class Then : uint8_t { Nothing, New, Open, OpenPath, Exit };

class EditorWindow : public wm::Window {
public:
    EditorWindow()
        : Window("Untitled - Editor", {150, 60, 440, 330}, &gfx::text_icon_16()), edit_({0, 0, 100, 100})
    {
        file_items_[0] = {"&New", &EditorWindow::menu_new, this, nullptr, nullptr, 0};
        file_items_[1] = {"&Open...", &EditorWindow::menu_open, this, nullptr, nullptr, 0};
        file_items_[2] = {"&Save", &EditorWindow::menu_save, this, nullptr, nullptr, 0};
        file_items_[3] = {"Save &As...", &EditorWindow::menu_save_as, this, nullptr, nullptr, 0};
        file_items_[4] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[5] = {"E&xit", &EditorWindow::menu_exit, this, nullptr, nullptr, 0};
        edit_items_[0] = {"&Undo", &EditorWindow::menu_undo, this, nullptr, nullptr, 0};
        edit_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        edit_items_[2] = {"Cu&t", &EditorWindow::menu_cut, this, nullptr, nullptr, 0};
        edit_items_[3] = {"&Copy", &EditorWindow::menu_copy, this, nullptr, nullptr, 0};
        edit_items_[4] = {"&Paste", &EditorWindow::menu_paste, this, nullptr, nullptr, 0};
        edit_items_[5] = {"De&lete", &EditorWindow::menu_delete, this, nullptr, nullptr, 0};
        edit_items_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        edit_items_[7] = {"Select &All", &EditorWindow::menu_select_all, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        add(edit_);
        edit_.set_on_change(&EditorWindow::changed, this);
        resized();
    }

    // The text memory, from large memory, on first use.
    bool ready()
    {
        if (!edit_.attached()) {
            auto *text = static_cast<uint8_t *>(memory().allocate(kTextCapacity));
            auto *undo = static_cast<uint8_t *>(memory().allocate(kUndoCapacity));
            if (text == nullptr || undo == nullptr) {
                message_box("Editor", "There is not enough memory for the Editor.");
                return false;
            }
            edit_.attach(text, kTextCapacity, undo, kUndoCapacity);
        }
        return true;
    }

    void open_path(const char *path)
    {
        if (!ready()) {
            return;
        }
        if (path == nullptr) {
            then(Then::New);
            return;
        }
        snprintf(pending_path_, sizeof(pending_path_), "%s", path);
        then(Then::OpenPath);
    }

    void resized() override
    {
        const gfx::Rect c = client();
        edit_.rect = {0, 0, c.w, c.h};
        edit_.layout();
    }

    bool close_requested() override
    {
        then(Then::Exit);
        return false; // then() closes it once any changes are dealt with
    }

    void menu_opening(int entry) override
    {
        if (entry != 1) {
            return;
        }
        const bool sel = edit_.has_selection();
        edit_items_[0].flags = edit_.can_undo() ? 0 : wm::kMenuDisabled;
        edit_items_[2].flags = sel ? 0 : wm::kMenuDisabled;
        edit_items_[3].flags = sel ? 0 : wm::kMenuDisabled;
        edit_items_[4].flags = wm::clipboard::size() > 0 ? 0 : wm::kMenuDisabled;
        edit_items_[5].flags = sel ? 0 : wm::kMenuDisabled;
    }

private:
    // Does `what` once any unsaved changes have been saved or let go.
    void then(Then what)
    {
        if (edit_.modified() && open()) {
            next_ = what;
            static const char *const buttons[] = {"&Yes", "&No", "Cancel"};
            char text[160];
            snprintf(text, sizeof(text), "The text in %s has changed.\nDo you want to save the changes?",
                     name());
            ask("Editor", text, buttons, 3, &EditorWindow::save_answer, this);
            return;
        }
        perform(what);
    }

    static void save_answer(int choice, void *context)
    {
        auto *w = static_cast<EditorWindow *>(context);
        if (choice == 0) {
            w->save(true); // performs next_ when saved
        } else if (choice == 1) {
            w->perform(w->next_);
        }
        // Cancel: nothing.
    }

    void perform(Then what)
    {
        next_ = Then::Nothing;
        switch (what) {
        case Then::New:
            edit_.clear();
            path_[0] = 0;
            retitle();
            show(*this);
            break;
        case Then::Open:
            open_file_dialog(this, folder(), &EditorWindow::chosen_to_open, this);
            break;
        case Then::OpenPath:
            load(pending_path_);
            break;
        case Then::Exit:
            close();
            break;
        default:
            break;
        }
    }

    void load(const char *path)
    {
        fs::File f;
        ports::FsError e = storage().open(path, ports::OpenMode::Read, f);
        if (e != ports::FsError::Ok) {
            report("open", path, e);
            return;
        }
        bool fits = true;
        {
            const wm::Wait hourglass(shell::manager());
            edit_.load_begin();
            uint8_t piece[1024];
            for (size_t got = 1; got > 0 && fits && e == ports::FsError::Ok;) {
                e = storage().read(f, piece, sizeof(piece), got);
                fits = edit_.load_append(piece, got);
            }
            storage().close(f);
            edit_.load_finish();
        }
        if (!fits) {
            edit_.clear();
            message_box("Editor", "This file is too large for the Editor (512 KB at most).");
            return;
        }
        if (e != ports::FsError::Ok) {
            report("read", path, e);
        }
        snprintf(path_, sizeof(path_), "%s", path);
        retitle();
        show(*this);
        set_focus(&edit_);
    }

    void save(bool continue_after)
    {
        if (path_[0] == 0) {
            save_as(continue_after);
            return;
        }
        // The text goes to a temporary file that takes the place of the old
        // one at the end, so a power cut, a full disk or a failed write
        // leaves the document as it was last saved. Nothing else runs until
        // this returns, so the pointer shows the hourglass meanwhile.
        ports::FsError e;
        {
            const wm::Wait hourglass(shell::manager());
            fs::Save save;
            e = storage().begin_save(path_, save);
            uint8_t piece[1024];
            size_t at = 0;
            while (e == ports::FsError::Ok) {
                const size_t n = edit_.save_bytes(at, piece, sizeof(piece));
                if (n == 0) {
                    break;
                }
                e = storage().write(save.file, piece, n);
                at += n;
            }
            if (save.file.open()) {
                const ports::FsError c = storage().finish_save(save, e == ports::FsError::Ok);
                e = e != ports::FsError::Ok ? e : c;
            }
        }
        if (e != ports::FsError::Ok) {
            next_ = Then::Nothing;
            report("save", path_, e);
            return;
        }
        edit_.set_modified(false);
        retitle();
        if (continue_after) {
            perform(next_);
        }
    }

    void save_as(bool continue_after)
    {
        continue_after_save_as_ = continue_after;
        save_file_dialog(this, folder(), path_[0] ? fs::path::name(path_) : "untitled.txt",
                         &EditorWindow::chosen_to_save, this);
    }

    static void chosen_to_save(const char *path, void *context)
    {
        auto *w = static_cast<EditorWindow *>(context);
        snprintf(w->path_, sizeof(w->path_), "%s", path);
        w->save(w->continue_after_save_as_);
    }

    static void chosen_to_open(const char *path, void *context)
    {
        static_cast<EditorWindow *>(context)->load(path);
    }

    void report(const char *what, const char *path, ports::FsError e)
    {
        snprintf(message_, sizeof(message_), "Cannot %s %s:\n%s.", what, fs::path::name(path), fs::error_text(e));
        message_box("Editor", message_);
    }

    const char *name() const { return path_[0] ? fs::path::name(path_) : "Untitled"; }

    // The folder of the file, for the file dialogs (C:\Documents by default).
    const char *folder()
    {
        if (path_[0] == 0) {
            return "C:\\Documents";
        }
        fs::path::parent(path_, folder_, sizeof(folder_));
        return folder_;
    }

    // "notes.txt - Editor", with a * after the name while there are changes.
    void retitle()
    {
        // Remembered here too, so the star comes back on the first change
        // after a save.
        shown_modified_ = edit_.modified();
        snprintf(title_, sizeof(title_), "%s%s - Editor", name(), edit_.modified() ? "*" : "");
        set_title(title_);
    }

    static void changed(wm::TextEdit &edit, void *context)
    {
        auto *w = static_cast<EditorWindow *>(context);
        // Only the first change alters the title.
        if (edit.modified() != w->shown_modified_) {
            w->shown_modified_ = edit.modified();
            w->retitle();
        }
    }

    static void menu_new(void *c) { static_cast<EditorWindow *>(c)->then(Then::New); }
    static void menu_open(void *c) { static_cast<EditorWindow *>(c)->then(Then::Open); }
    static void menu_save(void *c) { static_cast<EditorWindow *>(c)->save(false); }
    static void menu_save_as(void *c) { static_cast<EditorWindow *>(c)->save_as(false); }
    static void menu_exit(void *c) { static_cast<EditorWindow *>(c)->then(Then::Exit); }
    static void menu_undo(void *c) { static_cast<EditorWindow *>(c)->edit_.undo(); }
    static void menu_cut(void *c) { static_cast<EditorWindow *>(c)->edit_.cut(); }
    static void menu_copy(void *c) { static_cast<EditorWindow *>(c)->edit_.copy(); }
    static void menu_paste(void *c) { static_cast<EditorWindow *>(c)->edit_.paste(); }
    static void menu_delete(void *c) { static_cast<EditorWindow *>(c)->edit_.delete_selection(); }
    static void menu_select_all(void *c) { static_cast<EditorWindow *>(c)->edit_.select_all(); }

    char path_[fs::kMaxPath] = "";
    char pending_path_[fs::kMaxPath] = "";
    char folder_[fs::kMaxPath] = "";
    char title_[96] = "";
    char message_[200] = "";
    Then next_ = Then::Nothing;
    bool continue_after_save_as_ = false;
    bool shown_modified_ = false;
    wm::TextEdit edit_;
    wm::MenuItem file_items_[6];
    wm::MenuItem edit_items_[8];
    wm::Menu file_ = {file_items_, 6};
    wm::Menu edit_menu_ = {edit_items_, 8};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&Edit", &edit_menu_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

EditorWindow &editor()
{
    static EditorWindow w;
    return w;
}

} // namespace

void open_in_editor(const char *path)
{
    const AsProgram as("Editor");
    editor().open_path(path);
}

} // namespace shell
