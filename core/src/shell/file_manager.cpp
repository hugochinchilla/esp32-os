// The File manager: a window on a folder, in the look of the target picture
// (toolbar, address bar, icon view, status bar), and My Board (the drives).
#include <cstdio>
#include <cstring>

#include "core/gfx/art.hpp"
#include "core/net/wifi.hpp"
#include "core/sh/sh.hpp"
#include "core/os/os.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"
#include "internal.hpp"
#include "listing.hpp"

namespace shell {

namespace {

constexpr uint16_t kF2 = 0x3B, kF5 = 0x3E;
constexpr gfx::Rgb kHighlight = {0, 0, 128};

// A flat toolbar button with an icon over its caption (target picture).
class ToolButton : public wm::Widget {
public:
    using Callback = void (*)(void *context);
    ToolButton(const gfx::Rect &r, const char *text, const gfx::Bitmap *icon, Callback fn, void *context)
        : Widget(r), text_(text), icon_(icon), fn_(fn), context_(context)
    {
    }
    void set_enabled(bool on)
    {
        if (on != enabled_) {
            enabled_ = on;
            invalidate();
        }
    }
    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.fill(r, gfx::kGrey);
        if (held_ && inside_) {
            c.edge(r, gfx::kDarkGrey, gfx::kWhite);
        }
        const int d = held_ && inside_ ? 1 : 0;
        c.blit(*icon_, (rect.w - 16) / 2 + d, 2 + d);
        const int tx = (rect.w - gfx::Canvas::text_width(text_)) / 2 + d;
        if (enabled_) {
            c.text(tx, 20 + d, text_, gfx::kBlack);
        } else {
            c.text(tx + 1, 21, text_, gfx::kWhite);
            c.text(tx, 20, text_, gfx::kDarkGrey);
        }
    }
    bool wants_mouse() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (!enabled_) {
            return;
        }
        const bool in = m.x >= 0 && m.y >= 0 && m.x < rect.w && m.y < rect.h;
        if (m.kind == wm::MouseKind::Down) {
            held_ = true;
        }
        const bool was = inside_;
        inside_ = in;
        if (m.kind == wm::MouseKind::Up) {
            held_ = false;
            invalidate();
            if (in) {
                fn_(context_);
            }
            return;
        }
        if (was != inside_ || m.kind == wm::MouseKind::Down) {
            invalidate();
        }
    }

private:
    const char *text_;
    const gfx::Bitmap *icon_;
    Callback fn_;
    void *context_;
    bool enabled_ = true, held_ = false, inside_ = false;
};

// Icons in a grid, 76 x 66 per entry as in the target picture.
class FolderView : public wm::Widget {
public:
    using Callback = void (*)(void *context);
    static constexpr int kCellW = 76, kCellH = 66;

