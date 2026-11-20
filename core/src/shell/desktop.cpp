// The desktop: Start menu, desktop icons and message box.
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <strings.h>
#include "core/job/job.hpp"
#include "core/msg/queue.hpp"
#include "core/prog/prog.hpp"
#include "core/fs/ini.hpp"
#include "core/app/linked.hpp"
#include "core/gfx/art.hpp"
#include "core/desk/desk.hpp"
#include "core/os/os.hpp"
#include "core/init/init.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/text_edit.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "core/shell/listing.hpp"

namespace shell {

namespace {

wm::Wm *g_wm = nullptr;
ports::System *g_system = nullptr;
fs::Storage *g_storage = nullptr;

/* ---- Message box ---------------------------------------------------------------- */

class MessageBox : public wm::Window {
public:
    static constexpr int kMaxLines = 8;
    static constexpr int kMaxButtons = 3;

    MessageBox()
        : Window("", {0, 0, 300, 140}, nullptr),
          buttons_{{{0, 0, 75, 23}, "", &MessageBox::pressed, this},
                   {{0, 0, 75, 23}, "", &MessageBox::pressed, this},
                   {{0, 0, 75, 23}, "", &MessageBox::pressed, this}}
    {
        set_dialog(true);
        for (wm::Button &b : buttons_) {
            add(b);
        }
    }

    void ask(const char *title, const char *text, const char *const *labels, int count,
             AnswerFn answer, void *context)
    {
        if (open()) {
            answered_ = true; // the old question is dropped without an answer
            close();
        }
        snprintf(title_, sizeof(title_), "%s", title);
        snprintf(text_, sizeof(text_), "%s", text);
        answer_ = answer;
        context_ = context;
        asker_ = proc::table().current(); // the answer goes back to who asked
        answered_ = false;
        count_ = count < 1 ? 1 : (count > kMaxButtons ? kMaxButtons : count);
        // Size to the text: the longest line and the number of lines.
        int lines = 1, longest = 0, run = 0;
        for (const char *p = text_; *p != 0; p++) {
            if (*p == '\n') {
                lines++;
                run = 0;
            } else if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) {
                run++;
                longest = run > longest ? run : longest;
            }
        }
        lines = lines > kMaxLines ? kMaxLines : lines;
        const int buttons_w = count_ * 75 + (count_ - 1) * 6;
        int w = 56 + longest * 6 + 20 + 2 * kBorder;
        w = w < buttons_w + 40 ? buttons_w + 40 : w;
        w = w < 220 ? 220 : w;
        const int text_h = lines * 13;
        const int client_h = (text_h > 32 ? text_h : 32) + 20 + 23 + 12;
        const int h = kClientTop + client_h + kBorder;
        set_frame(centred(shell::manager(), w, h));
        const int x0 = (w - 2 * kBorder - buttons_w) / 2;
        for (int i = 0; i < kMaxButtons; i++) {
            buttons_[i].rect = i < count_ ? gfx::Rect{x0 + i * 81, client_h - 23 - 10, 75, 23}
                                          : gfx::Rect{-200, -200, 0, 0};
            labels_[i] = i < count_ ? labels[i] : "";
            buttons_[i].set_label(labels_[i]);
            buttons_[i].set_default(i == 0);
        }
        set_default(&buttons_[0]);
        set_focus(&buttons_[0]);
        set_title(title_);
        // Modal to the window that was in use.
        wm::Window *owner = shell::manager().active();
        set_modal(owner != this ? owner : nullptr);
        shell::manager().add(*this);
        shell::manager().discard(*this); // if there was no room: gone, unanswered
    }

    bool layered() const override { return true; } // the picture over the face
    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit(gfx::mascot_32(), 12, 12);
        char line[96];
        const char *p = text_;
        for (int y = 14; *p != 0; y += 13) {
            int n = 0;
            while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(line)) - 1) {
                line[n] = p[n];
                n++;
            }
            line[n] = 0;
            c.text(56, y, line, gfx::ink());
            p += n;
            if (*p == '\n') {
                p++;
            }
        }
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close(); // answers as Cancel (closed() below)
            return true;
        }
        return Window::key(k);
    }

    void closed() override
    {
        // Closed without a button (Escape, the close box): the last button,
        // which is Cancel or No when there is more than one.
        finish(count_ - 1);
    }

private:
    static void pressed(wm::Button &button, void *context)
    {
        auto *box = static_cast<MessageBox *>(context);
        for (int i = 0; i < box->count_; i++) {
            if (&box->buttons_[i] == &button) {
                box->chosen_ = i;
            }
        }
        box->answered_by_button_ = true;
        box->close();
    }

    void finish(int fallback)
    {
        if (answered_) {
            return;
        }
        answered_ = true;
        const int choice = answered_by_button_ ? chosen_ : fallback;
        answered_by_button_ = false;
        if (answer_ != nullptr) {
            // On the asker's own task if it has one, and not at all if it
            // has ended meanwhile or is not answering.
            const AnswerFn answer = answer_;
            void *context = context_;
            if (asker_ == proc::kNone || proc::table().alive(asker_)) {
                proc::call_as(&proc::table(), asker_, [answer, choice, context] { answer(choice, context); });
            }
        }
    }

    char title_[64] = "";
    char text_[320] = "";
    const char *labels_[kMaxButtons] = {"", "", ""};
    wm::Button buttons_[kMaxButtons];
    int count_ = 1;
    int chosen_ = 0;
    bool answered_ = true;
    bool answered_by_button_ = false;
    AnswerFn answer_ = nullptr;
    proc::Pid asker_ = proc::kNone;
    void *context_ = nullptr;
};

// The one message box there can be at a time: made when a message is
// shown, gone once it has closed. It belongs to the process that asks (a
// program's question is the program's own window). nullptr: no memory even
// for that, and nothing is shown.
MessageBox *message_window()
{
    if (the<MessageBox>() == nullptr) {
        the<MessageBox>() = make_window<MessageBox>();
    }
    return the<MessageBox>();
}

