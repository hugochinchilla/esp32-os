// The File manager: a window on a folder, in the look of the target picture
// (toolbar, address bar, icon view, status bar), and My Board (the drives).
// The Trash is the same window on C:\.trash: there it has Restore and Empty
// Trash as well, and Delete is for good (the owner, 2026-11-17: one program,
// so that whatever the folder view gains the Trash gains too).
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/net/wifi.hpp"
#include "core/sh/sh.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "internal.hpp"
#include "listing.hpp"

namespace shell {

namespace {

constexpr uint16_t kF2 = 0x3B, kF5 = 0x3E;

// A flat toolbar button with an icon over its caption (target picture): a
// picture of the window's that takes the mouse, and what it shows.
struct ToolButton {
    app::Picture picture;
    gfx::Rect place;
    const char *text;
    const gfx::Bitmap *icon;
    bool enabled = true, held = false, inside = false;

    void set_enabled(bool on)
    {
        if (on != enabled) {
            enabled = on;
            picture.redraw();
        }
    }
    void paint(app::Canvas &c) const
    {
        const gfx::Rect r = {0, 0, place.w, place.h};
        c.fill(r, gfx::face());
        if (held && inside) {
            c.dent(r);
        }
        const int d = held && inside ? 1 : 0;
        c.blit(*icon, (place.w - 16) / 2 + d, 2 + d);
        const int tx = (place.w - app::Canvas::text_width(text)) / 2 + d;
        if (enabled) {
            c.text(tx, 20 + d, text, gfx::ink());
        } else {
            c.text(tx + 1, 21, text, gfx::paper());
            c.text(tx, 20, text, gfx::dim());
        }
    }
    // True: it was pressed and let go inside.
    bool mouse(const wm::MouseMsg &m)
    {
        if (!enabled) {
            return false;
        }
        const bool in = m.x >= 0 && m.y >= 0 && m.x < place.w && m.y < place.h;
        if (m.kind == wm::MouseKind::Down) {
            held = true;
        }
        const bool was = inside;
        inside = in;
        if (m.kind == wm::MouseKind::Up) {
            held = false;
            picture.redraw();
            return in;
        }
        if (was != inside || m.kind == wm::MouseKind::Down) {
            picture.redraw();
        }
        return false;
    }
};

// A sunken box with a line of text (address bar, status bar panels): a
// picture of the window's, and what it shows.
struct Panel {
    app::Picture picture;
    gfx::Rect place;
    bool white;
    const gfx::Bitmap *icon;
    char text[fs::kMaxPath] = "";

    void set_text(const char *to)
    {
        snprintf(text, sizeof(text), "%.*s", static_cast<int>(sizeof(text)) - 1, to);
        picture.redraw();
    }
    void move(const gfx::Rect &to)
    {
        place = to;
        picture.place(to);
    }
    void paint(app::Canvas &c) const
    {
        const gfx::Rect r = {0, 0, place.w, place.h};
        if (white) {
            c.sunken(r);
            c.fill(r.inset(2), gfx::paper());
        } else {
            c.fill(r, gfx::face());
            c.dent(r);
        }
        app::Canvas inner = c.sub(r.inset(2));
        int x = 4;
        if (icon != nullptr) {
            inner.blit(*icon, 2, (place.h - 4 - 16) / 2);
            x = 22;
        }
        inner.text(x, (place.h - 4 - 12) / 2, text, gfx::ink());
    }
};

// The groove under the toolbar: a dark line over a white one. A picture, so
// the window's grey is not painted under it first.
void paint_groove(app::Canvas &c, int width)
{
    c.hline(0, 0, width, gfx::dim());
    c.hline(0, 1, width, gfx::paper());
}

// The pictures of a folder's entries, as the icon view takes them.
void large_icons(const Listing &listing, const gfx::Bitmap **icons)
{
    for (int i = 0; i < listing.count(); i++) {
        icons[i] = listing.icon(i, true);
    }
}

// The folder windows that exist: as many as there is memory for (the
// owner's decision, 2026-11-17). Each is made when it opens and gone once it
// has closed; they are a chain through the windows themselves, which each
// joins when it is made and leaves when it is destroyed.
class FolderWindow;
FolderWindow *g_windows = nullptr;

class FolderWindow : public app::Window {
public:
    FolderWindow *next_ = nullptr;
    ~FolderWindow() override
    {
        for (FolderWindow **at = &g_windows; *at != nullptr; at = &(*at)->next_) {
            if (*at == this) {
                *at = next_;
                break;
            }
        }
    }
    static constexpr int kHistory = 8;
    static constexpr int kViewTop = 64;

