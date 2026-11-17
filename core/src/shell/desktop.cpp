// The desktop: Start menu, desktop icons and message box.
#include <cstdio>
#include <cstring>

#include <algorithm>
#include <strings.h>
#include "core/prog/prog.hpp"
#include "core/fs/ini.hpp"
#include "core/gfx/art.hpp"
#include "core/desk/desk.hpp"
#include "core/os/os.hpp"
#include "core/init/init.hpp"
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

constexpr int kMaxApps = 16;
wm::MenuItem g_program_items[kMaxApps];
wm::Menu g_programs = {g_program_items, 0};
void open_screen_saver(void *) { screen_saver_dialog(); }
void network_panel(void *) { open_network(); }
void open_display(void *) { display_dialog(); }
void open_run(void *) { run_dialog(); }
// A right click on the desktop: a menu with what can be done to it.
void toggle_snap(void *);
void realign_icons(void *);
wm::MenuItem g_desktop_items[] = {
    {"&Snap icons to grid", &toggle_snap, nullptr, nullptr, nullptr, 0},
    {"&Realign icons", &realign_icons, nullptr, nullptr, nullptr, 0},
    {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator},
    {"&Display...", &open_display, nullptr, nullptr, nullptr, 0},
};
wm::Menu g_desktop_menu = {g_desktop_items, 4};
void desktop_menu(int x, int y, void *);
wm::MenuItem g_settings_items[] = {
    {"&Control Panel", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Taskbar...", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Network", &network_panel, nullptr, nullptr, nullptr, wm::kMenuDisabled},
    {"&Screen Saver...", &open_screen_saver, nullptr, nullptr, &gfx::saver_icon_16(), 0},
    {"&Display...", &open_display, nullptr, nullptr, &gfx::display_icon_16(), 0},
};
wm::Menu g_settings = {g_settings_items, 5};
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
// The tray icon is the network's to set (network.cpp), whether or not there
// is a taskbar to show it at the moment.
const gfx::Bitmap *g_tray_icon = nullptr;
void (*g_tray_click)(void *context) = nullptr;
void *g_tray_context = nullptr;

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

void desktop_menu(int x, int y, void *)
{
    g_desktop_items[0].flags = g_desk != nullptr && g_desk->snap() ? wm::kMenuChecked : 0;
    manager().open_menu(g_desktop_menu, x, y);
}

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
    if (!g_taskbar_up) {
        return; // no shell, no icons to keep right
    }
    uint32_t signature = 2166136261u;
    int n = 0;
    struct Found {
        char name[64];
        bool folder;
    };
    static Found found[desk::Icons::kMax - kSystemIcons];
    for (int i = 0; n < desk::Icons::kMax - kSystemIcons; i++) {
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

void desktop_files_tick(void *) { refresh_desktop_files(false); }

} // namespace

void files_changed()
{
    const prog::InCall in_call;
    // The File manager's windows are the File manager's to touch.
    const proc::Pid files = proc::table().find_by_name("File manager");
    if (files != proc::kNone) {
        enter_program(files, nullptr, [](void *) { refresh_folder_windows(); }, nullptr, 0);
    }
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
    }
}

void ask(const char *title, const char *text, const char *const *buttons, int count, AnswerFn answer,
         void *context)
{
    const prog::InCall in_call;
    if (MessageBox *box = message_window()) {
        box->ask(title, text, buttons, count, answer, context);
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

    const int n = app_count() < kMaxApps ? app_count() : kMaxApps;
    for (int i = 0; i < n; i++) {
        const App &a = app(i);
        g_program_items[i] = {a.name, &launch_app, const_cast<App *>(&a), nullptr, a.icon, 0};
    }
    g_programs.count = n;
    g_start_items[0] = {"&Programs", nullptr, nullptr, &g_programs, &gfx::folder_icon_16(), 0};
    g_start_items[1] = {"&Settings", nullptr, nullptr, &g_settings, &gfx::settings_icon_16(), 0};
    g_start_items[2] = {"&Run...", &open_run, nullptr, nullptr, &gfx::run_icon_16(), 0};
    g_start_items[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
    g_start_items[4] = {"Sh&ut Down...", &shut_down, nullptr, nullptr, &gfx::power_icon_16(), 0};
    wm.set_start_menu(&g_start, "Chinchilla");

    // Quick launch, by the Start button: one click opens the program
    // (backlog item 30; which three is in docs/DECISIONS.md).
    {
        static const char *const kQuick[] = {"File manager", "Editor", "Terminal"};
        desk::Taskbar::QuickLaunch quick[desk::Taskbar::kMaxQuick];
        int count = 0;
        for (const char *name : kQuick) {
            if (const App *program = find_app(name)) {
                quick[count++] = {program->icon,
                                  [](void *context) { launch(*static_cast<const App *>(context)); },
                                  const_cast<App *>(program)};
            }
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
    g_bar->set_tray_icon(g_tray_icon, g_tray_click, g_tray_context);
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
    refresh_desktop_files(true);
    wm.start_timer(2000000, 2000000, &desktop_files_tick, nullptr);
    g_desk->attach(owner);
    g_bar->attach(owner);
}

} // namespace

void set_tray_icon(const gfx::Bitmap *icon, void (*click)(void *context), void *context)
{
    const prog::InCall in_call;
    g_tray_icon = icon;
    g_tray_click = click;
    g_tray_context = context;
    if (g_bar != nullptr) {
        g_bar->set_tray_icon(icon, click, context);
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
    os::Kit kit;
    kit.wm = &wm;
    kit.system = &system;
    kit.storage = &storage;
    kit.memory = &memory;
    kit.shut_down = &power_down;
    kit.open_session = &open_session;
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
    move_old_documents();
    welcome_file();
    install_screen_saver();
    install_display();
    start_network();
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
}

void open_demo_windows()
{
    launch(*find_app("Welcome"));
    launch(*find_app("About Chinchilla"));
}

} // namespace shell
