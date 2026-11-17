// The thin classes a program is written with (backlog item 37, the owner's
// choice: "clases finas encima"; docs/journal/2026-11-17-ui-handles.md).
//
// They are compiled into each program, not into the system: everything here
// is in this header. A Button is the number of a button and a few functions
// that call the system with it (core/os/ui.hpp). So a program reads much as
// it did when it inherited from the system's window class, while what
// crosses between program and system is numbers and text only: the list of
// calls a program built apart would use.
//
//     class Hello : public app::Window {
//     public:
//         Hello() : ok_(button({10, 40, 75, 23}, "OK")) { set_default(ok_); }
//         void command(app::Control which) override
//         {
//             if (which == ok_) {
//                 close();
//             }
//         }
//     private:
//         app::Button ok_;
//     };
//     ...
//     app::Look look;                 // title, size, icon
//     app::open<Hello>(look);
//
// The object is made by open() in memory that comes with the window, and is
// destroyed when the window has closed. A program keeps no pointer to it
// from outside; a program that runs once at a time keeps its window's
// number (app::Window::number()).
#pragma once

#include <new>
#include <utility>

#include "core/os/ui.hpp"

namespace app {

// What a program draws on while it paints: the system's canvas by its
// number, with the canvas's own calls (core/gfx/canvas.hpp). Good only
// inside the paint() it was handed to.
class Canvas {
public:
    explicit Canvas(os::ui::Drawing number) : number_(number) {}

    // A part of this one, with its own corner as (0, 0).
    Canvas sub(const gfx::Rect &r) const { return Canvas(os::ui::draw::part(number_, r)); }