    FolderView(const gfx::Rect &r, Listing &listing, Callback on_open, Callback on_select, Callback on_drag,
               void *context)
        : Widget(r), listing_(listing), on_open_(on_open), on_select_(on_select), on_drag_(on_drag),
          context_(context),
          bar_({0, 0, wm::ScrollBar::kThickness, 10}, true, &FolderView::scrolled, this)
    {
        bar_.set_parent(this);
    }
    int selected() const { return selected_; }
    // The entry at a point of the view, or -1.
    int item_at(int x, int y) const
    {
        const int col = (x - 2 - 10) / kCellW, row = (y - 2 - 8) / kCellH;
        const int i = (row + top_row_) * columns() + col;
        return x >= 12 && y >= 10 && col < columns() && i < listing_.count() ? i : -1;
    }
    void select(int i)
    {
        i = i < -1 ? -1 : (i >= listing_.count() ? listing_.count() - 1 : i);
        if (i == selected_) {
            return;
        }
        selected_ = i;
        if (i >= 0) {
            const int row = i / columns();
            if (row < top_row_) {
                top_row_ = row;
            } else if (row >= top_row_ + visible_rows()) {
                top_row_ = row - visible_rows() + 1;
            }
        }
        layout();
        if (on_select_ != nullptr) {
            on_select_(context_);
        }
    }
    void reset()
    {
        selected_ = -1;
        top_row_ = 0;
        layout();
    }
    void layout()
    {
        bar_.rect = {rect.w - 2 - wm::ScrollBar::kThickness, 2, wm::ScrollBar::kThickness, rect.h - 4};
        const int rows = (listing_.count() + columns() - 1) / columns();
        bar_.set_range(rows > visible_rows() ? rows - visible_rows() : 0, visible_rows());
        bar_.set_value_quietly(top_row_);
        invalidate();
    }
    int columns() const
    {
        const int c = (rect.w - 4 - wm::ScrollBar::kThickness - 8) / kCellW;
        return c < 1 ? 1 : c;
    }
    int visible_rows() const
    {
        const int r = (rect.h - 4 - 10) / kCellH;
        return r < 1 ? 1 : r;
    }

    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        c.sunken(r);
        const gfx::Rect area = {2, 2, rect.w - 4 - (bar_.maximum() > 0 ? wm::ScrollBar::kThickness : 0), rect.h - 4};
        c.fill(area, gfx::kWhite);
        gfx::Canvas inner = c.sub(area);
        const int cols = columns();
        for (int i = top_row_ * cols; i < listing_.count(); i++) {
            const int row = i / cols - top_row_, col = i % cols;
            if (row > visible_rows()) {
                break;
            }
            const int x = 10 + col * kCellW, y = 8 + row * kCellH;
            const gfx::Bitmap *icon = listing_.icon(i, true);
            const bool chosen = i == selected_;
            if (chosen) {
                inner.blit_selected(*icon, x + 22, y, gfx::rgb(0, 0, 2));
            } else {
                inner.blit(*icon, x + 22, y);
            }
            // The name under it, on two lines if it must (broken at a space
            // when there is one), cut with "..." beyond that.
            char lines[2][48];
            split_name(listing_.item(i).name, lines);
            for (int l = 0; l < 2 && lines[l][0] != 0; l++) {
                const int tw = gfx::Canvas::text_width(lines[l]);
                const int tx = x + 38 - tw / 2, ty = y + 36 + l * 12;
                if (chosen) {
                    inner.fill_rgb({tx - 1, ty - 1, tw + 2, 13}, kHighlight);
                }
                inner.text(tx, ty, lines[l], chosen ? gfx::kWhite : gfx::kBlack);
            }
        }
        if (bar_.maximum() > 0) {
            gfx::Canvas bc = c.sub(bar_.rect);
            bar_.paint(bc);
        }
    }

    bool wants_mouse() const override { return true; }
    bool focusable() const override { return true; }
    void mouse(const wm::MouseMsg &m) override
    {
        if (m.kind == wm::MouseKind::Down) {
            in_bar_ = bar_.maximum() > 0 && bar_.rect.contains(m.x, m.y);
        }
        if (in_bar_) {
            bar_.mouse({m.kind, m.x - bar_.rect.x, m.y - bar_.rect.y});
            invalidate();
            return;
        }
        if (m.kind == wm::MouseKind::Move) {
            // Held on an entry and moved a little: it is dragged.
            const int dx = m.x - press_x_, dy = m.y - press_y_;
            if (pressed_ && dx * dx + dy * dy > 16 && on_drag_ != nullptr) {
                pressed_ = false;
                on_drag_(context_);
            }
            return;
        }
        if (m.kind != wm::MouseKind::Down) {
            pressed_ = false;
            return;
        }
        const int i = item_at(m.x, m.y);
        select(i);
        pressed_ = i >= 0 && !m.double_click;
        press_x_ = m.x;
        press_y_ = m.y;
        if (i >= 0 && m.double_click && on_open_ != nullptr) {
            on_open_(context_);
        }
    }
    bool key(const wm::KeyMsg &k) override
    {
        if (!k.pressed) {
            return false;
        }
        const int cols = columns();
        switch (k.usage) {
        case wm::key::Left:
            select(selected_ < 0 ? 0 : selected_ - 1);
            return true;
        case wm::key::Right:
            select(selected_ + 1);
            return true;
        case wm::key::Up:
            select(selected_ < 0 ? 0 : (selected_ - cols >= 0 ? selected_ - cols : selected_));
            return true;
        case wm::key::Down:
            select(selected_ < 0 ? 0 : (selected_ + cols < listing_.count() ? selected_ + cols : selected_));
            return true;
        case wm::key::Home:
            select(0);
            return true;
        case wm::key::End:
            select(listing_.count() - 1);
            return true;
        case wm::key::Enter:
            if (selected_ >= 0 && on_open_ != nullptr) {
                on_open_(context_);
            }
            return true;
        default:
            return false;
        }
    }

