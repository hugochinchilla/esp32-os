// Windows and controls by number (core/os/ui.hpp): the system's side. One
// window class of the system's own stands behind every such window; it
// owns the controls and turns what the window manager tells it into events
// for the program.
#include "core/os/ui.hpp"

#include <new>

#include "core/os/os.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"

namespace os {
namespace ui {

namespace {

constexpr int kMaxWindows = 16; // ponytail: as many as the window manager shows

// Every window made here, open or not yet. A window's number is the one
// the window server knows it by (os::WindowId), taken when it is made.
class Behind;
Behind *g_windows[kMaxWindows] = {};

enum class Kind : uint8_t { Label, Button, CheckBox, Radio, TextField, ScrollBar, List, Group, TextArea };

class Behind : public wm::Window {
public:
    Behind(int slot, const Look &look, Events events, void *state)
        : Window(look.title, os::centred(look.width, look.height), look.icon), slot_(slot), events_(events),
          state_(state), flags_(look.flags)
    {
        if ((flags_ & kDialog) != 0) {
            set_dialog(true);
        }
        set_id(installed_kit().wm->new_id());
        if ((flags_ & kWholeDesktop) != 0) {
            set_modal(nullptr, true);
        } else if (look.over != kNoWindow) {
            // A dialog over another window, whoever's it is; if that one
            // has closed meanwhile, over nothing.
            if (wm::Window *owner = installed_kit().wm->window(look.over)) {
                set_modal(owner);
            }
        }
    }

    ~Behind()
    {
        closed(); // one that never reached the screen: the program still hears that it is gone
        for (int i = 0; i < count_; i++) {
            switch (kinds_[i]) {
            case Kind::Label: static_cast<wm::Label *>(controls_[i])->~Label(); break;
            case Kind::Button: static_cast<wm::Button *>(controls_[i])->~Button(); break;
            case Kind::CheckBox: static_cast<wm::CheckBox *>(controls_[i])->~CheckBox(); break;
            case Kind::Radio: static_cast<wm::RadioButton *>(controls_[i])->~RadioButton(); break;
            case Kind::TextField: static_cast<wm::TextField *>(controls_[i])->~TextField(); break;
            case Kind::ScrollBar: static_cast<wm::ScrollBar *>(controls_[i])->~ScrollBar(); break;
            case Kind::List: static_cast<wm::ListBox *>(controls_[i])->~ListBox(); break;
            case Kind::Group: static_cast<wm::GroupFrame *>(controls_[i])->~GroupFrame(); break;
            case Kind::TextArea: static_cast<wm::TextArea *>(controls_[i])->~TextArea(); break;
            }
            os::release(controls_[i]);
        }
        g_windows[slot_] = nullptr;
    }

    ui::Window handle() const { return id(); }
    void *state() const { return state_; }

    template <class T, class... Args> Control make(Kind kind, Args &&...args)
    {
        if (count_ == kMaxWidgets) {
            return kNoControl;
        }
        void *room = os::allocate(sizeof(T));
        if (room == nullptr) {
            return kNoControl;
        }
        T *control = new (room) T(static_cast<Args &&>(args)...);
        controls_[count_] = control;
        kinds_[count_] = kind;
        add(*control);
        return ++count_;
    }

    // The control with this number, if it is of this kind.
    template <class T> T *as(Control control, Kind kind) const
    {
        const int at = control - 1;
        return at >= 0 && at < count_ && kinds_[at] == kind ? static_cast<T *>(controls_[at]) : nullptr;
    }
    wm::Button *button(Control control) const { return as<wm::Button>(control, Kind::Button); }
    wm::Widget *any(Control control) const
    {
        return control >= 1 && control <= count_ ? controls_[control - 1] : nullptr;
    }
    Kind kind(Control control) const { return kinds_[control - 1]; }
    int *radio_group(int group) { return group >= 0 && group < kGroups ? &groups_[group] : nullptr; }