    FolderWindow()
        : view_(icon_view({0, kViewTop, 100, 100})),
          back_{picture({4, 0, 44, 34}, app::kMouse), {4, 0, 44, 34}, "Back", &gfx::back_icon_16()},
          up_{picture({50, 0, 44, 34}, app::kMouse), {50, 0, 44, 34}, "Up", &gfx::up_icon_16()},
          empty_{picture({0, 0, 0, 0}, app::kMouse), kEmptyPlace, "Empty Trash", &gfx::bin_icon_16()},
          groove_(picture({0, 36, 100, 2})),
          address_label_(label({4, 44, 46, 14}, "Address")),
          address_{picture({50, 40, 100, 20}), {50, 40, 100, 20}, true, &gfx::folder_icon_16()},
          objects_{picture({0, 0, 100, 20}), {0, 0, 100, 20}, false, nullptr},
          free_{picture({0, 0, 100, 20}), {0, 0, 100, 20}, false, nullptr}
    {
        file_items_[0] = {"&Open", &FolderWindow::open_selected, this, nullptr, nullptr, 0};
        file_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[2] = {"New &Folder...", &FolderWindow::new_folder, this, nullptr, nullptr, 0};
        file_items_[3] = {"&Rename...", &FolderWindow::rename, this, nullptr, nullptr, 0};
        file_items_[4] = {"&Delete", &FolderWindow::remove, this, nullptr, nullptr, 0};
        file_items_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[6] = {"Re&fresh", &FolderWindow::refresh_action, this, nullptr, nullptr, 0};
        file_items_[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[8] = {"&Close", &FolderWindow::close_action, this, nullptr, nullptr, 0};
        // In the Trash: Restore and Empty Trash where New Folder and Rename are.
        trash_items_[0] = file_items_[0];
        trash_items_[1] = file_items_[1];
        trash_items_[2] = {"&Restore", &FolderWindow::restore, this, nullptr, nullptr, 0};
        trash_items_[3] = file_items_[4];
        trash_items_[4] = file_items_[1];
        trash_items_[5] = {"&Empty Trash", &FolderWindow::empty, this, nullptr, nullptr, 0};
        trash_items_[6] = file_items_[1];
        trash_items_[7] = file_items_[6];
        trash_items_[8] = file_items_[1];
        trash_items_[9] = file_items_[8];
        view_items_[0] = {"Show &hidden files", &FolderWindow::toggle_hidden, this, nullptr, nullptr, 0};
        // Joins the list its destructor leaves: without it no folder window
        // was found again, to be refreshed or brought forward.
        next_ = g_windows;
        g_windows = this;
        set_menu_bar(&bar_);
        set_title(title_text_);
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
    }

    const char *folder() const { return folder_; }

    void go_to(const char *folder, bool remember)
    {
        // `folder` may be folder_ itself (a refresh of the same place) or a
        // history entry about to be overwritten: copy it out first, since
        // snprintf from a buffer into itself is undefined.
        char to[fs::kMaxPath], from[fs::kMaxPath];
        snprintf(to, sizeof(to), "%s", folder != nullptr ? folder : "");
        snprintf(from, sizeof(from), "%s", folder_);
        if (remember && on_screen()) {
            if (history_count_ == kHistory) {
                memmove(history_[0], history_[1], sizeof(history_[0]) * (kHistory - 1));
                history_count_--;
            }
            snprintf(history_[history_count_++], fs::kMaxPath, "%s", from);
        }
        snprintf(folder_, sizeof(folder_), "%s", to);
        refresh();
        view_.reset();
    }

    void forget_history() { history_count_ = 0; }

    void resized(int w, int h) override
    {
        width_ = w;
        address_.move({50, 40, w - 54, 20});
        groove_.place({0, 36, w, 2});
        view_.place({0, kViewTop, w, h - kViewTop - 22});
        objects_.move({0, h - 20, w - 120, 20});
        free_.move({w - 118, h - 20, 118, 20});
    }

    void paint_in(app::Control which, app::Canvas &c) override
    {
        if (which == back_.picture) {
            back_.paint(c);
        } else if (which == up_.picture) {
            up_.paint(c);
        } else if (which == empty_.picture) {
            empty_.paint(c);
        } else if (which == groove_) {
            paint_groove(c, width_);
        } else if (which == address_.picture) {
            address_.paint(c);
        } else if (which == objects_.picture) {
            objects_.paint(c);
        } else if (which == free_.picture) {
            free_.paint(c);
        }
    }
    void mouse(app::Control which, const wm::MouseMsg &m) override
    {
        if (which == back_.picture && back_.mouse(m)) {
            go_back();
        } else if (which == up_.picture && up_.mouse(m)) {
            go_up();
        } else if (which == empty_.picture && empty_.mouse(m)) {
            empty(this);
        }
    }
    // The icon view: an entry picked, opened, dragged.
    void changed(app::Control) override { status(); }
    bool chosen(app::Control) override
    {
        open_selected(this);
        return true;
    }
    void drag(app::Control) override
    {
        char p[fs::kMaxPath];
        if (folder_[0] != 0 && selected_path(p, sizeof(p))) {
            os::begin_drag(p);
        }
    }


    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed) {
            if (k.usage == wm::key::Backspace || (k.usage == wm::key::Left && (k.mods & wm::kAlt))) {
                k.usage == wm::key::Backspace ? go_up() : go_back();
                return true;
            }
            if (k.usage == kF5) {
                refresh();
                return true;
            }
            if (k.usage == kF2) {
                rename(this);
                return true;
            }
            if (k.usage == wm::key::Delete) {
                remove(this);
                return true;
            }
        }
        return false;
    }