/* ---- Start menu and desktop ------------------------------------------------------ */

// Start > Programs: the programs there are (programs.cpp), the system's
// own and the program files of C:\Programs alike, read again each time the
// Start menu opens.
wm::MenuItem g_program_items[kMaxPrograms];
wm::Menu g_programs = {g_program_items, 0};

void launch_program(void *context)
{
    const Program &program = *static_cast<const Program *>(context);
    static char text[200];
    char name[app::FileInfo::kTitleSize];
    snprintf(name, sizeof(name), "%s", program.name);
    char why[120] = "";
    if (!start_program(program, nullptr, why, sizeof(why))) {
        snprintf(text, sizeof(text), "%s could not be run:\n%s.", name, why);
        message_box("Run", text);
    }
}

void find_programs(void *)
{
    const int count = program_count();
    for (int i = 0; i < count; i++) {
        const Program &p = program(i);
        g_program_items[i] = {p.name, &launch_program, const_cast<Program *>(&p), nullptr, p.icon, 0};
    }
    g_programs.count = count;
}
void open_screen_saver(void *) { screen_saver_dialog(); }
void network_panel(void *) { open_network(); }
void bluetooth_panel(void *) { open_bluetooth(); }
void open_display(void *) { display_dialog(); }
void open_services(void *) { services_dialog(); }
void open_run(void *) { run_dialog(); }
// A right click on the desktop: a menu with what can be done to it.
void toggle_snap(void *);
void realign_icons(void *);
void paste_on_desktop(void *);
wm::MenuItem g_desktop_items[] = {
    {"&Paste", &paste_on_desktop, nullptr, nullptr, nullptr, 0},
    {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator},
    {"&Snap icons to grid", &toggle_snap, nullptr, nullptr, nullptr, 0},
    {"&Realign icons", &realign_icons, nullptr, nullptr, nullptr, 0},
    {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator},
    {"&Display...", &open_display, nullptr, nullptr, nullptr, 0},
};
wm::Menu g_desktop_menu = {g_desktop_items, 6};
void desktop_menu(int icon, int x, int y, void *);
wm::MenuItem g_settings_items[] = {
    {"&Control Panel", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Taskbar...", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Network", &network_panel, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Bluetooth", &bluetooth_panel, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Screen Saver...", &open_screen_saver, nullptr, nullptr, &gfx::saver_icon_16(), 0},
    {"&Display...", &open_display, nullptr, nullptr, &gfx::display_icon_16(), 0},
    {"Ser&vices...", &open_services, nullptr, nullptr, &gfx::services_icon_16(), 0},
};
wm::Menu g_settings = {g_settings_items, 7};
wm::MenuItem g_start_items[5];
wm::Menu g_start = {g_start_items, 5};

void shut_down(void *) { shut_down_dialog(); } // the Start menu's entry, and Alt+F4 on the desktop

struct NotYet {
    const char *title;
    const char *text;
};
const NotYet kNetwork = {"Network", "The network is not available yet.\nWi-Fi comes in a later version."};

void not_yet(void *context)
{
    const NotYet *n = static_cast<const NotYet *>(context);
    message_box(n->title, n->text);
}

void open_my_board(void *)
{
    open_folder(nullptr);
}

void open_documents(void *)
{
    open_folder("C:\\Documents");
}

void open_trash_icon(void *) { open_trash(); }
void drop_on_trash(const char *path, void *) { drop_in_trash(path); }

// The system icons, then the files in C:\Documents\Desktop.
constexpr int kSystemIcons = 4;
constexpr const char *kDesktopFolder = "C:\\Documents\\Desktop";
desk::Icon g_icons[desk::Icons::kMax];
struct DesktopFile {
    char path[fs::kMaxPath];
    char label[16];
};
DesktopFile g_files[desk::Icons::kMax - kSystemIcons];
uint32_t g_files_signature = 0;
proc::Pid g_taskbar = proc::kNone; // the taskbar's process while it runs
bool g_taskbar_up = false;
// Its two surfaces (core/desk), made when it starts and destroyed when it
// ends; the memory is counted as the taskbar's.
desk::Taskbar *g_bar = nullptr;
desk::Icons *g_desk = nullptr;
// The tray icons are the network's and Bluetooth's to set (their
// services), whether or not there is a taskbar to show them at the moment.
struct TrayIcon {
    const gfx::Bitmap *icon;
    void (*click)(void *context);
    void *context;
};
TrayIcon g_tray[desk::Taskbar::kMaxTray] = {};

// Where the icons are (backlog item 41). The desktop keeps the places in
// C:\.config\icons.ini: [system] by label, [files] by the file's name, each
// "x,y", the corner of the icon's place. Nothing is written until an icon is
// first moved; from then on every icon has its place there. The file is
// written whole each time, so files that are gone leave it.
bool read_place(const fs::Ini &ini, const char *section, const char *key, int &x, int &y)
{
    char value[16];
    int px = 0, py = 0;
    if (!ini.get(section, key, value, sizeof(value)) || sscanf(value, "%d,%d", &px, &py) != 2 || px < 0 || py < 0) {
        return false;
    }
    x = px;
    y = py;
    return true;
}

void write_place(fs::Ini &ini, const char *section, const char *key, int x, int y)
{
    char value[16];
    snprintf(value, sizeof(value), "%d,%d", x, y);
    ini.set(section, key, value); // a full file: that icon is not remembered
}