    // A control says something happened: which one, as an event.
    bool from(const wm::Widget &control, What what)
    {
        for (int i = 0; i < count_; i++) {
            if (controls_[i] == &control) {
                Event e{what};
                e.control = i + 1;
                return tell(e);
            }
        }
        return false;
    }
    static void pressed(wm::Button &b, void *context) { static_cast<Behind *>(context)->from(b, What::Command); }
    static void ticked(wm::CheckBox &b, void *context) { static_cast<Behind *>(context)->from(b, What::Changed); }
    static void scrolled(wm::ScrollBar &b, void *context) { static_cast<Behind *>(context)->from(b, What::Changed); }
    static void picked(wm::ListBox &l, void *context) { static_cast<Behind *>(context)->from(l, What::Changed); }
    // Enter on a row, or a double click. A program that has no use for it
    // gets what Enter does anywhere else in the window: the default button.
    static void chosen(wm::ListBox &l, void *context)
    {
        auto *w = static_cast<Behind *>(context);
        if (!w->from(l, What::Chosen) && w->default_widget() != nullptr) {
            w->default_widget()->press();
        }
    }

    bool layered() const override { return (flags_ & kPicture) != 0 || Window::layered(); }
    void paint(gfx::Canvas &c) override
    {
        tell(Event{What::Painting});
        Window::paint(c);
        if ((flags_ & kPicture) != 0) {
            Event e{What::Paint};
            e.canvas = &c;
            tell(e);
        }
    }
    bool key(const wm::KeyMsg &k) override
    {
        Event e{What::Key};
        e.key = &k;
        return tell(e) || Window::key(k);
    }
    void closed() override
    {
        if (!told_closed_) {
            told_closed_ = true;
            tell(Event{What::Closed});
        }
    }

private:
    bool tell(const Event &e) { return events_(handle(), e, state_); }

    int slot_;
    Events events_;
    void *state_;
    uint8_t flags_;
    bool told_closed_ = false;
    static constexpr int kGroups = 4;
    int groups_[kGroups] = {};
    wm::Widget *controls_[kMaxWidgets] = {};
    Kind kinds_[kMaxWidgets] = {};
    int count_ = 0;
};

Behind *behind(Window window)
{
    for (Behind *w : g_windows) {
        if (w != nullptr && window != kNoWindow && w->id() == window) {
            return w;
        }
    }
    return nullptr;
}

// The window manager's safe point after the window closed: the object, the
// controls and the program's state go back.
void destroy(wm::Window &w)
{
    Behind *window = static_cast<Behind *>(&w);
    window->~Behind();
    os::release(window);
}

constexpr size_t kAligned = (sizeof(Behind) + 15) & ~static_cast<size_t>(15);

} // namespace

Window create(const Look &look, Events events, size_t state_bytes)
{
    int slot = -1;
    for (int i = 0; i < kMaxWindows && slot < 0; i++) {
        slot = g_windows[i] == nullptr ? i : -1;
    }
    void *room = slot >= 0 && events != nullptr ? os::allocate(kAligned + state_bytes) : nullptr;
    if (room == nullptr) {
        return kNoWindow;
    }
    Behind *w = new (room) Behind(slot, look, events, static_cast<uint8_t *>(room) + kAligned);
    w->set_dispose(&destroy);
    g_windows[slot] = w;
    return w->handle();
}

void *state(Window window)
{
    Behind *w = behind(window);
    return w != nullptr ? w->state() : nullptr;
}

bool alive(Window window) { return behind(window) != nullptr; }

Control label(Window window, const gfx::Rect &place, const char *text)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Label>(Kind::Label, place, text) : kNoControl;
}

Control button(Window window, const gfx::Rect &place, const char *text)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::Button>(Kind::Button, place, text, &Behind::pressed, w) : kNoControl;
}

Control check_box(Window window, const gfx::Rect &place, const char *text, bool checked)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::CheckBox>(Kind::CheckBox, place, text, checked, &Behind::ticked, w) : kNoControl;
}

Control radio(Window window, const gfx::Rect &place, const char *text, int group, int value)
{
    Behind *w = behind(window);
    int *chosen = w != nullptr ? w->radio_group(group) : nullptr;
    return chosen != nullptr ? w->make<wm::RadioButton>(Kind::Radio, place, text, chosen, value) : kNoControl;
}

Control text_field(Window window, const gfx::Rect &place, const char *initial)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::TextField>(Kind::TextField, place, initial) : kNoControl;
}

Control scroll_bar(Window window, const gfx::Rect &place, bool vertical)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::ScrollBar>(Kind::ScrollBar, place, vertical, &Behind::scrolled, w) : kNoControl;
}

Control list(Window window, const gfx::Rect &place, const char *const *items, int count)
{
    Behind *w = behind(window);
    const Control c = w != nullptr ? w->make<wm::ListBox>(Kind::List, place, items, count, &Behind::picked, w) : kNoControl;
    if (c != kNoControl) {
        w->as<wm::ListBox>(c, Kind::List)->set_on_activate(&Behind::chosen);
    }
    return c;
}