private:
    static constexpr int kChars = (kCellW - 4) / 6; // characters per line

    // Up to kChars characters per line; byte offsets, as names are UTF-8.
    static void split_name(const char *name, char (&lines)[2][48])
    {
        lines[0][0] = lines[1][0] = 0;
        const char *p = name;
        const char *cut = nullptr, *space = nullptr;
        int n = 0;
        while (*p != 0) {
            const char *before = p;
            const uint8_t c = gfx::next_latin1(p);
            if (n == kChars) {
                cut = before;
                break;
            }
            if (c == ' ') {
                space = before;
            }
            n++;
        }
        if (cut == nullptr) {
            snprintf(lines[0], sizeof(lines[0]), "%.47s", name);
            return;
        }
        const char *second = space != nullptr ? space + 1 : cut;
        const int first_len = static_cast<int>((space != nullptr ? space : cut) - name);
        snprintf(lines[0], sizeof(lines[0]), "%.*s", first_len, name);
        // The rest, cut to a line with "..." if longer.
        const char *q = second;
        int m = 0;
        while (*q != 0 && m < kChars) {
            gfx::next_latin1(q);
            m++;
        }
        if (*q == 0) {
            snprintf(lines[1], sizeof(lines[1]), "%.47s", second);
        } else {
            const char *r = second;
            for (int k = 0; k < kChars - 3; k++) {
                gfx::next_latin1(r);
            }
            snprintf(lines[1], sizeof(lines[1]), "%.*s...", static_cast<int>(r - second), second);
        }
    }

    static void scrolled(wm::ScrollBar &bar, void *context)
    {
        auto *v = static_cast<FolderView *>(context);
        v->top_row_ = bar.value();
        v->invalidate();
    }

    Listing &listing_;
    Callback on_open_, on_select_, on_drag_;
    void *context_;
    bool pressed_ = false;
    int press_x_ = 0, press_y_ = 0;
    int selected_ = -1;
    int top_row_ = 0;
    bool in_bar_ = false;
    wm::ScrollBar bar_;
};

// A sunken box with a line of text (address bar, status bar panels).
class Panel : public wm::Widget {
public:
    Panel(const gfx::Rect &r, bool white, const gfx::Bitmap *icon = nullptr)
        : Widget(r), white_(white), icon_(icon)
    {
    }
    void set_text(const char *text)
    {
        snprintf(text_, sizeof(text_), "%.*s", static_cast<int>(sizeof(text_)) - 1, text);
        invalidate();
    }
    void paint(gfx::Canvas &c) override
    {
        const gfx::Rect r = {0, 0, rect.w, rect.h};
        if (white_) {
            c.sunken(r);
            c.fill(r.inset(2), gfx::kWhite);
        } else {
            c.fill(r, gfx::kGrey);
            c.edge(r, gfx::kDarkGrey, gfx::kWhite);
        }
        gfx::Canvas inner = c.sub(r.inset(2));
        int x = 4;
        if (icon_ != nullptr) {
            inner.blit(*icon_, 2, (rect.h - 4 - 16) / 2);
            x = 22;
        }
        inner.text(x, (rect.h - 4 - 12) / 2, text_, gfx::kBlack);
    }

private:
    bool white_;
    const gfx::Bitmap *icon_;
    char text_[fs::kMaxPath] = "";
};

// The groove under the toolbar: a dark line over a white one. A widget, so
// the window's grey is not painted under it first.
class Groove : public wm::Widget {
public:
    explicit Groove(const gfx::Rect &r) : Widget(r) {}
    void paint(gfx::Canvas &c) override
    {
        c.hline(0, 0, rect.w, gfx::kDarkGrey);
        c.hline(0, 1, rect.w, gfx::kWhite);
    }
};

void close_window(void *context) { static_cast<wm::Window *>(context)->close(); }

