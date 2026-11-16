// The built-in programs: About Chinchilla, Welcome, Widget gallery.
// (Larger ones have their own files: Editor, File manager, Picture viewer,
// Culebra.)
#include <cstdio>

#include "core/gfx/art.hpp"
#include "core/shell/defrag.hpp"
#include "core/shell/shell.hpp"
#include "core/wm/widgets.hpp"
#include "internal.hpp"

namespace shell {

namespace {

using wm::key::Escape;

void open_about();
void close_window(void *context) { static_cast<wm::Window *>(context)->close(); }
void about_action(void *) { open_about(); }

/* ---- About Chinchilla --------------------------------------------------------- */

class AboutWindow : public wm::Window {
public:
    AboutWindow()
        : Window("About Chinchilla", {300, 190, 300, 160}, &gfx::mascot_16()),
          name_({56, 14, 200, 14}, "Chinchilla", true),
          version_({56, 34, 220, 14}, "Version 0.2"),
          line_({56, 52, 220, 14}, "A 90s desktop for the ESP32-S3."),
          ok_({292 - 75 - 10, 132 - 23 - 10, 75, 23}, "OK", &AboutWindow::ok, this)
    {
        set_dialog(true);
        add(name_);
        add(version_);
        add(line_);
        add(ok_);
        ok_.set_default(true);
        set_default(&ok_);
    }

    bool layered() const override { return true; } // the picture over the face
    void paint(gfx::Canvas &c) override
    {
        Window::paint(c);
        c.blit(gfx::mascot_32(), 10, 10);
    }

    bool key(const wm::KeyMsg &k) override
    {
        if (k.pressed && k.usage == Escape) {
            close();
            return true;
        }
        return Window::key(k);
    }

private:
    static void ok(wm::Button &, void *context) { static_cast<AboutWindow *>(context)->close(); }