    // Dragged files: into the folder under the pointer, else into this one.
    bool drag_over(const char *path, int x, int y) override
    {
        char target[fs::kMaxPath];
        return path != nullptr && drop_folder(path, x, y, target, true);
    }

    void dropped(const char *path, int x, int y) override
    {
        char target[fs::kMaxPath];
        if (!drop_folder(path, x, y, target, false)) {
            return;
        }
        if (in_trash(target)) {
            drop_in_trash(path);
            return;
        }
        const ports::FsError e = move_to_folder(path, target);
        files_changed();
        if (e != ports::FsError::Ok) {
            snprintf(message_, sizeof(message_), "Cannot move '%s':\n%s.", fs::path::name(path),
                     e == ports::FsError::Exists ? "there is one with that name there already" : fs::error_text(e));
            message_box("Move", message_);
        }
    }

    void menu_opening(int) override
    {
        // In the Trash everything is shown, hidden names too: they count as much.
        const bool trash = in_trash(folder_);
        view_items_[0].flags = trash ? wm::kMenuChecked | wm::kMenuDisabled
                                     : (Listing::show_hidden ? wm::kMenuChecked : 0);
        const bool at_board = folder_[0] == 0;
        const uint8_t open = view_.selected() >= 0 ? 0 : wm::kMenuDisabled;
        const uint8_t chosen = view_.selected() >= 0 && !at_board ? 0 : wm::kMenuDisabled;
        if (at_trash()) {
            trash_items_[0].flags = open;
            trash_items_[2].flags = chosen;
            trash_items_[3].flags = chosen;
            trash_items_[5].flags = listing_.count() > 0 ? 0 : wm::kMenuDisabled;
            return;
        }
        // Nothing is made or renamed inside something thrown away.
        file_items_[0].flags = open;
        file_items_[2].flags = at_board || trash ? wm::kMenuDisabled : 0;
        file_items_[3].flags = trash ? static_cast<uint8_t>(wm::kMenuDisabled) : chosen;
        file_items_[4].flags = chosen;
    }

