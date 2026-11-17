// Windows and controls by number (core/os/ui.hpp): the system's side. One
// window class of the system's own stands behind every such window; it
// owns the controls and turns what the window manager tells it into events
// for the program.
#include "core/os/ui.hpp"

#include <cstdio>
#include <cstring>
#include <new>

#include "core/os/os.hpp"
#include "core/wm/widgets.hpp"
#include "core/wm/wm.hpp"

namespace os {
namespace ui {

namespace {

// The drawings in progress: the canvas of the Paint event being answered
// and the parts the program has asked of it. Numbers are given afresh for
// each event, never the same twice running, so one kept past its event
// draws nothing.
struct Drawings {
    static constexpr int kMax = 1 + draw::kMaxParts;
    alignas(gfx::Canvas) unsigned char room[kMax][sizeof(gfx::Canvas)];
    int count = 0;
    int base = 0; // the number of the first is base + 1

    gfx::Canvas *at(Drawing d)
    {
        const int i = d - base - 1;
        return i >= 0 && i < count ? reinterpret_cast<gfx::Canvas *>(room[i]) : nullptr;
    }
    Drawing add(const gfx::Canvas &c)
    {
        if (count >= kMax) {
            return kNoDrawing;
        }
        new (room[count]) gfx::Canvas(c);
        return base + ++count;
    }
    void end()
    {
        for (int i = 0; i < count; i++) {
            reinterpret_cast<gfx::Canvas *>(room[i])->~Canvas();
        }
        base += kMax; // the next event's numbers are all new
        count = 0;
    }
};
Drawings g_drawings;

constexpr int kMaxWindows = 16; // ponytail: as many as the window manager shows

// Every window made here, open or not yet. A window's number is the one
// the window server knows it by (os::WindowId), taken when it is made.
class Behind;
Behind *g_windows[kMaxWindows] = {};

enum class Kind : uint8_t { Label, Plain, Button, CheckBox, Radio, TextField, ScrollBar, List, Group, TextArea };

// A line of text exactly as given, kept by the control.
class PlainText : public wm::Widget {
public:
    explicit PlainText(const gfx::Rect &r) : Widget(r) {}
    void set(const char *text)
    {
        if (strcmp(text, text_) != 0) {
            snprintf(text_, sizeof(text_), "%.*s", static_cast<int>(sizeof(text_) - 1), text); // longer text is cut
            invalidate();
        }
    }
    void paint(gfx::Canvas &c) override
    {
        const int y = (rect.h - 12) / 2;
        c.fill({0, 0, rect.w, y}, gfx::kGrey);
        const int x = c.text_opaque(0, y, text_, gfx::kBlack, gfx::kGrey);
        c.fill({x, y, rect.w - x, 12}, gfx::kGrey);
        c.fill({0, y + 12, rect.w, rect.h - y - 12}, gfx::kGrey);
    }
    ~PlainText() = default;

private:
    char text_[96] = "";
};

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
            case Kind::Plain: static_cast<PlainText *>(controls_[i])->~PlainText(); break;
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
    // The pictures of the rows of the window's list that has them.
    void set_row_icons(const gfx::Bitmap *const *icons) { row_icons_ = icons; }
    static const gfx::Bitmap *row_icon(int index, void *context)
    {
        const auto *w = static_cast<Behind *>(context);
        return w->row_icons_ != nullptr ? w->row_icons_[index] : nullptr;
    }
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
            e.drawing = g_drawings.add(c);
            tell(e);
            g_drawings.end();
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
    const gfx::Bitmap *const *row_icons_ = nullptr;
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

bool on_screen(Window window)
{
    Behind *w = behind(window);
    return w != nullptr && w->open();
}

void redraw(Window window, const gfx::Rect &part)
{
    if (Behind *w = behind(window)) {
        w->invalidate_client(part);
    }
}

Control plain(Window window, const gfx::Rect &place)
{
    Behind *w = behind(window);
    return w != nullptr ? w->make<PlainText>(Kind::Plain, place) : kNoControl;
}

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
    case Kind::Plain: w->as<PlainText>(control, Kind::Plain)->set(text); break;
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

void set_items(Window window, Control control, const char *const *items, int count,
               const gfx::Bitmap *const *icons)
{
    if (auto *l = control_of<wm::ListBox>(window, control, Kind::List)) {
        if (icons != nullptr) {
            behind(window)->set_row_icons(icons);
            l->set_icons(&Behind::row_icon);
        }
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

/* ---- Drawing ---- */

namespace draw {

Drawing part(Drawing on, const gfx::Rect &part)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? g_drawings.add(c->sub(part)) : kNoDrawing;
}

#define DRAW(call)                                                                                                     \
    if (gfx::Canvas *c = g_drawings.at(on)) {                                                                          \
        c->call;                                                                                                       \
    }

void fill(Drawing on, const gfx::Rect &r, gfx::Colour col) { DRAW(fill(r, col)) }
void hline(Drawing on, int x, int y, int w, gfx::Colour col) { DRAW(hline(x, y, w, col)) }
void vline(Drawing on, int x, int y, int h, gfx::Colour col) { DRAW(vline(x, y, h, col)) }
void fill_rgb(Drawing on, const gfx::Rect &r, gfx::Rgb col) { DRAW(fill_rgb(r, col)) }
void fill_pattern(Drawing on, const gfx::Rect &r, gfx::Rgb paper, gfx::Rgb ink, const uint8_t rows[8])
{
    DRAW(fill_pattern(r, paper, ink, rows))
}
void fill_fade(Drawing on, const gfx::Rect &r, gfx::Rgb from, gfx::Rgb to, bool vertical, const gfx::Rect *span)
{
    DRAW(fill_fade(r, from, to, vertical, span))
}
void focus_rect(Drawing on, const gfx::Rect &r) { DRAW(focus_rect(r)) }
void checker(Drawing on, const gfx::Rect &r) { DRAW(checker(r)) }
void edge(Drawing on, const gfx::Rect &r, gfx::Colour tl, gfx::Colour br) { DRAW(edge(r, tl, br)) }
void raised(Drawing on, const gfx::Rect &r) { DRAW(raised(r)) }
void sunken(Drawing on, const gfx::Rect &r) { DRAW(sunken(r)) }
void button(Drawing on, const gfx::Rect &r, gfx::ButtonLook look) { DRAW(button(r, look)) }
void button_frame(Drawing on, const gfx::Rect &r, gfx::ButtonLook look) { DRAW(button_frame(r, look)) }
void blit(Drawing on, const gfx::Bitmap &b, int x, int y) { DRAW(blit(b, x, y)) }
void blit_selected(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour tint)
{
    DRAW(blit_selected(b, x, y, tint))
}
void blit2x(Drawing on, const gfx::Bitmap &b, int x, int y) { DRAW(blit2x(b, x, y)) }
void blit_opaque(Drawing on, const gfx::Bitmap &b, int x, int y, gfx::Colour bg) { DRAW(blit_opaque(b, x, y, bg)) }
void pattern(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour col)
{
    DRAW(pattern(x, y, rows, count, col))
}
void pattern_opaque(Drawing on, int x, int y, const char *const *rows, int count, gfx::Colour fg, gfx::Colour bg)
{
    DRAW(pattern_opaque(x, y, rows, count, fg, bg))
}
void text_up(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold) { DRAW(text_up(x, y, s, col, bold)) }
void fill_around(Drawing on, const gfx::Rect &area, const gfx::Rect &hole, gfx::Colour col)
{
    DRAW(fill_around(area, hole, col))
}

#undef DRAW

// Drawing nothing, a text still ends where it would have.
int text(Drawing on, int x, int y, const char *s, gfx::Colour col, bool bold)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text(x, y, s, col, bold) : x + gfx::Canvas::text_width(s, bold);
}
int text_opaque(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Colour bg, int caret_x)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_opaque(x, y, s, fg, bg, caret_x) : x + gfx::Canvas::text_width(s);
}
int text_opaque_rgb(Drawing on, int x, int y, const char *s, gfx::Colour fg, gfx::Rgb bg, int caret_x)
{
    gfx::Canvas *c = g_drawings.at(on);
    return c != nullptr ? c->text_opaque(x, y, s, fg, bg, caret_x) : x + gfx::Canvas::text_width(s);
}

int text_width(const char *s, bool bold) { return gfx::Canvas::text_width(s, bold); }
int text_height() { return gfx::Canvas::text_height(); }

} // namespace draw

} // namespace ui
} // namespace os
