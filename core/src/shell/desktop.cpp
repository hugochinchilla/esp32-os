// The desktop: Start menu, desktop icons, message box and Shut Down dialog.
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <strings.h>
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/text_edit.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"
#include "listing.hpp"

namespace shell {

namespace {

wm::Wm *g_wm = nullptr;
ports::System *g_system = nullptr;
fs::Storage *g_storage = nullptr;
ports::Memory *g_memory = nullptr;

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
    }

    bool layered() const override { return true; } // the picture over the face
    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit2x(gfx::mascot_16(), 12, 12);
        char line[96];
        const char *p = text_;
        for (int y = 14; *p != 0; y += 13) {
            int n = 0;
            while (p[n] != 0 && p[n] != '\n' && n < static_cast<int>(sizeof(line)) - 1) {
                line[n] = p[n];
                n++;
            }
            line[n] = 0;
            c.text(56, y, line, gfx::kBlack);
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
            answer_(choice, context_);
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
    void *context_ = nullptr;
};

MessageBox &message_window()
{
    static MessageBox box;
    return box;
}

/* ---- Shut Down dialog ----------------------------------------------------------- */

class ShutDownDialog : public wm::Window {
public:
    ShutDownDialog()
        : Window("Shut Down Chinchilla", {0, 0, 320, 150}, &gfx::mascot_16()),
          question_({56, 16, 240, 14}, "What do you want the computer to do?"),
          detail_({56, 34, 240, 14}, "Shut Down gets it ready to be unplugged."),
          shut_down_({312 - 3 * 75 - 2 * 6 - 10, 122 - 23 - 10, 75, 23}, "&Shut Down", &ShutDownDialog::shut_down, this),
          restart_({312 - 2 * 75 - 6 - 10, 122 - 23 - 10, 75, 23}, "&Restart", &ShutDownDialog::restart, this),
          cancel_({312 - 75 - 10, 122 - 23 - 10, 75, 23}, "Cancel", &ShutDownDialog::cancel, this)
    {
        set_dialog(true);
        set_modal(nullptr, true); // the whole desktop waits for an answer
        add(question_);
        add(detail_);
        add(shut_down_);
        add(restart_);
        add(cancel_);
        shut_down_.set_default(true);
        set_default(&shut_down_);
    }

    bool layered() const override { return true; } // the picture over the face
    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit2x(gfx::mascot_16(), 12, 12);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == wm::key::Escape) {
            close();
            return true;
        }
        if (k.pressed && (k.mods & wm::kAlt) && k.usage == wm::key::A + ('r' - 'a')) {
            restart_.click();
            return true;
        }
        return Window::key(k);
    }

private:
    static void shut_down(wm::Button &, void *context)
    {
        static_cast<ShutDownDialog *>(context)->close();
        power_down();
    }
    static void restart(wm::Button &, void *) { system_port().reboot(); }
    static void cancel(wm::Button &, void *context) { static_cast<ShutDownDialog *>(context)->close(); }

    wm::Label question_, detail_;
    wm::Button shut_down_, restart_, cancel_;
};

ShutDownDialog &shut_down_window()
{
    static ShutDownDialog dialog;
    return dialog;
}

/* ---- Start menu and desktop ------------------------------------------------------ */

constexpr int kMaxApps = 16;
wm::MenuItem g_program_items[kMaxApps];
wm::Menu g_programs = {g_program_items, 0};
void open_screen_saver(void *) { screen_saver_dialog(); }
void network_panel(void *) { open_network(); }
wm::MenuItem g_settings_items[] = {
    {"&Control Panel", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Taskbar...", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Network", &network_panel, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Screen Saver...", &open_screen_saver, nullptr, nullptr, nullptr, 0},
};
wm::Menu g_settings = {g_settings_items, 4};
wm::MenuItem g_start_items[5];
wm::Menu g_start = {g_start_items, 5};

void launch_app(void *context) { launch(*static_cast<const App *>(context)); }
void shut_down(void *) { shut_down_dialog(); }

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
wm::DesktopIcon g_icons[wm::Wm::kMaxIcons];
struct DesktopFile {
    char path[fs::kMaxPath];
    char label[16];
};
DesktopFile g_files[wm::Wm::kMaxIcons - kSystemIcons];
uint32_t g_files_signature = 0;
int g_file_timer = 0;

void open_desktop_file(void *context)
{
    const DesktopFile &f = *static_cast<const DesktopFile *>(context);
    ports::DirEntry e;
    if (storage().stat(f.path, e) == ports::FsError::Ok && e.folder) {
        open_folder(f.path);
    } else if (!open_file(f.path)) {
        message_box("Desktop", "There is no program for this kind of file yet.");
    }
}

// A file let go on the desktop itself goes into C:\Documents\Desktop
// (nothing happens if it is there already).
void drop_on_desktop(const char *path, void *)
{
    char parent[fs::kMaxPath];
    fs::path::parent(path, parent, sizeof(parent));
    if (strcasecmp(parent, kDesktopFolder) == 0) {
        return;
    }
    storage().make_folder("C:\\Documents");
    storage().make_folder(kDesktopFolder);
    const ports::FsError e = move_to_folder(path, kDesktopFolder);
    files_changed();
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
    files_changed();
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
    for (const char *e : {"png", "jpg", "jpeg", "bmp"}) {
        if (strcmp(ext, e) == 0) {
            return &gfx::picture_icon_32();
        }
    }
    return &gfx::file_icon_32();
}

// Reads C:\Documents\Desktop again; the icons change only if the files did.
// ponytail: looked at every 2 s (a few directory reads), so files put there
// any way (File manager, a save, tools/dbg.py put) appear without hooks.
void refresh_desktop_files(bool force)
{
    uint32_t signature = 2166136261u;
    int n = 0;
    struct Found {
        char name[64];
        bool folder;
    };
    static Found found[wm::Wm::kMaxIcons - kSystemIcons];
    for (int i = 0; n < wm::Wm::kMaxIcons - kSystemIcons; i++) {
        ports::DirEntry e;
        if (storage().entry(kDesktopFolder, i, e) != ports::FsError::Ok) {
            break;
        }
        if (e.name[0] == '.') {
            continue; // hidden
        }
        snprintf(found[n].name, sizeof(found[n].name), "%s", e.name);
        found[n].folder = e.folder;
        for (const char *c = e.name; *c != 0; c++) {
            signature = (signature ^ static_cast<uint8_t>(*c)) * 16777619u;
        }
        signature = (signature ^ (e.folder ? 1u : 2u)) * 16777619u;
        n++;
    }
    signature ^= static_cast<uint32_t>(n);
    // The Trash's picture: with paper in it when it holds anything.
    const bool full = trash_count() > 0;
    signature = (signature ^ (full ? 3u : 4u)) * 16777619u;
    g_icons[3].picture = full ? &gfx::bin_full_icon_32() : &gfx::bin_icon_32();
    if (!force && signature == g_files_signature) {
        return;
    }
    g_files_signature = signature;
    // Folders first, then by name, ignoring case, as in the File manager.
    std::sort(found, found + n, [](const Found &a, const Found &b) {
        if (a.folder != b.folder) {
            return a.folder;
        }
        return strcasecmp(a.name, b.name) < 0;
    });
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
    }
    manager().set_desktop_icons(g_icons, kSystemIcons + n);
}