    void refresh()
    {
        const bool trash = at_trash();
        if (folder_[0] == 0) {
            listing_.read_drives();
            set_title_text("My Board");
            address_.set_text("My Board");
            free_.set_text("");
        } else {
            // The Trash is there also when its folder is not: it is made
            // with the first thing thrown away, and goes when emptied.
            ports::DirEntry e;
            if (!trash && fs::path::relative(folder_)[0] != 0 && os::file::stat(folder_, e) != ports::FsError::Ok) {
                folder_[0] = 0; // the folder went away: show My Board
                refresh();
                return;
            }
            const bool hidden = Listing::show_hidden;
            Listing::show_hidden = hidden || in_trash(folder_);
            listing_.read(folder_);
            Listing::show_hidden = hidden;
            const char *name = fs::path::name(folder_);
            if (trash) {
                set_title_text("Trash");
            } else if (name[0] == 0) {
                ports::VolumeInfo info = {};
                os::file::drive_info(fs::path::drive(folder_), info);
                char t[48];
                snprintf(t, sizeof(t), "%s (%c:)", info.label, fs::path::drive(folder_));
                set_title_text(t);
            } else {
                set_title_text(name);
            }
            address_.set_text(trash ? "Trash" : folder_);
            ports::VolumeInfo info = {};
            os::file::drive_info(fs::path::drive(folder_), info);
            char t[40];
            if (info.free_bytes >= 10u * 1024 * 1024) {
                snprintf(t, sizeof(t), "%u MB free", static_cast<unsigned>(info.free_bytes >> 20));
            } else {
                snprintf(t, sizeof(t), "%u KB free", static_cast<unsigned>(info.free_bytes >> 10));
            }
            free_.set_text(t);
        }
        // What only the Trash has: its icon, its File menu, Empty Trash.
        set_icon(trash ? &gfx::bin_icon_16() : &gfx::folder_icon_16());
        address_.icon = trash ? &gfx::bin_icon_16() : &gfx::folder_icon_16();
        file_.items = trash ? trash_items_ : file_items_;
        file_.count = trash ? 10 : 9;
        // Back and Up are not there in the Trash (the owner): it is a place
        // of its own, not a folder to walk out of. Empty Trash takes theirs.
        const gfx::Rect none = {0, 0, 0, 0};
        back_.picture.place(trash ? none : back_.place);
        up_.picture.place(trash ? none : up_.place);
        empty_.picture.place(trash ? kEmptyPlace : none);
        empty_.set_enabled(listing_.count() > 0);
        back_.set_enabled(history_count_ > 0);
        up_.set_enabled(folder_[0] != 0);
        large_icons(listing_, icons_);
        view_.set_items(listing_.names(), listing_.count(), icons_);
        status();
    }

private:
    static constexpr gfx::Rect kEmptyPlace = {4, 0, 72, 34}; // Empty Trash, where Back and Up are elsewhere

    // Showing the Trash itself (not a folder inside it).
    bool at_trash() const { return strcasecmp(folder_, kTrashFolder) == 0; }

    void set_title_text(const char *t)
    {
        snprintf(title_text_, sizeof(title_text_), "%s", t);
        set_title(title_text_);
    }

    void go_up()
    {
        if (folder_[0] == 0 || at_trash()) {
            return;
        }
        char parent[fs::kMaxPath];
        if (fs::path::relative(folder_)[0] == 0) {
            parent[0] = 0; // from a drive to My Board
        } else {
            fs::path::parent(folder_, parent, sizeof(parent));
        }
        go_to(parent, true);
    }