// The icons panel moved some: all the places are written down.
void icons_moved(void *)
{
    fs::Ini ini;
    for (int i = 0; i < desk::Icons::kMax; i++) {
        const gfx::Rect r = g_desk->icon_rect(i);
        if (r.empty()) {
            break;
        }
        g_icons[i].x = r.x;
        g_icons[i].y = r.y;
        if (g_icons[i].path != nullptr) {
            write_place(ini, "files", fs::path::name(g_icons[i].path), r.x, r.y);
        } else {
            write_place(ini, "system", g_icons[i].label, r.x, r.y);
        }
    }
    save_settings("icons", ini);
}

void toggle_snap(void *)
{
    if (g_desk == nullptr) {
        return;
    }
    g_desk->set_snap(!g_desk->snap());
    fs::Ini ini;
    load_settings("desktop", ini);
    ini.set_bool("icons", "snap", g_desk->snap());
    save_settings("desktop", ini);
}

void realign_icons(void *)
{
    if (g_desk != nullptr) {
        g_desk->realign();
    }
}

void open_desktop_path(const char *path)
{
    ports::DirEntry e;
    if (storage().stat(path, e) == ports::FsError::Ok && e.folder) {
        open_folder(path);
    } else if (!open_file(path)) {
        message_box("Desktop", "There is no program for this kind of file yet.");
    }
}

void open_desktop_file(void *context) { open_desktop_path(static_cast<const DesktopFile *>(context)->path); }

void drop_on_folder(const char *path, void *context);

// A right click on an icon (backlog item 54): Open, and for a file or folder
// of C:\Documents\Desktop Cut, Copy and Delete; Paste on what is a folder.
// The menu keeps the icon's path, not its place in the table: the icons are
// read again every two seconds, and the menu may be open across that.
wm::MenuItem g_icon_items[7];
wm::Menu g_icon_menu = {g_icon_items, 0};
int g_menu_icon = -1;               // a system icon: its place in the table, which does not change
char g_menu_path[fs::kMaxPath];     // a file's icon: the file
char g_paste_target[fs::kMaxPath];  // the folder the icon stands for

void icon_open(void *)
{
    if (g_menu_path[0] != 0) {
        open_desktop_path(g_menu_path);
    } else if (g_menu_icon >= 0 && g_menu_icon < kSystemIcons && g_icons[g_menu_icon].open != nullptr) {
        g_icons[g_menu_icon].open(g_icons[g_menu_icon].context);
    }
}
// Several icons were picked when the menu opened (backlog item 55): the
// names of the files among them, copied for the same reason.
char g_menu_names[desk::Icons::kMax - kSystemIcons][64];
const char *g_menu_name_list[desk::Icons::kMax - kSystemIcons];
int g_menu_name_count = 0;

// What Cut, Copy and Delete of the menu act on: those, or the one file.
Selection menu_selection()
{
    if (g_menu_name_count == 0) {
        g_menu_name_list[0] = fs::path::name(g_menu_path);
        return {kDesktopFolder, g_menu_name_list, 1};
    }
    return {kDesktopFolder, g_menu_name_list, g_menu_name_count};
}
void icon_to_clipboard(bool cut) { copy_files(menu_selection(), cut); }
void icon_cut(void *) { icon_to_clipboard(true); }
void icon_copy(void *) { icon_to_clipboard(false); }
void icon_paste(void *) { paste_files(g_paste_target, os::kNoWindow); }
void icon_delete(void *)
{
    if (g_menu_name_count == 0) {
        delete_asking(g_menu_path);
    } else {
        delete_asking(menu_selection());
    }
}

void paste_on_desktop(void *)
{
    storage().make_folder("C:\\Documents");
    storage().make_folder(kDesktopFolder);
    paste_files(kDesktopFolder, os::kNoWindow);
}

void desktop_menu(int icon, int x, int y, void *)
{
    if (icon < 0) {
        g_desktop_items[0].flags = can_paste(kDesktopFolder) ? 0 : wm::kMenuDisabled;
        g_desktop_items[2].flags = g_desk != nullptr && g_desk->snap() ? wm::kMenuChecked : 0;
        manager().open_menu(g_desktop_menu, x, y);
        return;
    }
    const desk::Icon &it = g_icons[icon];
    g_menu_icon = icon;
    snprintf(g_menu_path, sizeof(g_menu_path), "%s", it.path != nullptr ? it.path : "");
    // The folders: one of the desktop's own, and Documents.
    g_paste_target[0] = 0;
    if (it.path != nullptr && it.drop == &drop_on_folder) {
        snprintf(g_paste_target, sizeof(g_paste_target), "%s", it.path);
    } else if (it.path == nullptr && icon == 1) {
        snprintf(g_paste_target, sizeof(g_paste_target), "C:\\Documents");
    }
    int n = 0;
    const auto add = [&](const char *label, void (*action)(void *), bool enabled = true) {
        g_icon_items[n++] = {label, action, nullptr, nullptr, nullptr,
                             static_cast<uint8_t>(enabled ? 0 : wm::kMenuDisabled)};
    };
    const auto line = [&] { g_icon_items[n++] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator}; };
    // On one of several picked icons: what goes for all the files among
    // them. No Open, and no Paste into one of them.
    g_menu_name_count = 0;
    if (g_desk != nullptr && g_desk->picked_count() > 1) {
        for (int i = kSystemIcons; i < desk::Icons::kMax; i++) {
            if (g_desk->picked(i) && g_icons[i].path != nullptr) {
                snprintf(g_menu_names[g_menu_name_count], sizeof(g_menu_names[0]), "%s", fs::path::name(g_icons[i].path));
                g_menu_name_list[g_menu_name_count] = g_menu_names[g_menu_name_count];
                g_menu_name_count++;
            }
        }
    }
    if (g_menu_name_count > 0) {
        add("Cu&t", &icon_cut);
        add("&Copy", &icon_copy);
        line();
        add("&Delete", &icon_delete);
        g_icon_menu.count = n;
        manager().open_menu(g_icon_menu, x, y);
        return;
    }
    add("&Open", &icon_open);
    if (it.path != nullptr || g_paste_target[0] != 0) {
        line();
    }
    if (it.path != nullptr) {
        add("Cu&t", &icon_cut);
        add("&Copy", &icon_copy);
    }
    if (g_paste_target[0] != 0) {
        add("&Paste", &icon_paste, can_paste(g_paste_target));
    }
    if (it.path != nullptr) {
        line();
        add("&Delete", &icon_delete);
    }
    g_icon_menu.count = n;
    manager().open_menu(g_icon_menu, x, y);
}