    void fill(const gfx::Rect &r, gfx::Colour c) { os::ui::draw::fill(number_, r, c); }
    void hline(int x, int y, int w, gfx::Colour c) { os::ui::draw::hline(number_, x, y, w, c); }
    void vline(int x, int y, int h, gfx::Colour c) { os::ui::draw::vline(number_, x, y, h, c); }
    void fill_rgb(const gfx::Rect &r, gfx::Rgb c) { os::ui::draw::fill_rgb(number_, r, c); }
    void fill_pattern(const gfx::Rect &r, gfx::Rgb paper, gfx::Rgb ink, const uint8_t rows[8])
    {
        os::ui::draw::fill_pattern(number_, r, paper, ink, rows);
    }
    void fill_fade(const gfx::Rect &r, gfx::Rgb from, gfx::Rgb to, bool vertical = false,
                   const gfx::Rect *span = nullptr)
    {
        os::ui::draw::fill_fade(number_, r, from, to, vertical, span);
    }
    void focus_rect(const gfx::Rect &r) { os::ui::draw::focus_rect(number_, r); }
    void checker(const gfx::Rect &r) { os::ui::draw::checker(number_, r); }
    void edge(const gfx::Rect &r, gfx::Colour tl, gfx::Colour br) { os::ui::draw::edge(number_, r, tl, br); }
    void dent(const gfx::Rect &r) { os::ui::draw::dent(number_, r); }
    void raised(const gfx::Rect &r) { os::ui::draw::raised(number_, r); }
    void sunken(const gfx::Rect &r) { os::ui::draw::sunken(number_, r); }
    void button(const gfx::Rect &r, gfx::ButtonLook look) { os::ui::draw::button(number_, r, look); }
    void button_frame(const gfx::Rect &r, gfx::ButtonLook look) { os::ui::draw::button_frame(number_, r, look); }
    void blit(const gfx::Bitmap &b, int x, int y) { os::ui::draw::blit(number_, b, x, y); }
    void blit_selected(const gfx::Bitmap &b, int x, int y, gfx::Colour tint)
    {
        os::ui::draw::blit_selected(number_, b, x, y, tint);
    }
    void blit2x(const gfx::Bitmap &b, int x, int y) { os::ui::draw::blit2x(number_, b, x, y); }
    void blit_opaque(const gfx::Bitmap &b, int x, int y, gfx::Colour bg)
    {
        os::ui::draw::blit_opaque(number_, b, x, y, bg);
    }
    void pattern(int x, int y, const char *const *rows, int count, gfx::Colour c)
    {
        os::ui::draw::pattern(number_, x, y, rows, count, c);
    }
    void pattern_opaque(int x, int y, const char *const *rows, int count, gfx::Colour fg, gfx::Colour bg)
    {
        os::ui::draw::pattern_opaque(number_, x, y, rows, count, fg, bg);
    }
    int text(int x, int y, const char *s, gfx::Colour c, bool bold = false)
    {
        return os::ui::draw::text(number_, x, y, s, c, bold);
    }
    int text_opaque(int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg, int caret_x = -1)
    {
        return os::ui::draw::text_opaque(number_, x, y, s, fg, bg, caret_x);
    }
    int text_opaque(int x, int y, const char *s, gfx::Colour fg, gfx::Rgb bg, int caret_x = -1)
    {
        return os::ui::draw::text_opaque_rgb(number_, x, y, s, fg, bg, caret_x);
    }
    void text_up(int x, int y, const char *s, gfx::Colour c, bool bold = false)
    {
        os::ui::draw::text_up(number_, x, y, s, c, bold);
    }
    void fill_around(const gfx::Rect &area, const gfx::Rect &hole, gfx::Colour c)
    {
        os::ui::draw::fill_around(number_, area, hole, c);
    }
    static int text_width(const char *s, bool bold = false) { return os::ui::draw::text_width(s, bold); }
    static int text_height() { return os::ui::draw::text_height(); }
    os::ui::Drawing number() const { return number_; }
    // What of it may be drawn now: a program can skip what lies outside.
    gfx::Rect limit() const { return os::ui::draw::bounds(number_); }

private:
    os::ui::Drawing number_;
};

using Look = os::ui::Look;
using Number = os::ui::Window; // a window's number: dead once it has closed
constexpr Number kNone = os::ui::kNoWindow;
using os::ui::kClientSize;
using os::ui::kDialog;
using os::ui::kFixedSize;
using os::ui::kKeys;
using os::ui::kMenuBar;
using os::ui::kMouse;
using os::ui::kOnce;
using os::ui::kPicture;
using os::ui::kWholeDesktop;

// A control: its number in its window. The kinds below add what each can do.
class Control {
public:
    Control() = default;
    Control(Number window, os::ui::Control number) : window_(window), number_(number) {}
    bool operator==(const Control &other) const { return window_ == other.window_ && number_ == other.number_; }
    bool operator!=(const Control &other) const { return !(*this == other); }
    os::ui::Control number() const { return number_; }
    void redraw() const { os::ui::redraw_control(window_, number_); } // painted again
    // Moved, or made another size; an empty place hides it.
    void place(const gfx::Rect &where) const { os::ui::place_control(window_, number_, where); }

protected:
    Number window_ = kNone;
    os::ui::Control number_ = os::ui::kNoControl;
};

class Label : public Control {
public:
    using Control::Control;
    void set_text(const char *text) const { os::ui::set_text(window_, number_, text); }
};

// A line of text shown exactly as given, of which the control keeps a copy.
class Plain : public Control {
public:
    using Control::Control;
    void set(const char *text) const { os::ui::set_text(window_, number_, text); }
};

class Button : public Control {
public:
    using Control::Control;
    void press() const { os::ui::press(window_, number_); } // as if clicked
    void set_label(const char *text) const { os::ui::set_text(window_, number_, text); }
    void set_enabled(bool on) const { os::ui::set_enabled(window_, number_, on); }
};

class CheckBox : public Control {
public:
    using Control::Control;
    bool checked() const { return os::ui::checked(window_, number_); }
    void set_checked(bool on) const { os::ui::set_checked(window_, number_, on); }
};

// One of a group of which one is chosen (Window::radio_value()).
class RadioButton : public Control {
public:
    using Control::Control;
};

class TextField : public Control {
public:
    static constexpr int kCapacity = 80; // characters; twice that in bytes as UTF-8
    using Control::Control;
    void get(char *out, int capacity) const { os::ui::get_text(window_, number_, out, capacity); }
    void set(const char *utf8) const { os::ui::set_text(window_, number_, utf8); }
    void select_all() const { os::ui::select_all(window_, number_); }
    void set_hidden(bool on) const { os::ui::set_hidden(window_, number_, on); }
};

class ScrollBar : public Control {
public:
    using Control::Control;
    void set_range(int maximum, int page) const { os::ui::set_range(window_, number_, maximum, page); }
    int value() const { return os::ui::value(window_, number_); }
    void set_value(int value) const { os::ui::set_value(window_, number_, value); }
};

class ListBox : public Control {
public:
    using Control::Control;
    // `icons`: a picture for each row (one list with pictures a window).
    void set_items(const char *const *items, int count, const gfx::Bitmap *const *icons = nullptr) const
    {
        os::ui::set_items(window_, number_, items, count, icons);
    }
    int selected() const { return os::ui::selected(window_, number_); }
    void select(int index) const { os::ui::select(window_, number_, index); }
};

// A rectangle the program paints (Window::paint_in).
class Picture : public Control {
public:
    using Control::Control;
    using Control::redraw;
    // For one made with kKeys. Where its text cursor is (all a blink
    // paints again); whether the keys go to it.
    void set_caret(const gfx::Rect &caret) const { os::ui::set_caret(window_, number_, caret); }
    bool has_keys() const { return os::ui::control_has_keys(window_, number_); }
    // A part of it painted again, in its own coordinates.
    void redraw(const gfx::Rect &part) const { os::ui::redraw_control_part(window_, number_, part); }
};

// A grid of cells to pick one from, each painted by the program
// (Window::paint_cell).
class Grid : public Control {
public:
    using Control::Control;
    int selected() const { return os::ui::selected(window_, number_); } // -1: none
    // Tells nobody. -1: none.
    void select(int index) const { os::ui::select(window_, number_, index); }
};

// A box showing a text and a picture, with an arrow to press
// (Window::command); menu() opens the program's menu under it.
class DropDown : public Control {
public:
    using Control::Control;
    void set_text(const char *text) const { os::ui::set_text(window_, number_, text); } // must stay valid
    void menu(wm::Menu &menu) const { os::ui::menu_under(window_, number_, menu); }
};

// Icons in a grid with their names under them: what a folder holds.
class IconView : public Control {
public:
    using Control::Control;
    // The program's tables, which must stay valid; what is picked stays.
    void set_items(const char *const *names, int count, const gfx::Bitmap *const *icons) const
    {
        os::ui::set_items(window_, number_, names, count, icons);
    }
    void reset() const { os::ui::reset_view(window_, number_); } // nothing picked, back at the top
    int selected() const { return os::ui::selected(window_, number_); } // -1: none
    void select(int index) const { os::ui::select(window_, number_, index); } // tells of it (changed)
    int item_at(int x, int y) const { return os::ui::item_at(window_, number_, x, y); } // in its coordinates
};

// The text editor: a whole text file, with its own memory for the text
// and for undo. made(): false when there was no memory for it.
class TextEdit : public Control {
public:
    using Control::Control;
    bool made() const { return number_ != os::ui::kNoControl; }
    void clear() const { os::ui::edit(window_, number_, os::ui::Edit::Clear); }
    void undo() const { os::ui::edit(window_, number_, os::ui::Edit::Undo); }
    void cut() const { os::ui::edit(window_, number_, os::ui::Edit::Cut); }
    void copy() const { os::ui::edit(window_, number_, os::ui::Edit::Copy); }
    void paste() const { os::ui::edit(window_, number_, os::ui::Edit::Paste); }
    void delete_selection() const { os::ui::edit(window_, number_, os::ui::Edit::Delete); }
    void select_all() const { os::ui::edit(window_, number_, os::ui::Edit::SelectAll); }
    bool modified() const { return (os::ui::edit_state(window_, number_) & os::ui::kModified) != 0; }
    bool can_undo() const { return (os::ui::edit_state(window_, number_) & os::ui::kCanUndo) != 0; }
    bool has_selection() const { return (os::ui::edit_state(window_, number_) & os::ui::kHasSelection) != 0; }
    void set_modified(bool on) const { os::ui::set_modified(window_, number_, on); }
    // A file's bytes into the text, in pieces.
    void load_begin() const { os::ui::load_begin(window_, number_); }
    bool load_append(const uint8_t *bytes, size_t size) const
    {
        return os::ui::load_append(window_, number_, bytes, size);
    }
    void load_finish() const { os::ui::load_finish(window_, number_); }
    // For a job that writes the text out while the window is locked
    // (os::ui::read_text).
    os::ui::TextReader reader() const { return os::ui::text_reader(window_, number_); }
};

// A bar that fills from the left in blocks: progress, or how full.
class Meter : public Control {
public:
    using Control::Control;
    void set(uint32_t value, uint32_t total) const { os::ui::set_level(window_, number_, value, total); }
};

class GroupFrame : public Control {
public:
    using Control::Control;
};

class TextArea : public Control {
public:
    using Control::Control;
    void set_text(const char *text) const { os::ui::set_text(window_, number_, text); }
};

class Window {
public:
    Number number() const { return number_; }
    void close() { os::ui::close(number_); }
    bool on_screen() const { return os::ui::on_screen(number_); }
    gfx::Rect client_size() const { return os::ui::client_size(number_); } // as w and h
    // Another size for the client area (see os::ui::resize).
    void resize(int client_width, int client_height) { os::ui::resize(number_, client_width, client_height); }
    bool in_front() const { return os::ui::in_front(number_); } // it is the window in use
    bool has_keys() const { return os::ui::has_keys(number_); } // the keys go to it, and it is not minimised
    // Locked: it takes no keys, no clicks and no request to close.
    void set_locked(bool on) { os::ui::set_locked(number_, on); }
    bool locked() const { return os::ui::locked(number_); }
    bool caret_lit() const { return os::ui::caret_lit(number_); } // the lit half of the text cursor's blink
    // Asks for a part of the client area to be painted again.
    void redraw(const gfx::Rect &part) { os::ui::redraw(number_, part); }

