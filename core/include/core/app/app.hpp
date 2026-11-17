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
};

class Button : public Control {
public:
    using Control::Control;
    void press() const { os::ui::press(window_, number_); } // as if clicked
};

class Window {
public:
    Number number() const { return number_; }
    void close() { os::ui::close(number_); }

    // What happens in the window. A button was pressed:
    virtual void command(Control /*which*/) {}
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
    void set_default(const Button &button) { os::ui::set_default(number_, button.number()); }

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