// A file let go on the desktop itself goes into C:\Documents\Desktop
// (the icons panel moves its own; these come from somewhere else). Its icon
// goes where the file was let go.
void drop_on_desktop(const char *path, int x, int y, void *)
{
    char parent[fs::kMaxPath];
    fs::path::parent(path, parent, sizeof(parent));
    if (strcasecmp(parent, kDesktopFolder) == 0) {
        return;
    }
    storage().make_folder("C:\\Documents");
    storage().make_folder(kDesktopFolder);
    const ports::FsError e = move_to_folder(path, kDesktopFolder);
    if (e == ports::FsError::Ok) {
        fs::Ini ini;
        load_settings("icons", ini);
        write_place(ini, "files", fs::path::name(path), x, y);
        save_settings("icons", ini);
    }
    if (e != ports::FsError::Ok) {
        static char text[fs::kMaxPath + 60];
        snprintf(text, sizeof(text), "Cannot move '%s':\n%s.", fs::path::name(path),
                 e == ports::FsError::Exists ? "there is one with that name there already" : fs::error_text(e));
        message_box("Move", text);
    }
}

// A file let go on a folder's icon on the desktop goes into that folder.
void drop_on_folder(const char *path, void *context)
{
    const ports::FsError e = move_to_folder(path, static_cast<const DesktopFile *>(context)->path);
    if (e != ports::FsError::Ok) {
        static char text[fs::kMaxPath + 60];
        snprintf(text, sizeof(text), "Cannot move '%s':\n%s.", fs::path::name(path),
                 e == ports::FsError::Exists ? "there is one with that name there already" : fs::error_text(e));
        message_box("Move", text);
    }
}

const gfx::Bitmap *file_picture(const char *name, bool folder)
{
    if (folder) {
        return &gfx::folder_icon_32();
    }
    char ext[8];
    fs::path::extension(name, ext, sizeof(ext));
    for (const char *e : {"txt", "log", "ini", "md"}) {
        if (strcmp(ext, e) == 0) {
            return &gfx::text_icon_32();
        }
    }
    for (const char *e : {"png", "jpg", "jpeg", "bmp", "gif"}) {
        if (strcmp(ext, e) == 0) {
            return &gfx::picture_icon_32();
        }
    }
    return &gfx::file_icon_32();
}

// What is in C:\Documents\Desktop, and whether the Trash holds anything:
// what the desktop's icons are made from. Read by list_desktop() on any
// task (a job's), shown by show_desktop() on the UI task.
struct DesktopListing {
    struct Found {
        char name[64];
        bool folder;
    };
    Found found[desk::Icons::kMax - kSystemIcons];
    int count = 0;
    bool trash_full = false;
};
// One: only one listing is made at a time (g_listing_busy).
DesktopListing g_listing;

// Any task. One pass over the desktop's folder and one look into the Trash.
// (Until 2026-11-18 each file was asked for by its index, which walks the
// folder again from the start: n + 1 walks for n files, and as many for the
// Trash, on the UI task, every 2 s.)
void list_desktop(DesktopListing &out)
{
    out.count = 0;
    constexpr int kRun = 8;
    static ports::DirEntry run[kRun]; // one listing at a time
    for (int first = 0; out.count < desk::Icons::kMax - kSystemIcons;) {
        int got = 0;
        if (storage().entries(kDesktopFolder, first, run, kRun, got) != ports::FsError::Ok) {
            break;
        }
        for (int i = 0; i < got && out.count < desk::Icons::kMax - kSystemIcons; i++) {
            if (run[i].name[0] == '.') {
                continue; // hidden
            }
            DesktopListing::Found &f = out.found[out.count++];
            static_assert(sizeof(f.name) == sizeof(run[i].name), "a name is copied whole");
            memcpy(f.name, run[i].name, sizeof(f.name));
            f.folder = run[i].folder;
        }
        if (got < kRun) {
            break;
        }
        first += got;
    }
    int in_trash = 0;
    storage().entries(kTrashFolder, 0, run, 1, in_trash);
    out.trash_full = in_trash > 0;
}

