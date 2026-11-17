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

class Behind;
struct Slot {
    Behind *window = nullptr;
    uint16_t serial = 0; // tells this use of the slot from the one before
};
Slot g_slots[kMaxWindows];

enum class Kind : uint8_t { Label, Button };

class Behind : public wm::Window {
public:
    Behind(int slot, const Look &look, Events events, void *state)
        : Window(look.title, os::centred(look.width, look.height), look.icon), slot_(slot), events_(events),
          state_(state), flags_(look.flags)
    {
        if ((flags_ & kDialog) != 0) {
            set_dialog(true);
        }
        if ((flags_ & kWholeDesktop) != 0) {
            set_modal(nullptr, true);
        }
    }

    ~Behind()
    {
        closed(); // one that never reached the screen: the program still hears that it is gone
        for (int i = 0; i < count_; i++) {
            switch (kinds_[i]) {
            case Kind::Label: static_cast<wm::Label *>(controls_[i])->~Label(); break;
            case Kind::Button: static_cast<wm::Button *>(controls_[i])->~Button(); break;
            }
            os::release(controls_[i]);
        }
        g_slots[slot_].window = nullptr;
    }

    ui::Window handle() const { return (static_cast<ui::Window>(g_slots[slot_].serial) << 8) | (slot_ + 1); }
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

    wm::Button *button(Control control) const
    {
        const int at = control - 1;
        return at >= 0 && at < count_ && kinds_[at] == Kind::Button ? static_cast<wm::Button *>(controls_[at]) : nullptr;
    }

    static void pressed(wm::Button &button, void *context)
    {
        auto *w = static_cast<Behind *>(context);
        for (int i = 0; i < w->count_; i++) {
            if (w->controls_[i] == &button) {
                Event e{What::Command};
                e.control = i + 1;
                w->tell(e);
                return;
            }
        }
    }

    bool layered() const override { return (flags_ & kPicture) != 0 || Window::layered(); }
    void paint(gfx::Canvas &c) override
    {
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
    wm::Widget *controls_[kMaxWidgets] = {};
    Kind kinds_[kMaxWidgets] = {};
    int count_ = 0;
};

Behind *behind(Window window)
{
    const int slot = (window & 0xFF) - 1;
    if (window <= 0 || slot < 0 || slot >= kMaxWindows) {
        return nullptr;
    }
    return g_slots[slot].serial == (window >> 8) ? g_slots[slot].window : nullptr;
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
        slot = g_slots[i].window == nullptr ? i : -1;
    }
    void *room = slot >= 0 && events != nullptr ? os::allocate(kAligned + state_bytes) : nullptr;
    if (room == nullptr) {
        return kNoWindow;
    }
    g_slots[slot].serial = g_slots[slot].serial >= 0x7FFF ? 1 : g_slots[slot].serial + 1;
    Behind *w = new (room) Behind(slot, look, events, static_cast<uint8_t *>(room) + kAligned);
    w->set_dispose(&destroy);
    g_slots[slot].window = w;
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