    // What happens in the window. A button was pressed:
    virtual void command(Control /*which*/) {}
    // A check box was ticked, a scroll bar moved, another row of a list picked:
    virtual void changed(Control /*which*/) {}
    // A row of a list was double-clicked, or Enter pressed on it. False:
    // the program has no use for it, and the default button is pressed.
    virtual bool chosen(Control /*which*/) { return false; }
    // A key the controls did not use. True: the program used it.
    virtual bool key(const wm::KeyMsg &) { return false; }
    // The window is about to be painted: the last moment to change a text
    // the controls show. Not for changing anything else.
    virtual void painting() {}
    // Only with kPicture: after the face and the controls are painted.
    virtual void paint(Canvas &) {}
    // A picture of the window's is to be painted, its corner as (0, 0).
    virtual void paint_in(Control /*which*/, Canvas &) {}
    // A cell of a grid is to be painted.
    virtual void paint_cell(Control /*which*/, int /*index*/, const gfx::Rect & /*cell*/, Canvas &) {}
    // A key, or characters typed (UTF-8), for the picture made with kKeys
    // that has the keys. True: the key was used.
    virtual bool key_in(Control /*which*/, const wm::KeyMsg &) { return false; }
    virtual void text_in(Control /*which*/, const char * /*utf8*/) {}
    // The left button in a picture made with kMouse, in its coordinates;
    // from Down to Up they all go to the picture that was pressed.
    virtual void mouse(Control /*which*/, const wm::MouseMsg &) {}
    // The right button went down in a picture made with kMouse.
    virtual void right_click(Control /*which*/, int /*x*/, int /*y*/) {}
    // The client area has another size: the controls go where they belong now.
    virtual void resized(int /*width*/, int /*height*/) {}
    // An entry of an icon view was held and moved: the program starts the
    // drag of what it stands for (os::begin_drag).
    virtual void drag(Control /*which*/) {}
    // A file dragged over the client area (client coordinates): true if
    // it would be taken there. nullptr: the drag left the window, or ended.
    virtual bool drag_over(const char * /*path*/, int /*x*/, int /*y*/) { return false; }
    // The file let go there, after drag_over() said yes.
    virtual void dropped(const char * /*path*/, int /*x*/, int /*y*/) {}
    // The close button, Alt+F4, a TERM signal. True: the program deals
    // with it (asks about unsaved work, and closes itself later).
    virtual bool closing() { return false; }
    // A menu of the menu bar is about to open: a chance to grey items out.
    virtual void menu_opening(int /*which*/) {}
    // The window has closed; the object is destroyed after this.
    virtual void closed() {}

protected:
    // The window being made: open() says which before the constructor runs,
    // so that a program's members can be made with button() and label().
    Window() : number_(making()) {}
    virtual ~Window() = default;