// The folder windows there can be at once; nullptr where there is none now.
// Each is made when it opens and gone once it has closed.
constexpr int kWindows = 3;
class FolderWindow;
FolderWindow *g_windows[kWindows] = {};

class FolderWindow : public wm::Window {
public:
    static constexpr int kHistory = 8;

    ~FolderWindow()
    {
        for (FolderWindow *&w : g_windows) {
            if (w == this) {
                w = nullptr;
            }
        }
    }

    FolderWindow()
        : Window("My Board", {96, 28, 400, 300}, &gfx::folder_icon_16()),
          back_({4, 0, 44, 34}, "Back", &gfx::back_icon_16(), &FolderWindow::back, this),
          up_({50, 0, 44, 34}, "Up", &gfx::up_icon_16(), &FolderWindow::up, this),
          groove_({0, 36, 100, 2}),
          address_label_({4, 44, 46, 14}, "Address"),
          address_({50, 40, 100, 20}, true, &gfx::folder_icon_16()),
          view_({0, 64, 100, 100}, listing_, &FolderWindow::opened, &FolderWindow::selected, &FolderWindow::dragged,
                this),
          objects_({0, 0, 100, 20}, false),
          free_({0, 0, 100, 20}, false)
    {
        add(view_);
        add(back_);
        add(up_);
        add(groove_);
        add(address_label_);
        add(address_);
        add(objects_);
        add(free_);
        file_items_[0] = {"&Open", &FolderWindow::open_selected, this, nullptr, nullptr, 0};
        file_items_[1] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[2] = {"New &Folder...", &FolderWindow::new_folder, this, nullptr, nullptr, 0};
        file_items_[3] = {"&Rename...", &FolderWindow::rename, this, nullptr, nullptr, 0};
        file_items_[4] = {"&Delete", &FolderWindow::remove, this, nullptr, nullptr, 0};
        file_items_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[6] = {"Re&fresh", &FolderWindow::refresh_action, this, nullptr, nullptr, 0};
        file_items_[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[8] = {"&Close", &close_window, this, nullptr, nullptr, 0};
        view_items_[0] = {"Show &hidden files", &FolderWindow::toggle_hidden, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        resized();
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
        if (remember && open()) {
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

    void resized() override
    {
        const gfx::Rect c = client();
        address_.rect = {50, 40, c.w - 54, 20};
        groove_.rect = {0, 36, c.w, 2};
        view_.rect = {0, 64, c.w, c.h - 64 - 22};
        objects_.rect = {0, c.h - 20, c.w - 120, 20};
        free_.rect = {c.w - 118, c.h - 20, 118, 20};
        view_.layout();
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
        return Window::key(k);
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
        view_.layout();
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
        const int i = view_.item_at(x - view_.rect.x, y - view_.rect.y);
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

    static void opened(void *context) { open_selected(context); }
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
    static void selected(void *context) { static_cast<FolderWindow *>(context)->status(); }
    static void dragged(void *context)
    {
        auto *w = static_cast<FolderWindow *>(context);
        char p[fs::kMaxPath];
        if (w->folder_[0] != 0 && w->selected_path(p, sizeof(p))) {
            os::begin_drag(p);
        }
    }
    static void back(void *context) { static_cast<FolderWindow *>(context)->go_back(); }
    static void up(void *context) { static_cast<FolderWindow *>(context)->go_up(); }
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
        input_dialog(w->id(), "New Folder", "Name of the new folder:", name, &FolderWindow::new_folder_named, w);
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
        input_dialog(w->id(), "Rename", "New name:", w->listing_.item(w->view_.selected()).name,
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
    ToolButton back_, up_;
    Groove groove_;
    wm::Label address_label_;
    Panel address_;
    FolderView view_;
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
class TrashWindow : public wm::Window {
public:
    TrashWindow()
        : Window("Trash", {120, 60, 400, 280}, &gfx::bin_icon_16()),
          view_({0, 0, 100, 100}, listing_, &TrashWindow::opened, &TrashWindow::selected, &TrashWindow::dragged, this),
          status_({0, 0, 100, 20}, false), free_({0, 0, 100, 20}, false),
          empty_button_({4, 0, 72, 34}, "Empty Trash", &gfx::bin_icon_16(), &TrashWindow::empty, this),
          groove_({0, 36, 100, 2})
    {
        add(view_);
        add(status_);
        add(free_);
        add(empty_button_);
        add(groove_);
        file_items_[0] = {"&Restore", &TrashWindow::restore, this, nullptr, nullptr, 0};
        file_items_[1] = {"&Delete", &TrashWindow::remove, this, nullptr, nullptr, 0};
        file_items_[2] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[3] = {"&Empty Trash", &TrashWindow::empty, this, nullptr, nullptr, 0};
        file_items_[4] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        file_items_[5] = {"&Close", &close_window, this, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        resized();
    }

    void resized() override
    {
        const gfx::Rect c = client();
        groove_.rect = {0, 36, c.w, 2};
        view_.rect = {0, 40, c.w, c.h - 40 - 22};
        status_.rect = {0, c.h - 20, c.w - 120, 20};
        free_.rect = {c.w - 118, c.h - 20, 118, 20};
        view_.layout();
    }

    void refresh()
    {
        // Everything in it, hidden names too: they count as much.
        const bool hidden = Listing::show_hidden;
        Listing::show_hidden = true;
        listing_.read(kTrashFolder);
        Listing::show_hidden = hidden;
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
        return Window::key(k);
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

    static void selected(void *context) { static_cast<TrashWindow *>(context)->status(); }
    static void opened(void *context)
    {
        auto *w = static_cast<TrashWindow *>(context);
        char p[fs::kMaxPath];
        if (w->selected_path(p, sizeof(p)) && !w->listing_.item(w->view_.selected()).folder) {
            open_file(p);
        }
    }
    static void dragged(void *context)
    {
        auto *w = static_cast<TrashWindow *>(context);
        char p[fs::kMaxPath];
        if (w->selected_path(p, sizeof(p))) {
            os::begin_drag(p);
        }
    }
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
    FolderView view_;
    Panel status_, free_;
    ToolButton empty_button_; // the owner: Empty Trash in the toolbar too
    Groove groove_;
    char name_[fs::kMaxPath] = "";
    char message_[fs::kMaxPath + 80] = "";
    wm::MenuItem file_items_[6];
    wm::Menu file_ = {file_items_, 6};
    const wm::MenuBarEntry entries_[1] = {{"&File", &file_}};
    const wm::MenuBar bar_ = {entries_, 1};
};

} // namespace

void refresh_folder_windows()
{
    for (FolderWindow *w : g_windows) {
        if (w != nullptr && w->open()) {
            w->refresh();
        }
    }
    if (TrashWindow *trash = the<TrashWindow>()) {
        if (trash->open()) {
            trash->refresh();
        }
    }
}

void open_trash()
{
    const AsProgram as("File manager");
    if (TrashWindow *w = one<TrashWindow>("Trash")) {
        w->refresh();
        show(*w);
        discard(w); // if the window manager had no room for it
    }
}

void open_folder(const char *path)
{
    const AsProgram as("File manager");
    const char *want = path != nullptr ? path : "";
    // Already shown: bring that window forward.
    for (FolderWindow *w : g_windows) {
        if (w != nullptr && w->open() && strcmp(w->folder(), want) == 0) {
            w->refresh();
            os::activate(*w);
            return;
        }
    }
    // A place with no open window, else the first window is reused.
    int n = 0;
    for (int i = 0; i < kWindows; i++) {
        if (g_windows[i] == nullptr || !g_windows[i]->open()) {
            n = i;
            break;
        }
    }
    if (g_windows[n] == nullptr) {
        g_windows[n] = make_window<FolderWindow>();
        if (g_windows[n] == nullptr) {
            no_memory_for("File manager");
            return;
        }
    }
    FolderWindow *use = g_windows[n];
    use->forget_history();
    use->go_to(want, false);
    if (!use->open()) {
        use->set_frame({96 + n * 24, 28 + n * 24, 400, 300});
        use->resized();
    }
    show(*use);
    discard(use); // if the window manager had no room for it
}

// What opens what, by extension: 1 the Editor, 2 the Picture viewer, 0 nothing.
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