// The UI task. The icons change only if the files did.
bool g_files_shown = false; // false: show them whatever the signature says
void show_desktop(DesktopListing &l)
{
    if (!g_taskbar_up) {
        return; // no shell, no icons to keep right
    }
    DesktopListing::Found *found = l.found;
    const int n = l.count;
    // Folders first, then by name, ignoring case, as in the File manager.
    std::sort(found, found + n, [](const DesktopListing::Found &a, const DesktopListing::Found &b) {
        if (a.folder != b.folder) {
            return a.folder;
        }
        return strcasecmp(a.name, b.name) < 0;
    });
    uint32_t signature = 2166136261u;
    for (int i = 0; i < n; i++) {
        for (const char *c = found[i].name; *c != 0; c++) {
            signature = (signature ^ static_cast<uint8_t>(*c)) * 16777619u;
        }
        signature = (signature ^ (found[i].folder ? 1u : 2u)) * 16777619u;
    }
    signature ^= static_cast<uint32_t>(n);
    // The Trash's picture: with paper in it when it holds anything.
    const bool full = l.trash_full;
    signature = (signature ^ (full ? 3u : 4u)) * 16777619u;
    g_icons[3].picture = full ? &gfx::bin_full_icon_32() : &gfx::bin_icon_32();
    if (g_files_shown && signature == g_files_signature) {
        return;
    }
    g_files_shown = true;
    g_files_signature = signature;
    fs::Ini places;
    load_settings("icons", places);
    for (int i = 0; i < n; i++) {
        DesktopFile &f = g_files[i];
        if (snprintf(f.path, sizeof(f.path), "%s\\%s", kDesktopFolder, found[i].name) >= static_cast<int>(sizeof(f.path))) {
            f.path[0] = 0; // too long to open; cannot happen with 64-byte names
        }
        // Long names are cut to fit under the icon.
        if (strlen(found[i].name) > 11) {
            snprintf(f.label, sizeof(f.label), "%.9s..", found[i].name);
        } else {
            snprintf(f.label, sizeof(f.label), "%.11s", found[i].name);
        }
        g_icons[kSystemIcons + i] = {f.label, file_picture(found[i].name, found[i].folder), &open_desktop_file, &f,
                                     f.path, found[i].folder ? &drop_on_folder : nullptr};
        read_place(places, "files", found[i].name, g_icons[kSystemIcons + i].x, g_icons[kSystemIcons + i].y);
    }
    g_desk->set_icons(g_icons, kSystemIcons + n);
}

// The desktop's folder or the Trash changed: the folder is read again by a
// job, so that the desktop goes on meanwhile (on the board's flash drive the
// reading is longer than a video frame), and the icons are set when the job
// comes back. One job at a time; a change that comes while it reads has it
// read once more. The job is the desktop's (PID of "desktop"), which never
// ends, so nothing waits for it.
bool g_listing_busy = false;  // a job is reading into g_listing
bool g_listing_again = false; // something changed while it read
bool g_listing_owed = false;  // no job could be started: tried again by desktop_files_tick

void desktop_changed();

uint32_t list_work(const job::Job &, void *)
{
    list_desktop(g_listing);
    return 0;
}

void listed(void *, uint32_t, bool)
{
    g_listing_busy = false;
    if (g_listing_again) {
        g_listing_again = false;
        desktop_changed(); // what it read may already be old
        return;
    }
    show_desktop(g_listing);
}

// The UI task, as any process.
void desktop_changed()
{
    if (!g_taskbar_up) {
        return;
    }
    if (g_listing_busy) {
        g_listing_again = true;
        return;
    }
    const proc::Scope as(&proc::table(), proc::table().find_by_name("desktop"));
    job::Spec spec;
    spec.name = "desktop";
    spec.work = &list_work;
    spec.done = &listed;
    spec.stack_bytes = 8192; // the file system's calls for a listing
    // Where there are no tasks the work, and `listed`, are over when this
    // returns.
    g_listing_busy = true;
    g_listing_owed = false;
    if (!job::jobs().start(spec)) {
        g_listing_busy = false;
        g_listing_owed = true;
    }
}

// When the taskbar starts: read here and now, so that the first picture of
// the desktop has its icons.
void show_desktop_now()
{
    g_files_shown = false;
    if (g_listing_busy) {
        g_listing_again = true; // g_listing is the job's until it is back
        return;
    }
    list_desktop(g_listing);
    show_desktop(g_listing);
}

/* ---- Change notices -------------------------------------------------------------- */

// fs::Storage says which folder changed (Storage::set_changed), on whatever
// task changed it. Here that becomes work on the UI task: the folders are
// kept as keys (fs::path::key) in a few slots, a message to the desktop says
// there is something to look at, and settle() then has the desktop read its
// folder again if it or the Trash is among them, and the File manager its
// windows on those folders. This took the place of the 2 s timer that read
// the desktop's folder whether or not anything had changed, and of the
// files_changed() calls after each move and delete
// (docs/journal/2026-11-18-change-notices.md).
constexpr int kChangeSlots = 8;
std::atomic<uint32_t> g_changed[kChangeSlots]; // 0: free
std::atomic<bool> g_changed_all{false};        // more folders than slots: every one
std::atomic<bool> g_change_told{false};        // a message is on its way, or a timer set
// With a queue, other tasks can bring changes at any pace (a paste of a
// hundred files): after settling, nothing is settled again for this long.
constexpr uint32_t kSettleGapUs = 250000;
bool g_settle_wait = false; // the UI task's: inside that gap

void settle();

void settle_later(void *)
{
    g_settle_wait = false;
    if (g_change_told.load()) {
        settle();
    }
}

void change_message(const msg::Message &)
{
    if (!g_settle_wait) {
        settle();
    }
}

// The UI task, as the desktop.
void settle()
{
    g_change_told.store(false); // from here on a change sends word again
    FolderKeys keys;
    keys.all = g_changed_all.exchange(false);
    for (std::atomic<uint32_t> &slot : g_changed) {
        const uint32_t key = slot.exchange(0);
        if (key != 0) {
            keys.key[keys.count++] = key;
        }
    }
    if (g_listing_owed || keys.has(kDesktopFolder) || keys.has(kTrashFolder)) {
        desktop_changed();
    }
    if (keys.all || keys.count > 0) {
        // A program that shows folders (the File manager) reads them
        // again itself: its windows are its own to touch.
        tell_watchers(keys);
    }
    if (msg::queue() != nullptr && !g_settle_wait) {
        g_settle_wait = os::start_timer(kSettleGapUs, 0, &settle_later, nullptr) != 0;
    }
}