    Label label(const gfx::Rect &place, const char *text) { return {number_, os::ui::label(number_, place, text)}; }
    Plain plain(const gfx::Rect &place) { return {number_, os::ui::plain(number_, place)}; }
    Button button(const gfx::Rect &place, const char *text)
    {
        return {number_, os::ui::button(number_, place, text)};
    }
    CheckBox check_box(const gfx::Rect &place, const char *text, bool checked = false)
    {
        return {number_, os::ui::check_box(number_, place, text, checked)};
    }
    // `group`: 0 to 3; `value`: what radio_value(group) answers while this one is chosen.
    RadioButton radio(const gfx::Rect &place, const char *text, int group, int value)
    {
        return {number_, os::ui::radio(number_, place, text, group, value)};
    }
    TextField text_field(const gfx::Rect &place, const char *initial = "")
    {
        return {number_, os::ui::text_field(number_, place, initial)};
    }
    ScrollBar scroll_bar(const gfx::Rect &place, bool vertical)
    {
        return {number_, os::ui::scroll_bar(number_, place, vertical)};
    }
    ListBox list(const gfx::Rect &place, const char *const *items, int count)
    {
        return {number_, os::ui::list(number_, place, items, count)};
    }
    DropDown drop_down(const gfx::Bitmap *icon, const gfx::Rect &place)
    {
        return {number_, os::ui::drop_down(number_, icon, place)};
    }
    IconView icon_view(const gfx::Rect &place) { return {number_, os::ui::icon_view(number_, place)}; }
    TextEdit text_edit(const gfx::Rect &place, size_t text_bytes, size_t undo_bytes)
    {
        return {number_, os::ui::text_edit(number_, place, text_bytes, undo_bytes)};
    }
    Meter meter(const gfx::Rect &place, gfx::Colour colour = gfx::rgb(0, 0, 2))
    {
        return {number_, os::ui::meter(number_, place, colour)};
    }
    // `how`: kOnce (every pixel of it is written exactly once when it is
    // painted), kMouse (mouse() and right_click() for it).
    Picture picture(const gfx::Rect &place, uint8_t how = 0)
    {
        return {number_, os::ui::picture(number_, place, how)};
    }
    Grid grid(const gfx::Rect &place, int columns, int count, int cell_w, int cell_h, int margin = 3)
    {
        return {number_, os::ui::grid(number_, place, columns, count, cell_w, cell_h, margin)};
    }
    GroupFrame group(const gfx::Rect &place, const char *caption)
    {
        return {number_, os::ui::group(number_, place, caption)};
    }
    TextArea text_area(const gfx::Rect &place, const char *text)
    {
        return {number_, os::ui::text_area(number_, place, text)};
    }
    void set_default(const Button &button) { os::ui::set_default(number_, button.number()); }
    void set_focus(const Control &control) { os::ui::set_focus(number_, control.number()); }
    Control focus() const { return {number_, os::ui::focus(number_)}; }
    void set_title(const char *title) { os::ui::set_title(number_, title); } // must stay valid
    // The window was opened with kMenuBar; the table must stay valid.
    void set_menu_bar(const wm::MenuBar *bar) { os::ui::set_menu_bar(number_, bar); }
    int radio_value(int group) const { return os::ui::radio_value(number_, group); }
    void set_radio_value(int group, int value) { os::ui::set_radio_value(number_, group, value); }

private:
    template <class T, class... Args> friend Number make(const Look &look, Args &&...args);