    void go_back()
    {
        if (history_count_ == 0 || at_trash()) {
            return;
        }
        char to[fs::kMaxPath];
        snprintf(to, sizeof(to), "%s", history_[--history_count_]);
        go_to(to, false);
    }

    bool selected_path(char *out, int capacity)
    {
        const int i = view_.selected();
        if (i < 0) {
            return false;
        }
        if (folder_[0] == 0) {
            snprintf(out, capacity, "%c:\\", listing_.drive(i));
            return true;
        }
        return fs::path::join(folder_, listing_.item(i).name, out, capacity);
    }

    // Where a file let go at (x, y) would go; false if nowhere (another
    // drive, the folder it is in already, inside itself).
    bool drop_folder(const char *path, int x, int y, char *target, bool show)
    {
        if (folder_[0] == 0) {
            return false;
        }
        const int i = view_.item_at(x, y - kViewTop);
        // In the Trash a file goes to the Trash, not into a folder of it.
        const bool onto = i >= 0 && listing_.item(i).folder && !in_trash(folder_) &&
                          fs::path::join(folder_, listing_.item(i).name, target, fs::kMaxPath) &&
                          strcmp(target, path) != 0;
        if (!onto) {
            snprintf(target, fs::kMaxPath, "%s", folder_);
        }
        char parent[fs::kMaxPath];
        fs::path::parent(path, parent, sizeof(parent));
        const size_t n = strlen(path);
        bool ok;
        if (in_trash(target)) {
            ok = trashable(path);
        } else {
            ok = fs::path::drive(path) == fs::path::drive(target) && strcmp(parent, target) != 0 &&
                 !(strncmp(target, path, n) == 0 && (target[n] == 0 || target[n] == '\\'));
        }
        if (show) {
            view_.select(ok && onto ? i : -1); // the folder it would go into
        }
        return ok;
    }

    // The left of the status bar: "3 objects", or about the chosen one: its
    // size and when it was last written ("1,234 bytes, 2026-11-17 14:05");
    // in the Trash, where it came from.
    void status()
    {
        char t[fs::kMaxPath + 8], from[fs::kMaxPath];
        const int i = view_.selected();
        if (i < 0 || i >= listing_.count() || folder_[0] == 0) {
            snprintf(t, sizeof(t), "%d object%s%s", listing_.count(), listing_.count() == 1 ? "" : "s",
                     listing_.truncated() ? " (more not shown)" : "");
        } else if (at_trash()) {
            if (trash_origin(listing_.item(i).name, from, sizeof(from))) {
                char folder[fs::kMaxPath];
                fs::path::parent(from, folder, sizeof(folder));
                snprintf(t, sizeof(t), "From %s", folder);
            } else {
                snprintf(t, sizeof(t), "From an unknown place");
            }
        } else {
            const Listing::Item &it = listing_.item(i);
            char when[24], size[24];
            sh::date_text(it.modified, network() != nullptr ? &network()->zone() : nullptr, when, sizeof(when));
            if (it.folder) {
                snprintf(t, sizeof(t), "%s%s", when[0] != 0 ? "Folder, " : "Folder", when);
            } else {
                // Thousands apart, as the shell's dir shows them.
                char digits[16];
                const int n = snprintf(digits, sizeof(digits), "%u", static_cast<unsigned>(it.size));
                int at = 0;
                for (int d = 0; d < n; d++) {
                    size[at++] = digits[d];
                    if ((n - d - 1) % 3 == 0 && d != n - 1) {
                        size[at++] = ',';
                    }
                }
                size[at] = 0;
                snprintf(t, sizeof(t), "%s byte%s%s%s", size, it.size == 1 ? "" : "s", when[0] != 0 ? ", " : "", when);
            }
        }
        objects_.set_text(t);
    }
    static void close_action(void *context) { static_cast<FolderWindow *>(context)->close(); }
    static void refresh_action(void *context) { static_cast<FolderWindow *>(context)->refresh(); }
    // Kept in C:\.config\files.ini.
    static void toggle_hidden(void *context)
    {
        if (in_trash(static_cast<FolderWindow *>(context)->folder_)) {
            return;
        }
        Listing::show_hidden = !Listing::show_hidden;
        fs::Ini ini;
        load_settings("files", ini);
        ini.set_bool("view", "show_hidden", Listing::show_hidden);
        save_settings("files", ini);
        static_cast<FolderWindow *>(context)->refresh();
    }