// Any task, and in the middle of whatever changed the folder: nothing is
// read or painted here.
void folder_changed(const char *folder, void *)
{
    uint32_t key = fs::path::key(folder);
    key = key != 0 ? key : 1; // 0 means a free slot
    bool kept = false;
    for (std::atomic<uint32_t> &slot : g_changed) {
        uint32_t free_slot = 0;
        if (slot.load() == key || slot.compare_exchange_strong(free_slot, key) || free_slot == key) {
            kept = true;
            break;
        }
    }
    if (!kept) {
        g_changed_all.store(true);
    }
    if (g_change_told.exchange(true)) {
        return;
    }
    const proc::Pid desktop = proc::table().find_by_name("desktop");
    if (msg::queue() != nullptr) {
        if (!msg::post(desktop, &change_message)) {
            g_change_told.store(false); // no room: the next change, or the tick, tries again
        }
        return;
    }
    // A machine with no queue has one task: this is it, in the middle of a
    // program's work. A timer of the desktop's, due at once, is the same
    // "between two rounds".
    const proc::Scope as(&proc::table(), desktop);
    if (os::start_timer(0, 0, [](void *) { settle(); }, nullptr) == 0) {
        g_change_told.store(false);
    }
}

// Every 2 s, and nearly always nothing: a notice whose message found the
// queue full, or a listing whose job could not be started, is picked up
// here. It reads no folder itself.
void desktop_files_tick(void *)
{
    bool waiting = g_listing_owed || g_changed_all.load();
    for (const std::atomic<uint32_t> &slot : g_changed) {
        waiting = waiting || slot.load() != 0;
    }
    if (waiting && !g_settle_wait && !g_change_told.load()) {
        const proc::Scope as(&proc::table(), proc::table().find_by_name("desktop"));
        settle();
    }
}

} // namespace

namespace {

// Drives formatted before 2026-11-16 keep documents in C:\docs; they move
// into C:\Documents once (the owner's choice), numbered on a name clash,
// never overwritten. C:\.config\desktop.ini remembers it was done.
void move_old_documents()
{
    fs::Ini ini;
    load_settings("desktop", ini);
    if (ini.get_bool("documents", "moved", false)) {
        return;
    }
    const char *kOld = "C:\\docs";
    ports::DirEntry e;
    bool all = true;
    if (storage().stat(kOld, e) == ports::FsError::Ok && e.folder) {
        storage().make_folder("C:\\Documents");
        // Entry i stays put when a move fails, so step past it.
        for (int i = 0; storage().entry(kOld, i, e) == ports::FsError::Ok;) {
            char from[fs::kMaxPath], name[fs::kMaxPath], to[fs::kMaxPath];
            const bool named = fs::path::join(kOld, e.name, from, sizeof(from)) &&
                               free_name_in("C:\\Documents", e.name, e.folder, name, sizeof(name)) &&
                               fs::path::join("C:\\Documents", name, to, sizeof(to));
            if (!named || storage().rename(from, to) != ports::FsError::Ok) {
                all = false;
                i++;
            }
        }
        if (all) {
            storage().remove(kOld);
        }
    }
    if (all) {
        ini.set_bool("documents", "moved", true);
        save_settings("desktop", ini);
    }
}

// The welcome text, put on the desktop once: if it is deleted it does not
// come back (C:\.config\desktop.ini remembers).
void welcome_file()
{
    fs::Ini ini;
    load_settings("desktop", ini);
    if (ini.get_bool("welcome", "created", false)) {
        return;
    }
    storage().make_folder("C:\\Documents");
    storage().make_folder(kDesktopFolder);
    static const char kText[] =
        "Welcome to Chinchilla\r\n"
        "=====================\r\n"
        "\r\n"
        "Chinchilla is a small desktop in the style of the late 1990s, running on\r\n"
        "an ESP32-S3 with a VGA monitor. Everything you see is drawn by the board\r\n"
        "itself, 640 x 480 in 256 colours.\r\n"
        "\r\n"
        "Getting around\r\n"
        "  - Start (bottom left) > Programs lists the programs.\r\n"
        "  - Double-click an icon on the desktop to open it. Files you put in\r\n"
        "    C:\\Documents\\Desktop show up here as icons.\r\n"
        "  - My Board shows the drives; C: is the flash disk and keeps your files.\r\n"
        "\r\n"
        "Keys\r\n"
        "  - Ctrl+Alt+Del opens the Task Manager.\r\n"
        "  - Alt+F4 closes a window. Alt and a letter opens a menu.\r\n"
        "  - A key during start-up goes straight to the desktop.\r\n"
        "\r\n"
        "Settings\r\n"
        "  Each program keeps its settings in a plain text file in C:\\.config\r\n"
        "  (for example boot.ini, network.ini, viewer.ini). The File manager shows\r\n"
        "  that folder with View > Show hidden files. You can edit them here,\r\n"
        "  in the Editor.\r\n"
        "\r\n"
        "You can delete this file; it will not come back.\r\n";
    storage().write_file("C:\\Documents\\Desktop\\welcome.txt", kText, sizeof(kText) - 1);
    ini.set_bool("welcome", "created", true);
    save_settings("desktop", ini);
}

} // namespace

// A program's memory is the OS's to give and to count (core/os/os.hpp).
wm::Window *window_numbered(int number) { return g_wm != nullptr ? g_wm->window(number) : nullptr; }

bool has_dialog(int number)
{
    const prog::InCall in_call;
    wm::Window *w = window_numbered(number);
    if (w == nullptr || g_wm->blocker(*w) == nullptr) {
        return false;
    }
    os::bring_forward(number);
    return true;
}

void *program_memory(size_t bytes) { return os::allocate(bytes); }
void free_program_memory(void *block) { os::release(block); }
ports::Memory &program_port() { return os::memory(); }

void no_memory_for(const char *program)
{
    const prog::InCall in_call;
    char text[96]; // the box keeps its own copy
    snprintf(text, sizeof(text), "There is not enough memory to open\n%s.", program);
    message_box("Chinchilla", text);
    os::play_sound(sound::Id::Error); // in place of the box's own knock
}

