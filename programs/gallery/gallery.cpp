// Widget gallery: one of each control, to look at and to try. A program of
// its own, written with the thin classes over windows and controls by
// number (core/app/app.hpp).
#include <cstdio>

#include "core/app/app.hpp"
#include "core/gfx/art.hpp"
#include "core/shell/shell.hpp"
#include "art.hpp"
#include "internal.hpp"

namespace shell {

namespace {

const char *const kFruit[] = {"Apple",   "Banana", "Cherry", "Fig",    "Grape",
                              "Kiwi",    "Lemon",  "Mango",  "Melon",  "Naranja",
                              "Papaya",  "Pear",   "Piña",   "Plum",   "Quince"};

class GalleryWindow : public app::Window {
public:
    GalleryWindow()
        : // The text field first, so it starts with the focus.
          name_(text_field({52, 116, 220, 21}, "Chinchilla")),
          buttons_(group({8, 4, 136, 104}, "Buttons")),
          push_(button({18, 22, 112, 23}, "Push me")),
          pushes_(label({18, 50, 116, 14}, "Pushed 0 times")),
          bold_(check_box({18, 66, 116, 18}, "Bold", true)),
          underline_(check_box({18, 84, 116, 18}, "Underline")),
          size_frame_(group({152, 4, 120, 104}, "Size")),
          small_(radio({162, 22, 100, 18}, "Small", kSize, 0)),
          medium_(radio({162, 44, 100, 18}, "Medium", kSize, 1)),
          large_(radio({162, 66, 100, 18}, "Large", kSize, 2)),
          name_label_(label({8, 120, 40, 14}, "&Name:")),
          list_(list({280, 8, 140, 130}, kFruit, 15)),
          hbar_(scroll_bar({8, 150, 264, 16}, false)),
          hvalue_(label({8, 172, 160, 14}, "Value: 0")),
          vbar_(scroll_bar({428, 8, 16, 160}, true)),
          close_(button({452 - 75 - 10, 270 - 23 - 10, 75, 23}, "Close"))
    {
        hbar_.set_range(100, 10);
        vbar_.set_range(50, 10);
        set_radio_value(kSize, 1);
        set_default(close_);

        file_items_[0] = {"&Close", &GalleryWindow::menu_close, this, nullptr, nullptr, 0};
        option_items_[0] = {"&Bold", &GalleryWindow::toggle_bold, this, nullptr, nullptr, 0};
        option_items_[1] = {"&Size", nullptr, nullptr, &size_menu_, nullptr, 0};
        option_items_[2] = {"&Hourglass", &GalleryWindow::toggle_hourglass, this, nullptr, nullptr, 0};
        option_items_[3] = {nullptr, nullptr, nullptr, nullptr, nullptr, wm::kMenuSeparator};
        option_items_[4] = {"&Not yet", nullptr, nullptr, nullptr, nullptr, wm::kMenuDisabled};
        size_items_[0] = {"&Small", &GalleryWindow::pick_small, this, nullptr, nullptr, 0};
        size_items_[1] = {"&Medium", &GalleryWindow::pick_medium, this, nullptr, nullptr, 0};
        size_items_[2] = {"&Large", &GalleryWindow::pick_large, this, nullptr, nullptr, 0};
        help_items_[0] = {"&About Chinchilla", &GalleryWindow::menu_about, nullptr, nullptr, nullptr, 0};
        set_menu_bar(&bar_);
    }

    void command(app::Control which) override
    {
        if (which == push_) {
            push_count_++;
            snprintf(pushes_text_, sizeof(pushes_text_), "Pushed %d time%s", push_count_,
                     push_count_ == 1 ? "" : "s");
            pushes_.set_text(pushes_text_);
        } else if (which == close_) {
            close();
        }
    }

    void changed(app::Control which) override
    {
        if (which == hbar_ || which == vbar_) {
            snprintf(value_text_, sizeof(value_text_), "Value: %d / %d", hbar_.value(), vbar_.value());
            hvalue_.set_text(value_text_);
        }
    }

    // The menus say what the controls say.
    void menu_opening(int) override
    {
        option_items_[0].flags = bold_.checked() ? wm::kMenuChecked : 0;
        option_items_[2].flags = hourglass() ? wm::kMenuChecked : 0;
        for (int i = 0; i < 3; i++) {
            size_items_[i].flags = radio_value(kSize) == i ? wm::kMenuChecked : 0;
        }
    }

private:
    static constexpr int kSize = 0; // the group of the three radio buttons
    static GalleryWindow &self(void *context) { return *static_cast<GalleryWindow *>(context); }
    static void menu_close(void *context) { self(context).close(); }
    static void menu_about(void *) { os::launch("About Chinchilla"); }
    static void toggle_bold(void *context) { self(context).bold_.set_checked(!self(context).bold_.checked()); }
    // The hourglass on demand, to look at: over the controls, and the window
    // answers as before.
    static void toggle_hourglass(void *context) { self(context).set_hourglass(!self(context).hourglass()); }
    static void pick_small(void *context) { self(context).set_radio_value(kSize, 0); }
    static void pick_medium(void *context) { self(context).set_radio_value(kSize, 1); }
    static void pick_large(void *context) { self(context).set_radio_value(kSize, 2); }

    int push_count_ = 0;
    char pushes_text_[32] = "";
    char value_text_[32] = "";
    app::TextField name_;
    app::GroupFrame buttons_;
    app::Button push_;
    app::Label pushes_;
    app::CheckBox bold_, underline_;
    app::GroupFrame size_frame_;
    app::RadioButton small_, medium_, large_;
    app::Label name_label_;
    app::ListBox list_;
    app::ScrollBar hbar_;
    app::Label hvalue_;
    app::ScrollBar vbar_;
    app::Button close_;

    wm::MenuItem file_items_[1];
    wm::MenuItem option_items_[5];
    wm::MenuItem size_items_[3];
    wm::MenuItem help_items_[1];
    wm::Menu file_ = {file_items_, 1};
    wm::Menu size_menu_ = {size_items_, 3};
    wm::Menu options_ = {option_items_, 5};
    wm::Menu help_ = {help_items_, 1};
    const wm::MenuBarEntry entries_[3] = {{"&File", &file_}, {"&Options", &options_}, {"&Help", &help_}};
    const wm::MenuBar bar_ = {entries_, 3};
};

} // namespace

} // namespace shell

// The Widget gallery is a program kept in a file (backlog item 27): not
// part of the system.
APP_INFO(app::file_info("Widget gallery", 0, art::kSmall, art::kSmallInks, art::kLarge, art::kLargeInks));

namespace {
// The window's icon: the header's own pixels are not in memory with the
// program, so they are made again here from the same rows.
constexpr app::Icon16 kWindowIcon = app::icon16(art::kSmall, art::kSmallInks);
} // namespace

APP_MAIN(int, char **)
{
    static const gfx::Bitmap icon = {16, 16, kWindowIcon.pixels, app::kClearInk};
    app::Look look;
    look.title = "Widget gallery";
    look.x = 120;
    look.y = 60;
    look.width = 460;
    look.height = 316;
    look.icon = &icon;
    look.flags = app::kMenuBar;
    return app::open<shell::GalleryWindow>(look) != app::kNone ? 0 : 1;
}