void desktop_files_tick(void *) { refresh_desktop_files(false); }

} // namespace

void files_changed()
{
    refresh_folder_windows();
    refresh_desktop_files(false);
}

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

wm::Wm &manager() { return *g_wm; }
ports::System &system_port() { return *g_system; }
fs::Storage &storage() { return *g_storage; }
ports::Memory &memory() { return *g_memory; }

void show(wm::Window &w)
{
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
    static const char *const ok[] = {"OK"};
    message_window().ask(title, text, ok, 1, nullptr, nullptr);
}

void ask(const char *title, const char *text, const char *const *buttons, int count, AnswerFn answer,
         void *context)
{
    message_window().ask(title, text, buttons, count, answer, context);
}

void shut_down_dialog()
{
    ShutDownDialog &d = shut_down_window();
    if (!d.open()) {
        d.set_frame(centred(manager(), d.frame().w, d.frame().h));
    }
    show(d);
}

void start(wm::Wm &wm, ports::System &system, fs::Storage &storage, ports::Memory &memory)
{
    g_wm = &wm;
    g_system = &system;
    g_storage = &storage;
    g_memory = &memory;
    constexpr size_t kClipboard = 64 * 1024;
    wm::clipboard::init(static_cast<uint8_t *>(memory.allocate(kClipboard)), kClipboard);
    {
        fs::Ini files; // File manager settings: hidden files shown or not
        load_settings("files", files);
        Listing::show_hidden = files.get_bool("view", "show_hidden", false);
    }

    const int n = app_count() < kMaxApps ? app_count() : kMaxApps;
    for (int i = 0; i < n; i++) {
        const App &a = app(i);
        g_program_items[i] = {a.name, &launch_app, const_cast<App *>(&a), nullptr, a.icon, 0};
    }
    g_programs.count = n;
    g_start_items[0] = {"&Programs", nullptr, nullptr, &g_programs, &gfx::folder_icon_16(), 0};
    g_start_items[1] = {"&Settings", nullptr, nullptr, &g_settings, &gfx::settings_icon_16(), 0};
    g_start_items[2] = {"&Run...", nullptr, nullptr, nullptr, &gfx::text_icon_16(), wm::kMenuDisabled};
    g_start_items[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
    g_start_items[4] = {"Sh&ut Down...", &shut_down, nullptr, nullptr, &gfx::mascot_16(), 0};
    wm.set_start_menu(&g_start, "Chinchilla");
    wm.set_attention_key(&attention_key, nullptr);

    g_icons[0] = {"My Board", &gfx::board_icon_32(), &open_my_board, nullptr};
    g_icons[1] = {"Documents", &gfx::folder_icon_32(), &open_documents, nullptr};
    g_icons[2] = {"Network", &gfx::network_icon_32(), &not_yet, const_cast<NotYet *>(&kNetwork)};
    g_icons[3] = {"Trash", &gfx::bin_icon_32(), &open_trash_icon, nullptr, nullptr, &drop_on_trash};
    if (network() != nullptr) {
        g_icons[2] = {"Network", &gfx::network_icon_32(), &network_panel, nullptr};
        g_settings_items[2].icon = &gfx::network_icon_16();
        g_settings_items[2].flags = 0;
    }
    wm.set_desktop_icons(g_icons, kSystemIcons);
    wm.set_desktop_drop(&drop_on_desktop, nullptr);
    move_old_documents();
    welcome_file();
    refresh_desktop_files(true);
    g_file_timer = wm.start_timer(2000000, 2000000, &desktop_files_tick, nullptr);
    install_screen_saver();
    start_network();
    // The desktop starts with no windows open (the owner's choice).
}

void open_demo_windows()
{
    launch(*find_app("Welcome"));
    launch(*find_app("About Chinchilla"));
}

} // namespace shell