Control group(Window window, const gfx::Rect &place, const char *caption)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::GroupFrame>(Kind::Group, place, caption) : kNoControl;
}

Control text_area(Window window, const gfx::Rect &place, const char *text)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<wm::TextArea>(Kind::TextArea, place, text) : kNoControl;
}

void set_text(Window window, Control control, const char *text)
{
    Behind *w = behind(window);
    if (w == nullptr || w->any(control) == nullptr) {
        return;
    }
    switch (w->kind(control)) {
    case Kind::Label: w->as<wm::Label>(control, Kind::Label)->set_text(text); break;
    case Kind::Button: w->button(control)->set_label(text); break;
    case Kind::TextArea: w->as<wm::TextArea>(control, Kind::TextArea)->set_text(text); break;
    case Kind::TextField: w->as<wm::TextField>(control, Kind::TextField)->set(text); break;
    default: break;
    }
}

namespace {
template <class T> T *control_of(Window window, Control control, Kind kind)
{
    Behind *w = behind(window);
    return w != nullptr ? w->as<T>(control, kind) : nullptr;
}
} // namespace

void get_text(Window window, Control control, char *out, int capacity)
{
    if (capacity > 0) {
        out[0] = 0;
    }
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->get(out, capacity);
    }
}

void select_all(Window window, Control control)
{
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->select_all();
    }
}

void set_hidden(Window window, Control control, bool hidden)
{
    if (auto *f = control_of<wm::TextField>(window, control, Kind::TextField)) {
        f->set_hidden(hidden);
    }
}

void set_enabled(Window window, Control control, bool enabled)
{
    if (auto *b = control_of<wm::Button>(window, control, Kind::Button)) {
        b->set_enabled(enabled);
    }
}

bool checked(Window window, Control control)
{
    auto *b = control_of<wm::CheckBox>(window, control, Kind::CheckBox);
    return b != nullptr && b->checked();
}

void set_checked(Window window, Control control, bool on)
{
    if (auto *b = control_of<wm::CheckBox>(window, control, Kind::CheckBox)) {
        b->set_checked(on);
    }
}

int radio_value(Window window, int group)
{
    Behind *w = behind(window);
    const int *chosen = w != nullptr ? w->radio_group(group) : nullptr;
    return chosen != nullptr ? *chosen : 0;
}

void set_radio_value(Window window, int group, int value)
{
    Behind *w = behind(window);
    if (int *chosen = w != nullptr ? w->radio_group(group) : nullptr) {
        *chosen = value;
        w->invalidate();
    }
}

void set_range(Window window, Control control, int maximum, int page)
{
    if (auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar)) {
        b->set_range(maximum, page);
    }
}

int value(Window window, Control control)
{
    auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar);
    return b != nullptr ? b->value() : 0;
}

void set_value(Window window, Control control, int value)
{
    if (auto *b = control_of<wm::ScrollBar>(window, control, Kind::ScrollBar)) {
        b->set_value_quietly(value);
    }
}

void set_items(Window window, Control control, const char *const *items, int count)
{
    if (auto *l = control_of<wm::ListBox>(window, control, Kind::List)) {
        l->set_items(items, count);
    }
}

int selected(Window window, Control control)
{
    auto *l = control_of<wm::ListBox>(window, control, Kind::List);
    return l != nullptr ? l->selected() : -1;
}

void select(Window window, Control control, int index)
{
    if (auto *l = control_of<wm::ListBox>(window, control, Kind::List)) {
        l->select(index);
    }
}

void set_focus(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Widget *c = w != nullptr ? w->any(control) : nullptr) {
        w->set_focus(c);
    }
}

void set_default(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Button *b = w != nullptr ? w->button(control) : nullptr) {
        b->set_default(true);
        w->set_default(b);
    }
}

void press(Window window, Control control)
{
    Behind *w = behind(window);
    if (wm::Button *b = w != nullptr ? w->button(control) : nullptr) {
        b->click();
    }
}

bool show(Window window)
{
    Behind *w = behind(window);
    if (w == nullptr) {
        return false;
    }
    os::show(*w);
    if (!w->open()) {
        installed_kit().wm->discard(*w); // the window manager is full
        return false;
    }
    return true;
}

void close(Window window)
{
    if (Behind *w = behind(window)) {
        w->close();
    }
}

} // namespace ui
} // namespace os
