// The File manager: a window on a folder, in the look of the target picture
// (toolbar, address bar, icon view, status bar), and My Board (the drives).
// What a folder holds shows as icons, as a list or as a list with details
// (View menu; backlog item 56).
// The Trash is the same window on C:\.trash: there it has Restore and Empty
// Trash as well, and Delete is for good (the owner, 2026-11-17: one program,
// so that whatever the folder view gains the Trash gains too).
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/files.hpp"
#include "core/net/wifi.hpp"
#include "core/sh/sh.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "art.hpp"

namespace shell {

namespace {

constexpr uint16_t kF2 = 0x3B, kF5 = 0x3E;
constexpr uint16_t kKeyC = wm::key::A + 2, kKeyV = wm::key::A + 21, kKeyX = wm::key::A + 23;

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

// How the folder windows show what they hold: one way for all of them, kept
// in C:\.config\files.ini and read when the first of them opens.
os::ui::ViewMode g_mode = os::ui::ViewMode::Icons;
constexpr const char *kModeNames[] = {"icons", "list", "details"};

void read_mode()
{
    fs::Ini ini;
    char name[12] = "";
    load_settings("files", ini);
    ini.get("view", "mode", name, sizeof(name));
    g_mode = os::ui::ViewMode::Icons;
    for (int m = 0; m < 3; m++) {
        if (strcmp(name, kModeNames[m]) == 0) {
            g_mode = static_cast<os::ui::ViewMode>(m);
        }
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
        edit_items_[0] = {"Cu&t", &FolderWindow::cut, this, nullptr, nullptr, 0};
        edit_items_[1] = {"&Copy", &FolderWindow::copy, this, nullptr, nullptr, 0};
        edit_items_[2] = {"&Paste", &FolderWindow::paste_here, this, nullptr, nullptr, 0};
        view_items_[0] = {"&Icons", &FolderWindow::as_icons, this, nullptr, nullptr, 0};
        view_items_[1] = {"&List", &FolderWindow::as_list, this, nullptr, nullptr, 0};
        view_items_[2] = {"&Details", &FolderWindow::as_details, this, nullptr, nullptr, 0};
        view_items_[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        view_items_[4] = {"Show &hidden files", &FolderWindow::toggle_hidden, this, nullptr, nullptr, 0};
        for (int i = 0; i < Listing::kMax; i++) {
            size_cells_[i] = sizes_[i];
            kind_cells_[i] = kinds_[i];
            date_cells_[i] = dates_[i];
        }
        // The first folder window reads how they all show; the others
        // show as the ones there are.
        if (g_windows == nullptr) {
            read_mode();
        }
        view_.set_mode(g_mode);
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
    void show_as(os::ui::ViewMode mode) { view_.set_mode(mode); }

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
    // The right button in the icon view: a menu for the entry under it,
    // which is picked first, or for the folder shown when it is on none.
    // On one of several that are picked they stay picked, and the menu has
    // what goes for all of them: no Open, and no Paste into one of them.
    void right_click(app::Control which, int x, int y) override
    {
        if (!(which == view_)) {
            return;
        }
        const int i = view_.item_at(x, y);
        if (i < 0 || !view_.is_selected(i)) {
            view_.select(i);
        }
        const bool several = view_.selected_count() > 1;
        const bool board = folder_[0] == 0, trash = in_trash(folder_);
        int n = 0;
        const auto add = [&](const char *label, void (*action)(void *), bool enabled = true) {
            menu_items_[n++] = {label, action, this, nullptr, nullptr,
                                static_cast<uint8_t>(enabled ? 0 : wm::kMenuDisabled)};
        };
        const auto line = [&] { menu_items_[n++] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator}; };
        if (i >= 0) {
            if (!several) {
                add("&Open", &FolderWindow::open_selected);
            }
            if (at_trash()) {
                add("&Restore", &FolderWindow::restore);
            }
            if (!board && !trash) {
                if (n > 0) {
                    line();
                }
                add("Cu&t", &FolderWindow::cut);
                add("&Copy", &FolderWindow::copy);
                // On a folder: Paste puts things into it.
                if (!several && listing_.item(i).folder &&
                    fs::path::join(folder_, listing_.item(i).name, paste_target_, sizeof(paste_target_))) {
                    add("&Paste", &FolderWindow::paste_there, can_paste(paste_target_));
                }
            }
            if (!board) {
                if (n > 0) {
                    line();
                }
                add("&Delete", &FolderWindow::remove);
            }
        } else {
            if (at_trash()) {
                add("&Empty Trash", &FolderWindow::empty, listing_.count() > 0);
                line();
            } else if (!board && !trash) {
                snprintf(paste_target_, sizeof(paste_target_), "%s", folder_);
                add("&Paste", &FolderWindow::paste_there, can_paste(paste_target_));
                line();
                add("New &Folder...", &FolderWindow::new_folder);
            }
            add("Re&fresh", &FolderWindow::refresh_action);
        }
        menu_.count = n;
        view_.menu_at(x, y, menu_);
    }
    // The icon view: an entry picked, opened, dragged.
    void changed(app::Control) override { status(); }
    bool chosen(app::Control) override
    {
        open_selected(this);
        return true;
    }
    // All that is picked goes together: the one held first, then the others.
    void drag(app::Control) override
    {
        char p[fs::kMaxPath];
        if (folder_[0] == 0 || !selected_path(p, sizeof(p))) {
            return;
        }
        os::begin_drag(p);
        const int held = view_.selected();
        for (int i = 0; i < listing_.count(); i++) {
            if (i != held && view_.is_selected(i) && fs::path::join(folder_, listing_.item(i).name, p, sizeof(p))) {
                os::add_to_drag(p);
            }
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
            // Ctrl+X, Ctrl+C, Ctrl+V, as in the Editor.
            if ((k.mods & wm::kCtrl) && !(k.mods & wm::kAlt)) {
                if (k.usage == kKeyX || k.usage == kKeyC) {
                    k.usage == kKeyX ? cut(this) : copy(this);
                    return true;
                }
                if (k.usage == kKeyV) {
                    paste_here(this);
                    return true;
                }
            }
        }
        return false;
    }

    // Dragged files: into the folder under the pointer, else into this one.
    bool drag_over(const char *path, int x, int y) override
    {
        char target[fs::kMaxPath];
        if (path == nullptr) {
            view_.highlight(-1); // it left, or was let go
            return false;
        }
        return drop_folder(path, x, y, target, true);
    }

    // When several were dragged this comes once for each. The folders are
    // looked at again after the last, and one box says what went wrong: the
    // first that could not be moved, and how many more.
    void dropped(const char *path, int x, int y) override
    {
        char target[fs::kMaxPath];
        if (drop_folder(path, x, y, target, false)) {
            // Into the Trash too: move_to_folder() throws away what goes there.
            const ports::FsError e = move_to_folder(path, target);
            if (e != ports::FsError::Ok && drop_failed_++ == 0) {
                snprintf(message_, sizeof(message_), "Cannot move '%s':\n%s.", fs::path::name(path),
                         e == ports::FsError::Exists ? "there is one with that name there already"
                                                     : fs::error_text(e));
            }
        }
        if (os::drops_to_come() > 0) {
            return;
        }
        const int failed = drop_failed_;
        drop_failed_ = 0;
        if (failed > 1) {
            const size_t n = strlen(message_);
            snprintf(message_ + n, sizeof(message_) - n, "\n%d more could not be moved either.", failed - 1);
        }
        if (failed > 0) {
            message_box("Move", message_);
        }
    }

    void menu_opening(int) override
    {
        // In the Trash everything is shown, hidden names too: they count as much.
        const bool trash = in_trash(folder_);
        view_items_[4].flags = trash ? wm::kMenuChecked | wm::kMenuDisabled
                                     : (Listing::show_hidden ? wm::kMenuChecked : 0);
        for (int m = 0; m < 3; m++) {
            view_items_[m].flags = static_cast<int>(g_mode) == m ? wm::kMenuChecked : 0;
        }
        const bool at_board = folder_[0] == 0;
        // Open and Rename are for one; the others for all that is picked.
        const bool one = view_.selected_count() == 1;
        const uint8_t open = one ? 0 : wm::kMenuDisabled;
        const uint8_t chosen = view_.selected() >= 0 && !at_board ? 0 : wm::kMenuDisabled;
        // Nothing is copied out of the Trash or pasted into it: Restore is its way out.
        edit_items_[0].flags = edit_items_[1].flags = trash ? static_cast<uint8_t>(wm::kMenuDisabled) : chosen;
        edit_items_[2].flags = can_paste(folder_) ? 0 : wm::kMenuDisabled;
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
        file_items_[3].flags = trash || !one ? static_cast<uint8_t>(wm::kMenuDisabled) : chosen;
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
        details();
        view_.set_small_icons(small_);
        view_.set_columns(columns_, 4);
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

    // What is selected (backlog item 55: several at once): calls fn(index) for each selected entry of the
    // listing and returns how many there were. Cut, Copy, Delete and
    // Restore all come through here; Open and Rename are for one.
    template <typename F> int for_selected(F fn)
    {
        int n = 0;
        for (int i = 0; i < listing_.count(); i++) {
            if (view_.is_selected(i)) {
                fn(i);
                n++;
            }
        }
        return n;
    }

    // The same as what the file commands take. Good until the folder is
    // read again.
    Selection selection()
    {
        int n = 0;
        for_selected([&](int i) { chosen_[n++] = listing_.item(i).name; });
        return {folder_, chosen_, n};
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
            view_.highlight(ok && onto ? i : -1); // the folder it would go into
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
        const int picked = view_.selected_count();
        if (picked > 1) {
            // Several: how many, and what the files among them come to.
            uint32_t bytes = 0;
            bool files = false;
            for (int k = 0; k < listing_.count() && folder_[0] != 0; k++) {
                if (view_.is_selected(k) && !listing_.item(k).folder) {
                    bytes += listing_.item(k).size;
                    files = true;
                }
            }
            char size[24];
            with_commas(bytes, size);
            if (files) {
                snprintf(t, sizeof(t), "%d objects selected, %s byte%s", picked, size, bytes == 1 ? "" : "s");
            } else {
                snprintf(t, sizeof(t), "%d objects selected", picked);
            }
        } else if (i < 0 || i >= listing_.count() || folder_[0] == 0) {
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
            sh::date_text(it.modified, os::network() != nullptr ? &os::network()->zone() : nullptr, when, sizeof(when));
            if (it.folder) {
                snprintf(t, sizeof(t), "%s%s", when[0] != 0 ? "Folder, " : "Folder", when);
            } else {
                with_commas(it.size, size);
                snprintf(t, sizeof(t), "%s byte%s%s%s", size, it.size == 1 ? "" : "s", when[0] != 0 ? ", " : "", when);
            }
        }
        objects_.set_text(t);
    }
    // What the List and the Details show of each entry: its small picture,
    // and the texts of the columns. A file's size in bytes, what kind it
    // is, when it was last written; of a drive how much it holds and how
    // much of that is free.
    void details()
    {
        const bool board = folder_[0] == 0;
        columns_[0] = {"Name", 110, false, nullptr};
        columns_[1] = {"Size", 70, true, size_cells_};
        columns_[2] = {"Type", 72, false, kind_cells_};
        columns_[3] = {board ? "Free" : "Modified", 110, board, date_cells_};
        const net::TimeZone *zone = os::network() != nullptr ? &os::network()->zone() : nullptr;
        for (int i = 0; i < listing_.count(); i++) {
            const Listing::Item &it = listing_.item(i);
            small_[i] = listing_.icon(i, false);
            sizes_[i][0] = dates_[i][0] = 0;
            if (board) {
                ports::VolumeInfo info = {};
                os::file::drive_info(listing_.drive(i), info);
                amount(info.total_bytes, sizes_[i], sizeof(sizes_[i]));
                amount(info.free_bytes, dates_[i], sizeof(dates_[i]));
                snprintf(kinds_[i], sizeof(kinds_[i]), "%s", info.keeps_files ? "Drive" : "RAM disk");
                continue;
            }
            sh::date_text(it.modified, zone, dates_[i], sizeof(dates_[i]));
            if (it.folder) {
                snprintf(kinds_[i], sizeof(kinds_[i]), "Folder");
                continue;
            }
            char number[24];
            with_commas(it.size, number);
            snprintf(sizes_[i], sizeof(sizes_[i]), "%.13s", number);
            const char *kind = file_kind(it.name);
            char ext[8];
            fs::path::extension(it.name, ext, sizeof(ext));
            if (kind != nullptr) {
                snprintf(kinds_[i], sizeof(kinds_[i]), "%s", kind);
            } else if (ext[0] != 0) {
                for (char *c = ext; *c != 0; c++) {
                    *c = *c >= 'a' && *c <= 'z' ? static_cast<char>(*c - 32) : *c;
                }
                snprintf(kinds_[i], sizeof(kinds_[i]), "%s file", ext);
            } else {
                snprintf(kinds_[i], sizeof(kinds_[i]), "File");
            }
        }
    }
    // How much a drive holds: "16 MB", "512 KB".
    static void amount(uint64_t bytes, char *out, int capacity)
    {
        if (bytes >= 10u * 1024 * 1024) {
            snprintf(out, capacity, "%u MB", static_cast<unsigned>(bytes >> 20));
        } else {
            snprintf(out, capacity, "%u KB", static_cast<unsigned>(bytes >> 10));
        }
    }
    // Thousands apart, as the shell's dir shows them.
    static void with_commas(uint32_t number, char (&out)[24])
    {
        char digits[16];
        const int n = snprintf(digits, sizeof(digits), "%u", static_cast<unsigned>(number));
        int at = 0;
        for (int d = 0; d < n; d++) {
            out[at++] = digits[d];
            if ((n - d - 1) % 3 == 0 && d != n - 1) {
                out[at++] = ',';
            }
        }
        out[at] = 0;
    }
    static void close_action(void *context) { static_cast<FolderWindow *>(context)->close(); }
    static void refresh_action(void *context) { static_cast<FolderWindow *>(context)->refresh(); }
    // Icons, a List or Details: every folder window changes, and the next
    // ones open the same way. Kept in C:\.config\files.ini.
    static void as_icons(void *) { show_all_as(os::ui::ViewMode::Icons); }
    static void as_list(void *) { show_all_as(os::ui::ViewMode::List); }
    static void as_details(void *) { show_all_as(os::ui::ViewMode::Details); }
    static void show_all_as(os::ui::ViewMode mode)
    {
        if (mode == g_mode) {
            return;
        }
        g_mode = mode;
        fs::Ini ini;
        load_settings("files", ini);
        ini.set("view", "mode", kModeNames[static_cast<int>(mode)]);
        save_settings("files", ini);
        for (FolderWindow *w = g_windows; w != nullptr; w = w->next_) {
            w->show_as(mode);
        }
    }
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
        if (w->folder_[0] == 0 || in_trash(w->folder_) || w->view_.selected_count() != 1) {
            return; // one at a time
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
        if (w->folder_[0] != 0) {
            delete_asking(w->selection());
        }
    }

    // Onto the clipboard (file_ops.cpp). Not from My Board, whose entries
    // are drives, and not from the Trash.
    static void cut(void *context) { static_cast<FolderWindow *>(context)->to_clipboard(true); }
    static void copy(void *context) { static_cast<FolderWindow *>(context)->to_clipboard(false); }
    void to_clipboard(bool cut)
    {
        if (folder_[0] != 0 && !in_trash(folder_)) {
            copy_files(selection(), cut);
        }
    }
    // Into the folder shown; from the right-click menu, into the folder it
    // was opened on.
    static void paste_here(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        paste_files(w->folder_, w->number());
    }
    static void paste_there(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        paste_files(w->paste_target_, w->number());
    }

    // Back where it came from (the Trash only).
    static void restore(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        if (!w->at_trash()) {
            return;
        }
        // Each one; it says which was the first that could not be put back.
        ports::FsError e = ports::FsError::Ok;
        char name[fs::kMaxPath] = "";
        const int n = w->for_selected([&](int i) {
            const ports::FsError one = restore_from_trash(w->listing_.item(i).name);
            if (one != ports::FsError::Ok && e == ports::FsError::Ok) {
                e = one;
                snprintf(name, sizeof(name), "%s", w->listing_.item(i).name);
            }
        });
        if (n == 0) {
            return;
        }
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
        if (e != ports::FsError::Ok) {
            static char text[120];
            snprintf(text, sizeof(text), "Cannot empty the Trash:\n%s.", fs::error_text(e));
            message_box("Empty Trash", text);
            return;
        }
        os::play_sound(sound::Id::Crumple); // paper crumpled into a ball (backlog item 70)
    }

    // Refreshes after a change, or says what went wrong.
    void after(ports::FsError e, const char *what, const char *name)
    {
        refresh();
        if (e == ports::FsError::Exists) {
            // Nothing is ever written over by a rename or a new folder.
            snprintf(message_, sizeof(message_),
                     "There is already a file or a folder named '%s' here.\nChoose another name.", name);
            message_box(what, message_);
            return;
        }
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
    char message_[fs::kMaxPath + 120] = "";
    int drop_failed_ = 0;     // of the files being dropped here, how many could not be moved
    Listing listing_;
    const gfx::Bitmap *icons_[Listing::kMax] = {};
    // For the List and the Details: the small pictures, and the columns
    // with the text of each entry in them (details()).
    const gfx::Bitmap *small_[Listing::kMax] = {};
    char sizes_[Listing::kMax][14], kinds_[Listing::kMax][14], dates_[Listing::kMax][18];
    const char *size_cells_[Listing::kMax], *kind_cells_[Listing::kMax], *date_cells_[Listing::kMax];
    os::ui::Column columns_[4] = {};
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
    wm::MenuItem edit_items_[3];
    wm::Menu edit_ = {edit_items_, 3};
    wm::MenuItem view_items_[5];
    wm::Menu view_menu_ = {view_items_, 5};
    const wm::MenuBarEntry entries_[3] = {{"&File", &file_}, {"&Edit", &edit_}, {"&View", &view_menu_}};
    const wm::MenuBar bar_ = {entries_, 3};
    // The right-click menu, filled in each time it opens, and the folder
    // its Paste puts things into.
    wm::MenuItem menu_items_[8];
    wm::Menu menu_ = {menu_items_, 0};
    char paste_target_[fs::kMaxPath] = "";
    const char *chosen_[Listing::kMax] = {}; // selection(): the names of what is selected
};

} // namespace

// The system's word that folders changed (os::file::watch): the windows on
// those folders, or inside them, read them again. Nobody calls this after
// moving or deleting a file: the notice does.
static void folders_changed(const os::file::Changes &changed)
{
    for (FolderWindow *w = g_windows; w != nullptr; w = w->next_) {
        if (!w->on_screen() || w->folder()[0] == 0) {
            continue; // My Board lists drives, not files
        }
        // The folder itself, or one it is inside: a folder that was removed
        // or renamed is a change to the one that held it.
        char at[fs::kMaxPath];
        snprintf(at, sizeof(at), "%s", w->folder());
        for (;;) {
            if (changed.has(at)) {
                w->refresh();
                break;
            }
            char up[fs::kMaxPath];
            fs::path::parent(at, up, sizeof(up));
            if (strcmp(up, at) == 0 || strlen(at) <= 3) {
                break; // the drive's root
            }
            snprintf(at, sizeof(at), "%s", up);
        }
    }
}

// A folder in a window: the one that shows it comes forward, else it gets
// a window of its own (nullptr: My Board, the drives). It is what the
// program does when it starts and each time it is started again
// (os::accept_open).
static void open_another(const char *path)
{
    const char *want = path != nullptr ? path : "";
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
        message_box("File manager", "There is not enough memory to open the folder.");
        return;
    }
    FolderWindow *use = app::program<FolderWindow>(number);
    use->forget_history();
    use->go_to(want, false);
    app::show(number);
}

} // namespace shell

// One copy, with a window for each folder. On a task of its own where the
// machine has them: the file system's code runs on its stack too. 12 KB:
// with 8 the board measured 3,120 bytes never used after its first turn,
// and a deep folder copy or the details of a long folder had not run yet.
APP_INFO(app::file_info("File manager", 12 * 1024, art::kSmall, art::kSmallInks, art::kLarge, art::kLargeInks));

// argv[1], if there is one, is the folder to show; without it My Board.
APP_MAIN(int argc, char **argv)
{
    os::accept_open(&shell::open_another);
    os::file::watch(&shell::folders_changed);
    shell::open_another(argc > 1 ? argv[1] : nullptr);
    return 0;
}