    static Number &making()
    {
        static Number number = kNone; // only between create() and the constructor, on the UI task
        return number;
    }

    // The one function the system calls: it finds the object and its virtual.
    template <class T> static bool events(os::ui::Window, const os::ui::Event &event, void *state)
    {
        T *program = static_cast<T *>(state);
        Window *window = program;
        switch (event.what) {
        case os::ui::What::Command:
            window->command(Control(window->number_, event.control));
            return true;
        case os::ui::What::Changed:
            window->changed(Control(window->number_, event.control));
            return true;
        case os::ui::What::Chosen:
            return window->chosen(Control(window->number_, event.control));
        case os::ui::What::Key:
            return event.control != os::ui::kNoControl
                       ? window->key_in(Control(window->number_, event.control), *event.key)
                       : window->key(*event.key);
        case os::ui::What::Text:
            window->text_in(Control(window->number_, event.control), event.text);
            return true;
        case os::ui::What::Painting:
            window->painting();
            return true;
        case os::ui::What::Paint:
            {
                Canvas canvas(event.drawing);
                if (event.control != os::ui::kNoControl) {
                    window->paint_in(Control(window->number_, event.control), canvas);
                } else {
                    window->paint(canvas);
                }
            }
            return true;
        case os::ui::What::Cell:
            {
                Canvas canvas(event.drawing);
                window->paint_cell(Control(window->number_, event.control), event.index, event.part, canvas);
            }
            return true;
        case os::ui::What::Mouse:
            window->mouse(Control(window->number_, event.control), *event.mouse);
            return true;
        case os::ui::What::RightClick:
            window->right_click(Control(window->number_, event.control), event.x, event.y);
            return true;
        case os::ui::What::Resized:
            window->resized(event.x, event.y);
            return true;
        case os::ui::What::Drag:
            window->drag(Control(window->number_, event.control));
            return true;
        case os::ui::What::DragOver: return window->drag_over(event.text, event.x, event.y);
        case os::ui::What::Dropped:
            window->dropped(event.text, event.x, event.y);
            return true;
        case os::ui::What::Closing: return window->closing();
        case os::ui::What::Menu:
            window->menu_opening(event.index);
            return true;
        case os::ui::What::Closed:
            window->closed();
            program->~T(); // its memory goes with the window
            return true;
        }
        return false;
    }

