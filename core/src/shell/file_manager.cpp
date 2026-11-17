// The File manager: a window on a folder, in the look of the target picture
// (toolbar, address bar, icon view, status bar), and My Board (the drives).
#include <cstdio>
#include <cstring>

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
        c.fill(r, gfx::kGrey);
        if (held && inside) {
            c.edge(r, gfx::kDarkGrey, gfx::kWhite);
        }
        const int d = held && inside ? 1 : 0;
        c.blit(*icon, (place.w - 16) / 2 + d, 2 + d);
        const int tx = (place.w - app::Canvas::text_width(text)) / 2 + d;
        if (enabled) {
            c.text(tx, 20 + d, text, gfx::kBlack);
        } else {
            c.text(tx + 1, 21, text, gfx::kWhite);
            c.text(tx, 20, text, gfx::kDarkGrey);
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
            c.fill(r.inset(2), gfx::kWhite);
        } else {
            c.fill(r, gfx::kGrey);
            c.edge(r, gfx::kDarkGrey, gfx::kWhite);
        }
        app::Canvas inner = c.sub(r.inset(2));
        int x = 4;
        if (icon != nullptr) {
            inner.blit(*icon, 2, (place.h - 4 - 16) / 2);
            x = 22;
        }
        inner.text(x, (place.h - 4 - 12) / 2, text, gfx::kBlack);
    }
};

// The groove under the toolbar: a dark line over a white one. A picture, so
// the window's grey is not painted under it first.
void paint_groove(app::Canvas &c, int width)
{
    c.hline(0, 0, width, gfx::kDarkGrey);
    c.hline(0, 1, width, gfx::kWhite);
}

// The pictures of a folder's entries, as the icon view takes them.
void large_icons(const Listing &listing, const gfx::Bitmap **icons)
{
    for (int i = 0; i < listing.count(); i++) {
        icons[i] = listing.icon(i, true);
    }
}

// The folder windows there can be at once; nullptr where there is none now.
// Each is made when it opens and gone once it has closed.
constexpr int kWindows = 3;
app::Number g_windows[kWindows] = {}; // their numbers: dead once closed

class FolderWindow : public app::Window {
public:
    static constexpr int kHistory = 8;
    static constexpr int kViewTop = 64;