    wm::Label name_, version_, line_;
    wm::Button ok_;
};

AboutWindow &about()
{
    static AboutWindow window;
    return window;
}

void open_about() { show(about()); }

/* ---- Welcome ------------------------------------------------------------------ */

const char kWelcomeText[] =
    "Welcome to Chinchilla, a 90s-style desktop\n"
    "for the ESP32-S3.\n"
    "\n"
    "Try it:\n"
    "  - drag a window by its title bar\n"
    "  - click a window to bring it to the front\n"
    "  - minimise one and get it back from the\n"
    "    taskbar\n"
    "\n"
    "640 x 480, 256 colours, 60 Hz.\n"
    "Texto en español: año, niño, café, ¡hola!";

class WelcomeWindow : public wm::Window {
public:
    WelcomeWindow()
        : Window("Welcome to Chinchilla", {96, 30, 400, 280}, &gfx::welcome_icon_16()),
          text_({6, 6, 100, 100}, kWelcomeText)
    {
        file_items_[0] = {"&Close", &close_window, this, nullptr, nullptr, 0};
        help_items_[0] = {"&About Chinchilla", &about_action, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        add(text_);
        resized();
    }

    void resized() override
    {
        const gfx::Rect c = client();
        text_.rect = {6, 6, c.w - 12, c.h - 12};
    }

private:
    wm::MenuItem file_items_[1];
    wm::MenuItem help_items_[1];
    wm::Menu file_ = {file_items_, 1};
    wm::Menu help_ = {help_items_, 1};
    const wm::MenuBarEntry entries_[2] = {{"&File", &file_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 2};
    wm::TextArea text_;
};

WelcomeWindow &welcome()
{
    static WelcomeWindow window;
    return window;
}

void open_welcome() { show(welcome()); }

/* ---- Widget gallery --------------------------------------------------------- */

const char *const kFruit[] = {"Apple",   "Banana", "Cherry", "Fig",    "Grape",
                              "Kiwi",    "Lemon",  "Mango",  "Melon",  "Naranja",
                              "Papaya",  "Pear",   "Piña",   "Plum",   "Quince"};

class GalleryWindow : public wm::Window {
public:
    GalleryWindow()
        : Window("Widget gallery", {120, 60, 460, 316}, &gfx::gallery_icon_16()),
          buttons_({8, 4, 136, 104}, "Buttons"),
          push_({18, 22, 112, 23}, "Push me", &GalleryWindow::pushed, this),
          pushes_({18, 50, 116, 14}, "Pushed 0 times"),
          bold_({18, 66, 116, 18}, "Bold", true),
          underline_({18, 84, 116, 18}, "Underline"),
          size_frame_({152, 4, 120, 104}, "Size"),
          small_({162, 22, 100, 18}, "Small", &size_, 0),
          medium_({162, 44, 100, 18}, "Medium", &size_, 1),
          large_({162, 66, 100, 18}, "Large", &size_, 2),
          name_label_({8, 120, 40, 14}, "&Name:"),
          name_({52, 116, 220, 21}, "Chinchilla"),
          list_({280, 8, 140, 130}, kFruit, 15),
          hbar_({8, 150, 264, 16}, false, &GalleryWindow::scrolled, this),
          hvalue_({8, 172, 160, 14}, "Value: 0"),
          vbar_({428, 8, 16, 160}, true, &GalleryWindow::scrolled, this),
          close_({452 - 75 - 10, 270 - 23 - 10, 75, 23}, "Close", &GalleryWindow::closed_by_button, this)
    {
        hbar_.set_range(100, 10);
        vbar_.set_range(50, 10);
        // The text field first, so it starts with the focus.
        add(name_);
        add(buttons_);
        add(push_);
        add(pushes_);
        add(bold_);
        add(underline_);
        add(size_frame_);
        add(small_);
        add(medium_);
        add(large_);
        add(name_label_);
        add(list_);
        add(hbar_);
        add(hvalue_);
        add(vbar_);
        add(close_);
        close_.set_default(true);
        set_default(&close_);

        file_items_[0] = {"&Close", &close_window, this, nullptr, nullptr, 0};
        option_items_[0] = {"&Bold", &GalleryWindow::toggle_bold, this, nullptr, nullptr, 0};
        option_items_[1] = {"&Size", nullptr, nullptr, &size_menu_, nullptr, 0};
        option_items_[2] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        option_items_[3] = {"&Not yet", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled};
        size_items_[0] = {"&Small", &GalleryWindow::pick_small, this, nullptr, nullptr, 0};
        size_items_[1] = {"&Medium", &GalleryWindow::pick_medium, this, nullptr, nullptr, 0};
        size_items_[2] = {"&Large", &GalleryWindow::pick_large, this, nullptr, nullptr, 0};
        help_items_[0] = {"&About Chinchilla", &about_action, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
        sync_menus();
    }

private:
    static void pushed(wm::Button &, void *context)
    {
        auto *g = static_cast<GalleryWindow *>(context);
        g->push_count_++;
        snprintf(g->pushes_text_, sizeof(g->pushes_text_), "Pushed %d time%s", g->push_count_,
                 g->push_count_ == 1 ? "" : "s");
        g->pushes_.set_text(g->pushes_text_);
    }
    static void scrolled(wm::ScrollBar &, void *context)
    {
        auto *g = static_cast<GalleryWindow *>(context);
        snprintf(g->value_text_, sizeof(g->value_text_), "Value: %d / %d", g->hbar_.value(),
                 g->vbar_.value());
        g->hvalue_.set_text(g->value_text_);
    }
    static void closed_by_button(wm::Button &, void *context)
    {
        static_cast<GalleryWindow *>(context)->close();
    }
    static void toggle_bold(void *context)
    {
        auto *g = static_cast<GalleryWindow *>(context);
        g->bold_.set_checked(!g->bold_.checked());
        g->sync_menus();
    }
    void sync_menus()
    {
        option_items_[0].flags = bold_.checked() ? wm::kMenuChecked : 0;
        for (int i = 0; i < 3; i++) {
            size_items_[i].flags = size_ == i ? wm::kMenuChecked : 0;
        }
    }
    static void pick_small(void *c) { pick(c, 0); }
    static void pick_medium(void *c) { pick(c, 1); }
    static void pick_large(void *c) { pick(c, 2); }
    static void pick(void *context, int which)
    {
        auto *g = static_cast<GalleryWindow *>(context);
        wm::RadioButton *buttons[3] = {&g->small_, &g->medium_, &g->large_};
        buttons[which]->select();
        g->sync_menus();
    }

    int size_ = 1;
    int push_count_ = 0;
    char pushes_text_[32] = "";
    char value_text_[32] = "";
    wm::GroupFrame buttons_;
    wm::Button push_;
    wm::Label pushes_;
    wm::CheckBox bold_, underline_;
    wm::GroupFrame size_frame_;
    wm::RadioButton small_, medium_, large_;
    wm::Label name_label_;
    wm::TextField name_;
    wm::ListBox list_;
    wm::ScrollBar hbar_;
    wm::Label hvalue_;
    wm::ScrollBar vbar_;
    wm::Button close_;

    wm::MenuItem file_items_[1];
    wm::MenuItem option_items_[4];
    wm::MenuItem size_items_[3];
    wm::MenuItem help_items_[1];
    wm::Menu file_ = {file_items_, 1};
    wm::Menu size_menu_ = {size_items_, 3};
    wm::Menu options_ = {option_items_, 4};
    wm::Menu help_ = {help_items_, 1};
    const wm::MenuBarEntry entries_[3] = {{"&File", &file_}, {"&Options", &options_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 3};
};

GalleryWindow &gallery()
{
    static GalleryWindow window;
    return window;
}

void open_gallery() { show(gallery()); }

void open_editor() { open_in_editor(nullptr); }
void open_viewer() { open_picture(nullptr); }
void open_file_manager() { open_folder(nullptr); }

// Each program with the function that gives its icon (icons are built on
// first use, so the table is filled once).
struct Entry {
    App app;
    const gfx::Bitmap &(*icon)();
};
const Entry kApps[] = {
    {{"About Chinchilla", nullptr, &open_about}, &gfx::mascot_16},
    {{"Cactus Patch", nullptr, &open_cactus_patch}, &gfx::cactus_icon_16},
    {{"Culebra", nullptr, &open_culebra}, &gfx::snake_icon_16},
    {{"Defragmenter", nullptr, &open_defragmenter}, &gfx::defrag_icon_16},
    {{"Editor", nullptr, &open_editor}, &gfx::editor_icon_16},
    {{"File manager", nullptr, &open_file_manager}, &gfx::files_icon_16},
    {{"Picture viewer", nullptr, &open_viewer}, &gfx::viewer_icon_16},
    {{"Task Manager", nullptr, &open_task_manager}, &gfx::tasks_icon_16},
    {{"Welcome", nullptr, &open_welcome}, &gfx::welcome_icon_16},
    {{"Widget gallery", nullptr, &open_gallery}, &gfx::gallery_icon_16},
};
constexpr int kAppCount = sizeof(kApps) / sizeof(kApps[0]);

App apps_with_icons[kAppCount];
bool icons_set = false;

const App *table()
{
    if (!icons_set) {
        for (int i = 0; i < kAppCount; i++) {
            apps_with_icons[i] = kApps[i].app;
            apps_with_icons[i].icon = &kApps[i].icon();
        }
        icons_set = true;
    }
    return apps_with_icons;
}

} // namespace

int app_count() { return kAppCount; }

const App &app(int index) { return table()[index]; }

const App *find_app(const char *name)
{
    for (int i = 0; i < kAppCount; i++) {
        const char *a = table()[i].name, *b = name;
        while (*a != 0 && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) {
            return &table()[i];
        }
    }
    return nullptr;
}

void launch(const App &app)
{
    const AsProgram as(app.name);
    app.open();
}

proc::Pid process_for(const char *program)
{
    proc::Table &t = proc::table();
    proc::Pid pid = t.find_by_name(program);
    if (pid == proc::kNone) {
        pid = t.spawn(program, t.current(), proc::kWindowed);
    }
    return pid != proc::kNone ? pid : t.current();
}

} // namespace shell