void startup_trouble(int lines)
{
    if (lines <= 0 || g_wm == nullptr) {
        return;
    }
    char text[160]; // the box keeps its own copy
    snprintf(text, sizeof(text),
             "%d line%s of C:\\.config\\startup.txt could not\nbe done. Type dmesg in a Terminal to see which.", lines,
             lines == 1 ? "" : "s");
    const proc::Scope as(&proc::table(), proc::table().find_by_name("desktop"));
    message_box("Start-up", text);
}

wm::Wm &manager() { return *g_wm; }
ports::System &system_port() { return *g_system; }
fs::Storage &storage() { return *g_storage; }

void show(wm::Window &w)
{
    const prog::InCall in_call;
    if (w.open()) {
        manager().activate(w);
    } else {
        manager().add(w);
    }
}

gfx::Rect centred(const wm::Wm &wm, int w, int h)
{
    const gfx::Rect area = wm.work_area();
    return {(area.w - w) / 2, (area.h - h) / 2, w, h};
}

void message_box(const char *title, const char *text)
{
    const prog::InCall in_call;
    static const char *const ok[] = {"OK"};
    if (MessageBox *box = message_window()) {
        box->ask(title, text, ok, 1, nullptr, nullptr);
        os::play_sound(sound::Id::Alert);
    }
}

void ask(const char *title, const char *text, const char *const *buttons, int count, AnswerFn answer,
         void *context)
{
    const prog::InCall in_call;
    if (MessageBox *box = message_window()) {
        box->ask(title, text, buttons, count, answer, context);
        os::play_sound(sound::Id::Alert);
    }
}