    FolderWindow()
        : view_(icon_view({0, kViewTop, 100, 100})),
          back_{picture({4, 0, 44, 34}, app::kMouse), {4, 0, 44, 34}, "Back", &gfx::back_icon_16()},
          up_{picture({50, 0, 44, 34}, app::kMouse), {50, 0, 44, 34}, "Up", &gfx::up_icon_16()},
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
        view_items_[0] = {"Show &hidden files", &FolderWindow::toggle_hidden, this, nullptr, nullptr, 0};
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
        view_items_[0].flags = Listing::show_hidden ? wm::kMenuChecked : 0;
        const bool at_board = folder_[0] == 0;
        const bool chosen = view_.selected() >= 0 && !at_board;
        file_items_[0].flags = view_.selected() >= 0 ? 0 : wm::kMenuDisabled;
        file_items_[2].flags = at_board ? wm::kMenuDisabled : 0;
        file_items_[3].flags = chosen ? 0 : wm::kMenuDisabled;
        file_items_[4].flags = chosen ? 0 : wm::kMenuDisabled;
    }

    void refresh()
    {
        if (folder_[0] == 0) {
            listing_.read_drives();
            set_title_text("My Board");
            address_.set_text("My Board");
            free_.set_text("");
        } else {
            ports::DirEntry e;
            if (fs::path::relative(folder_)[0] != 0 && os::file::stat(folder_, e) != ports::FsError::Ok) {
                folder_[0] = 0; // the folder went away: show My Board
                refresh();
                return;
            }
            listing_.read(folder_);
            const char *name = fs::path::name(folder_);
            if (name[0] == 0) {
                ports::VolumeInfo info = {};
                os::file::drive_info(fs::path::drive(folder_), info);
                char t[48];
                snprintf(t, sizeof(t), "%s (%c:)", info.label, fs::path::drive(folder_));
                set_title_text(t);
            } else {
                set_title_text(name);
            }
            address_.set_text(folder_);
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
        back_.set_enabled(history_count_ > 0);
        up_.set_enabled(folder_[0] != 0);
        large_icons(listing_, icons_);
        view_.set_items(listing_.names(), listing_.count(), icons_);
        status();
    }

private:
    void set_title_text(const char *t)
    {
        snprintf(title_text_, sizeof(title_text_), "%s", t);
        set_title(title_text_);
    }

    void go_up()
    {
        if (folder_[0] == 0) {
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
        if (history_count_ == 0) {
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
        const bool onto = i >= 0 && listing_.item(i).folder &&
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
    // size and when it was last written ("1,234 bytes, 2026-11-17 14:05").
    void status()
    {
        char t[96];
        const int i = view_.selected();
        if (i < 0 || i >= listing_.count() || folder_[0] == 0) {
            snprintf(t, sizeof(t), "%d object%s%s", listing_.count(), listing_.count() == 1 ? "" : "s",
                     listing_.truncated() ? " (more not shown)" : "");
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
        if (w->folder_[0] == 0) {
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
        if (w->folder_[0] == 0 || w->view_.selected() < 0) {
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

    // To the Trash on C: (asked first), for good elsewhere (trash.cpp).
    static void remove(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        char p[fs::kMaxPath];
        if (w->folder_[0] != 0 && w->selected_path(p, sizeof(p))) {
            delete_asking(p);
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
    char message_[200] = "";
    Listing listing_;
    const gfx::Bitmap *icons_[Listing::kMax] = {};
    int width_ = 100; // of the client area
    app::IconView view_;
    ToolButton back_, up_;
    app::Picture groove_;
    app::Label address_label_;
    Panel address_;
    Panel objects_, free_;
    wm::MenuItem file_items_[9];
    wm::Menu file_ = {file_items_, 9};
    wm::MenuItem view_items_[1];
    wm::Menu view_menu_ = {view_items_, 1};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&View", &view_menu_}};
    const wm::MenuBar bar_ = {entries_, 2};
};

// The Trash: what is in C:\.trash, with Restore, Delete (for good) and
// Empty Trash; files dragged onto it are moved there.
class TrashWindow : public app::Window {
public:
    TrashWindow()
        : view_(icon_view({0, 0, 100, 100})),
          status_{picture({0, 0, 100, 20}), {0, 0, 100, 20}, false, nullptr},
          free_{picture({0, 0, 100, 20}), {0, 0, 100, 20}, false, nullptr},
          empty_button_{picture({4, 0, 72, 34}, app::kMouse), {4, 0, 72, 34}, "Empty Trash", &gfx::bin_icon_16()},
          groove_(picture({0, 36, 100, 2}))
    {
        file_items_[0] = {"&Restore", &TrashWindow::restore, this, nullptr, nullptr, 0};
        file_items_[1] = {"&Delete", &TrashWindow::remove, this, nullptr, nullptr, 0};
        file_items_[2] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[3] = {"&Empty Trash", &TrashWindow::empty, this, nullptr, nullptr, 0};
        file_items_[4] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[5] = {"&Close", &TrashWindow::close_action, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        const gfx::Rect c = client_size();
        resized(c.w, c.h);
    }

    void resized(int w, int h) override
    {
        width_ = w;
        groove_.place({0, 36, w, 2});
        view_.place({0, 40, w, h - 40 - 22});
        status_.move({0, h - 20, w - 120, 20});
        free_.move({w - 118, h - 20, 118, 20});
    }

    void paint_in(app::Control which, app::Canvas &c) override
    {
        if (which == empty_button_.picture) {
            empty_button_.paint(c);
        } else if (which == groove_) {
            paint_groove(c, width_);
        } else if (which == status_.picture) {
            status_.paint(c);
        } else if (which == free_.picture) {
            free_.paint(c);
        }
    }
    void mouse(app::Control which, const wm::MouseMsg &m) override
    {
        if (which == empty_button_.picture && empty_button_.mouse(m)) {
            empty(this);
        }
    }
    // The icon view: an entry picked, opened, dragged.
    void changed(app::Control) override { status(); }
    bool chosen(app::Control) override
    {
        char p[fs::kMaxPath];
        if (selected_path(p, sizeof(p)) && !listing_.item(view_.selected()).folder) {
            open_file(p);
        }
        return true;
    }
    void drag(app::Control) override
    {
        char p[fs::kMaxPath];
        if (selected_path(p, sizeof(p))) {
            os::begin_drag(p);
        }
    }

    void refresh()
    {
        // Everything in it, hidden names too: they count as much.
        const bool hidden = Listing::show_hidden;
        Listing::show_hidden = true;
        listing_.read(kTrashFolder);
        Listing::show_hidden = hidden;
        large_icons(listing_, icons_);
        view_.set_items(listing_.names(), listing_.count(), icons_);
        view_.reset();
        empty_button_.set_enabled(listing_.count() > 0);
        ports::VolumeInfo info = {};
        os::file::drive_info('C', info);
        char t[40];
        snprintf(t, sizeof(t), "%u KB free", static_cast<unsigned>(info.free_bytes >> 10));
        free_.set_text(t);
        status();
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Delete) {
            remove(this);
            return true;
        }
        return false;
    }

    void menu_opening(int) override
    {
        const int chosen = view_.selected() >= 0 ? 0 : wm::kMenuDisabled;
        file_items_[0].flags = chosen;
        file_items_[1].flags = chosen;
        file_items_[3].flags = listing_.count() > 0 ? 0 : wm::kMenuDisabled;
    }

    bool drag_over(const char *path, int, int) override { return path != nullptr && trashable(path); }
    void dropped(const char *path, int, int) override { drop_in_trash(path); }

private:
    // "3 objects", or where the chosen one came from.
    void status()
    {
        char t[fs::kMaxPath + 8], from[fs::kMaxPath];
        const int i = view_.selected();
        if (i < 0) {
            snprintf(t, sizeof(t), "%d object%s", listing_.count(), listing_.count() == 1 ? "" : "s");
        } else if (trash_origin(listing_.item(i).name, from, sizeof(from))) {
            char folder[fs::kMaxPath];
            fs::path::parent(from, folder, sizeof(folder));
            snprintf(t, sizeof(t), "From %s", folder);
        } else {
            snprintf(t, sizeof(t), "From an unknown place");
        }
        status_.set_text(t);
    }

    bool selected_path(char *out, int capacity)
    {
        const int i = view_.selected();
        return i >= 0 && fs::path::join(kTrashFolder, listing_.item(i).name, out, capacity);
    }

    static void close_action(void *context) { static_cast<TrashWindow *>(context)->close(); }
    static void restore(void *context)
    {
        auto *w = static_cast<TrashWindow *>(context);
        const int i = w->view_.selected();
        if (i < 0) {
            return;
        }
        snprintf(w->name_, sizeof(w->name_), "%s", w->listing_.item(i).name);
        const ports::FsError e = restore_from_trash(w->name_);
        files_changed();
        if (e != ports::FsError::Ok) {
            snprintf(w->message_, sizeof(w->message_), "Cannot put back '%s':\n%s.", w->name_,
                     e == ports::FsError::NotFound ? "where it came from is not known" : fs::error_text(e));
            message_box("Restore", w->message_);
        }
    }
    static void remove(void *context)
    {
        auto *w = static_cast<TrashWindow *>(context);
        char p[fs::kMaxPath];
        if (w->selected_path(p, sizeof(p))) {
            delete_asking(p); // in the Trash: for good, asked first
        }
    }
    static void empty(void *context)
    {
        auto *w = static_cast<TrashWindow *>(context);
        const int n = w->listing_.count();
        if (n == 0) {
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
        ask("Empty Trash", w->message_, yes_no, 2, &TrashWindow::empty_answer, w);
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

    Listing listing_;
    const gfx::Bitmap *icons_[Listing::kMax] = {};
    int width_ = 100; // of the client area
    app::IconView view_;
    Panel status_, free_;
    ToolButton empty_button_; // the owner: Empty Trash in the toolbar too
    app::Picture groove_;
    char name_[fs::kMaxPath] = "";
    char message_[fs::kMaxPath + 80] = "";
    wm::MenuItem file_items_[6];
    wm::Menu file_ = {file_items_, 6};
    const wm::MenuBarEntry entries_[1] = {{"&File", &file_}};
    const wm::MenuBar bar_ = {entries_, 1};
};

app::Number g_trash = app::kNone; // the Trash window's number: dead once closed

} // namespace

void refresh_folder_windows()
{
    for (const app::Number number : g_windows) {
        if (FolderWindow *w = app::program<FolderWindow>(number)) {
            if (w->on_screen()) {
                w->refresh();
            }
        }
    }
    if (TrashWindow *trash = app::program<TrashWindow>(g_trash)) {
        if (trash->on_screen()) {
            trash->refresh();
        }
    }
}

void open_trash()
{
    const AsProgram as("File manager");
    if (TrashWindow *w = app::program<TrashWindow>(g_trash)) {
        w->refresh();
        app::bring_forward(g_trash);
        return;
    }
    app::Look look;
    look.title = "Trash";
    look.x = 120;
    look.y = 60;
    look.width = 400;
    look.height = 280;
    look.icon = &gfx::bin_icon_16();
    look.flags = app::kMenuBar;
    g_trash = app::make<TrashWindow>(look);
    if (g_trash == app::kNone) {
        no_memory_for("Trash");
        return;
    }
    app::program<TrashWindow>(g_trash)->refresh();
    app::show(g_trash);
}

void open_folder(const char *path)
{
    const AsProgram as("File manager");
    const char *want = path != nullptr ? path : "";
    // Already shown: bring that window forward.
    for (const app::Number number : g_windows) {
        FolderWindow *w = app::program<FolderWindow>(number);
        if (w != nullptr && w->on_screen() && strcmp(w->folder(), want) == 0) {
            w->refresh();
            app::bring_forward(number);
            return;
        }
    }
    // A place with no open window, else the first window is reused.
    int n = 0;
    for (int i = 0; i < kWindows; i++) {
        if (!os::ui::alive(g_windows[i])) {
            n = i;
            break;
        }
    }
    if (!os::ui::alive(g_windows[n])) {
        app::Look look;
        look.title = "My Board";
        look.x = 96 + n * 24;
        look.y = 28 + n * 24;
        look.width = 400;
        look.height = 300;
        look.icon = &gfx::folder_icon_16();
        look.flags = app::kMenuBar;
        g_windows[n] = app::make<FolderWindow>(look);
        if (g_windows[n] == app::kNone) {
            no_memory_for("File manager");
            return;
        }
    }
    FolderWindow *use = app::program<FolderWindow>(g_windows[n]);
    use->forget_history();
    use->go_to(want, false);
    app::show(g_windows[n]);
}

// What opens what, by extension: 1 the Editor, 2 the Picture viewer, 3 the
// web browser, 0 nothing.
static int program_kind(const char *path)
{
    static const char *const kText[] = {"txt", "log", "ini", "md"};
    static const char *const kPictures[] = {"bmp", "png", "jpg", "jpeg"};
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
        return 3;
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
    case 3:
        open_browser(path);
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

} // namespace shell