    static void open_selected(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        char p[fs::kMaxPath];
        if (!w->selected_path(p, sizeof(p))) {
            return;
        }
        const int i = w->view_.selected();
        if (w->folder_[0] == 0 || w->listing_.item(i).folder) {
            w->go_to(p, true);
        } else {
            open_file(p);
        }
    }

    static void new_folder(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        if (w->folder_[0] == 0 || in_trash(w->folder_)) {
            return;
        }
        // "New Folder", or "New Folder (2)" and so on if taken.
        char name[40] = "New Folder", p[fs::kMaxPath];
        ports::DirEntry e;
        for (int n = 2; n < 100; n++) {
            fs::path::join(w->folder_, name, p, sizeof(p));
            if (os::file::stat(p, e) != ports::FsError::Ok) {
                break;
            }
            snprintf(name, sizeof(name), "New Folder (%d)", n);
        }
        input_dialog(w->number(), "New Folder", "Name of the new folder:", name, &FolderWindow::new_folder_named, w);
    }

    static void new_folder_named(const char *name, void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        char p[fs::kMaxPath];
        const ports::FsError e = fs::path::join(w->folder_, name, p, sizeof(p)) ? os::file::make_folder(p)
                                                                                : ports::FsError::BadName;
        w->after(e, "New Folder", name);
    }

    static void rename(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        if (w->folder_[0] == 0 || in_trash(w->folder_) || w->view_.selected() < 0) {
            return;
        }
        input_dialog(w->number(), "Rename", "New name:", w->listing_.item(w->view_.selected()).name,
                     &FolderWindow::renamed, w);
    }

    static void renamed(const char *name, void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        char from[fs::kMaxPath], to[fs::kMaxPath];
        if (!w->selected_path(from, sizeof(from))) {
            return;
        }
        const ports::FsError e = fs::path::join(w->folder_, name, to, sizeof(to)) ? os::file::rename(from, to)
                                                                                 : ports::FsError::BadName;
        w->after(e, "Rename", name);
    }

    // To the Trash on C: (asked first); for good elsewhere and in the Trash
    // (trash.cpp).
    static void remove(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        char p[fs::kMaxPath];
        if (w->folder_[0] != 0 && w->selected_path(p, sizeof(p))) {
            delete_asking(p);
        }
    }

    // Back where it came from (the Trash only).
    static void restore(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        const int i = w->view_.selected();
        if (!w->at_trash() || i < 0) {
            return;
        }
        char name[fs::kMaxPath];
        snprintf(name, sizeof(name), "%s", w->listing_.item(i).name);
        const ports::FsError e = restore_from_trash(name);
        files_changed();
        if (e != ports::FsError::Ok) {
            snprintf(w->message_, sizeof(w->message_), "Cannot put back '%s':\n%s.", name,
                     e == ports::FsError::NotFound ? "where it came from is not known" : fs::error_text(e));
            message_box("Restore", w->message_);
        }
    }