namespace {

template <class T> void destroy(T *&object)
{
    if (object != nullptr) {
        object->~T(); // gives its place on the screen up
        os::release(object);
        object = nullptr;
    }
}

// The taskbar's end, for TERM and for KILL alike (it has nothing to ask):
// its two surfaces leave the screen and are destroyed, the Start menu is
// taken back, its windows and timers go with its row, and the table starts
// it again.
void stop_taskbar(proc::Pid pid, proc::Signal, void *)
{
    wm::Wm &wm = manager();
    g_taskbar_up = false;
    g_taskbar = proc::kNone;
    wm.set_start_menu(nullptr, nullptr);
    wm.set_start_menu_opening(nullptr, nullptr);
    g_programs.count = 0;
    forget_programs();
    destroy(g_bar);
    destroy(g_desk);
    wm.end(pid);
}

// The desktop shell: the taskbar with the Start menu and the quick-launch
// icons, and the icons on the desktop. A process, not protected: End Task
// or kill ends it. init starts it and, if C:\.config\startup.txt says
// `taskbar:respawn` (or says nothing), starts it again; it is init's child
// then. On a machine with no init (a test) it is the desktop's child and
// stays away once ended.
void start_taskbar(void *)
{
    wm::Wm &wm = manager();
    proc::Table &processes = proc::table();
    const proc::Pid desktop = processes.find_by_name("desktop");
    const proc::Pid pid = processes.spawn("taskbar", init::the() != nullptr ? proc::kRoot : desktop, proc::kSystem);
    if (pid != proc::kNone) {
        processes.set_stop(pid, &stop_taskbar, nullptr);
        g_taskbar = pid;
    }
    // With no room in the table the taskbar is the desktop's, as it was
    // before it had a process.
    const proc::Pid owner = pid != proc::kNone ? pid : desktop;
    const proc::Scope as(&processes, owner);
    void *bar_place = os::allocate(sizeof(desk::Taskbar));
    void *desk_place = os::allocate(sizeof(desk::Icons));
    if (bar_place == nullptr || desk_place == nullptr) {
        // No memory for them: no taskbar this time. The row stays, so it
        // can be ended and tried again.
        os::release(bar_place);
        os::release(desk_place);
        return;
    }
    g_bar = new (bar_place) desk::Taskbar(wm);
    g_desk = new (desk_place) desk::Icons(wm);

    find_programs(nullptr);
    wm.set_start_menu_opening(&find_programs, nullptr);
    g_start_items[0] = {"&Programs", nullptr, nullptr, &g_programs, &gfx::folder_icon_16(), 0};
    g_start_items[1] = {"&Settings", nullptr, nullptr, &g_settings, &gfx::settings_icon_16(), 0};
    g_start_items[2] = {"&Run...", &open_run, nullptr, nullptr, &gfx::run_icon_16(), 0};
    g_start_items[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
    g_start_items[4] = {"Sh&ut Down...", &shut_down, nullptr, nullptr, &gfx::power_icon_16(), 0};
    wm.set_start_menu(&g_start, "Chinchilla");

    // Quick launch, by the Start button: one click opens the program
    // (backlog item 30; which three is in docs/DECISIONS.md).
    {
        // By name, as Run would start them: they are program files, and
        // one that is not on C: says so when its button is clicked. Their
        // pictures here are the system's own, which are always there.
        static const char *const kQuick[] = {"File manager", "Editor", "Terminal"};
        const gfx::Bitmap *const pictures[] = {&gfx::files_icon_16(), &gfx::editor_icon_16(),
                                               &gfx::terminal_icon_16()};
        desk::Taskbar::QuickLaunch quick[desk::Taskbar::kMaxQuick];
        int count = 0;
        for (const char *name : kQuick) {
            quick[count] = {pictures[count],
                            [](void *context) { start_program(static_cast<const char *>(context)); },
                            const_cast<char *>(name)};
            count++;
        }
        g_bar->set_quick_launch(quick, count);
    }

    g_icons[0] = {"My Board", &gfx::board_icon_32(), &open_my_board, nullptr};
    g_icons[1] = {"Documents", &gfx::folder_icon_32(), &open_documents, nullptr};
    g_icons[2] = {"Network", &gfx::network_icon_32(), &not_yet, const_cast<NotYet *>(&kNetwork)};
    g_icons[3] = {"Trash", &gfx::bin_icon_32(), &open_trash_icon, nullptr, nullptr, &drop_on_trash};
    if (network() != nullptr) {
        g_icons[2] = {"Network", &gfx::network_icon_32(), &network_panel, nullptr};
        g_settings_items[2].icon = &gfx::network_icon_16();
        g_settings_items[2].flags = 0;
    }
    if (bluetooth() != nullptr) {
        g_settings_items[3].icon = &gfx::bluetooth_icon_16();
        g_settings_items[3].flags = 0;
    }
    for (int i = 0; i < desk::Taskbar::kMaxTray; i++) {
        g_bar->set_tray_icon(i, g_tray[i].icon, g_tray[i].click, g_tray[i].context);
    }
    {
        fs::Ini ini;
        load_settings("icons", ini);
        for (int i = 0; i < kSystemIcons; i++) {
            read_place(ini, "system", g_icons[i].label, g_icons[i].x, g_icons[i].y);
        }
        load_settings("desktop", ini);
        g_desk->set_snap(ini.get_bool("icons", "snap", false));
    }
    g_desk->set_icons(g_icons, kSystemIcons);
    g_desk->set_drop(&drop_on_desktop, nullptr);
    g_desk->set_moved(&icons_moved, nullptr);
    g_desk->set_menu(&desktop_menu, nullptr);
    g_taskbar_up = true;
    show_desktop_now();
    wm.start_timer(2000000, 2000000, &desktop_files_tick, nullptr);
    g_desk->attach(owner);
    g_bar->attach(owner);
}

} // namespace

void set_tray_icon(int place, const gfx::Bitmap *icon, void (*click)(void *context), void *context)
{
    const prog::InCall in_call;
    if (place < 0 || place >= desk::Taskbar::kMaxTray) {
        return;
    }
    g_tray[place] = {icon, click, context};
    if (g_bar != nullptr) {
        g_bar->set_tray_icon(place, icon, click, context);
    }
}

desk::Taskbar *taskbar() { return g_bar; }
desk::Icons *desktop_icons() { return g_desk; }

namespace {

// For init: the taskbar as something it starts.
bool taskbar_unit(void *)
{
    start_taskbar(nullptr);
    return g_taskbar_up;
}

} // namespace

bool start_taskbar_again()
{
    if (g_taskbar_up) {
        return false;
    }
    if (init::Init *first = init::the()) {
        first->add("taskbar", &taskbar_unit, nullptr, true, init::Again::Does); // false: it has it already
        return first->start_again("taskbar"); // with all its tries again
    }
    start_taskbar(nullptr);
    return true;
}

void start(wm::Wm &wm, ports::System &system, fs::Storage &storage, ports::Memory &memory)
{
    g_wm = &wm;
    g_system = &system;
    g_storage = &storage;
    storage.set_changed(&folder_changed, nullptr);
    wm.set_memory(&memory); // its lists of windows grow from there
    os::Kit kit;
    kit.wm = &wm;
    kit.system = &system;
    kit.storage = &storage;
    kit.memory = &memory;
    kit.shut_down = &power_down;
    kit.open_session = &open_session;
    kit.launch = [](const char *program, const char *argument) {
        char why[120];
        const Program *found = find_program(program);
        return found != nullptr && start_program(*found, argument, why, sizeof(why));
    };
    kit.accept_open = &accept_open;
    kit.watch_files = &watch_files;
    os::install(kit);
    // The desktop is a process too: the Start menu, the icons and the
    // taskbar, and the dialogs they open. (It is the window server and the
    // desktop shell in one until they are split, backlog item 21.)
    proc::Table &processes = proc::table();
    proc::Pid desktop = processes.find_by_name("desktop");
    if (desktop == proc::kNone) {
        desktop = processes.spawn_service("desktop", proc::kProtected);
    }
    wm.set_processes(&processes, desktop);
    // Processor time is charged to whichever process the UI task is working
    // for (proc::Table::set_clock).
    processes.set_clock([](void *) { return static_cast<uint32_t>(g_wm->now_us()); }, nullptr);
    const proc::Scope as(&processes, desktop);
    constexpr size_t kClipboard = 64 * 1024;
    wm::clipboard::init(static_cast<uint8_t *>(memory.allocate(kClipboard)), kClipboard);
    {
        fs::Ini files; // File manager settings: hidden files shown or not
        load_settings("files", files);
        Listing::show_hidden = files.get_bool("view", "show_hidden", false);
    }

    wm.set_attention_key(&attention_key, nullptr);
    wm.set_leave_key(&shut_down, nullptr);
    // A build with the programs linked into it (the web page, the PC's
    // tests; core/app/linked.hpp) has their files written and finds their
    // code by itself. A machine with a loader of its own gives it with
    // set_loader(), before or after this.
    if (app::linked_count() > 0) {
        const os::As as_desktop(os::find("desktop"));
        set_loader(&app::linked_loader());
        app::install_linked(storage, kProgramsFolder);
    }
    move_old_documents();
    welcome_file();
    install_screen_saver();
    install_display();
    start_network();
    start_bluetooth();
    // The taskbar, the Start menu and the icons are a process of their own
    // (backlog item 21), started by init at its line of startup.txt and
    // again whenever it ends (item 39). With the file already done, or with
    // no init at all, it is started here.
    init::Init *first = init::the();
    if (first == nullptr) {
        start_taskbar(nullptr);
    } else if (first->booting()) {
        first->add("taskbar", &taskbar_unit, nullptr, true, init::Again::Does); // false: it has it already
    } else {
        start_taskbar_again();
    }
    // The desktop starts with no windows open (the owner's choice).
    // The welcome chime (backlog item 59).
    os::play_sound(sound::Id::Boot);
}

void open_demo_windows()
{
    launch(*find_app("Welcome"));
    launch(*find_app("About Chinchilla"));
}

} // namespace shell
