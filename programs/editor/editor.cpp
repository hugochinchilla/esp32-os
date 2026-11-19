// The Editor: a text file in a TextEdit, with File and Edit menus.
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/app/app.hpp"
#include "core/job/job.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/wm.hpp"
#include "art.hpp"

namespace shell {

namespace {

constexpr size_t kTextCapacity = 512 * 1024; // the largest file it opens
constexpr size_t kUndoCapacity = 64 * 1024;

enum class Then : uint8_t { Nothing, New, Open, OpenPath, Exit };

// More than one Editor can run at once, each a process of its own with its
// own window and text. As many as there is memory for (the owner's
// decision, 2026-11-17): each takes 576 KB of external RAM for its text and
// undo, and the one that finds none says so. A file that an Editor already
// has open is not opened twice: the window names its file to the system
// (set_document), which brings it forward instead.
class EditorWindow : public app::Window {
public:
    // The text and its undo memory are the text editor's own, and go back
    // with the window.
    EditorWindow() : edit_(text_edit({0, 0, 100, 100}, kTextCapacity, kUndoCapacity))
    {
        set_document(path_); // "" until it has a file
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
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
    }

    // The file it has open ("" for none).
    const char *path() const { return path_; }

    // Whether there was memory for the text when the Editor was made.
    bool ready()
    {
        if (!edit_.made()) {
            message_box("Editor", "There is not enough memory for the Editor.");
            return false;
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

    void resized(int w, int h) override { edit_.place({0, 0, w, h}); }

    bool closing() override
    {
        then(Then::Exit);
        return true; // then() closes it once any changes are dealt with
    }

    // The text went from unchanged to changed, or back: the star in the title.
    void changed(app::Control) override { retitle(); }

    void menu_opening(int entry) override
    {
        if (entry != 1) {
            return;
        }
        const bool sel = edit_.has_selection();
        edit_items_[0].flags = edit_.can_undo() ? 0 : wm::kMenuDisabled;
        edit_items_[2].flags = sel ? 0 : wm::kMenuDisabled;
        edit_items_[3].flags = sel ? 0 : wm::kMenuDisabled;
        edit_items_[4].flags = os::ui::clipboard_size() > 0 ? 0 : wm::kMenuDisabled;
        edit_items_[5].flags = sel ? 0 : wm::kMenuDisabled;
    }

private:
    // Does `what` once any unsaved changes have been saved or let go.
    void then(Then what)
    {
        if (edit_.modified() && on_screen()) {
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
            app::show(number());
            break;
        case Then::Open:
            open_file_dialog(number(), folder(), &EditorWindow::chosen_to_open, this);
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
        os::file::Handle f;
        ports::FsError e = os::file::open(path, ports::OpenMode::Read, f);
        if (e != ports::FsError::Ok) {
            report("open", path, e);
            return;
        }
        bool fits = true;
        {
            const os::Busy hourglass;
            edit_.load_begin();
            uint8_t piece[1024];
            for (size_t got = 1; got > 0 && fits && e == ports::FsError::Ok;) {
                e = os::file::read(f, piece, sizeof(piece), got);
                fits = edit_.load_append(piece, got);
            }
            os::file::close(f);
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
        app::show(number());
        set_focus(edit_);
    }

    void save(bool continue_after)
    {
        if (path_[0] == 0) {
            save_as(continue_after);
            return;
        }
        if (locked()) {
            return; // a save is already running
        }
        // The file is written by a job on a task of the Editor's own, so the
        // rest of the desktop goes on meanwhile. The job reads the text, so
        // the window is locked until it has finished: nothing can change the
        // text or close the window, and the pointer shows the hourglass over
        // it. Everything is set before start(), which on a machine without
        // tasks does the work and calls saved() before it returns.
        continue_after_save_ = continue_after;
        set_locked(true);
        job::Spec spec;
        spec.name = "save";
        spec.work = &EditorWindow::write_out;
        spec.done = &EditorWindow::saved;
        spec.context = this;
        reader_ = edit_.reader(); // for the job: good while the window is locked
        spec.stack_bytes = 8192; // the file system's calls, a 1 KB piece and the paths
        // Where the caller does the work, the whole desktop waits as it used to.
        const os::Busy hourglass;
        if (!os::start_job(spec)) {
            set_locked(false);
            next_ = Then::Nothing;
            snprintf(message_, sizeof(message_), "Cannot save %s:\nthe system is too busy. Try again in a moment.",
                     name());
            message_box("Editor", message_);
        }
    }

    // The job: on its own task. It only reads the text and the path, which
    // the locked window keeps as they are. The text goes to a temporary file
    // that takes the place of the old one at the end, so a power cut, a full
    // disk or a failed write leaves the document as it was last saved.
    static uint32_t write_out(const job::Job &job, void *context)
    {
        auto *w = static_cast<EditorWindow *>(context);
        os::file::Handle save;
        ports::FsError e = os::file::begin_save(w->path_, save, &job);
        uint8_t piece[1024];
        size_t at = 0;
        while (e == ports::FsError::Ok) {
            const size_t n = os::ui::read_text(w->reader_, at, piece, sizeof(piece));
            if (n == 0) {
                break;
            }
            e = os::file::write(save, piece, n);
            at += n;
        }
        if (save != os::file::kNoFile) {
            const ports::FsError c = os::file::finish_save(save, e == ports::FsError::Ok);
            e = e != ports::FsError::Ok ? e : c;
        }
        return static_cast<uint32_t>(e);
    }

    // Back on the UI task, when the job has finished.
    static void saved(void *context, uint32_t result, bool /*stopped*/)
    {
        auto *w = static_cast<EditorWindow *>(context);
        w->set_locked(false);
        const ports::FsError e = static_cast<ports::FsError>(result);
        if (e != ports::FsError::Ok) {
            w->next_ = Then::Nothing;
            w->report("save", w->path_, e);
            return;
        }
        w->edit_.set_modified(false);
        w->retitle();
        if (w->continue_after_save_) {
            w->perform(w->next_);
        }
    }

    void save_as(bool continue_after)
    {
        continue_after_save_as_ = continue_after;
        save_file_dialog(number(), folder(), path_[0] ? fs::path::name(path_) : "untitled.txt",
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
        snprintf(title_, sizeof(title_), "%s%s - Editor", name(), edit_.modified() ? "*" : "");
        set_title(title_);
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
    bool continue_after_save_ = false;
    os::ui::TextReader reader_ = nullptr; // the text, for the save job
    app::TextEdit edit_;
    wm::MenuItem file_items_[6];
    wm::MenuItem edit_items_[8];
    wm::Menu file_ = {file_items_, 6};
    wm::Menu edit_menu_ = {edit_items_, 8};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&Edit", &edit_menu_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

} // namespace

} // namespace shell

// The Editor is a program kept in a file (backlog item 82): not part of the
// system. Each start is another copy with its own process. 8 KB of stack:
// it reads and writes files on its own task, and the file system's code
// runs on it too.
APP_INFO(app::many(app::file_info("Editor", 8 * 1024, art::kSmall, art::kSmallInks, art::kLarge, art::kLargeInks)));

namespace {
// The window's icon: the header's own pixels are not in memory with the
// program, so they are made again here from the same rows.
constexpr app::Icon16 kWindowIcon = app::icon16(art::kSmall, art::kSmallInks);
} // namespace

// argv[1], if there is one, is the file to open.
APP_MAIN(int argc, char **argv)
{
    static const gfx::Bitmap icon = {16, 16, kWindowIcon.pixels, app::kClearInk};
    // Each new one a step down and to the right of where the last began.
    int place = 0;
    for (int i = 0; i < os::process_count(); i++) {
        const proc::Process &p = os::process_at(i);
        if (p.pid != os::self() && strcmp(p.name, "Editor") == 0 && os::main_window(p.pid) != os::kNoWindow) {
            place++;
        }
    }
    place %= 8;
    app::Look look;
    look.title = "Untitled - Editor";
    look.x = 150 + place * 24;
    look.y = 60 + place * 24;
    look.width = 440;
    look.height = 330;
    look.icon = &icon;
    look.flags = app::kMenuBar;
    // Shown once it has its text: a new one, or the file's.
    const app::Number number = app::make<shell::EditorWindow>(look);
    if (number == app::kNone) {
        shell::message_box("Editor", "There is not enough memory to open the Editor.");
        return 0; // said: the system has nothing to add
    }
    app::program<shell::EditorWindow>(number)->open_path(argc > 1 ? argv[1] : nullptr);
    if (!os::ui::on_screen(number)) {
        os::ui::close(number); // no memory for the text, the file would not load, or no room
    }
    return 0;
}