    // Asks first, saying how many (the Trash only).
    static void empty(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        const int n = w->listing_.count();
        if (!w->at_trash() || n == 0) {
            return;
        }
        if (n == 1) {
            snprintf(w->message_, sizeof(w->message_),
                     "Are you sure you want to delete the item in the Trash\nfor good? It cannot be put back.");
        } else {
            snprintf(w->message_, sizeof(w->message_),
                     "Are you sure you want to delete the %d items in the Trash\nfor good? They cannot be put back.", n);
        }
        static const char *const yes_no[] = {"&Yes", "&No"};
        ask("Empty Trash", w->message_, yes_no, 2, &FolderWindow::empty_answer, nullptr);
    }
    static void empty_answer(int choice, void *)
    {
        if (choice != 0) {
            return;
        }
        const ports::FsError e = empty_trash();
        files_changed();
        if (e != ports::FsError::Ok) {
            static char text[120];
            snprintf(text, sizeof(text), "Cannot empty the Trash:\n%s.", fs::error_text(e));
            message_box("Empty Trash", text);
        }
    }

    // Refreshes after a change, or says what went wrong.
    void after(ports::FsError e, const char *what, const char *name)
    {
        refresh();
        if (e != ports::FsError::Ok) {
            snprintf(message_, sizeof(message_), "Cannot %s '%s':\n%s.", what[0] == 'N' ? "create" : (what[0] == 'R' ? "rename" : "delete"),
                     name, fs::error_text(e));
            message_box(what, message_);
            return;
        }
        // Select the new or renamed entry.
        for (int i = 0; i < listing_.count(); i++) {
            if (strcmp(listing_.item(i).name, name) == 0) {
                view_.select(i);
            }
        }
    }

    char folder_[fs::kMaxPath] = "";
    char history_[kHistory][fs::kMaxPath];
    int history_count_ = 0;
    char title_text_[64] = "My Board";
    char message_[fs::kMaxPath + 80] = "";
    Listing listing_;
    const gfx::Bitmap *icons_[Listing::kMax] = {};
    int width_ = 100; // of the client area
    app::IconView view_;
    ToolButton back_, up_;
    ToolButton empty_; // the owner: Empty Trash in the toolbar too; only in the Trash
    app::Picture groove_;
    app::Label address_label_;
    Panel address_;
    Panel objects_, free_;
    wm::MenuItem file_items_[9];
    wm::MenuItem trash_items_[10];
    wm::Menu file_ = {file_items_, 9};
    wm::MenuItem view_items_[1];
    wm::Menu view_menu_ = {view_items_, 1};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&View", &view_menu_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

} // namespace

void refresh_folder_windows()
{
    for (FolderWindow *w = g_windows; w != nullptr; w = w->next_) {
        if (w->on_screen()) {
            w->refresh();
        }
    }
}

// The Trash is the File manager on C:\.trash.
void open_trash() { open_folder(kTrashFolder); }

void open_folder(const char *path)
{
    const PathArg p(path);
    in_program("File manager", [p] {
        const char *want = p.text;
        // Already shown: bring that window forward.
        int n = 0; // how many there are: each new one a step down and to the right
        for (FolderWindow *w = g_windows; w != nullptr; w = w->next_) {
            if (w->on_screen() && strcmp(w->folder(), want) == 0) {
                w->refresh();
                app::bring_forward(w->number());
                return;
            }
            n++;
        }
        n %= 8;
        // Every other folder gets a window of its own.
        app::Look look;
        look.title = "My Board";
        look.x = 96 + n * 24;
        look.y = 28 + n * 24;
        look.width = 400;
        look.height = 300;
        look.icon = &gfx::folder_icon_16();
        look.flags = app::kMenuBar;
        const app::Number number = app::make<FolderWindow>(look);
        if (number == app::kNone) {
            no_memory_for("File manager");
            return;
        }
        FolderWindow *use = app::program<FolderWindow>(number);
        use->forget_history();
        use->go_to(want, false);
        app::show(number);
    });
}

// What opens what, by extension: 1 the Editor, 2 the Picture viewer, 3 a
// program that is loaded and run, 0 nothing.
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
    return 0;
}

bool has_program_for(const char *path) { return program_kind(path) != 0; }

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

} // namespace shell