    Number number_;
};

// Makes a program's window without showing it yet, for a program that may
// still find it cannot start: app::show() puts it on the screen, and
// os::ui::close() takes it back. kNone: there was no memory.
template <class T, class... Args> Number make(const Look &look, Args &&...args)
{
    const Number number = os::ui::create(look, &Window::events<T>, sizeof(T));
    if (number == kNone) {
        return kNone;
    }
    Window::making() = number;
    new (os::ui::state(number)) T(std::forward<Args>(args)...);
    Window::making() = kNone;
    return number;
}
// False: there was no room on the screen, and the window is gone.
inline bool show(Number number) { return os::ui::show(number); }

// Makes a program's window and shows it. T inherits from app::Window; the
// arguments after `look` go to its constructor. kNone: there was no memory,
// or no room on the screen.
template <class T, class... Args> Number open(const Look &look, Args &&...args)
{
    const Number number = make<T>(look, std::forward<Args>(args)...);
    return number != kNone && os::ui::show(number) ? number : kNone;
}

// The program's object behind a window it opened as T; nullptr once the
// window has closed.
template <class T> T *program(Number number) { return static_cast<T *>(os::ui::state(number)); }

// For a program that runs once at a time: brings its window to the front.
// False if it is not open.
inline bool bring_forward(Number number) { return os::ui::alive(number) && os::ui::show(number); }

} // namespace app
