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

using Look = os::ui::Look;
using Number = os::ui::Window; // a window's number: dead once it has closed
constexpr Number kNone = os::ui::kNoWindow;
using os::ui::kDialog;
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

protected:
    Number window_ = kNone;
    os::ui::Control number_ = os::ui::kNoControl;
};

class Label : public Control {
public:
    using Control::Control;
    void set_text(const char *text) const { os::ui::set_text(window_, number_, text); }
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
    void set_items(const char *const *items, int count) const { os::ui::set_items(window_, number_, items, count); }
    int selected() const { return os::ui::selected(window_, number_); }
    void select(int index) const { os::ui::select(window_, number_, index); }
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

    // What happens in the window. A button was pressed:
    virtual void command(Control /*which*/) {}
    // A check box was ticked, a scroll bar moved, another row of a list picked:
    virtual void changed(Control /*which*/) {}
    // A row of a list was double-clicked, or Enter pressed on it:
    virtual void chosen(Control /*which*/) {}
    // A key the controls did not use. True: the program used it.
    virtual bool key(const wm::KeyMsg &) { return false; }
    // Only with kPicture: after the face and the controls are painted.
    virtual void paint(gfx::Canvas &) {}
    // The window has closed; the object is destroyed after this.
    virtual void closed() {}

protected:
    // The window being made: open() says which before the constructor runs,
    // so that a program's members can be made with button() and label().
    Window() : number_(making()) {}
    virtual ~Window() = default;

    Label label(const gfx::Rect &place, const char *text) { return {number_, os::ui::label(number_, place, text)}; }
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
    int radio_value(int group) const { return os::ui::radio_value(number_, group); }
    void set_radio_value(int group, int value) { os::ui::set_radio_value(number_, group, value); }

private:
    template <class T, class... Args> friend Number open(const Look &look, Args &&...args);

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
            window->chosen(Control(window->number_, event.control));
            return true;
        case os::ui::What::Key: return window->key(*event.key);
        case os::ui::What::Paint:
            window->paint(*event.canvas);
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

// Makes a program's window and shows it. T inherits from app::Window; the
// arguments after `look` go to its constructor. kNone: there was no memory,
// or no room on the screen.
template <class T, class... Args> Number open(const Look &look, Args &&...args)
{
    const Number number = os::ui::create(look, &Window::events<T>, sizeof(T));
    if (number == kNone) {
        return kNone;
    }
    Window::making() = number;
    new (os::ui::state(number)) T(std::forward<Args>(args)...);
    Window::making() = kNone;
    return os::ui::show(number) ? number : kNone;
}

// For a program that runs once at a time: brings its window to the front.
// False if it is not open.
inline bool bring_forward(Number number) { return os::ui::alive(number) && os::ui::show(number); }

} // namespace app
